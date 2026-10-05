/*
 * Copyright 2025 Marius
 *
 * This file is part of NetSurf, http://www.netsurf-browser.org/
 *
 * NetSurf is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * NetSurf is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * \file
 * \brief HTML grid layout implementation
 *
 * CSS Grid Layout Module Level 1 implementation.
 *
 * Box Model Notes (per CSS Grid spec):
 * ------------------------------------
 * - Grid tracks define the size of grid cells (content box of the grid area)
 * - Gaps are spaces BETWEEN tracks (like extra fixed-size tracks)
 * - Grid items are sized within their grid areas, respecting declared widths
 * - Auto widths stretch unless auto margins request shrink-to-fit sizing
 * - Remaining inline space is distributed to auto margins before placement
 * - Row heights track the full box height (content + padding + border)
 *   so that subsequent rows are positioned correctly
 *
 * Key relationships:
 *   cell_width = track_width (sum for spanning items)
 *   border_box_width = child->width + padding[L+R] + border[L+R]
 *   child->x is the padding edge: area_x + margin[L] + border[L]
 */

#include <assert.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <libcss/computed.h>
#include <libcss/libcss.h>

#include <dom/dom.h>
#include <wisp/content/handlers/html/box.h>
#include <wisp/content/handlers/html/private.h>
#include <wisp/utils/corestrings.h>
#include <wisp/utils/log.h>
#include <wisp/utils/utils.h>
#include "content/handlers/html/layout_grid.h"
#include "content/handlers/html/layout_internal.h"

/** Indicates grid placement is auto (not explicitly set) */
#define GRID_PLACEMENT_AUTO (-1)
/** Indicates grid end is a span value (value is the span count) */
#define GRID_PLACEMENT_SPAN (-2)

/** Placement phase for 3-phase algorithm (CSS Grid spec §8) */
typedef enum {
    GRID_PHASE_DEFINITE_BOTH = 1, /**< Both row and column are definite */
    GRID_PHASE_DEFINITE_ONE = 2, /**< Only row OR column is definite */
    GRID_PHASE_AUTO = 3 /**< Both are auto/span */
} grid_placement_phase_t;

/**
 * Cached placement info for a grid item.
 * Retained until track sizing finishes, so alignment only uses final sizes.
 */
struct grid_item_cache {
    struct box *box; /**< The grid item box */
    int item_col; /**< Column position (0-indexed) */
    int item_row; /**< Row position (0-indexed) */
    int col_span; /**< Column span */
    int row_span; /**< Row span */
    uint8_t align; /**< Vertical alignment (CSS_ALIGN_ITEMS_*) */
    bool baseline; /**< Participates in its row's first-baseline group */
    struct layout_containing_size area; /**< Input used for intrinsic measurement. */
};

/**
 * Determine which placement phase an item belongs to.
 *
 * \param col_start Column start placement value
 * \param row_start Row start placement value
 * \return The placement phase for this item
 */
static grid_placement_phase_t get_placement_phase(int col_start, int row_start)
{
    bool col_definite = (col_start != GRID_PLACEMENT_AUTO && col_start != GRID_PLACEMENT_SPAN);
    bool row_definite = (row_start != GRID_PLACEMENT_AUTO && row_start != GRID_PLACEMENT_SPAN);

    if (col_definite && row_definite) {
        return GRID_PHASE_DEFINITE_BOTH;
    } else if (col_definite || row_definite) {
        return GRID_PHASE_DEFINITE_ONE;
    } else {
        return GRID_PHASE_AUTO;
    }
}

/* Per-layout track sizing state; no additional storage in persistent boxes. */
struct grid_track {
    int size, limit, offset; /**< limit == -1 is an unresolved/infinite growth limit. */
    int planned_size, planned_limit, increase;
    css_fixed flex;
    css_unit min_unit, max_unit;
    bool flexible, intrinsic_min, intrinsic_max, auto_min, stretch, infinitely_growable;
};

/**
 * Get explicit row count from CSS grid-template-rows.
 *
 * Used for column-major auto-flow to know when to wrap to next column.
 *
 * \param grid  The grid container box
 * \return Number of explicit rows, or 1 if none defined
 */
static int layout_grid_get_explicit_row_count(struct box *grid)
{
    if (grid->style == NULL) {
        return 1;
    }

    int32_t n_tracks = 0;
    css_computed_grid_track *tracks = NULL;
    uint8_t type = css_computed_grid_template_rows(grid->style, &n_tracks, &tracks);

    if (type == CSS_GRID_TEMPLATE_SET && n_tracks > 0) {
        return n_tracks;
    }

    return 1; /* Default to 1 row if no explicit rows */
}

/**
 * Get grid item placement from CSS computed style.
 *
 * \param style     The computed style of the grid item
 * \param col_start Output: column start (0-indexed), or GRID_PLACEMENT_AUTO
 * \param col_end   Output: column end (0-indexed), GRID_PLACEMENT_AUTO, or GRID_PLACEMENT_SPAN
 * \param row_start Output: row start (0-indexed), or GRID_PLACEMENT_AUTO
 * \param row_end   Output: row end (0-indexed), GRID_PLACEMENT_AUTO, or GRID_PLACEMENT_SPAN
 * \param col_span  Output: explicit column span (when col_end is GRID_PLACEMENT_SPAN)
 * \param row_span  Output: explicit row span (when row_end is GRID_PLACEMENT_SPAN)
 */
static void get_grid_item_placement(const css_computed_style *style, int *col_start, int *col_end, int *row_start,
    int *row_end, int *col_span, int *row_span)
{
    int32_t val;
    uint8_t type;

    /* Initialize defaults */
    *col_span = 1;
    *row_span = 1;

    /* grid-column-start */
    if (style != NULL) {
        type = css_computed_grid_column_start(style, &val);
        if (type == CSS_GRID_LINE_SET) {
            *col_start = FIXTOINT(val) - 1; /* CSS is 1-indexed */
        } else if (type == CSS_GRID_LINE_SPAN) {
            /* Start is span - means span from end position */
            *col_start = GRID_PLACEMENT_SPAN;
            *col_span = FIXTOINT(val);
        } else {
            *col_start = GRID_PLACEMENT_AUTO;
        }
    } else {
        *col_start = GRID_PLACEMENT_AUTO;
    }

    /* grid-column-end */
    if (style != NULL) {
        type = css_computed_grid_column_end(style, &val);
        if (type == CSS_GRID_LINE_SET) {
            *col_end = FIXTOINT(val) - 1;
        } else if (type == CSS_GRID_LINE_SPAN) {
            *col_end = GRID_PLACEMENT_SPAN;
            *col_span = FIXTOINT(val);
        } else {
            *col_end = GRID_PLACEMENT_AUTO;
        }
    } else {
        *col_end = GRID_PLACEMENT_AUTO;
    }

    /* grid-row-start */
    if (style != NULL) {
        type = css_computed_grid_row_start(style, &val);
        if (type == CSS_GRID_LINE_SET) {
            *row_start = FIXTOINT(val) - 1;
        } else if (type == CSS_GRID_LINE_SPAN) {
            /* Start is span - means span from end position */
            *row_start = GRID_PLACEMENT_SPAN;
            *row_span = FIXTOINT(val);
        } else {
            *row_start = GRID_PLACEMENT_AUTO;
        }
    } else {
        *row_start = GRID_PLACEMENT_AUTO;
    }

    /* grid-row-end */
    if (style != NULL) {
        type = css_computed_grid_row_end(style, &val);
        if (type == CSS_GRID_LINE_SET) {
            *row_end = FIXTOINT(val) - 1;
        } else if (type == CSS_GRID_LINE_SPAN) {
            *row_end = GRID_PLACEMENT_SPAN;
            *row_span = FIXTOINT(val);
        } else {
            *row_end = GRID_PLACEMENT_AUTO;
        }
    } else {
        *row_end = GRID_PLACEMENT_AUTO;
    }

    /* Validate: per CSS Grid spec, span values must be >= 1 */
    if (*col_span < 1) {
        NSLOG(layout, ERROR,
            "Invalid grid col_span=%d (must be >= 1), "
            "possible propset normalization bug",
            *col_span);
    }
    if (*row_span < 1) {
        NSLOG(layout, ERROR,
            "Invalid grid row_span=%d (must be >= 1), "
            "possible propset normalization bug",
            *row_span);
    }
}

/**
 * Determine logical column count from CSS grid-template-columns.
 *
 * Uses the CSS computed style to get the actual track list values.
 */
static int layout_grid_get_column_count(struct box *grid)
{
    int32_t n_tracks = 0;
    css_computed_grid_track *tracks = NULL;
    uint8_t grid_template_type;

    /* Get column count from CSS computed style */
    if (grid->style != NULL) {
        grid_template_type = css_computed_grid_template_columns(grid->style, &n_tracks, &tracks);

        NSLOG(layout, INFO, "grid_get_column_count: type=%d, n_tracks=%d, tracks=%p", grid_template_type, n_tracks,
            tracks);

        if (grid_template_type == CSS_GRID_TEMPLATE_SET && n_tracks > 0) {
            NSLOG(layout, INFO, "CSS grid-template-columns: %d tracks", n_tracks);
            /* Log each track for debugging */
            for (int32_t i = 0; i < n_tracks; i++) {
                const char *unit_str = "unknown";
                switch (tracks[i].unit) {
                case CSS_UNIT_PX:
                    unit_str = "px";
                    break;
                case CSS_UNIT_EM:
                    unit_str = "em";
                    break;
                case CSS_UNIT_PCT:
                    unit_str = "%";
                    break;
                case CSS_UNIT_FR:
                    unit_str = "fr";
                    break;
                case CSS_UNIT_MIN_CONTENT:
                    unit_str = "min-content";
                    break;
                case CSS_UNIT_MAX_CONTENT:
                    unit_str = "max-content";
                    break;
                case CSS_UNIT_MINMAX:
                    NSLOG(layout, INFO, "  Track %d: minmax(min=%f %d, max=%f %d)", i, FIXTOFLT(tracks[i].value),
                        tracks[i].min_unit, FIXTOFLT(tracks[i].max_value), tracks[i].max_unit);
                    continue;
                default:
                    break;
                }
                NSLOG(layout, INFO, "  Track %d: %f %s", i, FIXTOFLT(tracks[i].value), unit_str);
            }
            return n_tracks;
        } else if (grid_template_type == CSS_GRID_TEMPLATE_NONE) {
            /* Explicit 'none' value means no explicit grid */
            return 1;
        }
        /* CSS_GRID_TEMPLATE_INHERIT or no tracks falls through to
         * default */
    }

    return 1; /* Default to 1 column */
}

/* Per-call scratch state. Helpers borrow these buffers; layout_grid owns
 * their lifetime. No state is shared between nested grid layouts. */
struct grid_layout_ctx {
    struct box *grid;
    html_content *content;
    const css_unit_ctx *unit_len_ctx;
    struct css_size min_height;
    int max_height;
    int num_cols, num_rows, column_gap, row_gap;
    int available_width;
    struct grid_track *columns;
    struct grid_track *rows;
    int row_capacity, max_row;
    int *row_baselines;
    css_computed_grid_track *row_tracks;
    int32_t row_track_count;
    bool flow_is_column, is_dense;
    int auto_col, auto_row;
    bool *occupied;
    int occupied_max_rows;
    struct grid_item_cache *item_cache;
    int item_count, cache_idx;
};

/* Requested CSS grid lines and the resolved grid area for one item. */
struct grid_placement {
    int col_start, col_end, row_start, row_end;
    int col, row, col_span, row_span;
};

static void layout_grid_cleanup(struct grid_layout_ctx *ctx)
{
    free(ctx->item_cache);
    free(ctx->row_baselines);
    free(ctx->occupied);
    free(ctx->rows);
    free(ctx->columns);
}

/* Resolve the container separately from its rows. Refresh auto heights on each
 * pass, while preserving a size already assigned by a parent flex/grid. */
static void layout_grid_prepare_height(struct grid_layout_ctx *ctx, int containing_width)
{
    struct box *grid = ctx->grid;
    bool stretched = (grid->flags & HEIGHT_STRETCHED) != 0;
    layout_find_dimensions(ctx->unit_len_ctx, containing_width, -1, grid,
        &(struct layout_dimension_outputs){
            .height = stretched ? NULL : &grid->height,
            .min_height = &ctx->min_height,
            .max_height = &ctx->max_height,
        });
    if (!stretched)
        layout_prepare_aspect_ratio_height(grid);
    /* Descendants must see the constrained definite height, not its CSS input.
     * An auto height stays indefinite until the rows have been measured. */
    if (grid->height != AUTO) {
        if (ctx->max_height >= 0)
            grid->height = min(grid->height, ctx->max_height);
        if (ctx->min_height.type == CSS_SIZE_SET)
            grid->height = max(grid->height, ctx->min_height.value);
    }
}

/* Row percentages use the grid's block size. During intrinsic auto-height
 * measurement an unresolved percentage is zero; final placement resolves it
 * against the resulting height without feeding that gap back into sizing. */
static void layout_grid_resolve_row_gap(struct grid_layout_ctx *ctx)
{
    css_fixed_or_calc value = (css_fixed_or_calc)0;
    css_unit unit = CSS_UNIT_PX;
    ctx->row_gap = 0;
    int height = ctx->grid->height == AUTO ? -1 : ctx->grid->height;
    if (css_computed_row_gap(ctx->grid->style, &value, &unit) == CSS_ROW_GAP_SET &&
        !lh__length_to_px(ctx->grid->style, ctx->unit_len_ctx, height, value, unit, &ctx->row_gap))
        ctx->row_gap = 0;
    ctx->row_gap = max(0, ctx->row_gap);
}

/* Allocate the existing scratch buffers and resolve tracks and flow direction.
 * The caller also cleans up partially initialized state on failure. */
static bool layout_grid_prepare(struct grid_layout_ctx *ctx, int available_width)
{
    struct box *grid = ctx->grid;
    ctx->num_cols = layout_grid_get_column_count(grid);
    NSLOG(layout, WARNING, "GRID LAYOUT: grid=%p avail_w=%d num_cols=%d children=%p", grid, available_width,
        ctx->num_cols, grid->children);
    ctx->available_width = available_width;
    ctx->columns = calloc(ctx->num_cols, sizeof(*ctx->columns));
    if (!ctx->columns)
        return false;

    css_fixed_or_calc gap_len = (css_fixed_or_calc)0;
    css_unit gap_unit = CSS_UNIT_PX;
    if (grid->style != NULL && css_computed_column_gap(grid->style, &gap_len, &gap_unit) == CSS_COLUMN_GAP_SET) {
        if (!lh__length_to_px(grid->style, ctx->unit_len_ctx, available_width, gap_len, gap_unit, &ctx->column_gap)) {
            ctx->column_gap = 0;
        }
    }
    layout_grid_resolve_row_gap(ctx);

    if (grid->style &&
        css_computed_grid_template_rows(grid->style, &ctx->row_track_count, &ctx->row_tracks) == CSS_GRID_TEMPLATE_SET)
        ctx->max_row = ctx->row_track_count;

    uint8_t auto_flow = CSS_GRID_AUTO_FLOW_ROW;
    if (grid->style != NULL)
        auto_flow = css_computed_grid_auto_flow(grid->style);
    ctx->flow_is_column = (auto_flow == CSS_GRID_AUTO_FLOW_COLUMN || auto_flow == CSS_GRID_AUTO_FLOW_COLUMN_DENSE);
    ctx->is_dense = (auto_flow == CSS_GRID_AUTO_FLOW_ROW_DENSE || auto_flow == CSS_GRID_AUTO_FLOW_COLUMN_DENSE);
    ctx->num_rows = ctx->flow_is_column ? layout_grid_get_explicit_row_count(grid) : 1;
    NSLOG(layout, INFO, "GRID LAYOUT: grid-auto-flow=%d (column=%s)", auto_flow, ctx->flow_is_column ? "yes" : "no");

    ctx->occupied_max_rows = 100;
    ctx->occupied = calloc(ctx->occupied_max_rows * ctx->num_cols, sizeof(bool));
    if (!ctx->occupied)
        return false;
    NSLOG(layout, INFO, "GRID LAYOUT: allocated %dx%d occupation grid (dense=%d)", ctx->num_cols,
        ctx->occupied_max_rows, ctx->is_dense);

    for (struct box *child = grid->children; child; child = child->next) {
        /* Absolute/fixed children do not occupy grid cells. Their separate
         * positioning pass also needs a fresh fallback origin on reflow. */
        if (child->style && lh__box_is_absolute(child)) {
            continue;
        }
        ctx->item_count++;
    }
    if (ctx->item_count > 0) {
        ctx->item_cache = calloc(ctx->item_count, sizeof(struct grid_item_cache));
        if (!ctx->item_cache)
            return false;
    }
    return true;
}

/* Resolve the grid area without changing the placement cursor or occupancy. */
static void layout_grid_resolve_placement(const struct grid_layout_ctx *ctx, struct grid_placement *placement)
{
    /* Determine span from explicit line numbers if not using span syntax */
    if (placement->col_end == GRID_PLACEMENT_SPAN || placement->col_start == GRID_PLACEMENT_SPAN) {
        /* col_span already set by get_grid_item_placement */
    } else if (placement->col_end != GRID_PLACEMENT_AUTO && placement->col_end > placement->col_start &&
        placement->col_start != GRID_PLACEMENT_AUTO) {
        placement->col_span = placement->col_end - placement->col_start;
    }
    /* else col_span defaults to 1 from get_grid_item_placement */

    if (placement->row_end == GRID_PLACEMENT_SPAN || placement->row_start == GRID_PLACEMENT_SPAN) {
        /* row_span already set by get_grid_item_placement */
    } else if (placement->row_end != GRID_PLACEMENT_AUTO && placement->row_end > placement->row_start &&
        placement->row_start != GRID_PLACEMENT_AUTO) {
        placement->row_span = placement->row_end - placement->row_start;
    }
    /* else row_span defaults to 1 from get_grid_item_placement */

    /* Determine item position based on explicit placement or
     * auto-flow */
    /* Note: GRID_PLACEMENT_SPAN means auto-place but with a span,
     * not an explicit column/row position. Only SET provides position. */
    if (placement->col_start != GRID_PLACEMENT_AUTO && placement->col_start != GRID_PLACEMENT_SPAN) {
        placement->col = placement->col_start;
    } else if (ctx->is_dense) {
        /* CSS Grid spec §8.5: Dense mode - scan from start
         * for first available cell that fits item's span.
         * For row flow: scan row-by-row (row 0: cols 0,1,2..., row 1: cols 0,1,2...)
         * For column flow: scan column-by-column (col 0: rows 0,1,2..., col 1: rows 0,1,2...)
         */
        placement->col = -1;
        if (ctx->flow_is_column) {
            /* Column flow: scan column-first, respecting explicit row count */
            int max_scan_row = ctx->num_rows - placement->row_span;
            for (int scan_col = 0; scan_col <= ctx->num_cols - placement->col_span && placement->col < 0; scan_col++) {
                for (int scan_row = 0; scan_row <= max_scan_row; scan_row++) {
                    /* Check if all cells in span are free */
                    bool fits = true;
                    for (int dr = 0; dr < placement->row_span && fits; dr++) {
                        for (int dc = 0; dc < placement->col_span && fits; dc++) {
                            int idx = (scan_row + dr) * ctx->num_cols + scan_col + dc;
                            if (idx >= ctx->occupied_max_rows * ctx->num_cols || ctx->occupied[idx]) {
                                fits = false;
                            }
                        }
                    }
                    if (fits) {
                        placement->col = scan_col;
                        placement->row = scan_row;
                        goto dense_found;
                    }
                }
            }
        } else {
            /* Row flow: scan row-first */
            for (int scan_row = 0; scan_row < ctx->occupied_max_rows && placement->col < 0; scan_row++) {
                for (int scan_col = 0; scan_col <= ctx->num_cols - placement->col_span; scan_col++) {
                    /* Check if all cells in span are free */
                    bool fits = true;
                    for (int dr = 0; dr < placement->row_span && fits; dr++) {
                        for (int dc = 0; dc < placement->col_span && fits; dc++) {
                            int idx = (scan_row + dr) * ctx->num_cols + scan_col + dc;
                            if (idx >= ctx->occupied_max_rows * ctx->num_cols || ctx->occupied[idx]) {
                                fits = false;
                            }
                        }
                    }
                    if (fits) {
                        placement->col = scan_col;
                        placement->row = scan_row;
                        goto dense_found;
                    }
                }
            }
        }
    dense_found:
        if (placement->col < 0) {
            /* Fallback: use cursor if scan failed */
            placement->col = ctx->auto_col;
            placement->row = ctx->auto_row;
        }
    } else {
        /* Normal mode: scan from cursor for first free cell */
        placement->col = -1;
        placement->row = ctx->auto_row;
        for (int scan_col = ctx->auto_col; scan_col < ctx->num_cols && placement->col < 0; scan_col++) {
            /* Check if cell is free */
            int idx = ctx->auto_row * ctx->num_cols + scan_col;
            if (idx < ctx->occupied_max_rows * ctx->num_cols && !ctx->occupied[idx]) {
                placement->col = scan_col;
                break; /* Found first free cell */
            }
        }
        /* If no free cell in current row, advance to next rows */
        if (placement->col < 0) {
            for (int scan_row = ctx->auto_row + 1; scan_row < ctx->occupied_max_rows && placement->col < 0;
                scan_row++) {
                for (int scan_col = 0; scan_col < ctx->num_cols; scan_col++) {
                    int idx = scan_row * ctx->num_cols + scan_col;
                    if (!ctx->occupied[idx]) {
                        placement->col = scan_col;
                        placement->row = scan_row;
                        break;
                    }
                }
            }
        }
        if (placement->col < 0) {
            /* Fallback: use cursor if scan failed */
            placement->col = ctx->auto_col;
            placement->row = ctx->auto_row;
        }
    }

    if (placement->row_start != GRID_PLACEMENT_AUTO && placement->row_start != GRID_PLACEMENT_SPAN) {
        placement->row = placement->row_start;
    } else if (placement->col_start != GRID_PLACEMENT_AUTO && placement->col_start != GRID_PLACEMENT_SPAN) {
        /* Phase 2: definite column, auto row - find first free row in this column */
        placement->row = 0;
        for (int scan_row = 0; scan_row < ctx->occupied_max_rows; scan_row++) {
            int idx = scan_row * ctx->num_cols + placement->col;
            if (idx < ctx->occupied_max_rows * ctx->num_cols && !ctx->occupied[idx]) {
                placement->row = scan_row;
                break;
            }
        }
    }
    /* Note: for fully auto-placed items, item_row is already set by the scan above */

    NSLOG(layout, WARNING, "GRID PLACE DECISION: item_col=%d item_row=%d (cursor col=%d row=%d) flow_is_column=%d",
        placement->col, placement->row, ctx->auto_col, ctx->auto_row, ctx->flow_is_column);

    /* Clamp to valid range */
    if (placement->col < 0)
        placement->col = 0;
    if (placement->col >= ctx->num_cols)
        placement->col = ctx->num_cols - 1;
    if (placement->row < 0)
        placement->row = 0;

    /* Recalculate span after positioning (for auto end values) */
    if (placement->col_end != GRID_PLACEMENT_AUTO && placement->col_end > placement->col) {
        placement->col_span = placement->col_end - placement->col;
    }
    if (placement->row_end != GRID_PLACEMENT_AUTO && placement->row_end > placement->row) {
        placement->row_span = placement->row_end - placement->row;
    }

    /* Clamp span to grid bounds */
    if (placement->col + placement->col_span > ctx->num_cols) {
        placement->col_span = ctx->num_cols - placement->col;
    }
}

/* Preserve the item's inner formatting context for both measurement and stretch. */
static bool layout_grid_item_contents(const struct grid_layout_ctx *ctx, struct box *child, int available_width)
{
    bool success = true;
    child->float_container = ctx->grid;
    switch (child->type) {
    case BOX_BLOCK:
    case BOX_INLINE_BLOCK:
        success = layout_block_context(child, -1, ctx->content);
        break;
    case BOX_FLEX:
    case BOX_INLINE_FLEX:
        success = layout_flex(child, available_width, ctx->content);
        break;
    case BOX_GRID:
    case BOX_INLINE_GRID:
        success = layout_grid(child, available_width, ctx->content);
        break;
    case BOX_TABLE:
        success = layout_table(child, available_width, ctx->content);
        break;
    default:
        break;
    }
    child->float_container = NULL;
    return success;
}

/* Resolve inline sizing, lay out contents, then distribute inline auto margins. */
static bool layout_grid_size_item(const struct grid_layout_ctx *ctx, struct box *child, int child_width)
{
    assert(lh__is_resolved(child_width));
    /* Resolve CSS dimensions - this populates child->padding and child->border */
    /* CSS constraints are separate from the cached intrinsic widths. */
    struct css_size item_min_width, item_min_height;
    int item_max_width, item_max_height;
    layout_find_dimensions(ctx->unit_len_ctx, child_width, -1, child,
        &(struct layout_dimension_outputs){
            .width = &child->width,
            .height = &child->height,
            .max_width = &item_max_width,
            .min_width = &item_min_width,
            .min_height = &item_min_height,
            .max_height = &item_max_height,
            .margin = child->margin,
            .padding = child->padding,
            .border = child->border,
        });

    bool auto_left = child->margin[LEFT] == AUTO;
    bool auto_right = child->margin[RIGHT] == AUTO;
    int margin_left = auto_left ? 0 : child->margin[LEFT];
    int margin_right = auto_right ? 0 : child->margin[RIGHT];
    int edges = child->padding[LEFT] + child->padding[RIGHT] + child->border[LEFT].width + child->border[RIGHT].width;

    if (child->width == AUTO) {
        int outer_width = child_width;
        if (auto_left || auto_right) {
            /* Intrinsic widths include margins, borders and padding,
             * with auto margins treated as zero during min/max layout. */
            outer_width = min(max(child->min_width.value, outer_width), child->max_width);
        }
        child->width = max(0, outer_width - margin_left - margin_right - edges);
    }
    if (item_max_width >= 0) {
        child->width = min(child->width, item_max_width);
    }
    if (item_min_width.type == CSS_SIZE_SET) {
        child->width = max(child->width, item_min_width.value);
    }
    child->margin[LEFT] = margin_left;
    child->margin[RIGHT] = margin_right;

    if (child->height != AUTO) {
        if (item_max_height >= 0)
            child->height = min(child->height, item_max_height);
        if (item_min_height.type == CSS_SIZE_SET)
            child->height = max(child->height, item_min_height.value);
    }

    child->flags &= ~(HEIGHT_STRETCHED | ASPECT_RATIO_HEIGHT);
    if (!layout_grid_item_contents(ctx, child, child_width))
        return false;

    /* Auto margins absorb only positive free space, including after
     * a table or nested layout has adjusted the item's used width. */
    assert(lh__is_resolved(child->width));
    int free_width = max(0, child_width - child->width - edges - margin_left - margin_right);
    child->margin[LEFT] = auto_left ? free_width / (auto_right ? 2 : 1) : margin_left;
    child->margin[RIGHT] = auto_right ? free_width - (auto_left ? child->margin[LEFT] : 0) : margin_right;
    return true;
}

/* Each inner formatter sees the same explicit area input, including any
 * further dimension/constraint lookups it performs. Never change parent boxes
 * to emulate a containing block, and restore the borrowed input on failure. */
static bool layout_grid_measure_item(
    const struct grid_layout_ctx *ctx, struct box *child, const struct layout_containing_size *area)
{
    const struct layout_containing_size *previous = child->containing_size;
    child->containing_size = area;
    bool success = layout_grid_size_item(ctx, child, area->width);
    child->containing_size = previous;
    return success;
}

/* Resolve normal separately from explicit stretch: replaced elements and
 * items with a preferred ratio use start alignment (CSS Box Alignment §6.1). */
static uint8_t layout_grid_item_alignment(const struct box *grid, const struct box *child)
{
    uint8_t align = child->style ? css_computed_align_self(child->style) : CSS_ALIGN_SELF_AUTO;
    if (align == CSS_ALIGN_SELF_AUTO)
        align = grid->style ? css_computed_align_items(grid->style) : CSS_ALIGN_ITEMS_NORMAL;
    if (align == CSS_ALIGN_ITEMS_NORMAL)
        align = lh__box_is_replace(child) || layout_ratio_dimension(child, child->width, true) != AUTO
            ? CSS_ALIGN_ITEMS_FLEX_START
            : CSS_ALIGN_ITEMS_STRETCH;
    return align;
}

/* The same track definitions apply on both axes. An indefinite percentage
 * is auto during intrinsic sizing (Grid 7.2.1 / 11.4). */
static bool
layout_grid_track_length(const struct grid_layout_ctx *ctx, int available, css_fixed value, css_unit unit, int *length)
{
    if (unit >= CSS_UNIT_FR && unit != CSS_UNIT_PCT)
        return false;
    return lh__length_to_px(ctx->grid->style, ctx->unit_len_ctx, available, (css_fixed_or_calc)value, unit, length);
}

static struct grid_track
layout_grid_init_track(const struct grid_layout_ctx *ctx, const css_computed_grid_track *track, int available)
{
    css_unit min_unit = CSS_UNIT_GRID_AUTO, max_unit = CSS_UNIT_GRID_AUTO;
    css_fixed min_value = 0, max_value = 0;
    if (track != NULL) {
        min_unit = max_unit = track->unit;
        min_value = max_value = track->value;
        if (track->unit == CSS_UNIT_MINMAX) {
            min_unit = track->min_unit;
            max_unit = track->max_unit;
            max_value = track->max_value;
        } else if (track->unit == CSS_UNIT_FR) {
            min_unit = CSS_UNIT_GRID_AUTO;
        }
    }
    if (available < 0) {
        if (min_unit == CSS_UNIT_PCT)
            min_unit = CSS_UNIT_GRID_AUTO;
        if (max_unit == CSS_UNIT_PCT)
            max_unit = CSS_UNIT_GRID_AUTO;
    }
    struct grid_track result = {.min_unit = min_unit, .max_unit = max_unit};
    result.intrinsic_min = !layout_grid_track_length(ctx, available, min_value, min_unit, &result.size);
    result.auto_min = min_unit == CSS_UNIT_GRID_AUTO;
    result.size = max(0, result.size);
    result.flexible = max_unit == CSS_UNIT_FR;
    result.flex = result.flexible ? max(0, max_value) : 0;
    result.intrinsic_max = !result.flexible &&
        !layout_grid_track_length(ctx, available, max_value, max_unit, &result.limit);
    result.limit = result.flexible || result.intrinsic_max ? -1 : max(result.size, result.limit);
    result.stretch = max_unit == CSS_UNIT_GRID_AUTO;
    return result;
}

static struct grid_track layout_grid_init_row(const struct grid_layout_ctx *ctx, int index)
{
    const css_computed_grid_track *track = index < ctx->row_track_count ? &ctx->row_tracks[index] : NULL;
    return layout_grid_init_track(ctx, track, ctx->grid->height == AUTO ? -1 : ctx->grid->height);
}

static bool layout_grid_init_rows(struct grid_layout_ctx *ctx)
{
    if (ctx->max_row > ctx->row_capacity) {
        struct grid_track *rows = realloc(ctx->rows, ctx->max_row * sizeof(*rows));
        if (rows == NULL)
            return false;
        ctx->rows = rows;
        ctx->row_capacity = ctx->max_row;
    }
    for (int r = 0; r < ctx->max_row; r++)
        ctx->rows[r] = layout_grid_init_row(ctx, r);
    return true;
}

/* Size non-spanning items first, then distribute any spanning deficit over
 * content-sized tracks. Fixed rows remain fixed even when a baseline overflows. */
static double layout_grid_find_fr(const struct grid_track *tracks, int count, int space);

enum grid_track_growth {
    GRID_TRACK_MINIMUM,
    GRID_TRACK_CONTENT_MINIMUM,
    GRID_TRACK_MAX_CONTENT_MINIMUM,
    GRID_TRACK_MAXIMUM,
    GRID_TRACK_MAX_CONTENT_MAXIMUM
};

static bool layout_grid_track_affected(const struct grid_track *row, enum grid_track_growth growth)
{
    switch (growth) {
    case GRID_TRACK_MINIMUM:
        return row->intrinsic_min;
    case GRID_TRACK_CONTENT_MINIMUM:
        return row->intrinsic_min && !row->auto_min;
    case GRID_TRACK_MAX_CONTENT_MINIMUM:
        return row->min_unit == CSS_UNIT_MAX_CONTENT;
    case GRID_TRACK_MAXIMUM:
        return row->intrinsic_max;
    case GRID_TRACK_MAX_CONTENT_MAXIMUM:
        return row->intrinsic_max && row->max_unit != CSS_UNIT_MIN_CONTENT;
    }
    return false;
}

static int layout_grid_track_growth_capacity(
    const struct grid_track *row, enum grid_track_growth growth, int pass, bool prefer_intrinsic_max)
{
    bool affected = layout_grid_track_affected(row, growth);
    bool eligible = pass == 1 ? !affected : affected;
    if (pass == 2 && prefer_intrinsic_max)
        eligible &= row->intrinsic_max;
    if (!eligible)
        return 0;
    if (pass == 2 || row->limit < 0 || (growth >= GRID_TRACK_MAXIMUM && row->infinitely_growable))
        return INT_MAX;
    int size = growth >= GRID_TRACK_MAXIMUM ? max(row->size, row->limit) : row->size;
    return max(0, row->limit - size - row->increase);
}

/* Freeze at growth limits before redistributing to other tracks. Only after
 * those are exhausted may intrinsic minima grow beyond their limits. The
 * per-item increases stay separate from the same-span group's planned sizes. */
static void layout_grid_distribute_tracks(struct grid_track *rows, int count, int space, enum grid_track_growth growth)
{
    bool prefer_intrinsic_max = false;
    for (int r = 0; r < count; r++)
        prefer_intrinsic_max |= layout_grid_track_affected(&rows[r], growth) && rows[r].intrinsic_max;
    /* Affected tracks up to limits, other tracks up to limits, then the
     * permitted intrinsic tracks beyond limits (Grid 11.5.1). */
    for (int pass = 0; pass < 3 && space > 0; pass++) {
        for (;;) {
            int growable = 0;
            for (int r = 0; r < count; r++)
                growable += layout_grid_track_growth_capacity(&rows[r], growth, pass, prefer_intrinsic_max) > 0;
            if (growable == 0 || space == 0)
                break;
            int share = max(1, space / growable);
            for (int r = 0; r < count && space > 0; r++) {
                int capacity = layout_grid_track_growth_capacity(&rows[r], growth, pass, prefer_intrinsic_max);
                int extra = min(space, min(share, capacity));
                rows[r].increase += extra;
                space -= extra;
            }
        }
    }
}

static void
layout_grid_grow_tracks(struct grid_track *tracks, int count, int gap, int contribution, enum grid_track_growth growth)
{
    int remaining = contribution - (count - 1) * gap;
    int growable = 0;
    if (count > 1) {
        bool flexible = false;
        for (int r = 0; r < count; r++)
            flexible |= tracks[r].flexible;
        if (flexible) {
            if (growth >= GRID_TRACK_MAXIMUM)
                return;
            double factors = 0;
            for (int r = 0; r < count; r++) {
                remaining -= tracks[r].size;
                if (tracks[r].flexible && layout_grid_track_affected(&tracks[r], growth)) {
                    factors += FIXTOFLT(tracks[r].flex);
                    growable++;
                }
            }
            if (remaining <= 0 || growable == 0)
                return;
            /* Grid 11.5 step 4: distribute the deficit by flex factors.
             * Below a total factor of one, share the unclaimed part equally;
             * partial fill applies to final fr expansion, not item minima. */
            double assigned = 0;
            int rounded = 0;
            for (int r = 0; r < count; r++) {
                struct grid_track *row = &tracks[r];
                if (row->flexible && layout_grid_track_affected(row, growth)) {
                    assigned += remaining *
                        (FIXTOFLT(row->flex) / max(1.0, factors) + max(0.0, 1.0 - factors) / growable);
                    int next = layout_round_ratio_dimension(assigned);
                    row->planned_size = max(row->planned_size, row->size + next - rounded);
                    rounded = next;
                }
            }
            return;
        }
    }
    for (int r = 0; r < count; r++) {
        struct grid_track *row = &tracks[r];
        row->increase = 0;
        remaining -= growth >= GRID_TRACK_MAXIMUM ? max(row->size, row->limit) : row->size;
        growable += layout_grid_track_affected(row, growth);
    }
    if (growable == 0)
        return;
    layout_grid_distribute_tracks(tracks, count, remaining, growth);
    for (int r = 0; r < count; r++) {
        struct grid_track *row = &tracks[r];
        if (growth >= GRID_TRACK_MAXIMUM)
            row->planned_limit = max(row->planned_limit, max(row->size, row->limit) + row->increase);
        else
            row->planned_size = max(row->planned_size, row->size + row->increase);
    }
}

/* CSS Grid 6.6/11.5: an auto track uses an item's minimum contribution,
 * which can be smaller than its content height. Explicit sizes, automatic
 * minima, scroll containers and a span crossing flexible rows differ here. */
static int layout_grid_minimum_contribution(const struct grid_layout_ctx *ctx, const struct grid_item_cache *item)
{
    const struct box *child = item->box;
    int edges = lh__delta_outer_height(child);
    int content = child->height + edges;
    if (child->style == NULL)
        return content;
    css_fixed_or_calc value = (css_fixed_or_calc)0;
    css_unit unit = CSS_UNIT_PX;
    uint8_t type = css_computed_height(child->style, &value, &unit);
    if (type == CSS_HEIGHT_SET && unit != CSS_UNIT_PCT && unit != CSS_UNIT_CALC)
        return content;
    struct css_size minimum;
    const struct layout_dimension_context dims = {
        .box = child,
        .unit_len_ctx = lh__with_container_dimensions(ctx->unit_len_ctx, item->area.width, item->area.height),
        .available_width = item->area.width,
        .available_height = item->area.height,
    };
    if (css_computed_min_height(child->style, &value, &unit) == CSS_MIN_HEIGHT_SET) {
        layout_find_height_limits(&dims, &(struct layout_dimension_outputs){.min_height = &minimum});
        if (minimum.type == CSS_SIZE_SET)
            return minimum.value + edges;
    }
    bool auto_min = false, flexible = false, fixed_max = true;
    int cap = (item->row_span - 1) * ctx->row_gap;
    for (int r = item->item_row; r < item->item_row + item->row_span; r++) {
        const struct grid_track *row = &ctx->rows[r];
        auto_min |= row->auto_min;
        flexible |= row->flexible;
        fixed_max &= !row->intrinsic_max && !row->flexible;
        cap += row->limit;
    }
    if (!auto_min || (item->row_span > 1 && flexible) ||
        css_computed_overflow_x(child->style) != CSS_OVERFLOW_VISIBLE ||
        css_computed_overflow_y(child->style) != CSS_OVERFLOW_VISIBLE)
        return edges;
    return fixed_max ? min(content, max(edges, cap)) : content;
}

/* Reused before intrinsic sizing and after final item percentage/stretch sizing. */
static void layout_grid_update_baselines(struct grid_layout_ctx *ctx)
{
    if (ctx->row_baselines == NULL)
        return;
    for (int r = 0; r < ctx->max_row; r++)
        ctx->row_baselines[r] = INT_MIN;
    for (int i = 0; i < ctx->cache_idx; i++) {
        const struct grid_item_cache *item = &ctx->item_cache[i];
        const struct box *child = item->box;
        if (item->baseline)
            ctx->row_baselines[item->item_row] = max(ctx->row_baselines[item->item_row],
                child->margin[TOP] + child->border[TOP].width + lh__box_first_baseline(child));
    }
}

/* Baseline shims are part of each item's intrinsic contribution, including
 * margins and spanning items. No per-box persistent baseline storage is added. */
static bool layout_grid_size_rows(struct grid_layout_ctx *ctx)
{
    if (ctx->max_row == 0)
        return true;
    if (!layout_grid_init_rows(ctx))
        return false;
    bool has_baselines = false;
    for (int i = 0; i < ctx->cache_idx; i++)
        has_baselines |= ctx->item_cache[i].baseline;
    if (has_baselines) {
        if (ctx->row_baselines == NULL)
            ctx->row_baselines = malloc(ctx->max_row * sizeof(int));
        if (ctx->row_baselines == NULL)
            return false;
        layout_grid_update_baselines(ctx);
    }
    /* Increasing span lengths, with simultaneous increases within each group
     * (Grid 11.5.1), make the result independent of DOM order. */
    int span = 0;
    for (;;) {
        int next_span = INT_MAX;
        for (int i = 0; i < ctx->cache_idx; i++)
            if (ctx->item_cache[i].row_span > span)
                next_span = min(next_span, ctx->item_cache[i].row_span);
        if (next_span == INT_MAX)
            break;
        span = next_span;
        for (enum grid_track_growth growth = GRID_TRACK_MINIMUM; growth <= GRID_TRACK_MAXIMUM; growth++) {
            for (int r = 0; r < ctx->max_row; r++) {
                ctx->rows[r].planned_size = ctx->rows[r].size;
                ctx->rows[r].planned_limit = ctx->rows[r].limit;
            }
            for (int i = 0; i < ctx->cache_idx; i++) {
                const struct grid_item_cache *item = &ctx->item_cache[i];
                if (item->row_span != span)
                    continue;
                const struct box *b = item->box;
                assert(lh__is_resolved(b->height));
                int height = growth == GRID_TRACK_MINIMUM ? layout_grid_minimum_contribution(ctx, item)
                                                          : b->height + lh__delta_outer_height(b);
                if (item->baseline)
                    height += ctx->row_baselines[item->item_row] -
                        (b->margin[TOP] + b->border[TOP].width + lh__box_first_baseline(b));
                layout_grid_grow_tracks(&ctx->rows[item->item_row], item->row_span, ctx->row_gap, height, growth);
            }
            for (int r = 0; r < ctx->max_row; r++) {
                struct grid_track *row = &ctx->rows[r];
                row->size = row->planned_size;
                row->limit = row->planned_limit;
                if (row->limit >= 0)
                    row->limit = max(row->size, row->limit);
            }
        }
    }
    for (int r = 0; r < ctx->max_row; r++)
        if (!ctx->rows[r].flexible)
            ctx->rows[r].limit = max(ctx->rows[r].limit, ctx->rows[r].size);
    return true;
}

static int layout_grid_rows_height(const struct grid_layout_ctx *ctx)
{
    int height = max(0, ctx->max_row - 1) * ctx->row_gap;
    for (int r = 0; r < ctx->max_row; r++)
        height += ctx->rows[r].size;
    return height;
}

/* CSS Grid 11.7.1: freeze tracks whose content minimum exceeds their share,
 * then recompute the fraction for the others. Factors below one retain their
 * partial share; they must not be rounded to an integer number of fr units. */
static double layout_grid_find_fr(const struct grid_track *tracks, int count, int space)
{
    double fraction = (double)INT_MAX * 1024;
    for (int pass = 0; pass <= count; pass++) {
        double leftover = space, factors = 0;
        for (int r = 0; r < count; r++) {
            const struct grid_track *row = &tracks[r];
            double factor = FIXTOFLT(row->flex);
            if (!row->flexible || factor == 0 || factor * fraction < row->size)
                leftover -= row->size;
            else
                factors += factor;
        }
        if (factors == 0)
            return 0;
        double next = max(0, leftover) / max(1.0, factors);
        if (next >= fraction)
            break;
        fraction = next;
    }
    return fraction;
}

/* Maximize non-flexible tracks up to their growth limits before sizing fr.
 * Auto-height rows use those limits as their natural content size. */
static void layout_grid_maximize_tracks(struct grid_track *tracks, int count, int free)
{
    while (free > 0) {
        int growable = 0;
        for (int r = 0; r < count; r++)
            growable += tracks[r].limit > tracks[r].size;
        if (growable == 0)
            break;
        int share = max(1, free / growable);
        for (int r = 0; r < count && free > 0; r++) {
            struct grid_track *row = &tracks[r];
            int extra = min(free, min(share, max(0, row->limit - row->size)));
            row->size += extra;
            free -= extra;
        }
    }
}

static void layout_grid_maximize_rows(struct grid_layout_ctx *ctx)
{
    int free = ctx->grid->height == AUTO ? INT_MAX : max(0, ctx->grid->height - layout_grid_rows_height(ctx));
    layout_grid_maximize_tracks(ctx->rows, ctx->max_row, free);
}

enum grid_size_constraint { GRID_SIZE_DEFINITE, GRID_SIZE_MIN_CONTENT, GRID_SIZE_MAX_CONTENT };

/* Intrinsic contributions are already measured by the shared min/max pass.
 * Percentages in an intrinsic grid area are cyclic, so its outer percentage
 * edges contribute zero here and resolve during final item layout. */
static int layout_grid_column_minimum(const struct grid_layout_ctx *ctx, const struct grid_item_cache *item)
{
    const struct box *child = item->box;
    if (child->style == NULL)
        return child->min_width.value;
    css_fixed_or_calc value = (css_fixed_or_calc)0;
    css_unit unit = CSS_UNIT_PX;
    int preferred;
    if (css_computed_width_px(child->style, ctx->unit_len_ctx, -1, &preferred) == CSS_WIDTH_SET)
        return child->min_width.value;

    int edges = 0;
    float fraction = 0;
    calculate_mbp_width(ctx->unit_len_ctx, child->style, LEFT, true, true, true, &edges, &fraction);
    calculate_mbp_width(ctx->unit_len_ctx, child->style, RIGHT, true, true, true, &edges, &fraction);
    const struct layout_dimension_context dims = {
        .box = child,
        .unit_len_ctx = lh__with_container_dimensions(ctx->unit_len_ctx, 0, -1),
        .available_width = 0,
        .available_height = -1,
    };
    if (css_computed_min_width(child->style, &value, &unit) == CSS_MIN_WIDTH_SET) {
        struct css_size minimum;
        layout_find_width_limits(&dims, &(struct layout_dimension_outputs){.min_width = &minimum});
        if (minimum.type == CSS_SIZE_SET)
            return minimum.value + edges;
    }

    bool auto_min = false, flexible = false, fixed_max = true;
    int cap = (item->col_span - 1) * ctx->column_gap;
    for (int c = item->item_col; c < item->item_col + item->col_span; c++) {
        const struct grid_track *track = &ctx->columns[c];
        auto_min |= track->auto_min;
        flexible |= track->flexible;
        fixed_max &= !track->intrinsic_max && !track->flexible;
        cap += max(0, track->limit);
    }
    if (!auto_min || (item->col_span > 1 && flexible) ||
        css_computed_overflow_x(child->style) != CSS_OVERFLOW_VISIBLE ||
        css_computed_overflow_y(child->style) != CSS_OVERFLOW_VISIBLE)
        return edges;
    int content = child->min_width.value;
    /* The ordinary block intrinsic pass suppresses a cyclic percentage's
     * minimum contribution; Grid's automatic minimum still uses its content. */
    if (css_computed_width(child->style, &value, &unit) == CSS_WIDTH_SET && unit == CSS_UNIT_PCT &&
        (child->type == BOX_BLOCK || child->type == BOX_GRID || child->type == BOX_FLEX) && child->object == NULL)
        content = max(content, child->content_min_width + edges);
    return fixed_max ? min(content, max(edges, cap)) : content;
}

static int layout_grid_columns_width(const struct grid_layout_ctx *ctx)
{
    int width = max(0, ctx->num_cols - 1) * ctx->column_gap;
    for (int c = 0; c < ctx->num_cols; c++)
        width += ctx->columns[c].size;
    return width;
}

static bool layout_grid_column_flex_span(const struct grid_layout_ctx *ctx, const struct grid_item_cache *item)
{
    if (item->col_span > 1)
        for (int c = item->item_col; c < item->item_col + item->col_span; c++)
            if (ctx->columns[c].flexible)
                return true;
    return false;
}

static int layout_grid_column_contribution(const struct grid_layout_ctx *ctx, const struct grid_item_cache *item,
    enum grid_track_growth growth, enum grid_size_constraint constraint)
{
    if (growth == GRID_TRACK_MAX_CONTENT_MINIMUM || growth == GRID_TRACK_MAX_CONTENT_MAXIMUM)
        return item->box->max_width;
    if (growth != GRID_TRACK_MINIMUM)
        return item->box->min_width.value;
    int contribution = layout_grid_column_minimum(ctx, item);
    if (constraint != GRID_SIZE_DEFINITE) {
        int cap = (item->col_span - 1) * ctx->column_gap;
        bool fixed_max = true;
        for (int c = item->item_col; c < item->item_col + item->col_span; c++) {
            const struct grid_track *track = &ctx->columns[c];
            fixed_max &= !track->intrinsic_max && !track->flexible;
            cap += max(0, track->limit);
        }
        int limited = item->box->min_width.value;
        if (fixed_max)
            limited = min(limited, cap);
        contribution = max(contribution, limited);
    }
    return contribution;
}

/* Planned increases within a span group are simultaneous. Flexible spans are
 * considered together after non-flexible spans (Grid 11.5 steps 3 and 4). */
static void layout_grid_grow_columns(
    struct grid_layout_ctx *ctx, enum grid_size_constraint constraint, int span, bool flexible_spans)
{
    for (enum grid_track_growth growth = GRID_TRACK_MINIMUM; growth <= GRID_TRACK_MAX_CONTENT_MAXIMUM; growth++) {
        for (int c = 0; c < ctx->num_cols; c++) {
            struct grid_track *track = &ctx->columns[c];
            track->planned_size = track->size;
            track->planned_limit = track->limit;
            if (growth == GRID_TRACK_MAXIMUM)
                track->infinitely_growable = track->limit < 0;
        }
        for (int i = 0; i < ctx->cache_idx; i++) {
            const struct grid_item_cache *item = &ctx->item_cache[i];
            if (layout_grid_column_flex_span(ctx, item) != flexible_spans ||
                (!flexible_spans && item->col_span != span))
                continue;
            int contribution = layout_grid_column_contribution(ctx, item, growth, constraint);
            layout_grid_grow_tracks(
                &ctx->columns[item->item_col], item->col_span, ctx->column_gap, contribution, growth);
        }
        for (int c = 0; c < ctx->num_cols; c++) {
            struct grid_track *track = &ctx->columns[c];
            track->size = track->planned_size;
            track->limit = track->planned_limit;
            if (track->limit >= 0)
                track->limit = max(track->size, track->limit);
        }
    }
    for (int c = 0; c < ctx->num_cols; c++)
        ctx->columns[c].infinitely_growable = false;
}

static void layout_grid_intrinsic_columns(struct grid_layout_ctx *ctx, enum grid_size_constraint constraint)
{
    int span = 0;
    bool flexible_spans = false;
    for (;;) {
        int next_span = INT_MAX;
        for (int i = 0; i < ctx->cache_idx; i++) {
            const struct grid_item_cache *item = &ctx->item_cache[i];
            if (layout_grid_column_flex_span(ctx, item))
                flexible_spans = true;
            else if (item->col_span > span)
                next_span = min(next_span, item->col_span);
        }
        if (next_span == INT_MAX)
            break;
        span = next_span;
        layout_grid_grow_columns(ctx, constraint, span, false);
    }
    if (flexible_spans)
        layout_grid_grow_columns(ctx, constraint, 0, true);
    for (int c = 0; c < ctx->num_cols; c++)
        if (!ctx->columns[c].flexible)
            ctx->columns[c].limit = max(ctx->columns[c].size, ctx->columns[c].limit);
}

static void layout_grid_align_columns(struct grid_layout_ctx *ctx)
{
    uint8_t align = css_computed_justify_content(ctx->grid->style);
    int free = ctx->available_width - layout_grid_columns_width(ctx);
    if ((align == CSS_JUSTIFY_CONTENT_NORMAL || align == CSS_JUSTIFY_CONTENT_STRETCH) && free > 0) {
        int count = 0;
        for (int c = 0; c < ctx->num_cols; c++)
            count += ctx->columns[c].stretch;
        for (int c = 0; c < ctx->num_cols && count > 0; c++) {
            if (ctx->columns[c].stretch) {
                int extra = free / count;
                ctx->columns[c].size += extra;
                free -= extra;
                count--;
            }
        }
    }
    int x = 0;
    for (int c = 0; c < ctx->num_cols; c++) {
        int offset = 0;
        switch (align) {
        case CSS_JUSTIFY_CONTENT_FLEX_END:
            offset = free;
            break;
        case CSS_JUSTIFY_CONTENT_CENTER:
            offset = free / 2;
            break;
        case CSS_JUSTIFY_CONTENT_SPACE_BETWEEN:
            if (ctx->num_cols > 1)
                offset = (int64_t)max(0, free) * c / (ctx->num_cols - 1);
            break;
        case CSS_JUSTIFY_CONTENT_SPACE_AROUND:
            offset = (int64_t)max(0, free) * (2 * c + 1) / (2 * ctx->num_cols);
            break;
        case CSS_JUSTIFY_CONTENT_SPACE_EVENLY:
            offset = (int64_t)max(0, free) * (c + 1) / (ctx->num_cols + 1);
            break;
        }
        ctx->columns[c].offset = x + offset;
        x += ctx->columns[c].size + ctx->column_gap;
    }
}

/* Grid 11.4-11.8: initialize, resolve intrinsic contributions, maximize,
 * expand fr, then stretch auto tracks. The intrinsic container pass uses the
 * same placement and sizing algorithm with zero/infinite available free space. */
static void layout_grid_size_columns(struct grid_layout_ctx *ctx, enum grid_size_constraint constraint)
{
    int32_t count = 0;
    css_computed_grid_track *definitions = NULL;
    if (ctx->grid->style)
        css_computed_grid_template_columns(ctx->grid->style, &count, &definitions);
    int available = constraint == GRID_SIZE_DEFINITE ? ctx->available_width : -1;
    for (int c = 0; c < ctx->num_cols; c++)
        ctx->columns[c] = layout_grid_init_track(ctx, c < count ? &definitions[c] : NULL, available);
    layout_grid_intrinsic_columns(ctx, constraint);
    int free = constraint == GRID_SIZE_MIN_CONTENT
        ? 0
        : (constraint == GRID_SIZE_MAX_CONTENT ? INT_MAX : max(0, available - layout_grid_columns_width(ctx)));
    layout_grid_maximize_tracks(ctx->columns, ctx->num_cols, free);
    double fraction = 0;
    if (constraint == GRID_SIZE_DEFINITE) {
        fraction = layout_grid_find_fr(
            ctx->columns, ctx->num_cols, available - max(0, ctx->num_cols - 1) * ctx->column_gap);
    } else if (constraint == GRID_SIZE_MAX_CONTENT) {
        for (int c = 0; c < ctx->num_cols; c++)
            if (ctx->columns[c].flexible)
                fraction = max(fraction, ctx->columns[c].size / max(1.0, FIXTOFLT(ctx->columns[c].flex)));
        for (int i = 0; i < ctx->cache_idx; i++) {
            const struct grid_item_cache *item = &ctx->item_cache[i];
            int space = item->box->max_width - (item->col_span - 1) * ctx->column_gap;
            fraction = max(fraction, layout_grid_find_fr(&ctx->columns[item->item_col], item->col_span, space));
        }
    }
    for (int c = 0; c < ctx->num_cols; c++)
        if (ctx->columns[c].flexible)
            ctx->columns[c].size = max(
                ctx->columns[c].size, layout_round_ratio_dimension(fraction * FIXTOFLT(ctx->columns[c].flex)));
    if (constraint == GRID_SIZE_DEFINITE)
        layout_grid_align_columns(ctx);
    else {
        int offset = 0;
        for (int c = 0; c < ctx->num_cols; c++) {
            ctx->columns[c].offset = offset;
            offset += ctx->columns[c].size + ctx->column_gap;
        }
    }
}

static void layout_grid_expand_flexible_rows(struct grid_layout_ctx *ctx)
{
    double fraction = 0;
    int available = ctx->grid->height;
    if (available == AUTO) {
        for (int r = 0; r < ctx->max_row; r++) {
            const struct grid_track *row = &ctx->rows[r];
            if (row->flexible)
                fraction = max(fraction, row->size / max(1.0, FIXTOFLT(row->flex)));
        }
        for (int i = 0; i < ctx->cache_idx; i++) {
            const struct grid_item_cache *item = &ctx->item_cache[i];
            int size = item->box->height + lh__delta_outer_height(item->box) - (item->row_span - 1) * ctx->row_gap;
            fraction = max(fraction, layout_grid_find_fr(&ctx->rows[item->item_row], item->row_span, size));
        }
        int natural = max(0, ctx->max_row - 1) * ctx->row_gap;
        for (int r = 0; r < ctx->max_row; r++)
            natural += max(ctx->rows[r].size, layout_round_ratio_dimension(fraction * FIXTOFLT(ctx->rows[r].flex)));
        if (ctx->max_height >= 0 && natural > ctx->max_height)
            available = ctx->max_height;
        if (ctx->min_height.type == CSS_SIZE_SET && natural < ctx->min_height.value)
            available = ctx->min_height.value;
        if (available != AUTO && ctx->min_height.type == CSS_SIZE_SET)
            available = max(available, ctx->min_height.value);
    }
    if (available != AUTO)
        fraction = layout_grid_find_fr(ctx->rows, ctx->max_row, available - max(0, ctx->max_row - 1) * ctx->row_gap);
    for (int r = 0; r < ctx->max_row; r++)
        if (ctx->rows[r].flexible)
            ctx->rows[r].size = max(
                ctx->rows[r].size, layout_round_ratio_dimension(fraction * FIXTOFLT(ctx->rows[r].flex)));
}

/* Final track offsets include content distribution. A spanning item's area
 * includes that additional space, as well as the declared row gap. */
static void layout_grid_align_rows(struct grid_layout_ctx *ctx)
{
    uint8_t align = css_computed_align_content(ctx->grid->style);
    int free = ctx->grid->height - layout_grid_rows_height(ctx);
    if (align == CSS_ALIGN_CONTENT_STRETCH && free > 0) {
        int count = 0;
        for (int r = 0; r < ctx->max_row; r++)
            count += ctx->rows[r].stretch;
        for (int r = 0; r < ctx->max_row && count > 0; r++) {
            if (ctx->rows[r].stretch) {
                int extra = free / count;
                ctx->rows[r].size += extra;
                free -= extra;
                count--;
            }
        }
    }
    int y = 0;
    for (int r = 0; r < ctx->max_row; r++) {
        int offset = 0;
        switch (align) {
        case CSS_ALIGN_CONTENT_FLEX_END:
            offset = free;
            break;
        case CSS_ALIGN_CONTENT_CENTER:
            offset = free / 2;
            break;
        case CSS_ALIGN_CONTENT_SPACE_BETWEEN:
            if (ctx->max_row > 1)
                offset = (int64_t)max(0, free) * r / (ctx->max_row - 1);
            break;
        case CSS_ALIGN_CONTENT_SPACE_AROUND:
            offset = (int64_t)max(0, free) * (2 * r + 1) / (2 * ctx->max_row);
            break;
        case CSS_ALIGN_CONTENT_SPACE_EVENLY:
            offset = (int64_t)max(0, free) * (r + 1) / (ctx->max_row + 1);
            break;
        }
        ctx->rows[r].offset = y + offset;
        y += ctx->rows[r].size + ctx->row_gap;
    }
}

static bool layout_grid_rows_depend_on_height(const struct grid_layout_ctx *ctx)
{
    for (int r = 0; r < ctx->row_track_count; r++) {
        const css_computed_grid_track *track = &ctx->row_tracks[r];
        if (track->unit == CSS_UNIT_PCT ||
            (track->unit == CSS_UNIT_MINMAX && (track->min_unit == CSS_UNIT_PCT || track->max_unit == CSS_UNIT_PCT)))
            return true;
    }
    return false;
}

static bool layout_grid_height_depends_on_area(const struct box *box);

/* Anonymous inline containers do not establish a percentage basis. Ordinary
 * child blocks do: only children whose containing block is this item need the
 * unchanged-size re-layout when its auto height becomes definite. */
static bool layout_grid_percentage_children(const struct box *box)
{
    for (const struct box *child = box->children; child; child = child->next) {
        if (child->style == NULL) {
            if (layout_grid_percentage_children(child))
                return true;
        } else if (!lh__box_is_absolute(child) && layout_grid_height_depends_on_area(child)) {
            return true;
        }
    }
    return false;
}

/* Re-layout at a definite stretched height so percentages, nested flex layout
 * and its exported baseline all see the final size. */
static bool
layout_grid_stretch_item(const struct grid_layout_ctx *ctx, struct box *child, int available_width, int height)
{
    int max_width, max_height;
    struct css_size min_width, min_height;
    layout_find_dimensions(ctx->unit_len_ctx, available_width, -1, child,
        &(struct layout_dimension_outputs){
            .max_width = &max_width,
            .min_width = &min_width,
            .max_height = &max_height,
            .min_height = &min_height,
        });
    if (max_height >= 0)
        height = min(height, max_height);
    if (min_height.type == CSS_SIZE_SET)
        height = max(height, min_height.value);
    int width = child->width;
    css_fixed_or_calc value = (css_fixed_or_calc)0;
    css_unit unit = CSS_UNIT_PX;
    if (child->style && css_computed_width(child->style, &value, &unit) == CSS_WIDTH_AUTO) {
        int ratio_width = layout_ratio_dimension(child, height, false);
        if (ratio_width != AUTO)
            width = ratio_width;
    }
    if (max_width >= 0)
        width = min(width, max_width);
    if (min_width.type == CSS_SIZE_SET)
        width = max(width, min_width.value);
    if (width == child->width && height == child->height && !layout_grid_percentage_children(child))
        return true;
    child->width = width;
    child->height = height;
    child->flags &= ~ASPECT_RATIO_HEIGHT;
    child->flags |= HEIGHT_STRETCHED;
    int margin[4];
    memcpy(margin, child->margin, sizeof(margin));
    bool success = layout_grid_item_contents(ctx, child, available_width);
    memcpy(child->margin, margin, sizeof(margin));
    child->flags &= ~HEIGHT_STRETCHED;
    return success;
}

static bool layout_grid_height_depends_on_area(const struct box *box)
{
    if (box->style == NULL)
        return false;
    css_fixed_or_calc value = (css_fixed_or_calc)0;
    css_unit unit = CSS_UNIT_PX;
    css_computed_height(box->style, &value, &unit);
    if (unit == CSS_UNIT_PCT || unit == CSS_UNIT_CALC)
        return true;
    unit = CSS_UNIT_PX;
    css_computed_min_height(box->style, &value, &unit);
    if (unit == CSS_UNIT_PCT || unit == CSS_UNIT_CALC)
        return true;
    unit = CSS_UNIT_PX;
    css_computed_max_height(box->style, &value, &unit);
    return unit == CSS_UNIT_PCT || unit == CSS_UNIT_CALC;
}

/* Resolve percentage item sizes only after their area is final. This pass is
 * separate from placement so all baselines can be updated before alignment. */
static bool layout_grid_finish_item(const struct grid_layout_ctx *ctx, const struct grid_item_cache *item)
{
    struct box *child = item->box;
    const struct grid_track *first = &ctx->rows[item->item_row];
    const struct grid_track *last = &ctx->rows[item->item_row + item->row_span - 1];
    int spanned_height = last->offset + last->size - first->offset;
    const struct layout_containing_size area = {.width = item->area.width, .height = spanned_height};
    const struct layout_containing_size *previous = child->containing_size;
    child->containing_size = &area;
    bool success = false;
    if (item->area.height != area.height && layout_grid_height_depends_on_area(child) &&
        !layout_grid_size_item(ctx, child, area.width))
        goto done;
    int edges = lh__delta_outer_height(child);
    bool auto_top = child->margin[TOP] == AUTO;
    bool auto_bottom = child->margin[BOTTOM] == AUTO;
    css_fixed_or_calc value = (css_fixed_or_calc)0;
    css_unit unit = CSS_UNIT_PX;
    if (item->align == CSS_ALIGN_ITEMS_STRETCH && !auto_top && !auto_bottom &&
        (!child->style || css_computed_height(child->style, &value, &unit) == CSS_HEIGHT_AUTO)) {
        if (!layout_grid_stretch_item(ctx, child, area.width, max(0, spanned_height - edges)))
            goto done;
    }
    success = true;
done:
    child->containing_size = previous;
    return success;
}

static void layout_grid_align_item(const struct grid_layout_ctx *ctx, const struct grid_item_cache *item)
{
    struct box *child = item->box;
    const struct grid_track *first = &ctx->rows[item->item_row];
    const struct grid_track *last = &ctx->rows[item->item_row + item->row_span - 1];
    int spanned_height = last->offset + last->size - first->offset;
    int edges = lh__delta_outer_height(child);
    bool auto_top = child->margin[TOP] == AUTO;
    bool auto_bottom = child->margin[BOTTOM] == AUTO;
    int space = spanned_height - child->height - edges;
    int offset = 0;
    if (auto_top || auto_bottom) {
        int free = max(0, space);
        if (auto_top)
            child->margin[TOP] = free / (auto_bottom ? 2 : 1);
        if (auto_bottom)
            child->margin[BOTTOM] = free - (auto_top ? child->margin[TOP] : 0);
    } else if (item->baseline) {
        offset = ctx->row_baselines[item->item_row] -
            (child->margin[TOP] + child->border[TOP].width + lh__box_first_baseline(child));
    } else if (item->align == CSS_ALIGN_ITEMS_CENTER) {
        offset = space / 2;
    } else if (item->align == CSS_ALIGN_ITEMS_FLEX_END || item->align == CSS_ALIGN_ITEMS_END) {
        offset = space;
    }
    int y = ctx->grid->padding[TOP] + first->offset;
    child->y = y + offset + child->margin[TOP] + child->border[TOP].width;
    int x = ctx->grid->padding[LEFT] + ctx->columns[item->item_col].offset;
    child->x = x + child->margin[LEFT] + child->border[LEFT].width;
}

/* Lay out one resolved grid area and cache its placement for the final pass. */
static bool layout_grid_layout_item(struct grid_layout_ctx *ctx, struct grid_item_cache *item)
{
    struct box *child = item->box;
    const struct grid_track *first = &ctx->columns[item->item_col];
    const struct grid_track *last = &ctx->columns[item->item_col + item->col_span - 1];
    int child_width = last->offset + last->size - first->offset;

    struct layout_containing_size area = {.width = child_width, .height = (item->row_span - 1) * ctx->row_gap};
    for (int r = item->item_row; r < item->item_row + item->row_span; r++) {
        struct grid_track row = layout_grid_init_row(ctx, r);
        if (row.intrinsic_min || row.intrinsic_max || row.flexible || row.size != row.limit) {
            area.height = -1;
            break;
        }
        area.height += row.size;
    }
    if (!layout_grid_measure_item(ctx, child, &area))
        return false;
    uint8_t align = layout_grid_item_alignment(ctx->grid, child);
    item->align = align;
    item->baseline = align == CSS_ALIGN_ITEMS_BASELINE && child->margin[TOP] != AUTO && child->margin[BOTTOM] != AUTO;
    item->area = area;
    return true;
}

/* Reserve the occupied cells and advance the auto-placement cursor. */
static void layout_grid_advance_placement(struct grid_layout_ctx *ctx, const struct grid_placement *placement)
{
    /* Mark cells as occupied for 3-phase placement tracking */
    if (ctx->occupied != NULL) {
        for (int dr = 0; dr < placement->row_span; dr++) {
            for (int dc = 0; dc < placement->col_span; dc++) {
                int occ_row = placement->row + dr;
                int occ_col = placement->col + dc;
                if (occ_row < ctx->occupied_max_rows && occ_col < ctx->num_cols) {
                    int idx = occ_row * ctx->num_cols + occ_col;
                    ctx->occupied[idx] = true;
                }
            }
        }
    }

    /* CSS Grid spec §8: Advance auto-placement cursor
     * - row mode: Advance column, wrap to next row at end
     * - column mode: Advance row, wrap to next column at end
     * Note: SPAN also uses auto-placement, so advance cursor for SPAN too
     *
     * CSS Grid spec §8.5: For items with definite column position,
     * "increment the cursor's column position to be one past the item's column-end line"
     */
    bool col_auto = (placement->col_start == GRID_PLACEMENT_AUTO || placement->col_start == GRID_PLACEMENT_SPAN);
    bool row_auto = (placement->row_start == GRID_PLACEMENT_AUTO || placement->row_start == GRID_PLACEMENT_SPAN);

    /* Phase 2: definite column, auto row - advance cursor past item */
    if (!col_auto && row_auto) {
        if (!ctx->flow_is_column) {
            /* Row mode: advance column past the placed item's column-end */
            ctx->auto_col = placement->col + placement->col_span;
            if (ctx->auto_col >= ctx->num_cols) {
                ctx->auto_col = 0;
                ctx->auto_row++;
            }
        }
        /* Column mode: definite column means advance row */
        /* (But this is unusual - definite column in column flow) */
    }

    /* Phase 3: fully auto items */
    if (col_auto && row_auto) {
        if (ctx->flow_is_column) {
            /* Column mode: advance row past the placed item's span */
            ctx->auto_row = placement->row + placement->row_span;
            if (ctx->auto_row >= ctx->num_rows) {
                ctx->auto_row = 0;
                ctx->auto_col++;
            }
        } else {
            /* Row mode (default): advance column past the placed item's span */
            ctx->auto_col = placement->col + placement->col_span;
            if (ctx->auto_col >= ctx->num_cols) {
                ctx->auto_col = 0;
                ctx->auto_row++;
            }
        }
    }
}

/* CSS Grid §10.6: use a first-row baseline group, otherwise the first
 * item in row-major grid order (with a border-edge fallback for empty items). */
static void layout_grid_first_baseline(const struct grid_layout_ctx *ctx)
{
    if (!layout_needs_first_baseline(ctx->grid) || ctx->cache_idx == 0)
        return;
    const struct grid_item_cache *first = &ctx->item_cache[0];
    for (int i = 1; i < ctx->cache_idx; i++) {
        const struct grid_item_cache *item = &ctx->item_cache[i];
        if (item->item_row < first->item_row ||
            (item->item_row == first->item_row &&
                (item->baseline > first->baseline ||
                    (item->baseline == first->baseline && item->item_col < first->item_col))))
            first = item;
    }
    ctx->grid->baseline = first->box->y + lh__box_first_baseline(first->box);
    ctx->grid->flags |= HAS_BASELINE;
    ctx->grid->baseline = lh__box_first_baseline(ctx->grid);
}

/* Publish final dimensions once all rows have been laid out. */
static void layout_grid_set_dimensions(struct grid_layout_ctx *ctx)
{
    /* Calculate total grid height */
    int grid_height = 0;
    for (int r = 0; r < ctx->max_row; r++) {
        grid_height += ctx->rows[r].size;
        if (r > 0) {
            grid_height += ctx->row_gap;
        }
    }

    if (ctx->grid->height == AUTO)
        ctx->grid->height = grid_height;
    if (!(ctx->grid->flags & HEIGHT_STRETCHED))
        layout_ratio_content_height(ctx->grid, grid_height);
    if (ctx->max_height >= 0)
        ctx->grid->height = min(ctx->grid->height, ctx->max_height);
    if (ctx->min_height.type == CSS_SIZE_SET)
        ctx->grid->height = max(ctx->grid->height, ctx->min_height.value);

    layout_grid_resolve_row_gap(ctx);

    /* IMPORTANT: layout_grid must set the grid's width */
    if (ctx->grid->width == UNKNOWN_WIDTH || ctx->grid->width < 0) {
        int intrinsic_width = 0;
        for (int i = 0; i < ctx->num_cols; i++) {
            intrinsic_width += ctx->columns[i].size;
        }
        if (ctx->num_cols > 1) {
            intrinsic_width += ctx->column_gap * (ctx->num_cols - 1);
        }
        ctx->grid->width = intrinsic_width;
    }
}

static void layout_grid_place_items(struct grid_layout_ctx *ctx)
{
    /* Reserve fully definite items first. Then place the remaining items in
     * DOM order, preserving the shared auto-placement cursor between passes. */
    for (int pass = 1; pass <= 2; pass++) {
        NSLOG(layout, INFO, "GRID PLACEMENT: Starting pass %d", pass);
        for (struct box *child = ctx->grid->children; child; child = child->next) {
            if (child->style && lh__box_is_absolute(child))
                continue;
            struct grid_placement placement = {0};
            get_grid_item_placement(child->style, &placement.col_start, &placement.col_end, &placement.row_start,
                &placement.row_end, &placement.col_span, &placement.row_span);
            int phase = get_placement_phase(placement.col_start, placement.row_start);
            if ((pass == 1 && phase != GRID_PHASE_DEFINITE_BOTH) || (pass == 2 && phase == GRID_PHASE_DEFINITE_BOTH))
                continue;
            NSLOG(layout, WARNING,
                "GRID PLACEMENT pass=%d item_phase=%d: col_start=%d col_end=%d row_start=%d row_end=%d col_span=%d row_span=%d",
                pass, phase, placement.col_start, placement.col_end, placement.row_start, placement.row_end,
                placement.col_span, placement.row_span);
            layout_grid_resolve_placement(ctx, &placement);
            ctx->item_cache[ctx->cache_idx++] = (struct grid_item_cache){
                .box = child,
                .item_col = placement.col,
                .item_row = placement.row,
                .col_span = placement.col_span,
                .row_span = placement.row_span,
            };
            ctx->max_row = max(ctx->max_row, placement.row + placement.row_span);
            layout_grid_advance_placement(ctx, &placement);
        }
    }
}

/* Container min/max widths use exactly the same placed items and track
 * definitions as final layout, without laying out descendants or mutating
 * their used dimensions. Percentage tracks/gaps are cyclic in this pass. */
bool layout_minmax_grid_content(
    struct box *grid, const struct gui_layout_table *font_func, const html_content *content, int *minimum, int *maximum)
{
    struct grid_layout_ctx ctx = {.grid = grid, .unit_len_ctx = &content->unit_len_ctx};
    bool success = false;
    for (struct box *child = grid->children; child; child = child->next) {
        if (child->style && lh__box_is_absolute(child))
            continue;
        if (child->max_width == UNKNOWN_MAX_WIDTH) {
#ifdef TESTING
            child->min_width.value = 0;
            child->max_width = 100;
#else
            if (!layout_minmax_box(child, font_func, content))
                return false;
#endif
        }
    }
    if (!layout_grid_prepare(&ctx, -1))
        goto cleanup;
    layout_grid_place_items(&ctx);
    layout_grid_size_columns(&ctx, GRID_SIZE_MIN_CONTENT);
    *minimum = layout_grid_columns_width(&ctx);
    layout_grid_size_columns(&ctx, GRID_SIZE_MAX_CONTENT);
    *maximum = max(*minimum, layout_grid_columns_width(&ctx));
    success = true;
cleanup:
    layout_grid_cleanup(&ctx);
    return success;
}

bool layout_grid(struct box *grid, int available_width, html_content *content)
{
    struct grid_layout_ctx ctx = {.grid = grid, .content = content, .unit_len_ctx = &content->unit_len_ctx};
    bool success = false;
    grid->flags &= ~HAS_BASELINE;
    for (struct box *child = grid->children; child; child = child->next)
        if (child->style && lh__box_is_absolute(child))
            child->x = child->y = 0;
    layout_grid_prepare_height(&ctx, available_width);
    if (lh__is_resolved(grid->width) && grid->width >= 0)
        available_width = grid->width;
    if (!layout_grid_prepare(&ctx, available_width))
        goto cleanup;

    layout_grid_place_items(&ctx);
    layout_grid_size_columns(
        &ctx, lh__is_resolved(available_width) && available_width >= 0 ? GRID_SIZE_DEFINITE : GRID_SIZE_MAX_CONTENT);
    for (int i = 0; i < ctx.cache_idx; i++)
        if (!layout_grid_layout_item(&ctx, &ctx.item_cache[i]))
            goto cleanup;
    if (!layout_grid_size_rows(&ctx))
        goto cleanup;
    layout_grid_maximize_rows(&ctx);
    layout_grid_expand_flexible_rows(&ctx);
    int previous_height = grid->height;
    layout_grid_set_dimensions(&ctx);
    /* Cyclic percentages (including minmax bounds) resolve after intrinsic
     * sizing. Repeat track sizing with that definite basis, not child layout,
     * and do not feed the percentage result back into the container height. */
    if (previous_height != grid->height && layout_grid_rows_depend_on_height(&ctx)) {
        if (!layout_grid_size_rows(&ctx))
            goto cleanup;
        layout_grid_maximize_rows(&ctx);
        layout_grid_expand_flexible_rows(&ctx);
    }
    layout_grid_align_rows(&ctx);
    for (int i = 0; i < ctx.cache_idx; i++) {
        if (!layout_grid_finish_item(&ctx, &ctx.item_cache[i]))
            goto cleanup;
    }
    layout_grid_update_baselines(&ctx);
    for (int i = 0; i < ctx.cache_idx; i++)
        layout_grid_align_item(&ctx, &ctx.item_cache[i]);
    layout_grid_first_baseline(&ctx);
    success = true;
cleanup:
    layout_grid_cleanup(&ctx);
    return success;
}

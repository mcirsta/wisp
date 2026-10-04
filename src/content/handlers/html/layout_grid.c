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

/**
 * Ensure row_heights array has capacity for at least required_row + 1 elements.
 * Grows the array by doubling capacity as needed.
 *
 * \param row_heights      Pointer to the row_heights array pointer
 * \param capacity         Pointer to current capacity
 * \param required_row     The row index that must be accessible
 * \return true on success, false on allocation failure
 */
static bool ensure_row_capacity(int **row_heights, int *capacity, int required_row)
{
    if (required_row < *capacity) {
        return true; /* Already have capacity */
    }

    int new_cap = *capacity;
    while (new_cap <= required_row) {
        new_cap *= 2;
    }

    int *new_rows = realloc(*row_heights, new_cap * sizeof(int));
    if (!new_rows) {
        return false;
    }

    /* Zero-initialize the new elements */
    memset(new_rows + *capacity, 0, (new_cap - *capacity) * sizeof(int));

    *row_heights = new_rows;
    *capacity = new_cap;
    return true;
}

/**
 * Initialize row heights array from CSS grid-template-rows property.
 *
 * Reads the computed row track values and populates the row_heights array.
 *
 * \param style              Grid container computed style
 * \param row_heights        Pointer to row heights array pointer
 * \param row_heights_capacity Pointer to array capacity
 * \return true on success, false on allocation failure
 */
static bool init_row_heights_from_css(const css_computed_style *style, int **row_heights, int *row_heights_capacity)
{
    if (style == NULL) {
        return true;
    }

    int32_t n_row_tracks = 0;
    css_computed_grid_track *row_tracks = NULL;
    uint8_t row_template_type;

    row_template_type = css_computed_grid_template_rows(style, &n_row_tracks, &row_tracks);

    if (row_template_type != CSS_GRID_TEMPLATE_SET || n_row_tracks <= 0 || row_tracks == NULL) {
        return true; /* No explicit rows defined */
    }

    NSLOG(layout, WARNING, "GRID LAYOUT: initializing %d row tracks from CSS", n_row_tracks);

    for (int32_t i = 0; i < n_row_tracks; i++) {
        if (!ensure_row_capacity(row_heights, row_heights_capacity, i)) {
            return false;
        }

        /* Debug: log raw track values */
        NSLOG(layout, WARNING, "GRID LAYOUT: row track[%d] raw: unit=%d value=%d", i, row_tracks[i].unit,
            FIXTOINT(row_tracks[i].value));

        int row_height_px = 0;
        /* Use CSS_UNIT directly from libcss (now properly converted) */

        switch (row_tracks[i].unit) {
        case CSS_UNIT_PX:
            row_height_px = FIXTOINT(row_tracks[i].value);
            break;
        case CSS_UNIT_EM:
            /* Convert em to px assuming 16px base */
            row_height_px = FIXTOINT(row_tracks[i].value) * 16;
            break;
        case CSS_UNIT_FR:
            /* fr units are flexible - use content height */
            row_height_px = 0;
            break;
        case CSS_UNIT_PCT:
            /* Percentage - treat as content-size */
            row_height_px = 0;
            break;
        default:
            /* Other units - approximate as px */
            row_height_px = FIXTOINT(row_tracks[i].value);
            break;
        }

        (*row_heights)[i] = row_height_px;
        NSLOG(layout, WARNING, "GRID LAYOUT: row[%d] height=%d (from CSS)", i, row_height_px);
    }

    return true;
}

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

/**
 * Calculate minimum and maximum width of a grid container.
 *
 * This function calculates the intrinsic min/max width based on:
 * - Grid track definitions (fixed, fr, percentage)
 * - Column gaps
 * - Content min/max widths per column
 *
 * \param grid      box of type BOX_GRID
 * \param font_func font functions for text measurement
 * \param content   The HTML content being laid out.
 * \post  grid->min_width and grid->max_width filled in
 */
bool layout_minmax_grid(struct box *grid, const struct gui_layout_table *font_func, const html_content *content)
{
    struct box *child;
    int num_cols;
    int32_t n_tracks = 0;
    css_computed_grid_track *tracks = NULL;
    int gap_px = 0;
    int *col_min = NULL;
    int *col_max = NULL;
    int i, col_idx;
    int min = 0, max = 0;

    assert(grid->type == BOX_GRID || grid->type == BOX_INLINE_GRID);

    /* Already calculated? */
    if (grid->max_width != UNKNOWN_MAX_WIDTH)
        return true;

    num_cols = layout_grid_get_column_count(grid);

    /* Allocate per-column min/max arrays */
    col_min = calloc(num_cols, sizeof(int));
    col_max = calloc(num_cols, sizeof(int));
    if (!col_min || !col_max) {
        free(col_min);
        free(col_max);
        return false;
    }

    /* Get column gap */
    if (grid->style != NULL) {
        css_fixed_or_calc gap_len = (css_fixed_or_calc)0;
        css_unit gap_unit = CSS_UNIT_PX;
        if (css_computed_column_gap(grid->style, &gap_len, &gap_unit) == CSS_COLUMN_GAP_SET) {
            int available_inline = (grid->width != AUTO && grid->width != UNKNOWN_WIDTH) ? grid->width : -1;
            if (!lh__length_to_px(grid->style, &content->unit_len_ctx, available_inline, gap_len, gap_unit, &gap_px)) {
                gap_px = 0;
            }
        }
    }

    /* Recursively calculate min/max for all children and accumulate
     * per-column. Children are placed in columns in order. */
    col_idx = 0;
    for (child = grid->children; child; child = child->next) {
        /* Skip absolutely positioned children - they don't affect
         * intrinsic sizing */
        if (child->style && lh__box_is_absolute(child)) {
            continue;
        }

        /* Recursively calculate child's min/max.
         * This is called during the minmax phase, so children
         * have not been processed yet. We must calculate their
         * minmax recursively.
         *
         * We use layout_minmax_box for the main library, but
         * for testing purposes grid_layout_test has stubs. */
        if (child->max_width == UNKNOWN_MAX_WIDTH) {
#ifdef TESTING
            /* In test environment, just use safe defaults
             * since we don't have full layout.c linked */
            child->min_width.value = 0;
            child->max_width = 100;
#else
            /* In production, use the dispatcher */
            if (!layout_minmax_box(child, font_func, content)) {
                free(col_min);
                free(col_max);
                return false;
            }
#endif
        }

        /* Accumulate child min/max into the column */
        if (child->min_width.value > col_min[col_idx])
            col_min[col_idx] = child->min_width.value;
        if (child->max_width > col_max[col_idx])
            col_max[col_idx] = child->max_width;

        col_idx = (col_idx + 1) % num_cols;
    }

    /* Get track definitions */
    if (grid->style != NULL) {
        css_computed_grid_template_columns(grid->style, &n_tracks, &tracks);
    }

    /* Calculate grid min/max from tracks and content */
    for (i = 0; i < num_cols; i++) {
        int track_min = col_min[i];
        int track_max = col_max[i];

        if (n_tracks > 0 && tracks != NULL) {
            css_computed_grid_track *t = &tracks[i % n_tracks];

            if (t->unit == CSS_UNIT_PX) {
                /* Fixed track: use the fixed size */
                int fixed_w = FIXTOINT(css_unit_len2device_px(grid->style, &content->unit_len_ctx, t->value, t->unit));
                track_min = fixed_w;
                track_max = fixed_w;
            } else if (t->unit == CSS_UNIT_FR) {
                /* FR track: use content min/max (already set)
                 */
            } else if (t->unit == CSS_UNIT_PCT) {
                /* Percentage: contributes 0 to intrinsic size
                 */
                track_min = 0;
                track_max = 0;
            }
        }

        min += track_min;
        max += track_max;
    }

    /* Add column gaps */
    if (num_cols > 1) {
        min += (num_cols - 1) * gap_px;
        max += (num_cols - 1) * gap_px;
    }

    free(col_min);
    free(col_max);

    /* Ensure max >= min */
    if (max < min)
        max = min;

    grid->min_width.value = min;
    grid->max_width = max;
    grid->flags |= HAS_HEIGHT;

    NSLOG(layout, DEBUG, "Grid %p minmax: min=%d max=%d cols=%d gap=%d", grid, min, max, num_cols, gap_px);
    return true;
}

static void layout_grid_compute_tracks(struct box *grid, int available_width, int *col_widths, int num_cols,
    const css_computed_style *style, const css_unit_ctx *unit_len_ctx)
{
    int32_t n_tracks = 0;
    css_computed_grid_track *tracks = NULL;
    css_fixed_or_calc gap_len = (css_fixed_or_calc)0;
    css_unit gap_unit = CSS_UNIT_PX;
    int gap_px = 0;
    int total_gap_width = 0;
    int i;
    int used_width = 0;
    int fr_tracks = 0;
    int fr_total = 0;

    /* Get column gap */
    if (css_computed_column_gap(style, &gap_len, &gap_unit) == CSS_COLUMN_GAP_SET) {
        if (!lh__length_to_px(style, unit_len_ctx, available_width, gap_len, gap_unit, &gap_px)) {
            gap_px = 0;
        }
    }
    NSLOG(layout, DEEPDEBUG, "Column Gap: %d px (from val %d unit %d)", gap_px, gap_len, gap_unit);

    /* Calculate total gap width consumed */
    if (num_cols > 1) {
        /* Parameters:
         * box->width is the available width for the grid container
         */
        NSLOG(layout, DEEPDEBUG, "Grid Layout Compute Tracks: Available Width %d, Num Cols %d", available_width,
            num_cols);
        total_gap_width = (num_cols - 1) * gap_px;
    }

    /* Get explicit track definitions */
    if (css_computed_grid_template_columns(style, &n_tracks, &tracks) == CSS_GRID_TEMPLATE_SET && n_tracks > 0) {
        /* Use parsed tracks */
        for (i = 0; i < num_cols; i++) {
            /* Cycle through tracks if num_cols > n_tracks (implicit
             * grid) */
            css_computed_grid_track *t = &tracks[i % n_tracks];

            NSLOG(layout, DEEPDEBUG, "Track %d: unit=%d value=%d (FR=%d PX=%d)", i, t->unit, FIXTOINT(t->value),
                CSS_UNIT_FR, CSS_UNIT_PX);
            if (t->unit == CSS_UNIT_FR) {
                fr_tracks++;
                fr_total += FIXTOINT(t->value);
                NSLOG(layout, DEEPDEBUG, "Track %d is FR: val %d", i, FIXTOINT(t->value));
                col_widths[i] = 0; /* Will be assigned later */
            } else if (t->unit == CSS_UNIT_MIN_CONTENT || t->unit == CSS_UNIT_MAX_CONTENT) {
                /* Treat min/max-content as auto/1fr for now to
                 * ensure visibility */
                fr_tracks++;
                fr_total += 1;
                NSLOG(layout, DEEPDEBUG, "Track %d is Content (fallback to 1fr)", i);
                col_widths[i] = 0;
            } else if (t->unit == CSS_UNIT_MINMAX) {
                /* For minmax(min, max), try to use max if it's
                 * a Length */
                /* If max is also dynamic, fallback to 1fr */
                if (t->max_unit == CSS_UNIT_PX || t->max_unit == CSS_UNIT_EM || t->max_unit == CSS_UNIT_PCT) {
                    int w = FIXTOINT(css_unit_len2device_px(style, unit_len_ctx, t->max_value, t->max_unit));
                    if (t->max_unit == CSS_UNIT_PCT) {
                        /* Resolve percentage against
                         * available width */
                        w = (w * available_width) / 100; // approximation if
                                                         // conversion not fully
                                                         // context-aware
                    }
                    col_widths[i] = w;
                    used_width += w;
                    NSLOG(layout, DEEPDEBUG, "Track %d is MINMAX(..., %d %s) -> width %d", i, FIXTOINT(t->max_value),
                        "fixed", w);
                } else {
                    /* Max is FR or Content -> Treat as 1fr
                     */
                    fr_tracks++;
                    fr_total += 1;
                    NSLOG(layout, DEEPDEBUG, "Track %d is MINMAX(..., dynamic) -> fallback to 1fr", i);
                    col_widths[i] = 0;
                }

            } else {
                /* Handle fixed units (px, etc) */
                if (t->unit == CSS_UNIT_PX || t->unit == CSS_UNIT_EM) {
                    int w = FIXTOINT(css_unit_len2device_px(style, unit_len_ctx, t->value, t->unit));
                    col_widths[i] = w;
                    used_width += w;
                    NSLOG(layout, DEEPDEBUG, "Track %d is Fixed: %d", i, w);
                } else {
                    col_widths[i] = 0; /* Auto/Other fallback */
                }
            }
        }
    } else {
        /* Fallback: treat as 1fr for all columns */
        fr_tracks = num_cols;
        fr_total = num_cols;
        for (i = 0; i < num_cols; i++)
            col_widths[i] = 0;
    }

    /* Distribute remaining space to FR tracks */
    int remaining_width = available_width - used_width - total_gap_width;
    if (remaining_width < 0)
        remaining_width = 0;

    /* Guard: if the grid has indefinite width (e.g. inline-grid with auto
     * width), fr tracks cannot be meaningfully distributed from infinite
     * space. Clamp to 0 so fr tracks get zero width rather than nonsense.
     * TODO: CSS Grid spec §11.5 says fr tracks in an indefinite context
     * should resolve to max-content size — implement proper intrinsic
     * sizing when content-based track sizing is available. */
    if (available_width == UNKNOWN_WIDTH && fr_tracks > 0) {
        assert(0 &&
            "Grid with indefinite width has fr tracks — "
            "fr distribution is not yet spec-correct");
        remaining_width = 0;
    }

    if (fr_tracks > 0 && fr_total > 0) {
        NSLOG(layout, DEEPDEBUG, "Distributing FR: Remaining %d, FR Total %d", remaining_width, fr_total);
        int px_per_fr = remaining_width / fr_total;
        NSLOG(layout, DEEPDEBUG, "PX per FR: %d", px_per_fr);
        for (i = 0; i < num_cols; i++) {
            /* Check if this column was an FR track.
               We need to re-check the track definition or use a
               flag. Re-checking logic for simplicity.
            */
            bool is_fr = false;
            int fr_val = 0;

            if (n_tracks > 0) {
                css_computed_grid_track *t = &tracks[i % n_tracks];
                if (t->unit == CSS_UNIT_FR) {
                    is_fr = true;
                    fr_val = FIXTOINT(t->value);
                } else if (t->unit == CSS_UNIT_MIN_CONTENT || t->unit == CSS_UNIT_MAX_CONTENT) {
                    /* min-content/max-content were marked
                     * for FR distribution */
                    is_fr = true;
                    fr_val = 1;
                }
            } else {
                is_fr = true; /* Fallback all fr */
                fr_val = 1;
            }

            if (is_fr) {
                col_widths[i] = px_per_fr * fr_val;
            }
        }
    }
}

/* Per-call scratch state. Helpers borrow these buffers; layout_grid owns
 * their lifetime. No state is shared between nested grid layouts. */
struct grid_layout_ctx {
    struct box *grid;
    html_content *content;
    int num_cols, num_rows, gap_px;
    int *col_widths, *row_heights;
    int row_heights_capacity, max_row;
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
    free(ctx->row_heights);
    free(ctx->col_widths);
}

/* Allocate the existing scratch buffers and resolve tracks and flow direction.
 * The caller also cleans up partially initialized state on failure. */
static bool layout_grid_prepare(struct grid_layout_ctx *ctx, int available_width)
{
    struct box *grid = ctx->grid;
    ctx->num_cols = layout_grid_get_column_count(grid);
    NSLOG(layout, WARNING, "GRID LAYOUT: grid=%p avail_w=%d num_cols=%d children=%p", grid, available_width,
        ctx->num_cols, grid->children);
    ctx->col_widths = malloc(sizeof(int) * ctx->num_cols);
    if (!ctx->col_widths)
        return false;

    css_fixed_or_calc gap_len = (css_fixed_or_calc)0;
    css_unit gap_unit = CSS_UNIT_PX;
    if (grid->style != NULL && css_computed_column_gap(grid->style, &gap_len, &gap_unit) == CSS_COLUMN_GAP_SET) {
        if (!lh__length_to_px(
                grid->style, &ctx->content->unit_len_ctx, available_width, gap_len, gap_unit, &ctx->gap_px)) {
            ctx->gap_px = 0;
        }
    }
    layout_grid_compute_tracks(
        grid, available_width, ctx->col_widths, ctx->num_cols, grid->style, &ctx->content->unit_len_ctx);
    for (int i = 0; i < ctx->num_cols; i++) {
        NSLOG(layout, WARNING, "GRID LAYOUT: col[%d] width=%d", i, ctx->col_widths[i]);
    }

    ctx->row_heights_capacity = 100;
    ctx->row_heights = calloc(ctx->row_heights_capacity, sizeof(int));
    if (!ctx->row_heights)
        return false;
    if (!init_row_heights_from_css(grid->style, &ctx->row_heights, &ctx->row_heights_capacity))
        return false;

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

    ctx->occupied_max_rows = ctx->row_heights_capacity;
    ctx->occupied = calloc(ctx->occupied_max_rows * ctx->num_cols, sizeof(bool));
    if (!ctx->occupied)
        return false;
    NSLOG(layout, INFO, "GRID LAYOUT: allocated %dx%d occupation grid (dense=%d)", ctx->num_cols,
        ctx->occupied_max_rows, ctx->is_dense);

    for (struct box *child = grid->children; child; child = child->next) {
        /* Absolute/fixed children do not occupy grid cells. Their separate
         * positioning pass also needs a fresh fallback origin on reflow. */
        if (child->style && lh__box_is_absolute(child)) {
            child->x = child->y = 0;
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
    struct css_size item_min_width;
    int item_max_width;
    layout_find_dimensions(&ctx->content->unit_len_ctx, child_width, -1, child,
        &(struct layout_dimension_outputs){
            .width = &child->width,
            .height = &child->height,
            .max_width = &item_max_width,
            .min_width = &item_min_width,
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

/* Resolve normal separately from explicit stretch: a preferred ratio makes
 * normal use start alignment (CSS Box Alignment §6.1). */
static uint8_t layout_grid_item_alignment(const struct box *grid, const struct box *child)
{
    uint8_t align = child->style ? css_computed_align_self(child->style) : CSS_ALIGN_SELF_AUTO;
    if (align == CSS_ALIGN_SELF_AUTO)
        align = grid->style ? css_computed_align_items(grid->style) : CSS_ALIGN_ITEMS_NORMAL;
    if (align == CSS_ALIGN_ITEMS_NORMAL)
        align = layout_ratio_dimension(child, child->width, true) != AUTO ? CSS_ALIGN_ITEMS_FLEX_START
                                                                          : CSS_ALIGN_ITEMS_STRETCH;
    return align;
}

static bool layout_grid_row_is_fixed(const struct grid_layout_ctx *ctx, int row)
{
    return row < ctx->row_track_count && ctx->row_tracks != NULL && ctx->row_tracks[row].unit < CSS_UNIT_FR;
}

/* Size non-spanning items first, then distribute any spanning deficit over
 * content-sized tracks. Fixed rows remain fixed even when a baseline overflows. */
static void layout_grid_grow_rows(struct grid_layout_ctx *ctx, const struct grid_item_cache *item, int height)
{
    int remaining = height - (item->row_span - 1) * ctx->gap_px;
    int growable = 0;
    int end = item->item_row + item->row_span;
    for (int r = item->item_row; r < end; r++) {
        remaining -= ctx->row_heights[r];
        if (!layout_grid_row_is_fixed(ctx, r))
            growable++;
    }
    for (int r = item->item_row; r < end && remaining > 0 && growable > 0; r++) {
        if (layout_grid_row_is_fixed(ctx, r))
            continue;
        int extra = (remaining + growable - 1) / growable;
        ctx->row_heights[r] += extra;
        remaining -= extra;
        growable--;
    }
}

/* Baseline shims are part of each item's intrinsic contribution, including
 * margins and spanning items. No per-box persistent baseline storage is added. */
static bool layout_grid_size_rows(struct grid_layout_ctx *ctx)
{
    if (ctx->max_row == 0)
        return true;
    if (!ensure_row_capacity(&ctx->row_heights, &ctx->row_heights_capacity, ctx->max_row - 1))
        return false;
    bool has_baselines = false;
    for (int i = 0; i < ctx->cache_idx; i++)
        has_baselines |= ctx->item_cache[i].baseline;
    if (has_baselines) {
        ctx->row_baselines = malloc(ctx->max_row * sizeof(int));
        if (ctx->row_baselines == NULL)
            return false;
        for (int r = 0; r < ctx->max_row; r++)
            ctx->row_baselines[r] = INT_MIN;
        for (int i = 0; i < ctx->cache_idx; i++) {
            const struct grid_item_cache *item = &ctx->item_cache[i];
            if (item->baseline) {
                const struct box *b = item->box;
                int baseline = b->margin[TOP] + b->border[TOP].width + lh__box_first_baseline(b);
                ctx->row_baselines[item->item_row] = max(ctx->row_baselines[item->item_row], baseline);
            }
        }
    }
    for (int spanning = 0; spanning < 2; spanning++) {
        for (int i = 0; i < ctx->cache_idx; i++) {
            const struct grid_item_cache *item = &ctx->item_cache[i];
            if ((item->row_span > 1) != spanning)
                continue;
            const struct box *b = item->box;
            assert(lh__is_resolved(b->height));
            int height = b->height + lh__delta_outer_height(b);
            if (item->baseline)
                height += ctx->row_baselines[item->item_row] -
                    (b->margin[TOP] + b->border[TOP].width + lh__box_first_baseline(b));
            layout_grid_grow_rows(ctx, item, height);
        }
    }
    return true;
}

/* Re-layout at a definite stretched height so percentages, nested flex layout
 * and its exported baseline all see the final size. */
static bool
layout_grid_stretch_item(const struct grid_layout_ctx *ctx, struct box *child, int available_width, int height)
{
    int max_width, max_height;
    struct css_size min_width, min_height;
    layout_find_dimensions(&ctx->content->unit_len_ctx, available_width, -1, child,
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
    if (width == child->width && height == child->height)
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

/* Every item is aligned once, after all rows have their final size. */
static bool layout_grid_align_item(const struct grid_layout_ctx *ctx, const struct grid_item_cache *item)
{
    struct box *child = item->box;
    int spanned_height = (item->row_span - 1) * ctx->gap_px;
    for (int r = item->item_row; r < item->item_row + item->row_span; r++)
        spanned_height += ctx->row_heights[r];
    int edges = lh__delta_outer_height(child);
    bool auto_top = child->margin[TOP] == AUTO;
    bool auto_bottom = child->margin[BOTTOM] == AUTO;
    css_fixed_or_calc value = (css_fixed_or_calc)0;
    css_unit unit = CSS_UNIT_PX;
    if (item->align == CSS_ALIGN_ITEMS_STRETCH && !auto_top && !auto_bottom &&
        (!child->style || css_computed_height(child->style, &value, &unit) == CSS_HEIGHT_AUTO)) {
        int available_width = (item->col_span - 1) * ctx->gap_px;
        for (int c = item->item_col; c < item->item_col + item->col_span; c++)
            available_width += ctx->col_widths[c];
        if (!layout_grid_stretch_item(ctx, child, available_width, max(0, spanned_height - edges)))
            return false;
    }
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
    } else if (item->align == CSS_ALIGN_ITEMS_FLEX_END) {
        offset = space;
    }
    int y = ctx->grid->padding[TOP];
    for (int r = 0; r < item->item_row; r++)
        y += ctx->row_heights[r] + ctx->gap_px;
    child->y = y + offset + child->margin[TOP] + child->border[TOP].width;
    int x = ctx->grid->padding[LEFT];
    for (int c = 0; c < item->item_col; c++)
        x += ctx->col_widths[c] + ctx->gap_px;
    child->x = x + child->margin[LEFT] + child->border[LEFT].width;
    return true;
}

/* Lay out one resolved grid area and cache its placement for the final pass. */
static bool
layout_grid_layout_item(struct grid_layout_ctx *ctx, struct box *child, const struct grid_placement *placement)
{
    /* Calculate child width (sum of spanned columns + gaps) */
    int child_width = 0;
    for (int c = placement->col; c < placement->col + placement->col_span; c++) {
        child_width += ctx->col_widths[c];
        if (c > placement->col) {
            child_width += ctx->gap_px; /* Add gap between columns */
        }
    }

    if (!layout_grid_size_item(ctx, child, child_width))
        return false;
    uint8_t align = layout_grid_item_alignment(ctx->grid, child);
    ctx->item_cache[ctx->cache_idx++] = (struct grid_item_cache){
        .box = child,
        .item_col = placement->col,
        .item_row = placement->row,
        .col_span = placement->col_span,
        .row_span = placement->row_span,
        .align = align,
        .baseline = align == CSS_ALIGN_ITEMS_BASELINE && child->margin[TOP] != AUTO && child->margin[BOTTOM] != AUTO,
    };
    ctx->max_row = max(ctx->max_row, placement->row + placement->row_span);
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
static void layout_grid_set_dimensions(const struct grid_layout_ctx *ctx)
{
    /* Calculate total grid height */
    int grid_height = 0;
    for (int r = 0; r < ctx->max_row; r++) {
        grid_height += ctx->row_heights[r];
        if (r > 0) {
            grid_height += ctx->gap_px;
        }
    }

    if (!(ctx->grid->flags & HEIGHT_STRETCHED)) {
        if (ctx->grid->flags & ASPECT_RATIO_HEIGHT) {
            layout_ratio_content_height(ctx->grid, grid_height);
        } else {
            ctx->grid->height = grid_height;
        }
    }

    /* IMPORTANT: layout_grid must set the grid's width */
    if (ctx->grid->width == UNKNOWN_WIDTH || ctx->grid->width < 0) {
        int intrinsic_width = 0;
        for (int i = 0; i < ctx->num_cols; i++) {
            intrinsic_width += ctx->col_widths[i];
        }
        if (ctx->num_cols > 1) {
            intrinsic_width += ctx->gap_px * (ctx->num_cols - 1);
        }
        ctx->grid->width = intrinsic_width;
    }
}

bool layout_grid(struct box *grid, int available_width, html_content *content)
{
    struct grid_layout_ctx ctx = {.grid = grid, .content = content};
    bool success = false;
    grid->flags &= ~HAS_BASELINE;
    if (lh__is_resolved(grid->width) && grid->width >= 0)
        available_width = grid->width;
    if (!layout_grid_prepare(&ctx, available_width))
        goto cleanup;

    /* Reserve fully definite items first. Then place the remaining items in
     * DOM order, preserving the shared auto-placement cursor between passes. */
    for (int pass = 1; pass <= 2; pass++) {
        NSLOG(layout, INFO, "GRID PLACEMENT: Starting pass %d", pass);
        for (struct box *child = grid->children; child; child = child->next) {
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
            layout_grid_resolve_placement(&ctx, &placement);
            if (!layout_grid_layout_item(&ctx, child, &placement))
                goto cleanup;
            layout_grid_advance_placement(&ctx, &placement);
        }
    }
    if (!layout_grid_size_rows(&ctx))
        goto cleanup;
    for (int i = 0; i < ctx.cache_idx; i++) {
        if (!layout_grid_align_item(&ctx, &ctx.item_cache[i]))
            goto cleanup;
    }
    layout_grid_set_dimensions(&ctx);
    layout_grid_first_baseline(&ctx);
    success = true;
cleanup:
    layout_grid_cleanup(&ctx);
    return success;
}

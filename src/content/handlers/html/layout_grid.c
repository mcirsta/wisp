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
 * - Grid items are placed within grid areas; their margin box fills the area
 * - For STRETCH alignment: item content = cell_size - padding - border
 * - Row heights track the full box height (content + padding + border)
 *   so that subsequent rows are positioned correctly
 *
 * Key relationships:
 *   cell_width = track_width (sum for spanning items)
 *   child->width = cell_width - padding[L+R] - border[L+R]  (content box)
 *   rendered_width = child->width + padding[L+R] + border[L+R] = cell_width
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
#include <neosurf/content/handlers/html/box.h>
#include <neosurf/content/handlers/html/private.h>
#include <neosurf/utils/corestrings.h>
#include <neosurf/utils/log.h>
#include <neosurf/utils/utils.h>
#include "content/handlers/html/layout_grid.h"
#include "content/handlers/html/layout_internal.h"

/** Indicates grid placement is auto (not explicitly set) */
#define GRID_PLACEMENT_AUTO (-1)
/** Indicates grid end is a span value (value is the span count) */
#define GRID_PLACEMENT_SPAN (-2)

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
void layout_minmax_grid(struct box *grid, const struct gui_layout_table *font_func, const html_content *content)
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

    assert(grid->type == BOX_GRID);

    /* Already calculated? */
    if (grid->max_width != UNKNOWN_MAX_WIDTH)
        return;

    num_cols = layout_grid_get_column_count(grid);

    /* Allocate per-column min/max arrays */
    col_min = calloc(num_cols, sizeof(int));
    col_max = calloc(num_cols, sizeof(int));
    if (!col_min || !col_max) {
        free(col_min);
        free(col_max);
        grid->min_width = 0;
        grid->max_width = 0;
        return;
    }

    /* Get column gap */
    if (grid->style != NULL) {
        css_fixed gap_len = 0;
        css_unit gap_unit = CSS_UNIT_PX;
        if (css_computed_column_gap(grid->style, &gap_len, &gap_unit) == CSS_COLUMN_GAP_SET) {
            gap_px = FIXTOINT(css_unit_len2device_px(grid->style, &content->unit_len_ctx, gap_len, gap_unit));
        }
    }

    /* Recursively calculate min/max for all children and accumulate
     * per-column. Children are placed in columns in order. */
    col_idx = 0;
    for (child = grid->children; child; child = child->next) {
        /* Skip absolutely positioned children - they don't affect
         * intrinsic sizing */
        if (child->style &&
            (css_computed_position(child->style) == CSS_POSITION_ABSOLUTE ||
                css_computed_position(child->style) == CSS_POSITION_FIXED)) {
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
            child->min_width = 0;
            child->max_width = 100;
#else
            /* In production, use the dispatcher */
            layout_minmax_box(child, font_func, content);
#endif
        }

        /* Accumulate child min/max into the column */
        if (child->min_width > col_min[col_idx])
            col_min[col_idx] = child->min_width;
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

    grid->min_width = min;
    grid->max_width = max;
    grid->flags |= HAS_HEIGHT;

    NSLOG(layout, DEBUG, "Grid %p minmax: min=%d max=%d cols=%d gap=%d", grid, min, max, num_cols, gap_px);
}

static void layout_grid_compute_tracks(struct box *grid, int available_width, int *col_widths, int num_cols,
    const css_computed_style *style, const css_unit_ctx *unit_len_ctx)
{
    int32_t n_tracks = 0;
    css_computed_grid_track *tracks = NULL;
    css_fixed gap_len = 0;
    css_unit gap_unit = CSS_UNIT_PX;
    int gap_px = 0;
    int total_gap_width = 0;
    int i;
    int used_width = 0;
    int fr_tracks = 0;
    int fr_total = 0;

    /* Get column gap */
    if (css_computed_column_gap(style, &gap_len, &gap_unit) == CSS_COLUMN_GAP_SET) {
        gap_px = FIXTOINT(css_unit_len2device_px(style, unit_len_ctx, gap_len, gap_unit));
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

    /* Distributed remaining space to FR tracks */
    int remaining_width = available_width - used_width - total_gap_width;
    if (remaining_width < 0)
        remaining_width = 0;

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

bool layout_grid(struct box *grid, int available_width, html_content *content)
{
    struct box *child;
    int grid_width = available_width;
    int grid_height = 0;
    int x, y;
    int col_idx = 0;
    int row_idx = 0;
    int num_cols = layout_grid_get_column_count(grid);

    NSLOG(layout, WARNING, "GRID LAYOUT: grid=%p avail_w=%d num_cols=%d children=%p", grid, available_width, num_cols,
        grid->children);

    int *col_widths; /* Array allocated locally */

    /* Just use stack for small col counts or VLA/malloc */
    /* VLA is risky for stack size. Malloc is safer. */
    col_widths = malloc(sizeof(int) * num_cols);
    if (!col_widths)
        return false;

    /* Get Gap for layout positioning */
    int gap_px = 0;
    css_fixed gap_len = 0;
    css_unit gap_unit = CSS_UNIT_PX;
    if (grid->style != NULL && css_computed_column_gap(grid->style, &gap_len, &gap_unit) == CSS_COLUMN_GAP_SET) {
        gap_px = FIXTOINT(css_unit_len2device_px(grid->style, &content->unit_len_ctx, gap_len, gap_unit));
    }

    layout_grid_compute_tracks(grid, available_width, col_widths, num_cols, grid->style, &content->unit_len_ctx);

    /* Log computed column widths */
    for (int i = 0; i < num_cols; i++) {
        NSLOG(layout, WARNING, "GRID LAYOUT: col[%d] width=%d", i, col_widths[i]);
    }

    int row_height = 0;
    int max_row = 0; /* Track highest row used */

    /* Dynamic row heights array - starts at 100, grows as needed */
    int row_heights_capacity = 100;
    int *row_heights = calloc(row_heights_capacity, sizeof(int));
    if (!row_heights) {
        free(col_widths);
        return false;
    }

    /* Initialize row heights from CSS grid-template-rows */
    if (!init_row_heights_from_css(grid->style, &row_heights, &row_heights_capacity)) {
        free(col_widths);
        free(row_heights);
        return false;
    }

    /* CSS Grid spec §8: Read grid-auto-flow to determine placement
     * direction
     * - row (default): Fill row by row, left to right, top to bottom
     * - column: Fill column by column, top to bottom, left to right
     * - dense variants: Backfill holes (Phase 3)
     */
    uint8_t auto_flow = CSS_GRID_AUTO_FLOW_ROW; /* default per spec */
    if (grid->style != NULL) {
        auto_flow = css_computed_grid_auto_flow(grid->style);
    }
    bool flow_is_column = (auto_flow == CSS_GRID_AUTO_FLOW_COLUMN || auto_flow == CSS_GRID_AUTO_FLOW_COLUMN_DENSE);

    NSLOG(layout, INFO, "GRID LAYOUT: grid-auto-flow=%d (column=%s)", auto_flow, flow_is_column ? "yes" : "no");

    /* For column mode, we need explicit row count to know when to wrap */
    int num_rows = flow_is_column ? layout_grid_get_explicit_row_count(grid) : 1; /* row mode doesn't need this */

    /* Auto-placement cursor for items without explicit placement */
    int auto_col = 0;
    int auto_row = 0;

    /* CSS Grid spec §8.5: Dense packing requires tracking occupied cells
     * to enable backfilling holes left by larger items.
     * We use a simple bitmap: occupied[row * num_cols + col]
     */
    bool is_dense = (auto_flow == CSS_GRID_AUTO_FLOW_ROW_DENSE || auto_flow == CSS_GRID_AUTO_FLOW_COLUMN_DENSE);
    bool *occupied = NULL;
    int occupied_max_rows = row_heights_capacity; /* Use same capacity */

    if (is_dense) {
        occupied = calloc(occupied_max_rows * num_cols, sizeof(bool));
        if (!occupied) {
            free(col_widths);
            free(row_heights);
            return false;
        }
        NSLOG(layout, INFO, "GRID LAYOUT: dense mode enabled, allocated %dx%d occupation grid", num_cols,
            occupied_max_rows);
    }

    for (child = grid->children; child; child = child->next) {
        int col_start, col_end, row_start, row_end;
        int item_col, item_row, col_span, row_span;
        int child_width, child_x, child_y;

        /* Get explicit placement from CSS (also extracts explicit spans) */
        get_grid_item_placement(child->style, &col_start, &col_end, &row_start, &row_end, &col_span, &row_span);

        NSLOG(layout, WARNING,
            "GRID PLACEMENT CSS: col_start=%d col_end=%d row_start=%d row_end=%d col_span=%d row_span=%d", col_start,
            col_end, row_start, row_end, col_span, row_span);

        /* Determine span from explicit line numbers if not using span syntax */
        if (col_end == GRID_PLACEMENT_SPAN || col_start == GRID_PLACEMENT_SPAN) {
            /* col_span already set by get_grid_item_placement */
        } else if (col_end != GRID_PLACEMENT_AUTO && col_end > col_start && col_start != GRID_PLACEMENT_AUTO) {
            col_span = col_end - col_start;
        }
        /* else col_span defaults to 1 from get_grid_item_placement */

        if (row_end == GRID_PLACEMENT_SPAN || row_start == GRID_PLACEMENT_SPAN) {
            /* row_span already set by get_grid_item_placement */
        } else if (row_end != GRID_PLACEMENT_AUTO && row_end > row_start && row_start != GRID_PLACEMENT_AUTO) {
            row_span = row_end - row_start;
        }
        /* else row_span defaults to 1 from get_grid_item_placement */

        /* Determine item position based on explicit placement or
         * auto-flow */
        /* Note: GRID_PLACEMENT_SPAN means auto-place but with a span,
         * not an explicit column/row position. Only SET provides position. */
        if (col_start != GRID_PLACEMENT_AUTO && col_start != GRID_PLACEMENT_SPAN) {
            item_col = col_start;
        } else if (is_dense) {
            /* CSS Grid spec §8.5: Dense mode - scan from start
             * for first available cell that fits item's span.
             * For row flow: scan row-by-row (row 0: cols 0,1,2..., row 1: cols 0,1,2...)
             * For column flow: scan column-by-column (col 0: rows 0,1,2..., col 1: rows 0,1,2...)
             */
            item_col = -1;
            if (flow_is_column) {
                /* Column flow: scan column-first, respecting explicit row count */
                int max_scan_row = num_rows - row_span;
                for (int scan_col = 0; scan_col <= num_cols - col_span && item_col < 0; scan_col++) {
                    for (int scan_row = 0; scan_row <= max_scan_row; scan_row++) {
                        /* Check if all cells in span are free */
                        bool fits = true;
                        for (int dr = 0; dr < row_span && fits; dr++) {
                            for (int dc = 0; dc < col_span && fits; dc++) {
                                int idx = (scan_row + dr) * num_cols + scan_col + dc;
                                if (idx >= occupied_max_rows * num_cols || occupied[idx]) {
                                    fits = false;
                                }
                            }
                        }
                        if (fits) {
                            item_col = scan_col;
                            item_row = scan_row;
                            goto dense_found;
                        }
                    }
                }
            } else {
                /* Row flow: scan row-first */
                for (int scan_row = 0; scan_row < occupied_max_rows && item_col < 0; scan_row++) {
                    for (int scan_col = 0; scan_col <= num_cols - col_span; scan_col++) {
                        /* Check if all cells in span are free */
                        bool fits = true;
                        for (int dr = 0; dr < row_span && fits; dr++) {
                            for (int dc = 0; dc < col_span && fits; dc++) {
                                int idx = (scan_row + dr) * num_cols + scan_col + dc;
                                if (idx >= occupied_max_rows * num_cols || occupied[idx]) {
                                    fits = false;
                                }
                            }
                        }
                        if (fits) {
                            item_col = scan_col;
                            item_row = scan_row;
                            goto dense_found;
                        }
                    }
                }
            }
        dense_found:
            if (item_col < 0) {
                /* Fallback: use cursor if scan failed */
                item_col = auto_col;
                item_row = auto_row;
            }
        } else {
            /* Normal mode: use cursor */
            item_col = auto_col;
        }

        if (row_start != GRID_PLACEMENT_AUTO && row_start != GRID_PLACEMENT_SPAN) {
            item_row = row_start;
        } else if (!is_dense) {
            /* Only set from cursor if not dense (dense sets both)
             */
            item_row = auto_row;
        }

        NSLOG(layout, WARNING, "GRID PLACE DECISION: item_col=%d item_row=%d (cursor col=%d row=%d) flow_is_column=%d",
            item_col, item_row, auto_col, auto_row, flow_is_column);

        /* Clamp to valid range */
        if (item_col < 0)
            item_col = 0;
        if (item_col >= num_cols)
            item_col = num_cols - 1;
        if (item_row < 0)
            item_row = 0;

        /* Recalculate span after positioning (for auto end values) */
        if (col_end != GRID_PLACEMENT_AUTO && col_end > item_col) {
            col_span = col_end - item_col;
        }
        if (row_end != GRID_PLACEMENT_AUTO && row_end > item_row) {
            row_span = row_end - item_row;
        }

        /* Clamp span to grid bounds */
        if (item_col + col_span > num_cols) {
            col_span = num_cols - item_col;
        }

        /* Calculate child width (sum of spanned columns + gaps) */
        child_width = 0;
        for (int c = item_col; c < item_col + col_span; c++) {
            child_width += col_widths[c];
            if (c > item_col) {
                child_width += gap_px; /* Add gap between columns */
            }
        }

        /* Calculate x position (sum of columns before item_col) */
        child_x = 0;
        for (int c = 0; c < item_col; c++) {
            child_x += col_widths[c] + gap_px;
        }

        /* Resolve CSS dimensions - this populates child->padding and child->border */
        layout_find_dimensions(&content->unit_len_ctx, child_width, -1, child, child->style, &child->width,
            &child->height, &child->max_width, &child->min_width, NULL, NULL, child->margin, child->padding,
            child->border);

        /* Subtract horizontal padding and border so total box width fits in cell */
        int content_width = child_width - child->padding[LEFT] - child->padding[RIGHT] - child->border[LEFT].width -
            child->border[RIGHT].width;
        if (content_width < 0) {
            content_width = 0;
        }
        child->width = content_width;

        /* Recursively layout the child */
        if (child->type == BOX_BLOCK || child->type == BOX_INLINE_BLOCK || child->type == BOX_FLEX ||
            child->type == BOX_GRID) {
            if (!layout_block_context(child, -1, content)) {
                free(col_widths);
                free(row_heights);
                return false;
            }
        }

        /* Track row heights for all spanned rows - include padding and border */
        int total_height = child->height + child->padding[TOP] + child->padding[BOTTOM] + child->border[TOP].width +
            child->border[BOTTOM].width;
        int height_per_row = total_height / row_span;
        for (int r = item_row; r < item_row + row_span; r++) {
            if (!ensure_row_capacity(&row_heights, &row_heights_capacity, r)) {
                free(col_widths);
                free(row_heights);
                return false;
            }
            if (height_per_row > row_heights[r]) {
                row_heights[r] = height_per_row;
            }
        }

        /* Track max row for grid height calculation */
        if (item_row + row_span > max_row) {
            max_row = item_row + row_span;
        }

        /* Determine vertical alignment for this grid item */
        uint8_t align = CSS_ALIGN_ITEMS_STRETCH; /* default */

        /* Check align-self on the child first */
        if (child->style) {
            uint8_t align_self = css_computed_align_self(child->style);
            if (align_self == CSS_ALIGN_SELF_AUTO) {
                /* Use align-items from grid container */
                if (grid->style) {
                    align = css_computed_align_items(grid->style);
                }
            } else {
                /* Use align-self value */
                align = align_self;
            }
        } else if (grid->style) {
            /* No child style, use grid container's align-items */
            align = css_computed_align_items(grid->style);
        }

        /* Calculate total available height for this item */
        int spanned_height = 0;
        for (int r = item_row; r < item_row + row_span; r++) {
            spanned_height += row_heights[r];
            if (r > item_row) {
                spanned_height += gap_px;
            }
        }

        /* Apply alignment */
        int content_height = child->height; /* Height from layout */
        int align_offset = 0;

        switch (align) {
        case CSS_ALIGN_ITEMS_STRETCH: {
            /* Stretch to fill entire spanned height - subtract padding/border */
            int stretch_height = spanned_height - child->padding[TOP] - child->padding[BOTTOM] -
                child->border[TOP].width - child->border[BOTTOM].width;
            if (stretch_height < 0) {
                stretch_height = 0;
            }
            child->height = stretch_height;
            align_offset = 0;
            break;
        }
        case CSS_ALIGN_ITEMS_FLEX_START:
        case CSS_ALIGN_ITEMS_BASELINE:
            /* Align to start (top) - keep content height */
            align_offset = 0;
            break;
        case CSS_ALIGN_ITEMS_FLEX_END:
            /* Align to end (bottom) */
            align_offset = spanned_height - content_height;
            if (align_offset < 0)
                align_offset = 0;
            break;
        case CSS_ALIGN_ITEMS_CENTER:
            /* Center vertically */
            align_offset = (spanned_height - content_height) / 2;
            if (align_offset < 0)
                align_offset = 0;
            break;
        default:
            /* Unknown - default to stretch */
            child->height = spanned_height;
            align_offset = 0;
            break;
        }

        /* Calculate y position (sum of row heights before item_row) */
        child_y = 0;
        for (int r = 0; r < item_row; r++) {
            child_y += row_heights[r] + gap_px;
        }
        /* Apply alignment offset within the grid area */
        child_y += align_offset;

        /* Position child */
        child->x = child_x;
        child->y = child_y;

        NSLOG(layout, INFO, "Grid item placed: col=%d-%d row=%d-%d x=%d y=%d w=%d h=%d", item_col, item_col + col_span,
            item_row, item_row + row_span, child_x, child_y, child->width, child->height);

        /* Mark cells as occupied for dense mode tracking */
        if (is_dense && occupied != NULL) {
            for (int dr = 0; dr < row_span; dr++) {
                for (int dc = 0; dc < col_span; dc++) {
                    int occ_row = item_row + dr;
                    int occ_col = item_col + dc;
                    if (occ_row < occupied_max_rows && occ_col < num_cols) {
                        int idx = occ_row * num_cols + occ_col;
                        occupied[idx] = true;
                    }
                }
            }
        }

        /* CSS Grid spec §8: Advance auto-placement cursor
         * - row mode: Advance column, wrap to next row at end
         * - column mode: Advance row, wrap to next column at end
         * Note: SPAN also uses auto-placement, so advance cursor for SPAN too
         */
        bool col_auto = (col_start == GRID_PLACEMENT_AUTO || col_start == GRID_PLACEMENT_SPAN);
        bool row_auto = (row_start == GRID_PLACEMENT_AUTO || row_start == GRID_PLACEMENT_SPAN);
        if (col_auto && row_auto) {
            if (flow_is_column) {
                /* Column mode: advance row past the placed item's span */
                auto_row = item_row + row_span;
                if (auto_row >= num_rows) {
                    auto_row = 0;
                    auto_col++;
                }
            } else {
                /* Row mode (default): advance column past the placed item's span */
                auto_col = item_col + col_span;
                if (auto_col >= num_cols) {
                    auto_col = 0;
                    auto_row++;
                }
            }
        }
    }

    /* Calculate total grid height */
    grid_height = 0;
    for (int r = 0; r < max_row; r++) {
        grid_height += row_heights[r];
        if (r > 0) {
            grid_height += gap_px;
        }
    }

    grid->height = grid_height;

    free(occupied);
    free(row_heights);
    free(col_widths);
    return true;
}

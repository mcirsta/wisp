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
 */

#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdint.h>
#include <limits.h>
#include <string.h>

#include <libcss/libcss.h>
#include <libcss/computed.h>

#include <neosurf/utils/log.h>
#include <neosurf/utils/utils.h>
#include <neosurf/content/handlers/html/private.h>
#include "content/handlers/html/layout_grid.h"
#include "content/handlers/html/layout_internal.h"
#include <neosurf/content/handlers/html/box.h>
#include <dom/dom.h>
#include <neosurf/utils/corestrings.h>


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
		grid_template_type = css_computed_grid_template_columns(
			grid->style, &n_tracks, &tracks);

		NSLOG(layout,
		      INFO,
		      "grid_get_column_count: type=%d, n_tracks=%d, tracks=%p",
		      grid_template_type,
		      n_tracks,
		      tracks);

		if (grid_template_type == CSS_GRID_TEMPLATE_SET &&
		    n_tracks > 0) {
			NSLOG(layout,
			      INFO,
			      "CSS grid-template-columns: %d tracks",
			      n_tracks);
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
					NSLOG(layout,
					      INFO,
					      "  Track %d: minmax(min=%f %d, max=%f %d)",
					      i,
					      FIXTOFLT(tracks[i].value),
					      tracks[i].min_unit,
					      FIXTOFLT(tracks[i].max_value),
					      tracks[i].max_unit);
					continue;
				default:
					break;
				}
				NSLOG(layout,
				      INFO,
				      "  Track %d: %f %s",
				      i,
				      FIXTOFLT(tracks[i].value),
				      unit_str);
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
void layout_minmax_grid(struct box *grid,
			const struct gui_layout_table *font_func,
			const html_content *content)
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
		if (css_computed_column_gap(grid->style, &gap_len, &gap_unit) ==
		    CSS_COLUMN_GAP_SET) {
			gap_px = FIXTOINT(
				css_unit_len2device_px(grid->style,
						       &content->unit_len_ctx,
						       gap_len,
						       gap_unit));
		}
	}

	/* Recursively calculate min/max for all children and accumulate
	 * per-column. Children are placed in columns in order. */
	col_idx = 0;
	for (child = grid->children; child; child = child->next) {
		/* Skip absolutely positioned children - they don't affect
		 * intrinsic sizing */
		if (child->style && (css_computed_position(child->style) ==
					     CSS_POSITION_ABSOLUTE ||
				     css_computed_position(child->style) ==
					     CSS_POSITION_FIXED)) {
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
		css_computed_grid_template_columns(grid->style,
						   &n_tracks,
						   &tracks);
	}

	/* Calculate grid min/max from tracks and content */
	for (i = 0; i < num_cols; i++) {
		int track_min = col_min[i];
		int track_max = col_max[i];

		if (n_tracks > 0 && tracks != NULL) {
			css_computed_grid_track *t = &tracks[i % n_tracks];

			if (t->unit == CSS_UNIT_PX) {
				/* Fixed track: use the fixed size */
				int fixed_w = FIXTOINT(css_unit_len2device_px(
					grid->style,
					&content->unit_len_ctx,
					t->value,
					t->unit));
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

	NSLOG(layout,
	      DEBUG,
	      "Grid %p minmax: min=%d max=%d cols=%d gap=%d",
	      grid,
	      min,
	      max,
	      num_cols,
	      gap_px);
}

static void layout_grid_compute_tracks(struct box *grid,
				       int available_width,
				       int *col_widths,
				       int num_cols,
				       const css_computed_style *style,
				       const css_unit_ctx *unit_len_ctx)
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
	if (css_computed_column_gap(style, &gap_len, &gap_unit) ==
	    CSS_COLUMN_GAP_SET) {
		gap_px = FIXTOINT(css_unit_len2device_px(
			style, unit_len_ctx, gap_len, gap_unit));
	}
	NSLOG(netsurf,
	      INFO,
	      "Column Gap: %d px (from val %d unit %d)",
	      gap_px,
	      gap_len,
	      gap_unit);

	/* Calculate total gap width consumed */
	if (num_cols > 1) {
		/* Parameters:
		 * box->width is the available width for the grid container
		 */
		NSLOG(netsurf,
		      INFO,
		      "Grid Layout Compute Tracks: Available Width %d, Num Cols %d",
		      available_width,
		      num_cols);
		total_gap_width = (num_cols - 1) * gap_px;
	}

	/* Get explicit track definitions */
	if (css_computed_grid_template_columns(style, &n_tracks, &tracks) ==
		    CSS_GRID_TEMPLATE_SET &&
	    n_tracks > 0) {
		/* Use parsed tracks */
		for (i = 0; i < num_cols; i++) {
			/* Cycle through tracks if num_cols > n_tracks (implicit
			 * grid) */
			css_computed_grid_track *t = &tracks[i % n_tracks];

			if (t->unit == CSS_UNIT_FR) {
				fr_tracks++;
				fr_total += FIXTOINT(t->value);
				NSLOG(netsurf,
				      INFO,
				      "Track %d is FR: val %d",
				      i,
				      FIXTOINT(t->value));
				col_widths[i] = 0; /* Will be assigned later */
			} else if (t->unit == CSS_UNIT_MIN_CONTENT ||
				   t->unit == CSS_UNIT_MAX_CONTENT) {
				/* Treat min/max-content as auto/1fr for now to
				 * ensure visibility */
				fr_tracks++;
				fr_total += 1;
				NSLOG(netsurf,
				      INFO,
				      "Track %d is Content (fallback to 1fr)",
				      i);
				col_widths[i] = 0;
			} else if (t->unit == CSS_UNIT_MINMAX) {
				/* For minmax(min, max), try to use max if it's
				 * a Length */
				/* If max is also dynamic, fallback to 1fr */
				if (t->max_unit == CSS_UNIT_PX ||
				    t->max_unit == CSS_UNIT_EM ||
				    t->max_unit == CSS_UNIT_PCT) {
					int w = FIXTOINT(css_unit_len2device_px(
						style,
						unit_len_ctx,
						t->max_value,
						t->max_unit));
					if (t->max_unit == CSS_UNIT_PCT) {
						/* Resolve percentage against
						 * available width */
						w = (w * available_width) /
						    100; // approximation if
							 // conversion not fully
							 // context-aware
					}
					col_widths[i] = w;
					used_width += w;
					NSLOG(netsurf,
					      INFO,
					      "Track %d is MINMAX(..., %d %s) -> width %d",
					      i,
					      FIXTOINT(t->max_value),
					      "fixed",
					      w);
				} else {
					/* Max is FR or Content -> Treat as 1fr
					 */
					fr_tracks++;
					fr_total += 1;
					NSLOG(netsurf,
					      INFO,
					      "Track %d is MINMAX(..., dynamic) -> fallback to 1fr",
					      i);
					col_widths[i] = 0;
				}

			} else {
				/* Handle fixed units (px, etc) */
				if (t->unit == CSS_UNIT_PX ||
				    t->unit == CSS_UNIT_EM) {
					int w = FIXTOINT(css_unit_len2device_px(
						style,
						unit_len_ctx,
						t->value,
						t->unit));
					col_widths[i] = w;
					used_width += w;
					NSLOG(netsurf,
					      INFO,
					      "Track %d is Fixed: %d",
					      i,
					      w);
				} else {
					col_widths[i] =
						0; /* Auto/Other fallback */
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
		NSLOG(netsurf,
		      INFO,
		      "Distributing FR: Remaining %d, FR Total %d",
		      remaining_width,
		      fr_total);
		int px_per_fr = remaining_width / fr_total;
		NSLOG(netsurf, INFO, "PX per FR: %d", px_per_fr);
		for (i = 0; i < num_cols; i++) {
			/* Check if this column was an FR track.
			   We need to re-check the track definition or use a
			   flag. Re-checking logic for simplicity.
			*/
			bool is_fr = false;
			int fr_val = 0;

			if (n_tracks > 0) {
				css_computed_grid_track *t =
					&tracks[i % n_tracks];
				if (t->unit == CSS_UNIT_FR) {
					is_fr = true;
					fr_val = FIXTOINT(t->value);
				} else if (t->unit == CSS_UNIT_MIN_CONTENT ||
					   t->unit == CSS_UNIT_MAX_CONTENT) {
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

	NSLOG(layout,
	      WARNING,
	      "GRID LAYOUT: grid=%p avail_w=%d num_cols=%d children=%p",
	      grid,
	      available_width,
	      num_cols,
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
	if (grid->style != NULL &&
	    css_computed_column_gap(grid->style, &gap_len, &gap_unit) ==
		    CSS_COLUMN_GAP_SET) {
		gap_px = FIXTOINT(css_unit_len2device_px(grid->style,
							 &content->unit_len_ctx,
							 gap_len,
							 gap_unit));
	}

	layout_grid_compute_tracks(grid,
				   available_width,
				   col_widths,
				   num_cols,
				   grid->style,
				   &content->unit_len_ctx);

	/* Log computed column widths */
	for (int i = 0; i < num_cols; i++) {
		NSLOG(layout,
		      WARNING,
		      "GRID LAYOUT: col[%d] width=%d",
		      i,
		      col_widths[i]);
	}

	int row_height = 0;

	/* Reset grid position */
	x = 0;
	y = 0;

	for (child = grid->children; child; child = child->next) {
		int child_width = col_widths[col_idx];

		/* Resolve CSS dimensions (height, borders, padding, etc.) */
		layout_find_dimensions(&content->unit_len_ctx,
				       child_width,
				       -1, /* viewport_height unknown here */
				       child,
				       child->style,
				       &child->width,
				       &child->height,
				       &child->max_width,
				       &child->min_width,
				       NULL,
				       NULL,
				       child->margin,
				       child->padding,
				       child->border);

		/* Force child width to track width (stretch) */
		child->width = child_width;

		/* Recursively layout the child */
		/* Note: We might need layout_minmax first if not done?
		   layout_block_context usually expects minmax to be done.
		   But layout_block_context does the layout. */


		/* We'll use layout_block_context if it's a block-like thing. */
		if (child->type == BOX_BLOCK ||
		    child->type == BOX_INLINE_BLOCK ||
		    child->type == BOX_FLEX || child->type == BOX_GRID) {
			if (!layout_block_context(child, -1, content)) {
				free(col_widths);
				return false;
			}
			NSLOG(layout,
			      INFO,
			      "Grid child %p type %d layout done. w %d h %d",
			      child,
			      child->type,
			      child->width,
			      child->height);
		} else {
			/* What if it is a text node? Anonymous block wrapper
			 * should have been created by box_normalise? */
			/* Assuming block level children for now as per HTML
			 * structure */
			NSLOG(layout,
			      INFO,
			      "Grid child %p type %d SKIPPED layout (not block-like)",
			      child,
			      child->type);
		}

		/* Position child */
		child->x = x;
		child->y = y;

		/* Track row height */
		if (child->height > row_height) {
			row_height = child->height;
		}

		/* Advance column */
		x += child_width + gap_px;
		col_idx++;
		if (col_idx >= num_cols) {
			/* New Row */
			col_idx = 0;
			x = 0;
			y += row_height + gap_px;
			grid_height += row_height + gap_px;
			row_height = 0;
		}
	}

	/* Add last row height if not empty */
	if (col_idx > 0) {
		grid_height += row_height;
	}

	grid->height = grid_height;

	free(col_widths);
	return true;
}

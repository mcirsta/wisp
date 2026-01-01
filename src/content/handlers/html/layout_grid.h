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
 * HTML layout grid private interface.
 */

#ifndef NETSURF_HTML_LAYOUT_GRID_H
#define NETSURF_HTML_LAYOUT_GRID_H

#include <stdbool.h>
#include <libwapcaplet/libwapcaplet.h>
#include <dom/dom.h>
#include <neosurf/content/handlers/html/html.h>
#include <neosurf/content/handlers/html/box.h>

/**
 * Layout a grid container.
 *
 * \param  grid             box of type BOX_GRID or BOX_INLINE_GRID
 * \param  available_width  width of containing block
 * \param  content          memory pool for any new boxes
 * \return  true on success, false on memory exhaustion
 */
bool layout_grid(struct box *grid,
		 int available_width,
		 struct html_content *content);

/**
 * Calculate minimum and maximum width of a grid container.
 *
 * \param grid      box of type BOX_GRID
 * \param font_func font functions for text measurement
 * \param content   The HTML content being laid out.
 * \post  grid->min_width and grid->max_width filled in
 */
void layout_minmax_grid(struct box *grid,
			const struct gui_layout_table *font_func,
			const struct html_content *content);

#endif

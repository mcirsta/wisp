/*
 * Copyright 2003 James Bursa <bursa@users.sourceforge.net>
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
 * interface to HTML layout.
 *
 * The main interface to the layout code is layout_document(), which takes a
 * normalized box tree and assigns coordinates and dimensions to the boxes, and
 * also adds boxes to the tree (eg. when formatting lines of text).
 */

#ifndef WISP_HTML_LAYOUT_H
#define WISP_HTML_LAYOUT_H

#include <stdbool.h>

struct box;
struct html_content;
struct gui_layout_table;

/**
 * Calculate positions of boxes in a document.
 *
 * \param content content of type CONTENT_HTML
 * \param width available width
 * \param height available height
 * \return true on success, false on memory exhaustion
 */
bool layout_document(struct html_content *content, int width, int height);

#ifdef WISP_ENABLE_PARTIAL_UPDATES
/**
 * Discard derived layout state after publishing a replacement box region.
 * The normalized construction tree, styles, resources and widgets survive.
 * Content must remain locked/restyling until full layout, document extents and
 * selection indexing have been updated. This allocation-free reset is not a
 * substitute for layout and must not be used for ordinary cached reflows.
 * The capture-only intrinsic-reuse opt-in may preserve text-only inline
 * measurements when the retained publication's style/font/unit inputs match.
 * Used geometry and line wrapping are always discarded and recalculated.
 */
void layout_reset_document(struct html_content *content);
#endif

#endif

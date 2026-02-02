/*
 * Copyright 2024 NeoSurf Contributors
 *
 * This file is part of NeoSurf, http://www.netsurf-browser.org/
 *
 * NeoSurf is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * NeoSurf is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * \file
 * Web font (font-face) loading interface.
 */

#ifndef NETSURF_HTML_FONT_FACE_H
#define NETSURF_HTML_FONT_FACE_H

#include <libcss/libcss.h>
#include <neosurf/utils/errors.h>

struct html_content;
struct nsurl;

/**
 * Process font-face rules from the selection context and start fetching fonts.
 *
 * \param c          HTML content with loaded stylesheets
 * \param select_ctx CSS selection context with all stylesheets
 * \return NSERROR_OK on success, or error code
 */
nserror html_font_face_init(struct html_content *c, css_select_ctx *select_ctx);

/**
 * Free font-face resources for an HTML content.
 *
 * \param c HTML content
 * \return NSERROR_OK on success
 */
nserror html_font_face_fini(struct html_content *c);

/**
 * Check if a font family is available (either system or loaded web font).
 *
 * \param family_name Font family name to check
 * \return true if available, false otherwise
 */
bool html_font_face_is_available(const char *family_name);

/**
 * Process a font-face rule and start downloading the font.
 *
 * This is called from the CSS module when a font-face rule is parsed.
 *
 * \param font_face   Parsed font-face rule from libcss
 * \param base_url    Base URL for resolving relative font URLs
 * \return NSERROR_OK on success, or error code
 */
nserror html_font_face_process(const css_font_face *font_face, const char *base_url);

/*
 * NOTE: Font data loading is handled via gui_layout_table.load_font_data
 * in include/neosurf/layout.h. Frontends MUST implement this table entry.
 * Do NOT define a function named html_font_face_load_data - it will not be called.
 */
/**
 * Callback invoked when all pending font downloads have completed.
 * This can be used to trigger a page redraw (FOUT strategy).
 */
typedef void (*html_font_face_done_cb)(void);

/**
 * Set the callback to invoke when all pending font downloads complete.
 *
 * \param cb Callback function (or NULL to unset)
 */
void html_font_face_set_done_callback(html_font_face_done_cb cb);

/**
 * Check if there are any pending font downloads.
 *
 * \return true if fonts are still downloading, false otherwise
 */
bool html_font_face_has_pending(void);

/**
 * Get the count of pending font downloads (for logging).
 *
 * \return number of fonts still downloading
 */
int html_font_face_pending_count(void);

#endif /* NETSURF_HTML_FONT_FACE_H */

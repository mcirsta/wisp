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

#ifndef WISP_HTML_FONT_FACE_H
#define WISP_HTML_FONT_FACE_H

#include <libcss/types.h>
#include <wisp/utils/errors.h>

struct html_content;
/** Start the document's applicable faces once its stylesheets are ready. */
nserror html_font_face_init(struct html_content *c, css_select_ctx *select_ctx);

/** Cancel only this document's pending downloads; retain already loaded faces. */
void html_font_face_stop(struct html_content *c);

/** Cancel downloads and release the document's native font registrations. */
void html_font_face_fini(struct html_content *c);

bool html_font_face_has_pending(const struct html_content *c);
unsigned html_font_face_pending_count(const struct html_content *c);

/* Native font database changes still invalidate retained font measurements. */
uint64_t html_font_face_generation(void);
void html_font_face_notify_changed(void);

#endif

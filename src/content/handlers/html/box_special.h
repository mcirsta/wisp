/*
 * Copyright 2020 Vincent Sanders <vince@netsurf-browser.org>
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
 * HTML Box tree construction special element conversion interface.
 */

#ifndef WISP_HTML_BOX_SPECIAL_H
#define WISP_HTML_BOX_SPECIAL_H

#include <stdbool.h>
#include <dom/dom.h>
#include <libcss/computed.h>
#include <wisp/utils/errors.h>

struct html_content;
struct box;
struct box_construct_region;
struct object_params;
struct nsurl;

/* Read-only image source/representation preflight, shared with search. The
 * caller owns a URL reference (possibly NULL); no resource is fetched here. */
bool box_image_prepare(dom_node *node, struct html_content *content, const css_computed_style *style,
    struct nsurl **url, bool *replaced);

/* Prepare an object's resource parameters without fetching or constructing
 * boxes. On success, NULL means known fallback; otherwise the caller owns a
 * talloc allocation under owner. Shared with discovery in skipped subtrees. */
bool box_object_prepare(dom_node *node, struct html_content *content, void *owner, struct object_params **result);

/**
 * call an elements special conversion handler
 *
 * \param box_context Allocation owner for the construction region. Framesets
 *                    remain document-owned and require full reconstruction.
 * \param region Optional detached result: stage supported bindings there.
 * \return NSERROR_OK, NSERROR_NOT_IMPLEMENTED for unsafe detached handlers, or an error.
 */
nserror convert_special_elements(dom_node *node, struct html_content *content, struct box *box, bool *convert_children,
    void *box_context, struct box_construct_region *region);

#endif

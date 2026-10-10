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
 * HTML Box tree construction interface.
 *
 * This stage of rendering converts a tree of dom_nodes (produced by libdom)
 * to a tree of struct box. The box tree represents the structure of the
 * document as given by the CSS display and float properties.
 *
 * For example, consider the following HTML:
 * \code
 *   <h1>Example Heading</h1>
 *   <p>Example paragraph <em>with emphasised text</em> etc.</p>       \endcode
 *
 * This would produce approximately the following box tree with default CSS
 * rules:
 * \code
 *   BOX_BLOCK (corresponds to h1)
 *     BOX_INLINE_CONTAINER
 *       BOX_INLINE "Example Heading"
 *   BOX_BLOCK (p)
 *     BOX_INLINE_CONTAINER
 *       BOX_INLINE "Example paragraph "
 *       BOX_INLINE "with emphasised text" (em)
 *       BOX_INLINE "etc."                                             \endcode
 *
 * Note that the em has been collapsed into the INLINE_CONTAINER.
 *
 * If these CSS rules were applied:
 * \code
 *   h1 { display: table-cell }
 *   p { display: table-cell }
 *   em { float: left; width: 5em }                                    \endcode
 *
 * then the box tree would instead look like this:
 * \code
 *   BOX_TABLE
 *     BOX_TABLE_ROW_GROUP
 *       BOX_TABLE_ROW
 *         BOX_TABLE_CELL (h1)
 *           BOX_INLINE_CONTAINER
 *             BOX_INLINE "Example Heading"
 *         BOX_TABLE_CELL (p)
 *           BOX_INLINE_CONTAINER
 *             BOX_INLINE "Example paragraph "
 *             BOX_FLOAT_LEFT (em)
 *               BOX_BLOCK
 *                 BOX_INLINE_CONTAINER
 *                   BOX_INLINE "with emphasised text"
 *             BOX_INLINE "etc."                                       \endcode
 *
 * Here implied boxes have been added and a float is present.
 */

#ifndef WISP_HTML_BOX_CONSTRUCT_H
#define WISP_HTML_BOX_CONSTRUCT_H

#include <wisp/content_type.h>
#include <wisp/content/handlers/html/box.h>
#include <libcss/libcss.h>

struct html_content;
struct dom_node;

/* Shared style selection for construction and discovery in skipped subtrees. */
css_select_results *box_get_style(struct html_content *c, const css_computed_style *parent_style,
    const css_computed_style *root_style, struct dom_node *n);
void box_text_transform(char *s, unsigned int len, enum css_text_transform_e transform);
/* Text from one generated-content item, allocated under owner with talloc.
 * Non-text and unsupported items return NULL with NSERROR_OK. */
nserror box_generated_text(const css_computed_content_item *item, struct dom_node *node, void *owner, char **text);
/* Formatting decisions shared with search discovery before boxes exist. */
box_type box_construct_type(const css_computed_style *style, box_type parent_type, bool root);
bool box_type_generates_content(box_type type);

/**
 * Construct a box tree from a dom and html content
 *
 * \param n document element (the full-document entry point, not a subtree)
 * \param c content of type CONTENT_HTML to construct box tree in
 * \param cb callback to report conversion completion
 * \param box_conversion_context pointer that recives the conversion context
 * \return netsurf error code indicating status of call
 */
nserror
dom_to_box(struct dom_node *n, struct html_content *c, box_construct_complete_cb cb, void **box_conversion_context);


/* A deferred request, owned by its unpublished region. The box (including
 * object parameters) belongs to that same region. No cache handle or live object
 * is borrowed here: callbacks must continue to address the displayed tree. */
struct box_construct_object {
    struct box_construct_object *next;
    struct box *box;
    struct nsurl *url;
    /* URL requests have buffer == NULL; buffer requests have url == NULL.
     * Serialized data and MIME type are copied, never borrowed from live state. */
    uint8_t *buffer;
    size_t buffer_length;
    char *mime_type;
    content_type permitted_types;
    bool background;
    uint32_t background_layer;
};

/* An unpublished tree and its allocation owner. root is NULL for display:none.
 * This is construction output only: it is not yet safe to splice into layout. */
struct box_construct_region {
    struct box *root;
    struct box_construct_object *objects;
    struct content_html_iframe *iframes;
};

typedef void (*box_construct_region_cb)(
    struct html_content *content, nserror error, struct box_construct_region *region, void *pw);

/** Allocation owner for an earmarked visible scope in a full reconstruction,
 * or NULL if it has no independent owner. Includes generated/anonymous boxes
 * and skipped descendants. Borrowed inherited data still belongs to ancestors.
 * This alone does not certify safe replacement: bindings, physical boundary
 * closure, input generations and retained layout must also be validated. */
void *box_construct_region_owner(const struct box *box);

/**
 * Construct an unpublished block/flex/grid subtree using the existing builder.
 *
 * parent must be the mapped DOM parent and remain alive, together with content
 * and its document root, until completion/cancellation and result retirement.
 * The result borrows inherited title/target strings from that parent. owner is
 * a talloc parent for the result arena and must also outlive the operation.
 *
 * No live box mappings or resource/control bindings are published. CSS selection
 * may update DOM selector caches; callers must invalidate dirty styles first.
 * Resource requests (URLs and SVG buffers) and iframe descriptors are staged.
 * Native buttons/checkboxes/radios are fresh gadgets, unlinked from live forms.
 * No fetches, browser windows, or widgets are started. Text/file inputs, selects,
 * textareas and unsupported boundaries return NOT_IMPLEMENTED
 * (synchronously or through cb). An initial error schedules no callback.
 * Success transfers the region to cb; failure passes NULL. Cancellation reports
 * no callback. The conversion-context slot must initially be NULL; the caller
 * clears it on completion.
 *
 * Publication still requires validation of input generations, boundary closure,
 * and retained layout state; this entry point does not provide those guarantees.
 */
nserror dom_to_box_region(struct dom_node *node, struct html_content *content, const struct box *parent, void *owner,
    box_construct_region_cb cb, void *pw, void **box_conversion_context);

/** Fetch through the existing document path, or stage a region-owned request.
 * A non-NULL region never modifies live resource bindings or loading state. */
bool box_construct_request_object(struct html_content *content, struct box_construct_region *region, struct nsurl *url,
    struct box *box, content_type permitted_types, bool background, uint32_t layer);

/** The buffer equivalent: a region owns a copy of both data and MIME type. */
bool box_construct_request_buffer(struct html_content *content, struct box_construct_region *region,
    const uint8_t *buffer, size_t length, const char *mime_type, struct box *box, content_type permitted_types);

/** Retire an unpublished region, including unlinked/hidden construction boxes. */
void box_construct_region_destroy(struct box_construct_region *region);

/**
 * aborts any ongoing box construction
 */
nserror cancel_dom_to_box(void *box_conversion_context);


/**
 * Retrieve the box for a dom node, if there is one
 *
 * \param node The DOM node
 * \return The box if there is one
 */
struct box *box_for_node(struct dom_node *node);

#endif

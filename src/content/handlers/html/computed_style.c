/* SPDX-License-Identifier: GPL-2.0-or-later
 * CSSOM reads share renderer selection, including elements without boxes. */
#include <stdlib.h>
#include <wisp/content/handlers/html/private.h>
#include <wisp/content/handlers/html/box.h>
#include "content/handlers/css/select.h"
#include "content/handlers/html/box_construct.h"
#include "content/handlers/html/css.h"

void html_computed_style_fini(struct html_computed_style *result)
{
    if (result->owned != NULL)
        css_select_results_destroy(result->owned);
    if (result->initial != NULL)
        css_computed_style_destroy(result->initial);
    *result = (struct html_computed_style){0};
}

nserror html_computed_style_get(html_content *html, dom_node *node,
    css_pseudo_element pseudo, struct html_computed_style *result)
{
    *result = (struct html_computed_style){0};
    dom_node **chain = NULL, *current = dom_node_ref(node);
    size_t count = 0, capacity = 0;
    const css_computed_style *parent = NULL, *root = html->unit_len_ctx.root_style;
    const css_select_results *selected = NULL;
    css_select_results *root_owned = NULL;
    nserror error = NSERROR_OK;
    /* Find the closest already-rendered ancestor. The common visible-element
     * path borrows its selected styles and allocates nothing. */
    while (current != NULL) {
        struct box *box = box_for_node(current);
        if (box != NULL && box->style != NULL) {
            parent = box->style;
            if (count == 0)
                selected = box->styles;
            break;
        }
        dom_node_type type;
        if (dom_node_get_node_type(current, &type) != DOM_NO_ERR) {
            error = NSERROR_DOM;
            goto out;
        }
        if (type != DOM_ELEMENT_NODE)
            break;
        if (count == capacity) {
            size_t next = capacity ? capacity * 2 : 16;
            if (next < capacity || next > SIZE_MAX / sizeof(*chain)) {
                error = NSERROR_NOMEM;
                goto out;
            }
            dom_node **grown = realloc(chain, next * sizeof(*chain));
            if (grown == NULL) {
                error = NSERROR_NOMEM;
                goto out;
            }
            chain = grown;
            capacity = next;
        }
        chain[count++] = current;
        current = NULL;
        if (dom_node_get_parent_node(chain[count - 1], &current) != DOM_NO_ERR) {
            error = NSERROR_DOM;
            goto out;
        }
    }
    for (size_t i = count; i > 0; i--) {
        /* A display:none node can have a libcss cache without a retained box
         * style. This path deliberately reselects it; retire the old selection
         * data first, just as the renderer does before rebuilding styles. */
        error = nscss_invalidate_node(chain[i - 1], NULL);
        if (error != NSERROR_OK)
            goto out;
        css_select_results *next = box_get_style(html, parent, root, chain[i - 1]);
        if (next == NULL) {
            error = NSERROR_NOMEM;
            goto out;
        }
        if (result->owned != NULL && result->owned != root_owned)
            css_select_results_destroy(result->owned);
        result->owned = next;
        selected = next;
        parent = next->styles[CSS_PSEUDO_ELEMENT_NONE];
        if (root == NULL) {
            root = parent;
            root_owned = next;
        }
    }
    result->style = pseudo == CSS_PSEUDO_ELEMENT_NONE ? parent
                                                   : selected ? selected->styles[pseudo] : NULL;
    if (result->style == NULL && parent != NULL && pseudo != CSS_PSEUDO_ELEMENT_NONE) {
        /* ::before/::after exist for style queries even without generated
         * content. Their unspecified values inherit from the originating node. */
        nscss_select_ctx ctx = {.ctx = html->select_ctx, .quirks = html->base.quirks,
            .base_url = html->base_url, .universal = html->universal, .root_style = root, .parent_style = parent};
        css_unit_ctx units = html->unit_len_ctx;
        units.root_style = root;
        result->initial = nscss_get_blank_style(&ctx, &units, parent);
        result->style = result->initial;
        if (result->style == NULL)
            error = NSERROR_NOMEM;
    }
out:
    dom_node_unref(current);
    for (size_t i = 0; i < count; i++)
        dom_node_unref(chain[i]);
    free(chain);
    if (root_owned != NULL && root_owned != result->owned)
        css_select_results_destroy(root_owned);
    if (error != NSERROR_OK)
        html_computed_style_fini(result);
    return error;
}

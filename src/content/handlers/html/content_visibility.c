/* SPDX-License-Identifier: GPL-2.0-only */
#include <stdlib.h>

#include <wisp/content/handlers/html/box.h>
#include <wisp/utils/corestrings.h>
#include "content/handlers/html/box_construct.h"
#include "content/handlers/css/select.h"
#include "content/handlers/html/content_visibility.h"
#include "content/handlers/html/restyle.h"

css_select_results *html_visibility_style(
    html_content *html, dom_node *node, const css_computed_style *parent, const css_computed_style *root)
{
    struct box *box = box_for_node(node);
    css_select_results *styles = NULL;
    if (box != NULL && box->styles != NULL) {
        if (css_select_results_clone(box->styles, &styles) != CSS_OK)
            return NULL;
        return styles;
    }
    /* Hidden nodes can have selector data from an earlier query but no box.
     * Replace only that node's data, retaining its ancestors' variable context. */
    if (nscss_invalidate_node(node, NULL) != NSERROR_OK)
        return NULL;
    return box_get_style(html, parent, root, node);
}

bool html_content_visibility_applies(const css_computed_style *style, bool replaced)
{
    switch (css_computed_display(style, false)) {
    case CSS_DISPLAY_NONE:
    case CSS_DISPLAY_TABLE_ROW:
    case CSS_DISPLAY_TABLE_ROW_GROUP:
    case CSS_DISPLAY_TABLE_HEADER_GROUP:
    case CSS_DISPLAY_TABLE_FOOTER_GROUP:
    case CSS_DISPLAY_TABLE_COLUMN:
    case CSS_DISPLAY_TABLE_COLUMN_GROUP:
        return false;
    case CSS_DISPLAY_INLINE:
    case CSS_DISPLAY_RUN_IN:
        return replaced;
    default:
        return true;
    }
}

bool html_until_found(dom_node *node)
{
    dom_node_type type;
    dom_string *value = NULL, *ns = NULL;
    if (node == NULL || dom_node_get_node_type(node, &type) != DOM_NO_ERR || type != DOM_ELEMENT_NODE ||
        dom_element_get_attribute(node, corestring_dom_hidden, &value) != DOM_NO_ERR)
        return false;
    bool html = dom_node_get_namespace(node, &ns) == DOM_NO_ERR &&
        (ns == NULL || dom_string_isequal(ns, corestring_dom_html_namespace));
    bool found = html && value != NULL && dom_string_caseless_lwc_isequal(value, corestring_lwc_until_found);
    dom_string_unref(ns);
    dom_string_unref(value);
    return found;
}

/* Hold the ancestor chain while dispatching events. It may be changed by a
 * beforematch listener; never borrow a box across the resulting restyle. */
nserror html_reveal_ancestors(html_content *html, dom_node *target, bool *changed)
{
    dom_node **chain = NULL, *node = NULL;
    size_t count = 0, capacity = 0;
    css_select_results **styles = NULL;
    nserror result = NSERROR_OK;
    bool found = false;
    *changed = false;
    dom_node_type type;
    if (dom_node_get_node_type(target, &type) != DOM_NO_ERR)
        return NSERROR_DOM;
    if (type == DOM_ELEMENT_NODE)
        node = dom_node_ref(target);
    else if (dom_node_get_parent_node(target, &node) != DOM_NO_ERR)
        return NSERROR_DOM;
    while (node != NULL && node != (dom_node *)html->document) {
        if (count == capacity) {
            size_t next = capacity ? capacity * 2 : 16;
            dom_node **grown = realloc(chain, next * sizeof(*chain));
            if (grown == NULL) {
                dom_node_unref(node);
                result = NSERROR_NOMEM;
                goto done;
            }
            chain = grown;
            capacity = next;
        }
        chain[count++] = node;
        found |= html_until_found(node);
        if (dom_node_get_parent_node(node, &node) != DOM_NO_ERR) {
            result = NSERROR_DOM;
            goto done;
        }
    }
    bool connected = node == (dom_node *)html->document;
    dom_node_unref(node);
    if (!connected || !found)
        goto done;
    styles = calloc(count, sizeof(*styles));
    if (styles == NULL) {
        result = NSERROR_NOMEM;
        goto done;
    }
    const css_computed_style *parent = NULL, *root = NULL;
    /* Check the complete path before changing any attributes. Hidden content
     * below an ordinary display:none or content-visibility:hidden barrier is
     * not a fragment/search reveal target. */
    for (size_t i = count; i-- > 0;) {
        styles[i] = html_visibility_style(html, chain[i], parent, root);
        if (styles[i] == NULL) {
            result = NSERROR_NOMEM;
            goto done;
        }
        parent = styles[i]->styles[CSS_PSEUDO_ELEMENT_NONE];
        if (root == NULL)
            root = parent;
        if (css_computed_display(parent, false) == CSS_DISPLAY_NONE)
            goto done;
        if (css_computed_content_visibility(parent) == CSS_CONTENT_VISIBILITY_HIDDEN &&
            html_content_visibility_applies(parent, false) && !html_until_found(chain[i]))
            goto done;
    }
    for (size_t i = 0; i < count; i++) {
        if (!html_until_found(chain[i]) ||
            !html_content_visibility_applies(styles[i]->styles[CSS_PSEUDO_ELEMENT_NONE], false))
            continue;
        fire_generic_dom_event(corestring_dom_beforematch, chain[i], true, false);
        /* The listener can itself remove the attribute or change the tree.
         * Reconstruct even when it has already performed the reveal. */
        *changed = true;
        /* Listeners may detach the target or remove/change the attribute. */
        dom_node *ancestor = dom_node_ref(target);
        bool still_ancestor = false;
        while (ancestor != NULL) {
            dom_node *next = NULL;
            still_ancestor |= ancestor == chain[i];
            bool in_document = ancestor == (dom_node *)html->document;
            if (dom_node_get_parent_node(ancestor, &next) != DOM_NO_ERR) {
                dom_node_unref(ancestor);
                result = NSERROR_DOM;
                goto done;
            }
            dom_node_unref(ancestor);
            ancestor = next;
            if (in_document) {
                dom_node_unref(ancestor);
                ancestor = NULL;
                if (still_ancestor && html_until_found(chain[i])) {
                    if (dom_element_remove_attribute(chain[i], corestring_dom_hidden) != DOM_NO_ERR) {
                        result = NSERROR_DOM;
                        goto done;
                    }
                    *changed = true;
                }
                break;
            }
        }
    }
done:
    for (size_t i = 0; i < count; i++) {
        if (styles != NULL && styles[i] != NULL)
            css_select_results_destroy(styles[i]);
        dom_node_unref(chain[i]);
    }
    free(styles);
    free(chain);
    if (*changed) {
        html_restyle_note_dom_change(html);
        html_request_restyle(html);
    }
    return result;
}

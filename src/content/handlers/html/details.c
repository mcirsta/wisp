/* SPDX-License-Identifier: GPL-2.0-only */
#include <wisp/utils/corestrings.h>
#include "content/handlers/html/details.h"
#include "content/handlers/html/restyle.h"

static bool html_tag_is(dom_node *node, dom_html_element_type wanted)
{
    dom_node_type type;
    dom_html_element_type tag;
    dom_string *ns = NULL;
    if (node == NULL || dom_node_get_node_type(node, &type) != DOM_NO_ERR || type != DOM_ELEMENT_NODE ||
        dom_html_element_get_tag_type(node, &tag) != DOM_NO_ERR || tag != wanted)
        return false;
    bool match = dom_node_get_namespace(node, &ns) == DOM_NO_ERR &&
        (ns == NULL || dom_string_isequal(ns, corestring_dom_html_namespace));
    dom_string_unref(ns);
    return match;
}

bool html_is_details(dom_node *node)
{
    return html_tag_is(node, DOM_HTML_ELEMENT_TYPE_DETAILS);
}

bool html_details_open(dom_node *node)
{
    bool open = false;
    dom_element_has_attribute(node, corestring_dom_open, &open);
    return open;
}

nserror html_details_set_open(dom_node *node, bool open)
{
    if (!open)
        return dom_element_remove_attribute(node, corestring_dom_open) == DOM_NO_ERR ? NSERROR_OK : NSERROR_DOM;
    dom_string *empty = NULL;
    if (dom_string_create((const uint8_t *)"", 0, &empty) != DOM_NO_ERR)
        return NSERROR_NOMEM;
    dom_exception err = dom_element_set_attribute(node, corestring_dom_open, empty);
    dom_string_unref(empty);
    return err == DOM_NO_ERR ? NSERROR_OK : NSERROR_DOM;
}

nserror html_details_summary(dom_node *node, dom_node **summary)
{
    *summary = NULL;
    dom_node *child = NULL;
    if (dom_node_get_first_child(node, &child) != DOM_NO_ERR)
        return NSERROR_DOM;
    while (child != NULL) {
        if (html_tag_is(child, DOM_HTML_ELEMENT_TYPE_SUMMARY)) {
            *summary = child;
            return NSERROR_OK;
        }
        dom_node *next = NULL;
        dom_exception err = dom_node_get_next_sibling(child, &next);
        dom_node_unref(child);
        if (err != DOM_NO_ERR)
            return NSERROR_DOM;
        child = next;
    }
    return NSERROR_OK;
}

bool html_details_hides_child(dom_node *details, dom_node *child)
{
    if (child == details || !html_is_details(details) || html_details_open(details))
        return false;
    dom_node *summary = NULL;
    if (html_details_summary(details, &summary) != NSERROR_OK)
        return false;
    bool hidden = child != summary;
    dom_node_unref(summary);
    return hidden;
}

nserror html_details_activate(html_content *html, dom_node *clicked)
{
    dom_node *node = dom_node_ref(clicked), *details = NULL;
    nserror result = NSERROR_OK;
    while (node != NULL) {
        dom_node *parent = NULL, *summary = NULL;
        dom_node_type type;
        if (dom_node_get_node_type(node, &type) != DOM_NO_ERR || type != DOM_ELEMENT_NODE)
            break;
        if (html_is_details(node)) {
            /* Only the anonymous legend targets the details element itself.
             * Clicking its ordinary descendants must not toggle it. */
            if (node == clicked && html_details_summary(node, &summary) == NSERROR_OK && summary == NULL)
                details = dom_node_ref(node);
            dom_node_unref(summary);
            break;
        }
        dom_html_element_type tag = DOM_HTML_ELEMENT_TYPE__UNKNOWN;
        dom_html_element_get_tag_type(node, &tag);
        bool href = false;
        if (tag == DOM_HTML_ELEMENT_TYPE_A)
            dom_element_has_attribute(node, corestring_dom_href, &href);
        if (href || tag == DOM_HTML_ELEMENT_TYPE_INPUT || tag == DOM_HTML_ELEMENT_TYPE_BUTTON ||
            tag == DOM_HTML_ELEMENT_TYPE_SELECT || tag == DOM_HTML_ELEMENT_TYPE_TEXTAREA ||
            tag == DOM_HTML_ELEMENT_TYPE_LABEL)
            break;
        if (dom_node_get_parent_node(node, &parent) != DOM_NO_ERR) {
            result = NSERROR_DOM;
            break;
        }
        if (html_tag_is(node, DOM_HTML_ELEMENT_TYPE_SUMMARY) && html_is_details(parent)) {
            result = html_details_summary(parent, &summary);
            if (result == NSERROR_OK && summary == node)
                details = dom_node_ref(parent);
            dom_node_unref(summary);
            dom_node_unref(parent);
            break;
        }
        dom_node_unref(node);
        node = parent;
    }
    dom_node_unref(node);
    if (details != NULL) {
        result = html_details_set_open(details, !html_details_open(details));
        if (result == NSERROR_OK) {
            html_restyle_note_dom_change(html);
            html_request_restyle(html);
        }
        dom_node_unref(details);
    }
    return result;
}

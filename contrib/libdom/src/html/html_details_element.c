/* SPDX-License-Identifier: MIT */
#include "html/html_details_element.h"
#include "html/html_document.h"

/* Inclusive tree-order walk, bounded by root (which may be detached). */
static dom_node_internal *details_next(dom_node_internal *node, dom_node_internal *root)
{
    if (node->first_child != NULL)
        return node->first_child;
    while (node != root && node->next == NULL)
        node = node->parent;
    return node == root ? NULL : node->next;
}

/* HTML's two exclusivity algorithms differ in which member loses open:
 * https://html.spec.whatwg.org/multipage/interactive-elements.html#the-details-element
 * Adding open closes the existing member; insertion or a name change closes
 * this element. Group names are exact nonempty strings within the same tree. */
static dom_exception details_ensure_exclusive(dom_node_internal *element, bool close_other)
{
    dom_document *doc = (dom_document *)element->owner;
    dom_string *open_name = doc->memoised[hds_open];
    dom_string *name_name = doc->memoised[hds_name];
    dom_string *name = NULL;
    bool open;
    dom_exception err = dom_element_has_attribute(element, open_name, &open);
    if (err != DOM_NO_ERR || !open)
        return err;
    err = dom_element_get_attribute(element, name_name, &name);
    if (err != DOM_NO_ERR || name == NULL)
        return err;
    if (dom_string_byte_length(name) == 0) {
        dom_string_unref(name);
        return DOM_NO_ERR;
    }

    dom_node_internal *root = element;
    while (root->parent != NULL)
        root = root->parent;
    for (dom_node_internal *node = root; node != NULL; node = details_next(node, root)) {
        if (node == element || !_dom_html_is_details(node))
            continue;
        dom_string *other_name = NULL;
        err = dom_element_get_attribute(node, name_name, &other_name);
        bool match = other_name != NULL && dom_string_isequal(name, other_name);
        dom_string_unref(other_name);
        if (err != DOM_NO_ERR)
            break;
        if (!match)
            continue;
        err = dom_element_has_attribute(node, open_name, &open);
        if (err != DOM_NO_ERR)
            break;
        if (open) {
            err = dom_element_remove_attribute(close_other ? node : element, open_name);
            break;
        }
    }
    dom_string_unref(name);
    return err;
}

dom_exception
_dom_html_details_attribute_changed(dom_element *element, dom_string *namespace, dom_string *name, bool added)
{
    dom_node_internal *node = (dom_node_internal *)element;
    if (namespace != NULL || !_dom_html_is_details(node))
        return DOM_NO_ERR;
    dom_document *doc = (dom_document *)node->owner;
    if (dom_string_isequal(name, doc->memoised[hds_name]))
        return details_ensure_exclusive(node, false);
    if (added && dom_string_isequal(name, doc->memoised[hds_open]))
        return details_ensure_exclusive(node, true);
    return DOM_NO_ERR;
}

dom_exception _dom_html_details_inserted(dom_node_internal *root)
{
    for (dom_node_internal *node = root; node != NULL; node = details_next(node, root)) {
        if (_dom_html_is_details(node)) {
            dom_exception err = details_ensure_exclusive(node, false);
            if (err != DOM_NO_ERR)
                return err;
        }
    }
    return DOM_NO_ERR;
}

/* SPDX-License-Identifier: MIT */
#ifndef dom_internal_html_details_element_h_
#define dom_internal_html_details_element_h_

#include "core/element.h"
#include "html/html_element.h"
#include <dom/dom.h>

/* Details currently uses the generic HTMLElement implementation. Check its
 * vtable before accessing HTML fields: core DOM nodes can also be XML nodes. */
static inline bool _dom_html_is_details(const dom_node_internal *node)
{
    return node != NULL && node->base.vtable == &_dom_html_element_vtable &&
        ((const dom_html_element *)node)->type == DOM_HTML_ELEMENT_TYPE_DETAILS &&
        (node->namespace == NULL || dom_string_isequal(node->namespace, dom_namespaces[DOM_NAMESPACE_HTML]));
}

/* Run after the attribute value/list or the inserted subtree is in place. */
dom_exception
_dom_html_details_attribute_changed(dom_element *element, dom_string *namespace, dom_string *name, bool added);
dom_exception _dom_html_details_inserted(dom_node_internal *root);

#endif

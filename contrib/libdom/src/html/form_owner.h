/* SPDX-License-Identifier: MIT */
#ifndef dom_internal_form_owner_h_
#define dom_internal_form_owner_h_

#include <dom/html/html_form_element.h>
#include "core/node.h"

/* Only parser associations need stored identity. Ordinary ownership is resolved
 * from the live tree/attributes. Ancestors are borrowed (and checked before use)
 * to avoid parent/child reference cycles; non-ancestors are retained. */
struct dom_html_form_association {
    dom_html_form_element *parser_form;
    bool ancestor;
};

dom_exception _dom_html_form_owner(dom_node_internal *control, dom_html_form_element **form);
dom_exception _dom_html_form_associate(dom_node_internal *control, dom_html_form_element *form);
void _dom_html_form_association_clear(struct dom_html_form_association *association);
void _dom_html_form_subtree_changed(dom_node_internal *root);
void _dom_html_form_attribute_changed(dom_node_internal *node, dom_string *namespace, dom_string *name);
void _dom_html_form_attribute_value_changed(struct dom_attr *attribute);

#endif

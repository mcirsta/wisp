/* SPDX-License-Identifier: MIT */
#ifndef dom_internal_html_template_element_h_
#define dom_internal_html_template_element_h_

#include <dom/html/html_template_element.h>
#include "core/doc_fragment.h"
#include "html/html_element.h"

struct dom_html_template_element {
    dom_html_element base;
    dom_document_fragment *content;
};

static inline bool _dom_html_is_template(const dom_node_internal *node)
{
    return _dom_html_node_type(node) == DOM_HTML_ELEMENT_TYPE_TEMPLATE;
}

dom_exception
_dom_html_template_element_create(struct dom_html_element_create_params *params, dom_html_template_element **result);
dom_exception _dom_html_template_clone_contents(
    dom_html_template_element *source, dom_html_template_element *copy, bool deep, dom_node_operation operation);

#endif

/* SPDX-License-Identifier: MIT */
#ifndef dom_html_template_element_h_
#define dom_html_template_element_h_

#include <dom/core/doc_fragment.h>
#include <dom/html/html_element.h>

typedef struct dom_html_template_element dom_html_template_element;

/* Return the same contents fragment with a reference owned by the caller. */
dom_exception
dom_html_template_element_get_content(dom_html_template_element *element, dom_document_fragment **content);

#endif

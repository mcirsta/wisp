/*
 * This file is part of libdom.
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 * Copyright 2009 Bo Yang <struggleyb.nku.com>
 */

#ifndef dom_html_form_element_h_
#define dom_html_form_element_h_

#include <dom/core/exceptions.h>
#include <dom/core/string.h>
#include <stddef.h>
#include <stdbool.h>

struct dom_html_collection;
struct dom_node;

typedef struct dom_html_form_element dom_html_form_element;

/* Owned, tree-ordered snapshot of the form's submittable built-in elements.
 * Unlike form.elements it includes image inputs and is not a live collection.
 * Disabled/unnamed controls are included; each consuming algorithm filters them.
 * No script is called while collecting. On error the result is empty. The caller
 * must finish the previous result before reusing it as an output parameter. */
typedef struct dom_html_form_control_list {
    struct dom_node **items;
    size_t length;
} dom_html_form_control_list;

dom_exception dom_html_form_element_get_submittable_elements(
    dom_html_form_element *form, dom_html_form_control_list *controls);
void dom_html_form_control_list_fini(dom_html_form_control_list *controls);

/* Shared exclusion for constraint validation and entry-list construction. */
bool dom_html_form_control_has_datalist_ancestor(struct dom_node *control);

dom_exception dom_html_form_element_get_elements(dom_html_form_element *ele, struct dom_html_collection **col);
dom_exception dom_html_form_element_get_length(dom_html_form_element *ele, uint32_t *len);

dom_exception dom_html_form_element_get_accept_charset(dom_html_form_element *ele, dom_string **accept_charset);

dom_exception dom_html_form_element_set_accept_charset(dom_html_form_element *ele, dom_string *accept_charset);

dom_exception dom_html_form_element_get_action(dom_html_form_element *ele, dom_string **action);

dom_exception dom_html_form_element_set_action(dom_html_form_element *ele, dom_string *action);

dom_exception dom_html_form_element_get_enctype(dom_html_form_element *ele, dom_string **enctype);

dom_exception dom_html_form_element_set_enctype(dom_html_form_element *ele, dom_string *enctype);

dom_exception dom_html_form_element_get_method(dom_html_form_element *ele, dom_string **method);

dom_exception dom_html_form_element_set_method(dom_html_form_element *ele, dom_string *method);

dom_exception dom_html_form_element_get_target(dom_html_form_element *ele, dom_string **target);

dom_exception dom_html_form_element_set_target(dom_html_form_element *ele, dom_string *target);

dom_exception dom_html_form_element_submit(dom_html_form_element *ele);
dom_exception dom_html_form_element_reset(dom_html_form_element *ele);

#endif

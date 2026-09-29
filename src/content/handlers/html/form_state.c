/* SPDX-License-Identifier: GPL-2.0-only */
#include <string.h>
#include <wisp/utils/corestrings.h>
#include "content/handlers/html/form_state.h"

static dom_html_element_type html_tag(dom_node *node)
{
    dom_node_type type;
    dom_string *ns = NULL;
    dom_html_element_type tag = DOM_HTML_ELEMENT_TYPE__UNKNOWN;
    if (dom_node_get_node_type(node, &type) != DOM_NO_ERR || type != DOM_ELEMENT_NODE ||
        dom_node_get_namespace(node, &ns) != DOM_NO_ERR)
        return tag;
    if (ns == NULL || dom_string_isequal(ns, corestring_dom_html_namespace))
        dom_html_element_get_tag_type(node, &tag);
    dom_string_unref(ns);
    return tag;
}

static bool input_type(dom_node *node, lwc_string *wanted)
{
    dom_string *type = NULL;
    if (html_tag(node) != DOM_HTML_ELEMENT_TYPE_INPUT ||
        dom_html_input_element_get_type((dom_html_input_element *)node, &type) != DOM_NO_ERR)
        return false;
    bool matches = type != NULL && dom_string_caseless_lwc_isequal(type, wanted);
    dom_string_unref(type);
    return matches;
}

static bool checkable(dom_node *node)
{
    return input_type(node, corestring_lwc_radio) || input_type(node, corestring_lwc_checkbox);
}

static bool labelable(dom_node *node)
{
    switch (html_tag(node)) {
    case DOM_HTML_ELEMENT_TYPE_INPUT:
        return !input_type(node, corestring_lwc_hidden);
    case DOM_HTML_ELEMENT_TYPE_BUTTON:
    case DOM_HTML_ELEMENT_TYPE_SELECT:
    case DOM_HTML_ELEMENT_TYPE_TEXTAREA:
        return true;
    default:
        return false;
    }
}

static dom_node *label_control(dom_node *label)
{
    dom_string *target = NULL;
    dom_node *control = NULL;
    if (dom_html_label_element_get_html_for((dom_html_label_element *)label, &target) != DOM_NO_ERR)
        return NULL;
    if (target != NULL) {
        dom_document *doc = NULL;
        if (dom_node_get_owner_document(label, &doc) == DOM_NO_ERR && doc != NULL) {
            dom_document_get_element_by_id(doc, target, &control);
            dom_node_unref(doc);
        }
        dom_string_unref(target);
    } else {
        dom_string *all;
        dom_nodelist *nodes;
        if (dom_string_create((const uint8_t *)"*", 1, &all) != DOM_NO_ERR)
            return NULL;
        dom_exception err = dom_element_get_elements_by_tag_name(label, all, &nodes);
        dom_string_unref(all);
        if (err != DOM_NO_ERR)
            return NULL;
        uint32_t count = 0;
        dom_nodelist_get_length(nodes, &count);
        for (uint32_t i = 0; i < count; i++) {
            dom_nodelist_item(nodes, i, &control);
            if (control != NULL && labelable(control))
                break;
            dom_node_unref(control);
            control = NULL;
        }
        dom_nodelist_unref(nodes);
    }
    if (control != NULL && !checkable(control)) {
        dom_node_unref(control);
        control = NULL;
    }
    return control;
}

dom_node *html_checkable_target(dom_node *clicked)
{
    dom_node *node = dom_node_ref(clicked);
    while (node != NULL) {
        dom_html_element_type tag = html_tag(node);
        if (checkable(node))
            return node;
        if (tag == DOM_HTML_ELEMENT_TYPE_LABEL) {
            dom_node *control = label_control(node);
            dom_node_unref(node);
            return control;
        }
        bool href = false;
        if (tag == DOM_HTML_ELEMENT_TYPE_A)
            dom_element_has_attribute(node, corestring_dom_href, &href);
        if (href || labelable(node)) {
            dom_node_unref(node);
            return NULL;
        }
        dom_node *parent = NULL;
        dom_node_get_parent_node(node, &parent);
        dom_node_unref(node);
        node = parent;
    }
    return NULL;
}

static bool input_disabled(dom_node *input)
{
    bool disabled = true;
    if (dom_html_input_element_get_disabled((dom_html_input_element *)input, &disabled) != DOM_NO_ERR || disabled)
        return true;
    dom_node *child = dom_node_ref(input), *parent = NULL;
    while (dom_node_get_parent_node(child, &parent) == DOM_NO_ERR && parent != NULL) {
        if (html_tag(parent) == DOM_HTML_ELEMENT_TYPE_FIELDSET) {
            dom_string *key = NULL;
            dom_string_create((const uint8_t *)"disabled", 8, &key);
            if (key == NULL) {
                dom_node_unref(child);
                dom_node_unref(parent);
                return true;
            }
            dom_element_has_attribute(parent, key, &disabled);
            dom_string_unref(key);
            if (disabled) {
                /* Descendants of the first legend are exempt. */
                dom_node *legend = NULL;
                dom_node_get_first_child(parent, &legend);
                while (legend != NULL && html_tag(legend) != DOM_HTML_ELEMENT_TYPE_LEGEND) {
                    dom_node *next = NULL;
                    dom_node_get_next_sibling(legend, &next);
                    dom_node_unref(legend);
                    legend = next;
                }
                disabled = legend != child;
                dom_node_unref(legend);
                if (disabled) {
                    dom_node_unref(child);
                    dom_node_unref(parent);
                    return true;
                }
            }
        }
        dom_node_unref(child);
        child = parent;
    }
    dom_node_unref(child);
    return false;
}

/* libdom's parser association predates the form= attribute. Resolve explicit
 * ownership first, including an invalid target (which means no owner). */
static dom_exception input_form(dom_html_input_element *input, dom_html_form_element **form)
{
    dom_string *key = NULL, *id = NULL;
    dom_exception err = dom_string_create((const uint8_t *)"form", 4, &key);
    *form = NULL;
    if (err != DOM_NO_ERR)
        return err;
    err = dom_element_get_attribute(input, key, &id);
    dom_string_unref(key);
    if (err != DOM_NO_ERR)
        return err;
    if (id == NULL)
        return dom_html_input_element_get_form(input, form);
    dom_document *doc = NULL;
    dom_node *owner = NULL;
    err = dom_node_get_owner_document(input, &doc);
    if (err == DOM_NO_ERR)
        err = dom_document_get_element_by_id(doc, id, &owner);
    if (owner != NULL && html_tag(owner) == DOM_HTML_ELEMENT_TYPE_FORM)
        *form = (dom_html_form_element *)owner;
    else
        dom_node_unref(owner);
    dom_node_unref(doc);
    dom_string_unref(id);
    return err;
}

bool html_checkable_activate(dom_node *input)
{
    bool checked = false;
    if (!checkable(input) || input_disabled(input) ||
        dom_html_input_element_get_checked((dom_html_input_element *)input, &checked) != DOM_NO_ERR)
        return false;
    if (input_type(input, corestring_lwc_checkbox))
        return dom_html_input_element_set_checked((dom_html_input_element *)input, !checked) == DOM_NO_ERR;
    if (checked)
        return false;

    dom_string *name = NULL;
    dom_html_form_element *form = NULL;
    dom_document *doc = NULL;
    dom_nodelist *nodes = NULL;
    bool changed = false;
    if (dom_html_input_element_get_name((dom_html_input_element *)input, &name) != DOM_NO_ERR ||
        input_form((dom_html_input_element *)input, &form) != DOM_NO_ERR ||
        dom_node_get_owner_document(input, &doc) != DOM_NO_ERR)
        goto out;
    if (name != NULL && dom_string_byte_length(name) != 0) {
        uint32_t count;
        if (dom_document_get_elements_by_tag_name(doc, corestring_dom_input, &nodes) != DOM_NO_ERR ||
            dom_nodelist_get_length(nodes, &count) != DOM_NO_ERR)
            goto out;
        for (uint32_t i = 0; i < count; i++) {
            dom_node *other = NULL;
            dom_string *other_name = NULL;
            dom_html_form_element *other_form = NULL;
            if (dom_nodelist_item(nodes, i, &other) != DOM_NO_ERR)
                goto out;
            if (other != input && input_type(other, corestring_lwc_radio) &&
                dom_html_input_element_get_name((dom_html_input_element *)other, &other_name) == DOM_NO_ERR &&
                other_name != NULL && dom_string_isequal(name, other_name) &&
                input_form((dom_html_input_element *)other, &other_form) == DOM_NO_ERR && other_form == form)
                dom_html_input_element_set_checked((dom_html_input_element *)other, false);
            dom_node_unref(other_form);
            dom_string_unref(other_name);
            dom_node_unref(other);
        }
    }
    changed = dom_html_input_element_set_checked((dom_html_input_element *)input, true) == DOM_NO_ERR;
out:
    dom_nodelist_unref(nodes);
    dom_node_unref(doc);
    dom_node_unref(form);
    dom_string_unref(name);
    return changed;
}

/* SPDX-License-Identifier: GPL-2.0-only */
#include <string.h>
#include <utf8proc.h>
#include <wisp/utils/corestrings.h>
#include "utils/libdom.h"
#include "content/handlers/html/direction.h"

/* https://html.spec.whatwg.org/multipage/dom.html#the-dir-attribute */
enum direction { DIR_UNDEFINED, DIR_LTR, DIR_RTL, DIR_AUTO };

static bool keyword(dom_string *value, const char *word)
{
    if (value == NULL || dom_string_byte_length(value) != strlen(word))
        return false;
    const char *data = dom_string_data(value);
    for (size_t i = 0; word[i] != '\0'; i++) {
        unsigned char c = data[i];
        if (c >= 'A' && c <= 'Z')
            c += 'a' - 'A';
        if (c != (unsigned char)word[i])
            return false;
    }
    return true;
}

/* Foreign elements inherit direction; their literal dir attributes have no
 * HTML semantics. Null namespaces are also used by libdom's HTML documents. */
static dom_exception direction_attributes(dom_node *node, dom_html_element_type *tag, enum direction *state)
{
    dom_string *ns = NULL, *value = NULL;
    *tag = DOM_HTML_ELEMENT_TYPE__UNKNOWN;
    *state = DIR_UNDEFINED;
    dom_exception err = dom_node_get_namespace(node, &ns);
    if (err != DOM_NO_ERR)
        return err;
    bool html = ns == NULL || dom_string_isequal(ns, corestring_dom_html_namespace);
    dom_string_unref(ns);
    if (!html)
        return DOM_NO_ERR;
    err = dom_html_element_get_tag_type(node, tag);
    if (err != DOM_NO_ERR)
        return err;
    err = dom_element_get_attribute(node, corestring_dom_dir, &value);
    if (err == DOM_NO_ERR) {
        if (keyword(value, "ltr"))
            *state = DIR_LTR;
        else if (keyword(value, "rtl"))
            *state = DIR_RTL;
        else if (keyword(value, "auto"))
            *state = DIR_AUTO;
    }
    dom_string_unref(value);
    return err;
}

static enum direction first_strong(dom_string *text)
{
    if (text == NULL)
        return DIR_UNDEFINED;
    const utf8proc_uint8_t *data = (const utf8proc_uint8_t *)dom_string_data(text);
    size_t remaining = dom_string_byte_length(text);
    while (remaining != 0) {
        utf8proc_int32_t cp;
        utf8proc_ssize_t length = utf8proc_iterate(data, remaining, &cp);
        if (length > 0) {
            utf8proc_bidi_class_t bidi = utf8proc_get_property(cp)->bidi_class;
            if (bidi == UTF8PROC_BIDI_CLASS_L)
                return DIR_LTR;
            if (bidi == UTF8PROC_BIDI_CLASS_R || bidi == UTF8PROC_BIDI_CLASS_AL)
                return DIR_RTL;
        } else {
            /* Invalid UTF-8 cannot supply a strong character. */
            length = 1;
        }
        data += length;
        remaining -= length;
    }
    return DIR_UNDEFINED;
}

/* Advance an owned reference in tree order, without recursing or visiting
 * siblings of root. Skipping excluded subtrees also bounds work on dir islands. */
static dom_exception next_text_candidate(dom_node *root, dom_node **node, bool descend)
{
    dom_node *next = NULL;
    dom_exception err = DOM_NO_ERR;
    if (descend)
        err = dom_node_get_first_child(*node, &next);
    while (err == DOM_NO_ERR && next == NULL && *node != root) {
        err = dom_node_get_next_sibling(*node, &next);
        if (err != DOM_NO_ERR || next != NULL)
            break;
        err = dom_node_get_parent_node(*node, &next);
        dom_node_unref(*node);
        *node = next;
        next = NULL;
    }
    dom_node_unref(*node);
    *node = next;
    return err;
}

static dom_exception contained_direction(dom_node *root, bool *rtl)
{
    dom_node *node = dom_node_ref(root);
    dom_exception err = next_text_candidate(root, &node, true);
    *rtl = false;
    while (err == DOM_NO_ERR && node != NULL) {
        dom_node_type type;
        bool descend = true;
        err = dom_node_get_node_type(node, &type);
        if (err != DOM_NO_ERR)
            break;
        if (type == DOM_ELEMENT_NODE) {
            dom_html_element_type tag;
            enum direction state;
            err = direction_attributes(node, &tag, &state);
            if (err != DOM_NO_ERR)
                break;
            descend = state == DIR_UNDEFINED && tag != DOM_HTML_ELEMENT_TYPE_BDI &&
                tag != DOM_HTML_ELEMENT_TYPE_SCRIPT && tag != DOM_HTML_ELEMENT_TYPE_STYLE &&
                tag != DOM_HTML_ELEMENT_TYPE_TEXTAREA;
        } else if (type == DOM_TEXT_NODE) {
            dom_string *text = NULL;
            err = dom_node_get_node_value(node, &text);
            enum direction found = first_strong(text);
            dom_string_unref(text);
            if (err != DOM_NO_ERR || found != DIR_UNDEFINED) {
                *rtl = found == DIR_RTL;
                break;
            }
        }
        err = next_text_candidate(root, &node, descend);
    }
    dom_node_unref(node);
    return err;
}

static bool input_uses_value(dom_string *type)
{
    /* All remaining valid states use the value for auto direction. Missing or
     * unknown types default to Text, so exclude only the other known states. */
    static const char *const other[] = {"date", "month", "week", "time", "datetime-local", "number", "range", "color",
        "checkbox", "radio", "file", "image"};
    for (size_t i = 0; i < sizeof(other) / sizeof(other[0]); i++) {
        if (keyword(type, other[i]))
            return false;
    }
    return true;
}

static dom_exception auto_direction(dom_node *node, dom_html_element_type tag, dom_string *input_type, bool *rtl)
{
    dom_string *value = NULL;
    dom_exception err;
    if (tag == DOM_HTML_ELEMENT_TYPE_TEXTAREA)
        err = dom_html_text_area_element_get_value((dom_html_text_area_element *)node, &value);
    else if (tag == DOM_HTML_ELEMENT_TYPE_INPUT && input_uses_value(input_type))
        err = dom_html_input_element_get_value((dom_html_input_element *)node, &value);
    else
        return contained_direction(node, rtl);
    /* Empty or entirely neutral auto content defaults to LTR, even under RTL. */
    *rtl = first_strong(value) == DIR_RTL;
    dom_string_unref(value);
    return err;
}

dom_exception html_element_direction(dom_node *element, bool *rtl)
{
    dom_node *node = dom_node_ref(element);
    dom_exception err = DOM_NO_ERR;
    *rtl = false;
    while (node != NULL) {
        dom_node_type type;
        dom_html_element_type tag;
        enum direction state;
        dom_string *input_type = NULL;
        err = dom_node_get_node_type(node, &type);
        if (err != DOM_NO_ERR || type != DOM_ELEMENT_NODE)
            break;
        err = direction_attributes(node, &tag, &state);
        if (err != DOM_NO_ERR)
            break;
        if (state == DIR_LTR || state == DIR_RTL) {
            *rtl = state == DIR_RTL;
            break;
        }
        if (tag == DOM_HTML_ELEMENT_TYPE_INPUT) {
            err = dom_html_input_element_get_type((dom_html_input_element *)node, &input_type);
            if (err != DOM_NO_ERR)
                break;
        }
        bool automatic = state == DIR_AUTO || tag == DOM_HTML_ELEMENT_TYPE_BDI;
        bool telephone = tag == DOM_HTML_ELEMENT_TYPE_INPUT && keyword(input_type, "tel");
        if (automatic)
            err = auto_direction(node, tag, input_type, rtl);
        dom_string_unref(input_type);
        if (automatic || telephone)
            break;
        dom_node *parent = NULL;
        err = dom_node_get_parent_node(node, &parent);
        dom_node_unref(node);
        node = parent;
        if (err != DOM_NO_ERR)
            break;
    }
    dom_node_unref(node);
    return err;
}

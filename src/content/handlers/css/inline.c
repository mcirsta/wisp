/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "inline.h"
#include "select.h"
#include <stdlib.h>
#include <wisp/utils/corestrings.h>

struct nscss_inline_style {
    css_declarations *declarations;
    dom_attr *attribute; /* Retained identity prevents address reuse. Not an element reference. */
    uint64_t revision;
};

static dom_exception css_exception(css_error error)
{
    return error == CSS_OK ? DOM_NO_ERR : error == CSS_NOMEM ? DOM_NO_MEM_ERR : DOM_INVALID_STATE_ERR;
}

static void style_destroy(struct nscss_inline_style *style)
{
    css_declarations_destroy(style->declarations);
    dom_node_unref(style->attribute);
    free(style);
}

static void style_user_data(dom_node_operation operation, dom_string *key, void *data, dom_node *src, dom_node *dst)
{
    (void)key;
    (void)src;
    (void)dst;
    /* Clone/import copy the attribute, not its pending CSSOM state. Adoption
     * retains this node and state; compilation uses its new document context. */
    if (operation == DOM_NODE_DELETED)
        style_destroy(data);
}

static dom_exception
style_refresh(dom_node *element, struct nscss_inline_style *style, const css_stylesheet_params *params)
{
    dom_attr *attribute = NULL;
    dom_exception error = dom_element_get_attribute_node_ns(element, NULL, corestring_dom_style, &attribute);
    if (error != DOM_NO_ERR)
        return error;
    uint64_t revision = attribute != NULL ? dom_attr_get_revision(attribute) : 0;
    if (style->declarations != NULL && attribute == style->attribute && revision == style->revision) {
        dom_node_unref(attribute);
        return DOM_NO_ERR;
    }
    dom_string *text = NULL;
    if (attribute != NULL)
        error = dom_attr_get_value(attribute, &text);
    css_declarations *parsed = NULL;
    if (error == DOM_NO_ERR)
        error = css_exception(
            css_declarations_parse(params, (const uint8_t *)(text != NULL ? dom_string_data(text) : ""),
                text != NULL ? dom_string_byte_length(text) : 0, &parsed));
    dom_string_unref(text);
    if (error != DOM_NO_ERR) {
        dom_node_unref(attribute);
        return error;
    }
    css_declarations_destroy(style->declarations);
    dom_node_unref(style->attribute);
    style->declarations = parsed;
    style->attribute = attribute;
    style->revision = revision;
    return DOM_NO_ERR;
}

dom_exception
nscss_inline_style_get(dom_node *element, const css_stylesheet_params *params, struct nscss_inline_style **out)
{
    *out = NULL;
    struct nscss_inline_style *style = NULL;
    dom_exception error = dom_node_get_user_data(element, corestring_dom___ns_key_inline_style, (void **)&style);
    if (error != DOM_NO_ERR)
        return error;
    bool fresh = style == NULL;
    if (fresh) {
        style = calloc(1, sizeof(*style));
        if (style == NULL)
            return DOM_NO_MEM_ERR;
    }
    error = style_refresh(element, style, params);
    if (error == DOM_NO_ERR && fresh) {
        void *old;
        error = dom_node_set_user_data(element, corestring_dom___ns_key_inline_style, style, style_user_data, &old);
    }
    if (error != DOM_NO_ERR) {
        if (fresh)
            style_destroy(style);
        return error;
    }
    *out = style;
    return DOM_NO_ERR;
}

const css_declarations *nscss_inline_style_declarations(const struct nscss_inline_style *style)
{
    return style->declarations;
}

dom_exception nscss_inline_style_commit(
    dom_node *element, struct nscss_inline_style *style, css_declarations **candidate, bool *changed)
{
    *changed = false;
    lwc_string *serialized;
    css_error css_error = css_declarations_serialize(*candidate, &serialized);
    if (css_error != CSS_OK)
        return css_exception(css_error);
    dom_string *text;
    dom_exception error = dom_string_create(
        (const uint8_t *)lwc_string_data(serialized), lwc_string_length(serialized), &text);
    lwc_string_unref(serialized);
    if (error != DOM_NO_ERR)
        return error;
    error = dom_element_set_attribute(element, corestring_dom_style, text);
    /* A deprecated subtree event can fail after publication. Observe the actual
     * write so selection and the DOM agree even when reporting that error. */
    dom_attr *attribute = NULL;
    dom_exception observed = dom_element_get_attribute_node_ns(element, NULL, corestring_dom_style, &attribute);
    uint64_t revision = attribute != NULL ? dom_attr_get_revision(attribute) : 0;
    *changed = observed == DOM_NO_ERR && (attribute != style->attribute || revision != style->revision);
    dom_string *current = NULL;
    if (*changed && attribute != NULL)
        observed = dom_attr_get_value(attribute, &current);
    if (*changed && observed == DOM_NO_ERR && current != NULL && dom_string_isequal(current, text)) {
        css_declarations_destroy(style->declarations);
        dom_node_unref(style->attribute);
        style->attribute = attribute;
        attribute = NULL;
        style->revision = revision;
        style->declarations = *candidate;
        *candidate = NULL;
    }
    dom_node_unref(attribute);
    dom_string_unref(current);
    dom_string_unref(text);
    return error != DOM_NO_ERR ? error : observed;
}

css_stylesheet *nscss_inline_style_for_element(dom_node *element, const char *url, bool quirks, bool *present)
{
    *present = true; /* Errors are distinguished from a missing attribute. */
    struct nscss_inline_style *style = NULL;
    if (dom_node_get_user_data(element, corestring_dom___ns_key_inline_style, (void **)&style) != DOM_NO_ERR)
        return NULL;
    if (style != NULL) {
        css_stylesheet_params params;
        nscss_inline_params(&params, url, quirks);
        css_stylesheet *compiled = NULL;
        if (style_refresh(element, style, &params) == DOM_NO_ERR)
            css_declarations_compile(style->declarations, &params, &compiled);
        return compiled;
    }
    dom_string *text = NULL;
    if (dom_element_get_attribute(element, corestring_dom_style, &text) != DOM_NO_ERR)
        return NULL;
    if (text == NULL) {
        *present = false;
        return NULL;
    }
    css_stylesheet *sheet = nscss_create_inline_style(
        (const uint8_t *)dom_string_data(text), dom_string_byte_length(text), "UTF-8", url, quirks);
    dom_string_unref(text);
    return sheet;
}

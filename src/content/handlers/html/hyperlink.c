/* SPDX-License-Identifier: GPL-2.0-only */
/* URL normalization originates in NetSurf's box_construct.c:
 * Copyright 2005 James Bursa <bursa@users.sourceforge.net>
 * Copyright 2003 Phil Mellor <monkeyson@users.sourceforge.net>
 * Copyright 2005 John M Bell <jmb202@ecs.soton.ac.uk>
 * Copyright 2006 Richard Wilson <info@tinct.net>
 * Copyright 2008 Michael Drake <tlsa@netsurf-browser.org>
 */
#include <stdlib.h>
#include <string.h>
#include <wisp/content/handlers/html/private.h>
#include <wisp/utils/corestrings.h>
#include <wisp/utils/nsurl.h>
#include <wisp/utils/ascii.h>
#include "content/handlers/html/hyperlink.h"

nserror html_link_url(const dom_string *value, nsurl *base, nsurl **result)
{
    const char *rel = dom_string_data(value);
    size_t end = dom_string_byte_length(value), start = 0, used = 0;
    *result = NULL;
    if (end > (SIZE_MAX - 1) / 3)
        return NSERROR_NOMEM;
    char *text = malloc(3 * end + 1);
    if (text == NULL)
        return NSERROR_NOMEM;
    /* Keep URL normalization shared with box construction and image maps. */
    while (start < end && (unsigned char)rel[start] <= 0x20)
        start++;
    while (end > start && (unsigned char)rel[end - 1] <= 0x20)
        end--;
    for (size_t i = start; i < end; i++) {
        /* URL preprocessing removes only ASCII tabs and newlines within a
         * URL. Other C0 controls must be escaped, not silently discarded. */
        if (rel[i] == '\t' || rel[i] == '\n' || rel[i] == '\r')
            continue;
        unsigned char c = (unsigned char)rel[i];
        if (c <= 0x20 || c == 0x7f) {
            static const char hex[] = "0123456789ABCDEF";
            text[used++] = '%';
            text[used++] = hex[c >> 4];
            text[used++] = hex[c & 15];
        } else if (i + 2 < end && c == 0xed && ((unsigned char)rel[i + 1] & 0xe0) == 0xa0 &&
            ((unsigned char)rel[i + 2] & 0xc0) == 0x80) {
            /* Native DOMString attributes may still contain isolated UTF-16
             * surrogates from setAttribute; URL input is a scalar value string. */
            memcpy(text + used, "\xef\xbf\xbd", 3);
            used += 3;
            i += 2;
        } else {
            text[used++] = rel[i];
        }
    }
    text[used] = 0;
    nserror error = nsurl_join(base, text, result);
    free(text);
    return error;
}

static dom_html_element_type html_tag(dom_node *node)
{
    dom_node_type type;
    dom_html_element_type tag = DOM_HTML_ELEMENT_TYPE__UNKNOWN;
    dom_string *ns = NULL;
    if (dom_node_get_node_type(node, &type) != DOM_NO_ERR || type != DOM_ELEMENT_NODE ||
        dom_node_get_namespace(node, &ns) != DOM_NO_ERR)
        return tag;
    if (ns == NULL || dom_string_isequal(ns, corestring_dom_html_namespace))
        dom_html_element_get_tag_type(node, &tag);
    dom_string_unref(ns);
    return tag;
}

dom_node *html_hyperlink_target(dom_node *clicked)
{
    dom_node *node = dom_node_ref(clicked);
    while (node != NULL) {
        switch (html_tag(node)) {
        case DOM_HTML_ELEMENT_TYPE_A:
        case DOM_HTML_ELEMENT_TYPE_AREA:
            /* Activation behavior exists even without href. A listener may add
             * it; conversely, removing it later suppresses this activation. */
            return node;
        case DOM_HTML_ELEMENT_TYPE_INPUT:
        case DOM_HTML_ELEMENT_TYPE_BUTTON:
        case DOM_HTML_ELEMENT_TYPE_LABEL:
        case DOM_HTML_ELEMENT_TYPE_SELECT:
        case DOM_HTML_ELEMENT_TYPE_TEXTAREA:
        case DOM_HTML_ELEMENT_TYPE_SUMMARY:
            dom_node_unref(node);
            return NULL;
        default:
            break;
        }
        dom_node *parent = NULL;
        dom_node_get_parent_node(node, &parent);
        dom_node_unref(node);
        node = parent;
    }
    return NULL;
}

static bool link_in_document(dom_node *link, dom_document *document)
{
    dom_document *owner = NULL;
    if (dom_node_get_owner_document(link, &owner) != DOM_NO_ERR)
        return false;
    bool same = owner == document;
    dom_node_unref(owner);
    if (!same)
        return false;
    /* HTML's "cannot navigate" permits a detached anchor, but not an area. */
    if (html_tag(link) == DOM_HTML_ELEMENT_TYPE_A)
        return true;
    dom_node *root = dom_node_ref(link), *parent = NULL;
    while (dom_node_get_parent_node(root, &parent) == DOM_NO_ERR && parent != NULL) {
        dom_node_unref(root);
        root = parent;
    }
    same = root == (dom_node *)document;
    dom_node_unref(root);
    return same;
}

nserror html_element_target(dom_document *document, dom_node *element, dom_string *override, char **target)
{
    dom_string *value = dom_string_ref(override);
    dom_exception error = value != NULL || element == NULL
        ? DOM_NO_ERR
        : dom_element_get_attribute(element, corestring_dom_target, &value);
    if (error != DOM_NO_ERR)
        return NSERROR_DOM;
    if (value == NULL) {
        dom_nodelist *bases = NULL;
        error = dom_document_get_elements_by_tag_name(document, corestring_dom_base, &bases);
        if (error != DOM_NO_ERR)
            return NSERROR_DOM;
        uint32_t count = 0;
        error = dom_nodelist_get_length(bases, &count);
        for (uint32_t i = 0; error == DOM_NO_ERR && value == NULL && i < count; i++) {
            dom_node *base = NULL;
            error = dom_nodelist_item(bases, i, &base);
            if (error == DOM_NO_ERR && base != NULL && html_tag(base) == DOM_HTML_ELEMENT_TYPE_BASE)
                error = dom_element_get_attribute(base, corestring_dom_target, &value);
            dom_node_unref(base);
        }
        dom_nodelist_unref(bases);
    }
    if (error != DOM_NO_ERR) {
        dom_string_unref(value);
        return NSERROR_DOM;
    }
    *target = value == NULL || dom_string_byte_length(value) == 0
        ? strdup("_self")
        : strndup(dom_string_data(value), dom_string_byte_length(value));
    dom_string_unref(value);
    if (*target == NULL)
        return NSERROR_NOMEM;
    /* HTML "get an element's target". Pass an explicit default so the window
     * helper cannot substitute a stale parser-time base target. */
    if (strpbrk(*target, "\t\n\r") != NULL && strchr(*target, '<') != NULL) {
        free(*target);
        *target = strdup("_blank");
        if (*target == NULL)
            return NSERROR_NOMEM;
    }
    return NSERROR_OK;
}

nserror html_hyperlink_resolve(html_content *html, dom_node *link, nsurl **url, char **target)
{
    *url = NULL;
    *target = NULL;
    if (!link_in_document(link, html->document))
        return NSERROR_OK;
    dom_string *href = NULL;
    if (dom_element_get_attribute(link, corestring_dom_href, &href) != DOM_NO_ERR)
        return NSERROR_DOM;
    if (href == NULL)
        return NSERROR_OK;
    nserror error = html_link_url(href, html->base_url, url);
    dom_string_unref(href);
    if (error != NSERROR_OK)
        return error == NSERROR_NOMEM ? error : NSERROR_OK; /* Invalid URL: no activation. */
    error = html_element_target(html->document, link, NULL, target);
    if (error != NSERROR_OK) {
        nsurl_unref(*url);
        *url = NULL;
    }
    return error;
}

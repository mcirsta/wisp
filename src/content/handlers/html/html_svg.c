/*
 * Copyright 2024 Marius
 *
 * This file is part of NeoSurf, http://www.neosurf-browser.org/
 *
 * NeoSurf is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * NeoSurf is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * \file
 * Inline SVG support for HTML content - implementation.
 */

#include <stdlib.h>
#include <string.h>

#include <dom/dom.h>
#include <svgtiny.h>

#include <neosurf/content/handlers/html/private.h>
#include <neosurf/utils/corestrings.h>
#include <neosurf/utils/errors.h>
#include <neosurf/utils/log.h>

#include "content/handlers/html/html_svg.h"

#define SVG_SYMBOL_INITIAL_CAPACITY 16

/**
 * Add a symbol to the registry.
 */
static nserror svg_registry_add(struct svg_symbol_registry *reg, const char *id, dom_element *element)
{
    if (reg->count >= reg->capacity) {
        unsigned int new_cap = reg->capacity * 2;
        struct svg_symbol_entry *new_entries;

        new_entries = realloc(reg->entries, new_cap * sizeof(struct svg_symbol_entry));
        if (new_entries == NULL) {
            return NSERROR_NOMEM;
        }
        reg->entries = new_entries;
        reg->capacity = new_cap;
    }

    reg->entries[reg->count].id = strdup(id);
    if (reg->entries[reg->count].id == NULL) {
        return NSERROR_NOMEM;
    }

    reg->entries[reg->count].element = element;
    dom_node_ref(element);
    reg->count++;

    NSLOG(neosurf, DEBUG, "SVG: Added symbol '%s' to registry (count=%u)", id, reg->count);

    return NSERROR_OK;
}

/**
 * Find a symbol in the registry by ID.
 */
static dom_element *svg_registry_find(struct svg_symbol_registry *reg, const char *id)
{
    for (unsigned int i = 0; i < reg->count; i++) {
        if (strcmp(reg->entries[i].id, id) == 0) {
            return reg->entries[i].element;
        }
    }
    return NULL;
}

/**
 * Recursively collect symbols from an element and its descendants.
 */
static nserror collect_symbols_from_element(dom_element *element, struct svg_symbol_registry *reg)
{
    dom_exception exc;
    dom_string *tag_name = NULL;
    dom_string *id_attr = NULL;
    dom_nodelist *children = NULL;
    uint32_t child_count, i;
    nserror err = NSERROR_OK;

    /* Get tag name */
    exc = dom_element_get_tag_name(element, &tag_name);
    if (exc != DOM_NO_ERR || tag_name == NULL) {
        return NSERROR_OK; /* Skip this element */
    }

    /* Check if this is a symbol or defs element */
    if (dom_string_caseless_isequal(tag_name, corestring_dom_symbol)) {
        /* Get the id attribute */
        exc = dom_element_get_attribute(element, corestring_dom_id, &id_attr);
        if (exc == DOM_NO_ERR && id_attr != NULL) {
            const char *id_str = dom_string_data(id_attr);
            if (id_str != NULL && id_str[0] != '\0') {
                err = svg_registry_add(reg, id_str, element);
            }
            dom_string_unref(id_attr);
        }
    }

    dom_string_unref(tag_name);

    /* Recurse into children */
    exc = dom_node_get_child_nodes((dom_node *)element, &children);
    if (exc != DOM_NO_ERR || children == NULL) {
        return err;
    }

    exc = dom_nodelist_get_length(children, &child_count);
    if (exc != DOM_NO_ERR) {
        dom_nodelist_unref(children);
        return err;
    }

    for (i = 0; i < child_count; i++) {
        dom_node *child = NULL;
        dom_node_type type;

        exc = dom_nodelist_item(children, i, &child);
        if (exc != DOM_NO_ERR || child == NULL) {
            continue;
        }

        exc = dom_node_get_node_type(child, &type);
        if (exc == DOM_NO_ERR && type == DOM_ELEMENT_NODE) {
            err = collect_symbols_from_element((dom_element *)child, reg);
        }

        dom_node_unref(child);

        if (err != NSERROR_OK) {
            break;
        }
    }

    dom_nodelist_unref(children);
    return err;
}

/**
 * Collect all SVG symbols from the HTML document.
 */
nserror html_collect_svg_symbols(struct html_content *c, dom_document *doc)
{
    dom_exception exc;
    dom_nodelist *svg_elements = NULL;
    uint32_t svg_count, i;
    nserror err = NSERROR_OK;
    struct svg_symbol_registry *reg;

    /* Allocate registry */
    reg = calloc(1, sizeof(struct svg_symbol_registry));
    if (reg == NULL) {
        return NSERROR_NOMEM;
    }

    reg->entries = calloc(SVG_SYMBOL_INITIAL_CAPACITY, sizeof(struct svg_symbol_entry));
    if (reg->entries == NULL) {
        free(reg);
        return NSERROR_NOMEM;
    }
    reg->capacity = SVG_SYMBOL_INITIAL_CAPACITY;
    reg->count = 0;

    /* Find all SVG elements in the document */
    exc = dom_document_get_elements_by_tag_name(doc, corestring_dom_svg, &svg_elements);
    if (exc != DOM_NO_ERR || svg_elements == NULL) {
        /* No SVG elements - that's fine */
        c->svg_symbols = reg;
        return NSERROR_OK;
    }

    exc = dom_nodelist_get_length(svg_elements, &svg_count);
    if (exc != DOM_NO_ERR) {
        dom_nodelist_unref(svg_elements);
        c->svg_symbols = reg;
        return NSERROR_OK;
    }

    NSLOG(neosurf, DEBUG, "SVG: Found %u SVG elements in document", svg_count);

    /* Collect symbols from each SVG element */
    for (i = 0; i < svg_count; i++) {
        dom_node *svg_node = NULL;

        exc = dom_nodelist_item(svg_elements, i, &svg_node);
        if (exc != DOM_NO_ERR || svg_node == NULL) {
            continue;
        }

        err = collect_symbols_from_element((dom_element *)svg_node, reg);
        dom_node_unref(svg_node);

        if (err != NSERROR_OK) {
            break;
        }
    }

    dom_nodelist_unref(svg_elements);

    c->svg_symbols = reg;

    NSLOG(neosurf, INFO, "SVG: Collected %u symbols from document", reg->count);

    return err;
}

/**
 * Resolve all <use href="#id"> references in the document.
 */
nserror html_resolve_svg_use_refs(struct html_content *c, dom_document *doc)
{
    dom_exception exc;
    dom_nodelist *use_elements = NULL;
    dom_string *use_tag = NULL;
    uint32_t use_count, i;
    struct svg_symbol_registry *reg;

    if (c->svg_symbols == NULL) {
        return NSERROR_OK;
    }

    reg = c->svg_symbols;

    if (reg->count == 0) {
        NSLOG(neosurf, DEBUG, "SVG: No symbols to resolve");
        return NSERROR_OK;
    }

    /* Create "use" string for lookup */
    exc = dom_string_create((const uint8_t *)"use", 3, &use_tag);
    if (exc != DOM_NO_ERR) {
        return NSERROR_DOM;
    }

    /* Find all use elements */
    exc = dom_document_get_elements_by_tag_name(doc, use_tag, &use_elements);
    dom_string_unref(use_tag);

    if (exc != DOM_NO_ERR || use_elements == NULL) {
        return NSERROR_OK;
    }

    exc = dom_nodelist_get_length(use_elements, &use_count);
    if (exc != DOM_NO_ERR) {
        dom_nodelist_unref(use_elements);
        return NSERROR_OK;
    }

    NSLOG(neosurf, DEBUG, "SVG: Found %u <use> elements to resolve", use_count);

    for (i = 0; i < use_count; i++) {
        dom_node *use_node = NULL;
        dom_element *use_elem;
        dom_string *href_attr = NULL;
        dom_string *href_name = NULL;
        const char *href_str;
        dom_element *symbol;

        exc = dom_nodelist_item(use_elements, i, &use_node);
        if (exc != DOM_NO_ERR || use_node == NULL) {
            continue;
        }

        use_elem = (dom_element *)use_node;

        /* Try href attribute first, then xlink:href */
        exc = dom_string_create((const uint8_t *)"href", 4, &href_name);
        if (exc == DOM_NO_ERR) {
            exc = dom_element_get_attribute(use_elem, href_name, &href_attr);
            dom_string_unref(href_name);
        }

        if (href_attr == NULL) {
            /* Try xlink:href */
            exc = dom_string_create((const uint8_t *)"xlink:href", 10, &href_name);
            if (exc == DOM_NO_ERR) {
                exc = dom_element_get_attribute(use_elem, href_name, &href_attr);
                dom_string_unref(href_name);
            }
        }

        if (href_attr == NULL) {
            dom_node_unref(use_node);
            continue;
        }

        href_str = dom_string_data(href_attr);

        /* Check if it's a local reference (starts with #) */
        if (href_str != NULL && href_str[0] == '#') {
            const char *id = href_str + 1; /* Skip the # */

            symbol = svg_registry_find(reg, id);
            if (symbol != NULL) {
                dom_node *cloned = NULL;

                NSLOG(neosurf, DEBUG, "SVG: Resolving <use href=\"#%s\">", id);

                /* Clone the symbol's children and append to use element */
                exc = dom_node_clone_node(symbol, true, &cloned);
                if (exc == DOM_NO_ERR && cloned != NULL) {
                    dom_node *result = NULL;

                    /* Append cloned content as child of use element */
                    exc = dom_node_append_child(use_node, cloned, &result);
                    if (exc == DOM_NO_ERR && result != NULL) {
                        dom_node_unref(result);
                        NSLOG(neosurf, DEBUG, "SVG: Successfully resolved <use> to symbol");
                    }
                    dom_node_unref(cloned);
                }
            } else {
                NSLOG(neosurf, WARNING, "SVG: Symbol '%s' not found for <use>", id);
            }
        }

        dom_string_unref(href_attr);
        dom_node_unref(use_node);
    }

    dom_nodelist_unref(use_elements);

    return NSERROR_OK;
}

/**
 * Free the SVG symbol registry.
 */
void html_free_svg_symbols(struct html_content *c)
{
    struct svg_symbol_registry *reg;

    if (c->svg_symbols == NULL) {
        return;
    }

    reg = c->svg_symbols;

    for (unsigned int i = 0; i < reg->count; i++) {
        free(reg->entries[i].id);
        if (reg->entries[i].element != NULL) {
            dom_node_unref(reg->entries[i].element);
        }
    }

    free(reg->entries);
    free(reg);
    c->svg_symbols = NULL;
}

/**
 * Helper to append text to a growing buffer.
 */
static nserror svg_buffer_append(char **buf, size_t *len, size_t *cap, const char *str, size_t str_len)
{
    if (*len + str_len + 1 > *cap) {
        size_t new_cap = (*cap == 0) ? 1024 : *cap * 2;
        while (new_cap < *len + str_len + 1) {
            new_cap *= 2;
        }
        char *new_buf = realloc(*buf, new_cap);
        if (new_buf == NULL) {
            return NSERROR_NOMEM;
        }
        *buf = new_buf;
        *cap = new_cap;
    }
    memcpy(*buf + *len, str, str_len);
    *len += str_len;
    (*buf)[*len] = '\0';
    return NSERROR_OK;
}

/**
 * Helper to append text to buffer as lowercase.
 * SVG is case-sensitive and libsvgtiny expects lowercase tags.
 */
static nserror svg_buffer_append_lower(char **buf, size_t *len, size_t *cap, const char *str, size_t str_len)
{
    if (*len + str_len + 1 > *cap) {
        size_t new_cap = (*cap == 0) ? 1024 : *cap * 2;
        while (new_cap < *len + str_len + 1) {
            new_cap *= 2;
        }
        char *new_buf = realloc(*buf, new_cap);
        if (new_buf == NULL) {
            return NSERROR_NOMEM;
        }
        *buf = new_buf;
        *cap = new_cap;
    }
    /* Copy with lowercase conversion */
    for (size_t i = 0; i < str_len; i++) {
        char c = str[i];
        if (c >= 'A' && c <= 'Z') {
            c = c + ('a' - 'A');
        }
        (*buf)[*len + i] = c;
    }
    *len += str_len;
    (*buf)[*len] = '\0';
    return NSERROR_OK;
}

/**
 * Append a string to the buffer, replacing all occurrences of 'currentColor'
 * with the provided color value.
 */
static nserror svg_buffer_append_replace_color(
    char **buf, size_t *len, size_t *cap, const char *str, size_t str_len, const char *current_color)
{
    if (current_color == NULL) {
        return svg_buffer_append(buf, len, cap, str, str_len);
    }

    size_t color_len = strlen(current_color);
    const char *pos = str;
    const char *end = str + str_len;

    while (pos < end) {
        /* Find case-insensitive match for 'currentColor' or '#currentColor' (typo variant) */
        const char *match = NULL;
        int match_len = 0;
        for (const char *p = pos; p < end; p++) {
            /* Check for #currentColor (13 chars) first */
            if (p <= end - 13 && strncasecmp(p, "#currentColor", 13) == 0) {
                match = p;
                match_len = 13;
                break;
            }
            /* Check for currentColor (12 chars) */
            if (p <= end - 12 && strncasecmp(p, "currentColor", 12) == 0) {
                match = p;
                match_len = 12;
                break;
            }
        }

        if (match == NULL) {
            /* No more matches, append rest of string */
            nserror err = svg_buffer_append(buf, len, cap, pos, end - pos);
            return err;
        }

        /* Append text before match */
        if (match > pos) {
            nserror err = svg_buffer_append(buf, len, cap, pos, match - pos);
            if (err != NSERROR_OK)
                return err;
        }

        /* Append replacement color */
        nserror err = svg_buffer_append(buf, len, cap, current_color, color_len);
        if (err != NSERROR_OK)
            return err;

        /* Move past the match */
        pos = match + match_len;
    }

    return NSERROR_OK;
}

/**
 * Recursively serialize a DOM node to XML string.
 *
 * @param current_color  Hex color string (e.g. "#808080") to substitute for currentColor
 */
static nserror svg_serialize_node(dom_node *node, char **buf, size_t *len, size_t *cap, const char *current_color)
{
    dom_exception exc;
    dom_node_type type;
    nserror err;

    exc = dom_node_get_node_type(node, &type);
    if (exc != DOM_NO_ERR) {
        return NSERROR_DOM;
    }

    if (type == DOM_TEXT_NODE) {
        /* Text node - output text content */
        dom_string *text = NULL;
        exc = dom_node_get_text_content(node, &text);
        if (exc == DOM_NO_ERR && text != NULL) {
            err = svg_buffer_append(buf, len, cap, dom_string_data(text), dom_string_length(text));
            dom_string_unref(text);
            if (err != NSERROR_OK)
                return err;
        }
    } else if (type == DOM_ELEMENT_NODE) {
        /* Element node - output tag and attributes */
        dom_string *name = NULL;
        dom_namednodemap *attrs = NULL;
        const char *tag_name_str;
        bool is_use = false;
        bool is_symbol = false;
        bool has_children = false;

        exc = dom_node_get_node_name(node, &name);
        if (exc != DOM_NO_ERR || name == NULL) {
            return NSERROR_DOM;
        }

        tag_name_str = dom_string_data(name);

        /* Check for special elements */
        if (strcasecmp(tag_name_str, "use") == 0) {
            is_use = true;
            /* Check if use has children (resolved symbols) */
            dom_node *first_child = NULL;
            exc = dom_node_get_first_child(node, &first_child);
            if (exc == DOM_NO_ERR && first_child != NULL) {
                has_children = true;
                dom_node_unref(first_child);
            }
        } else if (strcasecmp(tag_name_str, "symbol") == 0) {
            is_symbol = true;
        }

        /* For <use> elements with children, skip the use wrapper
         * and just serialize the children directly */
        if (is_use && has_children) {
            dom_node *child = NULL;
            exc = dom_node_get_first_child(node, &child);
            while (exc == DOM_NO_ERR && child != NULL) {
                err = svg_serialize_node(child, buf, len, cap, current_color);

                dom_node *next = NULL;
                exc = dom_node_get_next_sibling(child, &next);
                dom_node_unref(child);
                child = next;

                if (err != NSERROR_OK) {
                    if (child)
                        dom_node_unref(child);
                    dom_string_unref(name);
                    return err;
                }
            }
            dom_string_unref(name);
            return NSERROR_OK;
        }

        /* Opening tag - use 'g' instead of 'symbol' since libsvgtiny
         * doesn't render symbol elements directly */
        err = svg_buffer_append(buf, len, cap, "<", 1);
        if (err != NSERROR_OK) {
            dom_string_unref(name);
            return err;
        }

        if (is_symbol) {
            /* Convert symbol to g (group) */
            err = svg_buffer_append(buf, len, cap, "g", 1);
        } else {
            err = svg_buffer_append_lower(buf, len, cap, dom_string_data(name), dom_string_length(name));
        }
        if (err != NSERROR_OK) {
            dom_string_unref(name);
            return err;
        }

        /* Attributes */
        exc = dom_node_get_attributes(node, &attrs);
        if (exc == DOM_NO_ERR && attrs != NULL) {
            uint32_t attr_count = 0;
            dom_namednodemap_get_length(attrs, &attr_count);

            for (uint32_t i = 0; i < attr_count; i++) {
                dom_attr *attr = NULL;
                exc = dom_namednodemap_item(attrs, i, (dom_node **)&attr);
                if (exc == DOM_NO_ERR && attr != NULL) {
                    dom_string *attr_name = NULL;
                    dom_string *attr_value = NULL;

                    dom_attr_get_name(attr, &attr_name);
                    dom_attr_get_value(attr, &attr_value);

                    if (attr_name != NULL) {
                        err = svg_buffer_append(buf, len, cap, " ", 1);
                        if (err == NSERROR_OK) {
                            err = svg_buffer_append(
                                buf, len, cap, dom_string_data(attr_name), dom_string_length(attr_name));
                        }
                        if (err == NSERROR_OK && attr_value != NULL) {
                            err = svg_buffer_append(buf, len, cap, "=\"", 2);
                            if (err == NSERROR_OK) {
                                /* Replace any occurrence of currentColor with the actual color
                                 * since libsvgtiny doesn't support it */
                                const char *val = dom_string_data(attr_value);
                                size_t val_len = dom_string_length(attr_value);
                                err = svg_buffer_append_replace_color(buf, len, cap, val, val_len, current_color);
                            }
                            if (err == NSERROR_OK) {
                                err = svg_buffer_append(buf, len, cap, "\"", 1);
                            }
                        }
                        dom_string_unref(attr_name);
                    }
                    if (attr_value != NULL) {
                        dom_string_unref(attr_value);
                    }
                    dom_node_unref((dom_node *)attr);

                    if (err != NSERROR_OK) {
                        dom_namednodemap_unref(attrs);
                        dom_string_unref(name);
                        return err;
                    }
                }
            }
            dom_namednodemap_unref(attrs);
        }

        err = svg_buffer_append(buf, len, cap, ">", 1);
        if (err != NSERROR_OK) {
            dom_string_unref(name);
            return err;
        }

        /* Children */
        dom_node *child = NULL;
        exc = dom_node_get_first_child(node, &child);
        while (exc == DOM_NO_ERR && child != NULL) {
            err = svg_serialize_node(child, buf, len, cap, current_color);

            dom_node *next = NULL;
            exc = dom_node_get_next_sibling(child, &next);
            dom_node_unref(child);
            child = next;

            if (err != NSERROR_OK) {
                if (child)
                    dom_node_unref(child);
                dom_string_unref(name);
                return err;
            }
        }

        /* Closing tag */
        err = svg_buffer_append(buf, len, cap, "</", 2);
        if (err == NSERROR_OK) {
            if (is_symbol) {
                err = svg_buffer_append(buf, len, cap, "g", 1);
            } else {
                err = svg_buffer_append_lower(buf, len, cap, dom_string_data(name), dom_string_length(name));
            }
        }
        if (err == NSERROR_OK) {
            err = svg_buffer_append(buf, len, cap, ">", 1);
        }

        dom_string_unref(name);
        if (err != NSERROR_OK)
            return err;
    }
    /* Ignore other node types (comments, etc.) */

    return NSERROR_OK;
}

/**
 * Serialize an inline SVG element to a string for libsvgtiny parsing.
 *
 * @param current_color  Hex color string (e.g. "#808080") to substitute for currentColor, or NULL
 */
nserror html_serialize_inline_svg(dom_element *svg_element, const char *current_color, char **svg_data, size_t *svg_len)
{
    char *buf = NULL;
    size_t len = 0;
    size_t cap = 0;
    nserror err;

    /* Serialize the SVG element and its children */
    err = svg_serialize_node((dom_node *)svg_element, &buf, &len, &cap, current_color);
    if (err != NSERROR_OK) {
        free(buf);
        *svg_data = NULL;
        *svg_len = 0;
        return err;
    }

    *svg_data = buf;
    *svg_len = len;

    NSLOG(neosurf, DEBUG, "SVG: Serialized inline SVG (%zu bytes)", len);

    /* Debug: log first 500 chars of serialized SVG */
    if (len > 0) {
        size_t log_len = len > 500 ? 500 : len;
        char *log_buf = malloc(log_len + 1);
        if (log_buf) {
            memcpy(log_buf, buf, log_len);
            log_buf[log_len] = '\0';
            NSLOG(neosurf, DEBUG, "SVG content: %s%s", log_buf, len > 500 ? "..." : "");
            free(log_buf);
        }
    }

    return NSERROR_OK;
}

/**
 * Parse an inline SVG element using libsvgtiny.
 *
 * @param current_color  Hex color string to substitute for currentColor, or NULL
 */
nserror html_parse_inline_svg(dom_element *svg_element, int width, int height, const char *base_url,
    const char *current_color, struct svgtiny_diagram **diagram_out)
{
    struct svgtiny_diagram *diagram;
    svgtiny_code code;
    char *svg_data = NULL;
    size_t svg_len = 0;
    nserror err;

    *diagram_out = NULL;

    /* Serialize the SVG element */
    err = html_serialize_inline_svg(svg_element, current_color, &svg_data, &svg_len);
    if (err != NSERROR_OK || svg_data == NULL) {
        NSLOG(neosurf, WARNING, "SVG: Failed to serialize inline SVG");
        return err != NSERROR_OK ? err : NSERROR_DOM;
    }

    /* Create diagram */
    diagram = svgtiny_create();
    if (diagram == NULL) {
        free(svg_data);
        return NSERROR_NOMEM;
    }

    /* Parse the SVG */
    code = svgtiny_parse(diagram, svg_data, svg_len, base_url, width, height);
    free(svg_data);

    if (code != svgtiny_OK) {
        NSLOG(neosurf, WARNING, "SVG: libsvgtiny parse failed: %d", code);
        svgtiny_free(diagram);
        return NSERROR_SVG_ERROR;
    }

    *diagram_out = diagram;

    NSLOG(neosurf, DEBUG, "SVG: Parsed inline SVG (%dx%d, %u shapes)", diagram->width, diagram->height,
        diagram->shape_count);

    return NSERROR_OK;
}

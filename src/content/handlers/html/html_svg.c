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

#include <wisp/content/handlers/html/private.h>
#include <wisp/utils/corestrings.h>
#include <wisp/utils/errors.h>
#include <wisp/utils/log.h>

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

    NSLOG(wisp, DEBUG, "SVG: Added symbol '%s' to registry (count=%u)", id, reg->count);

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
 * Resolve all <use href="#id"> references in the document.
 */
nserror html_resolve_svg_use_refs(struct html_content *c, dom_document *doc)
{
    dom_exception exc;
    dom_nodelist *use_elements = NULL;
    dom_string *use_tag = NULL;
    uint32_t use_count, i;
    struct svg_symbol_registry *reg;
    dom_node **static_list = NULL;

    if (c->svg_symbols == NULL) {
        return NSERROR_OK;
    }

    reg = c->svg_symbols;

    if (reg->count == 0) {
        NSLOG(wisp, DEBUG, "SVG: No symbols to resolve");
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

    NSLOG(wisp, DEBUG, "SVG: Found %u <use> elements to resolve", use_count);

    if (use_count > 0) {
        static_list = malloc(sizeof(dom_node *) * use_count);
        if (static_list == NULL) {
            dom_nodelist_unref(use_elements);
            return NSERROR_NOMEM;
        }

        /* Collect into static list first */
        for (i = 0; i < use_count; i++) {
            dom_node *node = NULL;
            exc = dom_nodelist_item(use_elements, i, &node);
            /* Node is already ref'd by item() */
            static_list[i] = node;
        }
    }

    /* Now we can modify the DOM safely */
    for (i = 0; i < use_count; i++) {
        dom_node *use_node = static_list[i];
        dom_element *use_elem;
        dom_string *href_attr = NULL;
        dom_string *href_name = NULL;
        const char *href_str;
        dom_element *symbol;

        if (use_node == NULL)
            continue;

        use_elem = (dom_element *)use_node;

        /* Check for existing children - if so, already resolved? */
        dom_node *first_child = NULL;
        exc = dom_node_get_first_child(use_node, &first_child);
        if (exc == DOM_NO_ERR && first_child != NULL) {
            dom_node_unref(first_child);
            /* Already has content, skip */
            dom_node_unref(use_node);
            continue;
        }

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

                NSLOG(wisp, DEBUG, "SVG: Resolving <use href=\"#%s\">", id);

                /* Clone the symbol's children and append to use element */
                exc = dom_node_clone_node(symbol, true, &cloned);
                NSLOG(wisp, DEBUG, "SVG: Clone result: exc=%d, cloned=%p", (int)exc, (void *)cloned);
                if (exc == DOM_NO_ERR && cloned != NULL) {
                    dom_node *result = NULL;

                    /* Append cloned content as child of use element */
                    exc = dom_node_append_child(use_node, cloned, &result);
                    NSLOG(wisp, DEBUG, "SVG: Append result: exc=%d, result=%p", (int)exc, (void *)result);
                    if (exc == DOM_NO_ERR && result != NULL) {
                        dom_node_unref(result);
                        NSLOG(wisp, DEBUG, "SVG: Successfully resolved <use> to symbol");
                    } else {
                        NSLOG(wisp, ERROR, "SVG: Failed to append resolved symbol content (exc=%d)", (int)exc);
                    }
                    dom_node_unref(cloned);
                } else {
                    NSLOG(wisp, ERROR, "SVG: Failed to clone symbol (exc=%d)", (int)exc);
                }
            } else {
                NSLOG(wisp, WARNING, "SVG: Symbol '%s' not found for <use>", id);
            }
        }

        dom_string_unref(href_attr);
        dom_node_unref(use_node);
    }

    if (static_list) {
        free(static_list);
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
 * Free the pre-serialized inline SVG list.
 */
void html_free_inline_svgs(struct html_content *c)
{
    struct html_inline_svg *entry;
    struct html_inline_svg *next;

    entry = c->inline_svgs;
    while (entry != NULL) {
        next = entry->next;

        if (entry->node != NULL) {
            dom_node_unref(entry->node);
        }
        free(entry->svg_xml);
        free(entry);

        entry = next;
    }
    c->inline_svgs = NULL;
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
 * Serialize an element's attributes to XML.
 * Handles currentColor replacement and attribute filtering.
 */
static nserror svg_serialize_attributes(
    dom_element *element, char **buf, size_t *len, size_t *cap, const char *current_color, bool skip_id)
{
    dom_exception exc;
    dom_namednodemap *attrs = NULL;
    nserror err = NSERROR_OK;

    exc = dom_node_get_attributes(element, &attrs);
    if (exc != DOM_NO_ERR || attrs == NULL) {
        return NSERROR_OK;
    }

    uint32_t attr_count = 0;
    dom_namednodemap_get_length(attrs, &attr_count);

    NSLOG(wisp, DEBUG, "SVG serialize: Element has %u attributes", attr_count);

    for (uint32_t i = 0; i < attr_count; i++) {
        dom_attr *attr = NULL;
        exc = dom_namednodemap_item(attrs, i, (dom_node **)&attr);
        if (exc != DOM_NO_ERR || attr == NULL) {
            continue;
        }

        dom_string *attr_name = NULL;
        dom_string *attr_value = NULL;
        dom_attr_get_name(attr, &attr_name);
        dom_attr_get_value(attr, &attr_value);

        if (attr_name != NULL) {
            /* Skip 'id' attribute if requested */
            if (skip_id && dom_string_caseless_isequal(attr_name, corestring_dom_id)) {
                dom_string_unref(attr_name);
                if (attr_value)
                    dom_string_unref(attr_value);
                dom_node_unref((dom_node *)attr);
                continue;
            }

            /* Skip 'href' attribute - it is resolved in nested SVG */
            if (dom_string_caseless_isequal(attr_name, corestring_dom_href)) {
                NSLOG(wisp, DEBUG, "SVG serialize: Skipping href attribute (resolved)");
                dom_string_unref(attr_name);
                if (attr_value)
                    dom_string_unref(attr_value);
                dom_node_unref((dom_node *)attr);
                continue;
            }
            /* Skip xmlns - we add it explicitly for standalone SVG */
            {
                const char *aname = dom_string_data(attr_name);
                if (strncasecmp(aname, "xmlns", 5) == 0) {
                    NSLOG(wisp, DEBUG, "SVG serialize: Skipping xmlns attribute (added explicitly)");
                    dom_string_unref(attr_name);
                    if (attr_value)
                        dom_string_unref(attr_value);
                    dom_node_unref((dom_node *)attr);
                    continue;
                }
            }
            /* Check for xlink:href */
            {
                dom_string *xlink_href = NULL;
                if (dom_string_create((const uint8_t *)"xlink:href", 10, &xlink_href) == DOM_NO_ERR && xlink_href) {
                    if (dom_string_caseless_isequal(attr_name, xlink_href)) {
                        NSLOG(wisp, DEBUG, "SVG serialize: Skipping xlink:href attribute (resolved)");
                        dom_string_unref(xlink_href);
                        dom_string_unref(attr_name);
                        if (attr_value)
                            dom_string_unref(attr_value);
                        dom_node_unref((dom_node *)attr);
                        continue;
                    }
                    dom_string_unref(xlink_href);
                }
            }

            err = svg_buffer_append(buf, len, cap, " ", 1);
            if (err == NSERROR_OK) {
                /* Handle special case for viewBox attribute - must keep camelCase */
                const char *name_data = dom_string_data(attr_name);
                size_t name_len = dom_string_length(attr_name);
                NSLOG(wisp, DEBUG, "SVG serialize: attribute name='%s' len=%zu", name_data, name_len);
                if (name_len == 7 && strncasecmp(name_data, "viewbox", 7) == 0) {
                    /* Must be 'viewBox' with capital B */
                    err = svg_buffer_append(buf, len, cap, "viewBox", 7);
                } else {
                    /* Output attribute as-is from DOM */
                    err = svg_buffer_append(buf, len, cap, name_data, name_len);
                }
            }
            if (err == NSERROR_OK && attr_value != NULL) {
                err = svg_buffer_append(buf, len, cap, "=\"", 2);
            }
            if (err == NSERROR_OK && attr_value != NULL) {
                const char *val = dom_string_data(attr_value);
                size_t val_len = dom_string_length(attr_value);
                err = svg_buffer_append_replace_color(buf, len, cap, val, val_len, current_color);
            }
            if (err == NSERROR_OK && attr_value != NULL) {
                err = svg_buffer_append(buf, len, cap, "\"", 1);
            }

            dom_string_unref(attr_name);
            attr_name = NULL;
        }

        if (attr_value) {
            dom_string_unref(attr_value);
            attr_value = NULL;
        }
        dom_node_unref((dom_node *)attr);
        attr = NULL;

        if (err != NSERROR_OK) {
            break;
        }
    }

    dom_namednodemap_unref(attrs);
    return err;
}

/**
 * Serialize an element's opening tag: <tagname [attrs]>
 * Special version for <svg> that adds dimensions based on viewBox if present.
 */
static bool svg_element_is_use_with_resolved(dom_element *element, bool *has_resolved);

static nserror svg_serialize_element_open_with_dimensions(
    const char *tag_name, dom_element *element, char **buf, size_t *len, size_t *cap, const char *current_color)
{
    nserror err;
    int viewbox_x = 0, viewbox_y = 0;
    int viewbox_w = 24, viewbox_h = 24;
    bool has_viewbox = false;

    NSLOG(wisp, DEBUG, "SVG serialize: Opening tag <%s> with viewBox-based dimensions", tag_name);

    /* First, check if element has viewBox attribute to get dimensions */
    {
        dom_exception exc;
        dom_namednodemap *attrs = NULL;
        exc = dom_node_get_attributes((dom_node *)element, &attrs);
        if (exc == DOM_NO_ERR && attrs != NULL) {
            uint32_t attr_count = 0;
            dom_namednodemap_get_length(attrs, &attr_count);

            for (uint32_t i = 0; i < attr_count; i++) {
                dom_attr *attr = NULL;
                exc = dom_namednodemap_item(attrs, i, (dom_node **)&attr);
                if (exc == DOM_NO_ERR && attr != NULL) {
                    dom_string *attr_name = NULL;
                    dom_attr_get_name(attr, &attr_name);
                    if (attr_name != NULL) {
                        const char *name = dom_string_data(attr_name);
                        /* Check for viewBox (case-insensitive) */
                        if (strcasecmp(name, "viewBox") == 0 || strcasecmp(name, "viewbox") == 0) {
                            dom_string *attr_value = NULL;
                            dom_attr_get_value(attr, &attr_value);
                            if (attr_value != NULL) {
                                const char *val = dom_string_data(attr_value);
                                /* Parse viewBox="x y w h" */
                                int x, y, w, h;
                                if (sscanf(val, "%d %d %d %d", &x, &y, &w, &h) == 4) {
                                    viewbox_x = x;
                                    viewbox_y = y;
                                    viewbox_w = w;
                                    viewbox_h = h;
                                    has_viewbox = true;
                                    NSLOG(wisp, DEBUG, "SVG serialize: Element has viewBox %dx%d", w, h);
                                }
                                dom_string_unref(attr_value);
                            }
                        }
                        dom_string_unref(attr_name);
                    }
                    dom_node_unref((dom_node *)attr);
                }
            }
            dom_namednodemap_unref(attrs);
        }
    }

    /* If the outer <svg> has no viewBox, probe <use> children for the
     * resolved symbol's viewBox and lift it to the root element.
     * This flattens the structure so the serialized SVG looks like
     * a normal file SVG — one root <svg> with viewBox and paths. */
    if (!has_viewbox) {
        dom_exception exc2;
        dom_node *child = NULL;
        exc2 = dom_node_get_first_child((dom_node *)element, &child);
        while (exc2 == DOM_NO_ERR && child != NULL && !has_viewbox) {
            dom_node_type ctype;
            exc2 = dom_node_get_node_type(child, &ctype);
            if (exc2 == DOM_NO_ERR && ctype == DOM_ELEMENT_NODE) {
                bool has_resolved = false;
                bool is_use = svg_element_is_use_with_resolved((dom_element *)child, &has_resolved);
                if (is_use && has_resolved) {
                    /* Find the resolved symbol/g child of the <use> */
                    dom_node *resolved = NULL;
                    dom_node_get_first_child(child, &resolved);
                    while (resolved != NULL) {
                        dom_node_type rtype;
                        dom_node_get_node_type(resolved, &rtype);
                        if (rtype == DOM_ELEMENT_NODE) {
                            /* Check for viewBox on this resolved element */
                            dom_string *vb_val = NULL;
                            dom_string *vb_name = NULL;
                            if (dom_string_create((const uint8_t *)"viewBox", 7, &vb_name) == DOM_NO_ERR && vb_name) {
                                dom_element_get_attribute((dom_element *)resolved, vb_name, &vb_val);
                                dom_string_unref(vb_name);
                            }
                            if (vb_val != NULL) {
                                int x, y, w, h;
                                if (sscanf(dom_string_data(vb_val), "%d %d %d %d", &x, &y, &w, &h) == 4) {
                                    viewbox_x = x;
                                    viewbox_y = y;
                                    viewbox_w = w;
                                    viewbox_h = h;
                                    has_viewbox = true;
                                    NSLOG(wisp, DEBUG,
                                        "SVG serialize: Lifted viewBox "
                                        "%dx%d from resolved <use> target",
                                        w, h);
                                }
                                dom_string_unref(vb_val);
                            }
                            dom_node_unref(resolved);
                            break;
                        }
                        dom_node *next = NULL;
                        dom_node_get_next_sibling(resolved, &next);
                        dom_node_unref(resolved);
                        resolved = next;
                    }
                }
            }
            dom_node *next = NULL;
            dom_node_get_next_sibling(child, &next);
            dom_node_unref(child);
            child = next;
        }
        if (child != NULL)
            dom_node_unref(child);
    }

    err = svg_buffer_append(buf, len, cap, "<", 1);
    if (err == NSERROR_OK) {
        err = svg_buffer_append(buf, len, cap, tag_name, strlen(tag_name));
    }

    /* Always add xmlns for standalone SVG compatibility */
    if (err == NSERROR_OK) {
        err = svg_buffer_append(
            buf, len, cap, " xmlns=\"http://www.w3.org/2000/svg\"", strlen(" xmlns=\"http://www.w3.org/2000/svg\""));
    }

    if (err == NSERROR_OK) {
        err = svg_serialize_attributes(element, buf, len, cap, current_color, false);
    }

    /* Add viewBox and dimensions if we have them (either from the element
     * itself or lifted from a resolved <use> target) */
    if (err == NSERROR_OK && has_viewbox) {
        bool has_existing_viewbox = false;
        /* Check for existing viewBox in what we just wrote */
        if (*len > 6) {
            char *search = *buf + strlen(tag_name) + 1;
            if (strstr(search, "viewBox=") != NULL)
                has_existing_viewbox = true;
        }
        /* Add viewBox if it was lifted from a resolved symbol
         * (not already on the element).
         * Do NOT inject width/height — those are coordinate-space
         * dimensions, not display dimensions.  CSS controls the
         * display size via REPLACE_DIM on the box. */
        if (!has_existing_viewbox) {
            char vb_buf[64];
            snprintf(vb_buf, sizeof(vb_buf), " viewBox=\"%d %d %d %d\"", viewbox_x, viewbox_y, viewbox_w, viewbox_h);
            err = svg_buffer_append(buf, len, cap, vb_buf, strlen(vb_buf));
        }
    }
    if (err == NSERROR_OK) {
        err = svg_buffer_append(buf, len, cap, ">", 1);
    }

    return err;
}

/**
 * Serialize an element's opening tag: <tagname [attrs]>
 */
static nserror svg_serialize_element_open(const char *tag_name, dom_element *element, char **buf, size_t *len,
    size_t *cap, const char *current_color, bool skip_id)
{
    nserror err;

    NSLOG(wisp, DEBUG, "SVG serialize: Opening tag <%s>", tag_name);

    err = svg_buffer_append(buf, len, cap, "<", 1);
    if (err == NSERROR_OK) {
        err = svg_buffer_append(buf, len, cap, tag_name, strlen(tag_name));
    }
    if (err == NSERROR_OK) {
        err = svg_serialize_attributes(element, buf, len, cap, current_color, skip_id);
    }
    if (err == NSERROR_OK) {
        err = svg_buffer_append(buf, len, cap, ">", 1);
    }

    return err;
}

/**
 * Serialize an element's closing tag: </tagname>
 */
static nserror svg_serialize_element_close(const char *tag_name, char **buf, size_t *len, size_t *cap)
{
    NSLOG(wisp, DEBUG, "SVG serialize: Closing tag </%s>", tag_name);

    nserror err = svg_buffer_append(buf, len, cap, "</", 2);
    if (err == NSERROR_OK) {
        err = svg_buffer_append(buf, len, cap, tag_name, strlen(tag_name));
    }
    if (err == NSERROR_OK) {
        err = svg_buffer_append(buf, len, cap, ">", 1);
    }
    return err;
}

/**
 * Serialize children of an element recursively.
 */
static nserror
svg_serialize_children(dom_element *element, char **buf, size_t *len, size_t *cap, const char *current_color);

/* Forward declaration for svg_serialize_node */
static nserror svg_serialize_node(dom_node *node, char **buf, size_t *len, size_t *cap, const char *current_color);

/**
 * Serialize children of an element recursively.
 */
static nserror
svg_serialize_children(dom_element *element, char **buf, size_t *len, size_t *cap, const char *current_color)
{
    dom_exception exc;
    dom_node *child = NULL;
    nserror err = NSERROR_OK;

    exc = dom_node_get_first_child((dom_node *)element, &child);
    while (exc == DOM_NO_ERR && child != NULL) {
        err = svg_serialize_node(child, buf, len, cap, current_color);

        dom_node *next = NULL;
        exc = dom_node_get_next_sibling(child, &next);
        dom_node_unref(child);
        child = next;

        if (err != NSERROR_OK) {
            break;
        }
    }

    return err;
}

/**
 * Serialize a <use> element with resolved symbol content.
 * Since libsvgtiny doesn't support <use>, we inline the content directly.
 */
static nserror svg_serialize_use_with_resolved(
    dom_element *use_element, char **buf, size_t *len, size_t *cap, const char *current_color)
{
    dom_exception exc;
    nserror err = NSERROR_OK;

    NSLOG(wisp, DEBUG, "SVG serialize: Processing <use> element - inlining content (libsvgtiny doesn't support <use>)");

    /* Flatten: skip the <use> tag and its <symbol>/<g> wrapper entirely.
     * Just output the resolved symbol's children directly into the
     * parent <svg>.  The parent's viewBox was already set from the
     * resolved symbol by svg_serialize_element_open_with_dimensions. */

    /* Find the resolved symbol (first child element, typically <symbol> or <g>) */
    dom_node *child = NULL;
    exc = dom_node_get_first_child((dom_node *)use_element, &child);

    while (exc == DOM_NO_ERR && child != NULL) {
        dom_node_type child_type;
        exc = dom_node_get_node_type(child, &child_type);

        if (exc == DOM_NO_ERR && child_type == DOM_ELEMENT_NODE) {
            dom_string *child_name = NULL;
            exc = dom_node_get_node_name(child, &child_name);

            if (exc == DOM_NO_ERR && child_name != NULL) {
                const char *child_name_str = dom_string_data(child_name);
                bool is_symbol = (strcasecmp(child_name_str, "symbol") == 0);
                bool is_group = (strcasecmp(child_name_str, "g") == 0);

                NSLOG(wisp, DEBUG, "SVG serialize: Found child element: <%s> (symbol=%d, g=%d)", child_name_str,
                    is_symbol, is_group);

                if (is_symbol || is_group) {
                    /* Flatten: emit only the symbol/group's children,
                     * not the <symbol>/<g> itself or a <svg> wrapper.
                     * The viewBox has been lifted to the root <svg>. */
                    NSLOG(wisp, DEBUG, "SVG serialize: Flattening <%s> children into root <svg>", child_name_str);

                    err = svg_serialize_children((dom_element *)child, buf, len, cap, current_color);

                    dom_string_unref(child_name);
                    dom_node_unref(child);
                    if (err != NSERROR_OK) {
                        return err;
                    }
                    break; /* Only process first symbol/group */
                }

                dom_string_unref(child_name);
            }
        }

        dom_node *next = NULL;
        exc = dom_node_get_next_sibling(child, &next);
        dom_node_unref(child);
        child = next;
    }

    return NSERROR_OK;
}

/**
 * Check if an element is a <use> element with resolved symbol content.
 */
static bool svg_element_is_use_with_resolved(dom_element *element, bool *has_resolved)
{
    dom_exception exc;
    dom_string *name = NULL;
    bool is_use = false;
    *has_resolved = false;

    exc = dom_node_get_node_name((dom_node *)element, &name);
    if (exc != DOM_NO_ERR || name == NULL) {
        return false;
    }

    const char *tag_name = dom_string_data(name);
    if (strcasecmp(tag_name, "use") == 0) {
        is_use = true;

        /* Check if use has children (resolved symbols) */
        dom_node *first_child = NULL;
        exc = dom_node_get_first_child((dom_node *)element, &first_child);
        if (exc == DOM_NO_ERR && first_child != NULL) {
            *has_resolved = true;
            NSLOG(wisp, DEBUG, "SVG serialize: <use> has resolved children");
            dom_node_unref(first_child);
        } else {
            NSLOG(wisp, DEBUG, "SVG serialize: <use> has NO resolved children");
        }
    }

    dom_string_unref(name);
    return is_use;
}

/**
 * Recursively serialize a DOM node to XML string.
 *
 * This is the main entry point - it dispatches to specialized functions
 * based on node type and element type.
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
        NSLOG(wisp, DEBUG, "SVG serialize: Failed to get node type");
        return NSERROR_DOM;
    }

    if (type == DOM_TEXT_NODE) {
        /* Text node - output text content */
        dom_string *text = NULL;
        exc = dom_node_get_text_content(node, &text);
        if (exc == DOM_NO_ERR && text != NULL) {
            NSLOG(wisp, DEBUG, "SVG serialize: Text node: '%s'", dom_string_data(text));
            err = svg_buffer_append(buf, len, cap, dom_string_data(text), dom_string_length(text));
            dom_string_unref(text);
            if (err != NSERROR_OK)
                return err;
        }
    } else if (type == DOM_ELEMENT_NODE) {
        dom_element *element = (dom_element *)node;
        dom_string *name = NULL;
        const char *tag_name_str;

        exc = dom_node_get_node_name(node, &name);
        if (exc != DOM_NO_ERR || name == NULL) {
            NSLOG(wisp, DEBUG, "SVG serialize: Failed to get node name");
            return NSERROR_DOM;
        }

        tag_name_str = dom_string_data(name);
        NSLOG(wisp, DEBUG, "SVG serialize: Element <%s>", tag_name_str);

        /* Check if this is a <use> element with resolved content */
        bool has_resolved = false;
        bool is_use = svg_element_is_use_with_resolved(element, &has_resolved);

        if (is_use && has_resolved) {
            /* Special handling: serialize <use> with resolved symbol content */
            NSLOG(wisp, DEBUG, "SVG serialize: Using special <use> handling with resolved content");
            err = svg_serialize_use_with_resolved(element, buf, len, cap, current_color);
        } else {
            /* Normal element serialization: <tag> children </tag> */
            if (strcasecmp(tag_name_str, "symbol") == 0) {
                /* Convert <symbol> to <g> since libsvgtiny doesn't render symbol directly */
                err = svg_serialize_element_open("g", element, buf, len, cap, current_color, false);
            } else if (strcasecmp(tag_name_str, "svg") == 0) {
                /* For root <svg>, add default width/height if not present */
                err = svg_serialize_element_open_with_dimensions("svg", element, buf, len, cap, current_color);
            } else {
                err = svg_serialize_element_open(tag_name_str, element, buf, len, cap, current_color, false);
            }

            if (err == NSERROR_OK) {
                err = svg_serialize_children(element, buf, len, cap, current_color);
            }

            if (err == NSERROR_OK) {
                if (strcasecmp(tag_name_str, "symbol") == 0) {
                    err = svg_serialize_element_close("g", buf, len, cap);
                } else {
                    err = svg_serialize_element_close(tag_name_str, buf, len, cap);
                }
            }
        }

        dom_string_unref(name);
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

    NSLOG(wisp, DEBUG, "SVG: Serialized inline SVG (%zu bytes)", len);

    /* Debug: log first 500 chars of serialized SVG */
    if (len > 0) {
        size_t log_len = len > 500 ? 500 : len;
        char *log_buf = malloc(log_len + 1);
        if (log_buf) {
            memcpy(log_buf, buf, log_len);
            log_buf[log_len] = '\0';
            NSLOG(wisp, DEBUG, "SVG content: %s%s", log_buf, len > 500 ? "..." : "");
            free(log_buf);
        }
    }

    return NSERROR_OK;
}

/**
 * Parser callback when an SVG element completes parsing.
 *
 * This is called by hubbub via libdom when an <svg> element closes.
 * We serialize the SVG DOM to XML here to avoid redundant DOM traversal
 * during box construction.
 */
/**
 * Parser callback when an SVG element completes parsing.
 *
 * This is called by hubbub via libdom when an <svg> element closes.
 * We store the node reference to allow lazy serialization during box construction.
 */
dom_hubbub_error html_process_svg(void *ctx, dom_node *node)
{
    html_content *c = (html_content *)ctx;
    struct html_inline_svg *entry;

    if (c == NULL || node == NULL) {
        return DOM_HUBBUB_OK;
    }

    NSLOG(wisp, DEBUG, "SVG: Parser callback - storing inline SVG node for lazy serialization");

    /* Create entry for the inline_svgs list */
    entry = malloc(sizeof(struct html_inline_svg));
    if (entry == NULL) {
        return DOM_HUBBUB_OK;
    }

    /* Reference the node so it stays valid */
    dom_node_ref(node);

    entry->node = node;
    entry->svg_xml = NULL; /* Lazy serialization */
    entry->svg_xml_len = 0;
    entry->next = c->inline_svgs;
    c->inline_svgs = entry;

    /* Optimization: Collect symbols immediately from this SVG element
     * instead of scanning the entire document later. */
    if (c->svg_symbols == NULL) {
        c->svg_symbols = calloc(1, sizeof(struct svg_symbol_registry));
        if (c->svg_symbols != NULL) {
            c->svg_symbols->entries = calloc(SVG_SYMBOL_INITIAL_CAPACITY, sizeof(struct svg_symbol_entry));
            if (c->svg_symbols->entries != NULL) {
                c->svg_symbols->capacity = SVG_SYMBOL_INITIAL_CAPACITY;
                c->svg_symbols->count = 0;
            } else {
                free(c->svg_symbols);
                c->svg_symbols = NULL;
            }
        }
    }

    if (c->svg_symbols != NULL) {
        collect_symbols_from_element((dom_element *)node, c->svg_symbols);
        NSLOG(wisp, DEBUG, "SVG: Collected symbols from inline SVG (total: %u)", c->svg_symbols->count);
    }

    return DOM_HUBBUB_OK;
}

/**
 * Resolve all <use href="#id"> references in the document.
 */

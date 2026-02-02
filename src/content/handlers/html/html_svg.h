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
 * Inline SVG support for HTML content - header.
 *
 * Handles:
 * - Collection of SVG symbols from <symbol> and <defs> elements
 * - Resolution of <use href="#id"> references
 * - Serialization of inline SVG for libsvgtiny parsing
 */

#ifndef WISP_HTML_SVG_H
#define WISP_HTML_SVG_H

#include <dom/dom.h>
#include <wisp/utils/errors.h>
#include <stddef.h>

struct html_content;
struct svgtiny_diagram;

/**
 * Entry in the SVG symbol registry.
 */
struct svg_symbol_entry {
    char *id; /**< Symbol ID (without #) */
    dom_element *element; /**< DOM element for the symbol */
};

/**
 * Registry of SVG symbols collected from the document.
 */
struct svg_symbol_registry {
    struct svg_symbol_entry *entries;
    unsigned int count;
    unsigned int capacity;
};


/**
 * Resolve all <use href="#id"> references in the document.
 *
 * For each <use> element with an href starting with #, looks up the ID
 * in the symbol registry and clones the referenced content into the
 * <use> element.
 *
 * \param c    HTML content
 * \param doc  DOM document
 * \return NSERROR_OK on success, appropriate error otherwise
 */
nserror html_resolve_svg_use_refs(struct html_content *c, dom_document *doc);

/**
 * Free the SVG symbol registry.
 *
 * \param c  HTML content
 */
void html_free_svg_symbols(struct html_content *c);

/**
 * Free the pre-serialized inline SVG list.
 *
 * \param c  HTML content
 */
void html_free_inline_svgs(struct html_content *c);

/**
 * Serialize an inline SVG element to a string for libsvgtiny parsing.
 *
 * \param svg_element  The <svg> DOM element to serialize
 * \param svg_data     Returns the serialized SVG string (caller must free)
 * \param svg_len      Returns the length of the serialized string
 * \return NSERROR_OK on success, appropriate error otherwise
 */
nserror
html_serialize_inline_svg(dom_element *svg_element, const char *current_color, char **svg_data, size_t *svg_len);

#endif /* NETSURF_HTML_SVG_H */

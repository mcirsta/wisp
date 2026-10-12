/*
 * This file is part of LibCSS.
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 */

#ifndef css_parse_declaration_store_h_
#define css_parse_declaration_store_h_

#include "parse/declaration_order.h"

typedef struct css_declaration_store css_declaration_store;

/* Owned specified declarations for native editing. Copies only winning values,
 * not parsed stylesheets/bytecode; the input list may be destroyed immediately.
 * Shorthand members share one value source, including pending substitutions.
 * Ordinary page styling does not create this state. */
css_error css__declaration_store_create(const css_declaration_list *list, css_declaration_store **out);
void css__declaration_store_destroy(css_declaration_store *store);
size_t css__declaration_store_count(const css_declaration_store *store);
/* Borrowed until the next successful mutation or destruction. */
const css_declaration_contribution *css__declaration_store_get(const css_declaration_store *store, size_t index);

/* Apply one accepted, immutable parser declaration, expanding shorthands in
 * registered order. Assignment replaces priority rather than cascading against
 * it. Logical/physical ordering follows CSSOM's set-a-CSS-declaration algorithm.
 * Fatal errors leave the store and borrowed values unchanged. *changed may be
 * true for equivalent authored spellings; exact unchanged inputs are skipped.
 *
 * This is NOT a string-based setProperty entry point. Its caller must parse a
 * single property value with the appropriate API grammar/priority validation;
 * accepting the first declaration from an arbitrary CSS string is incorrect. */
css_error css__declaration_store_set(css_declaration_store *store, const css_declaration *value, bool *changed);

/* Names are decoded UTF-8 with explicit lengths (no CSS unescaping). Ordinary
 * names/aliases are ASCII-insensitive; custom names are case-sensitive.
 * remove is allocation-free and removes all members of a shorthand. A future
 * removeProperty binding must first obtain its return value with value() below,
 * then remove, so a readback allocation failure cannot partially change state. */
css_error css__declaration_store_remove(css_declaration_store *store, const char *name, size_t length, bool *changed);
css_error
css__declaration_store_value(const css_declaration_store *store, const char *name, size_t length, lwc_string **out);
css_error
css__declaration_store_priority(const css_declaration_store *store, const char *name, size_t length, bool *important);
css_error css__declaration_store_serialize(const css_declaration_store *store, lwc_string **out);

#endif

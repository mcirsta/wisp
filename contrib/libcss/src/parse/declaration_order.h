/*
 * This file is part of LibCSS.
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 */

#ifndef css_parse_declaration_order_h_
#define css_parse_declaration_order_h_

#include "parse/declarations.h"

typedef struct css_declaration_order css_declaration_order;

/* A winning contribution to one of the parser's property components. Values
 * still belong to the source declaration: component is its shorthand member
 * index, not a bytecode offset or a token index. Deferred var() shorthands must
 * retain that source too. Specified-value expansion/serialization comes later. */
typedef struct css_declaration_contribution {
    const char *name;
    const css_declaration *source;
    size_t component;
} css_declaration_contribution;

/* Build an immutable, ordered view of accepted declarations. Shorthand members
 * are named before duplicate/priority resolution; logical and physical names
 * remain distinct. Winners stay at their original relative positions (CSSOM
 * specified order). This is not setProperty's replacement-order algorithm.
 *
 * The completed list is borrowed and MUST outlive the view. This layer does
 * not mutate parser output or bytecode, and allocates only when requested. */
css_error css__declaration_order_create(const css_declaration_list *list, css_declaration_order **out);
void css__declaration_order_destroy(css_declaration_order *order);
size_t css__declaration_order_count(const css_declaration_order *order);
const css_declaration_contribution *css__declaration_order_get(const css_declaration_order *order, size_t index);

/* Registration supplying this component's logical group/mapping metadata.
 * Logical members without a direct parser use their source pair's metadata. */
const struct css_prop_entry *css__declaration_contribution_property(const css_declaration_contribution *value);

#endif

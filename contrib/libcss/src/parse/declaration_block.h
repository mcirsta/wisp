/*
 * This file is part of LibCSS.
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 */

#ifndef css_parse_declaration_block_h_
#define css_parse_declaration_block_h_

#include "parse/declaration_order.h"

/* Serialize the immutable specified-order view as a CSSOM declaration block.
 * This is an internal cssText foundation, not a live/mutable CSSStyleDeclaration.
 * Shorthand choice follows CSSOM preferred order and logical intermixing rules.
 * Unsupported value serialization propagates CSS_INVALID, never partial output
 * or raw source text. On success the caller owns *out; on failure it is NULL.
 * No state is added to ordinary styling or to the borrowed order/list. */
css_error css__declaration_block_serialize(const css_declaration_order *order, lwc_string **out);

/* Same serializer for an owned mutable block's ordered, unique contributions.
 * Values and their sources are borrowed for the duration of the call. */
css_error
css__declaration_block_serialize_values(const css_declaration_contribution *values, size_t count, lwc_string **out);

#endif

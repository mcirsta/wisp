/*
 * This file is part of LibCSS.
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 */

#ifndef css_parse_property_h_
#define css_parse_property_h_

#include "parse/properties/properties.h"
#include "declaration_ids.inc"

/* Specified-value serializers, independent of the renderer's fixed-point
 * bytecode. NONE deliberately has no generic source-text fallback. */
enum css_declaration_value_kind {
    CSS_DECL_VALUE_NONE,
    CSS_DECL_VALUE_KEYWORD,
    CSS_DECL_VALUE_NUMBER,
    CSS_DECL_VALUE_LENGTH,
    CSS_DECL_VALUE_NUMBER_LENGTH,
    CSS_DECL_VALUE_PAIR_LENGTH,
    CSS_DECL_VALUE_QUAD_LENGTH,
    CSS_DECL_VALUE_PAIR_KEYWORD,
    CSS_DECL_VALUE_QUAD_KEYWORD,
    CSS_DECL_VALUE_URI,
    CSS_DECL_VALUE_IMAGE_LIST,
    CSS_DECL_VALUE_KEYWORD_LIST,
    CSS_DECL_VALUE_CONTENT,
    CSS_DECL_VALUE_STRING_LIST,
    CSS_DECL_VALUE_COUNTER_LIST,
    CSS_DECL_VALUE_ASPECT_RATIO,
    CSS_DECL_VALUE_BORDER_SPACING,
    CSS_DECL_VALUE_CLIP,
    CSS_DECL_VALUE_FONT_FAMILY,
    CSS_DECL_VALUE_FLEX,
    CSS_DECL_VALUE_FLEX_FLOW,
    CSS_DECL_VALUE_LIST_STYLE,
    CSS_DECL_VALUE_COLUMNS,
    CSS_DECL_VALUE_POSITION,
    CSS_DECL_VALUE_POSITION_LIST,
    CSS_DECL_VALUE_GRID_LINE,
    CSS_DECL_VALUE_GRID_PAIR,
    CSS_DECL_VALUE_GRID_AREA,
    CSS_DECL_VALUE_BORDER,
    CSS_DECL_VALUE_BORDER_SIDE,
    CSS_DECL_VALUE_OUTLINE,
    CSS_DECL_VALUE_COLOUR,
    CSS_DECL_VALUE_QUAD_COLOUR,
};

/* Immutable registration shared by the parser, variable resolver and specified
 * declarations. IDs identify CSS names, not bytecode slots. In particular, a
 * logical property retains its identity even when layout maps it physically.
 * `shorthand` describes registered parser shorthands, not whether a handler is
 * generated via WRAP. This is not yet a CSSOM value-expansion catalogue. */
struct css_prop_entry {
    const char *name;
    css_prop_handler handler;
    int opcode; /**< Existing bytecode dispatch index, or -1 for delegated parsing */
    enum css_declaration_property_id id;
    enum css_logical_property_group logical_group;
    bool shorthand;
    bool logical;
    /* Supported parser components in declaration order, not bytecode order.
     * Some logical start/end names are only implemented through a shorthand.
     * This does not expand their values or expose additional parse handlers. */
    const char *const *longhands;
    size_t longhand_count;
    enum css_declaration_value_kind value_kind;
};

/* Name must already be CSS-unescaped. Unknown/non-ASCII names return NULL.
 * The returned registration has static lifetime. Custom properties use their
 * case-sensitive name and do not have an entry in this registry. */
const struct css_prop_entry *css_prop_lookup(const char *name, size_t len);

#endif

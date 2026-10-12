/*
 * This file is part of LibCSS.
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 */
#ifndef css_parse_declaration_text_h_
#define css_parse_declaration_text_h_

#include "parse/declarations.h"

/* Private specified-value serializer for the registered URI/image/keyword lists
 * and generated-content values. Ownership and unsupported-value rules match declaration_value.h. */
css_error css__declaration_text_serialize(const css_declaration *source, lwc_string **out);

typedef struct css_declaration_text {
    const char *name; /**< Decoded property name; serialize as an identifier. */
    lwc_string *value; /**< Already serialized specified value; borrowed. */
    bool important;
} css_declaration_text;

/* Format declarations with CSSOM name escaping, whitespace and delimiters.
 * Measure/write once per block, with no allocation per property name. */
css_error css__declaration_text_block(const css_declaration_text *declarations, size_t count, lwc_string **out);

#endif

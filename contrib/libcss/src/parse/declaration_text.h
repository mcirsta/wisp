/*
 * This file is part of LibCSS.
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 */
#ifndef css_parse_declaration_text_h_
#define css_parse_declaration_text_h_

#include "parse/declarations.h"

/* Private specified-value serializer for the registered URI/image/keyword-list
 * families. Ownership and unsupported-value rules match declaration_value.h. */
css_error css__declaration_text_serialize(const css_declaration *source, lwc_string **out);

#endif

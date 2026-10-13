/*
 * This file is part of LibCSS.
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 */
#ifndef css_parse_colour_h_
#define css_parse_colour_h_

#include "parse/declaration_order.h"

/* Parse RGB/HSL/HWB/Lab/LCH/color(srgb) bodies through the closing parenthesis. The caller
 * restores ctx on failure. No allocation; rendering alone quantizes to ARGB8. */
bool css__parse_colour_function(lwc_string *name, const parserutils_vector *vector, int32_t *ctx, uint32_t *result);

/* Specified named/hex/RGB/HSL/HWB/Lab/LCH/color(srgb) colours and border-color components,
 * preserving missing components and applying each form's serialization range.
 * Other functions remain explicitly unsupported, as in declaration_value.h. */
css_error css__declaration_colour_serialize(const css_declaration_contribution *value, lwc_string **out);

#endif

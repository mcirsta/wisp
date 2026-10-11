/*
 * This file is part of LibCSS.
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 */
#ifndef css_parse_colour_h_
#define css_parse_colour_h_

#include "parse/declaration_order.h"

/* Parse the body of rgb()/rgba(), through its closing parenthesis. The caller
 * restores ctx on failure. No allocation; rendering alone quantizes to ARGB8. */
bool css__parse_rgb_colour(const parserutils_vector *vector, int32_t *ctx, uint32_t *result);

/* Specified named/hex/RGB colours and border-color components. Other colour
 * functions remain explicitly unsupported, as in declaration_value.h. */
css_error css__declaration_colour_serialize(const css_declaration_contribution *value, lwc_string **out);

#endif

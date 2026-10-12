/* Specified gradient values. Licensed under the MIT License. */
#ifndef css_parse_declaration_gradient_h_
#define css_parse_declaration_gradient_h_

#include "parse/declarations.h"

/* One already validated linear/radial gradient. Uses authored component tokens,
 * not the renderer's quantized colours, positions or cardinal directions. */
css_error css__declaration_gradient_serialize(const css_declaration *source, lwc_string **out);

#endif

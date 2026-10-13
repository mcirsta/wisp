/*
 * This file is part of LibCSS.
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 */

#ifndef css_parse_transform_h_
#define css_parse_transform_h_

#include <libwapcaplet/libwapcaplet.h>
#include "parse/declarations.h"

enum css_transform_argument { CSS_TRANSFORM_LENGTH, CSS_TRANSFORM_NUMBER, CSS_TRANSFORM_ANGLE };

struct css_transform_syntax {
    const char *name;
    uint32_t opcode;
    enum css_transform_argument argument;
    unsigned arity; /* Maximum; the second translate/scale argument is optional. */
};

const struct css_transform_syntax *css__transform_syntax(lwc_string *name);
css_error css__declaration_transform_serialize(const css_declaration *source, lwc_string **out);

#endif

/*
 * This file is part of LibCSS.
 * Licensed under the MIT License,
 *		  http://www.opensource.org/licenses/mit-license.php
 * Copyright 2009 John-Mark Bell <jmb@netsurf-browser.org>
 */

#include "bytecode/bytecode.h"
#include "bytecode/opcodes.h"
#include "parse/properties/properties.h"
#include "parse/properties/utils.h"

css_error css__parse_outline(css_language *c, const parserutils_vector *vector, int32_t *ctx, css_style *result)
{
    static const struct css_border_shorthand syntax = {
        {CSS_PROP_OUTLINE_COLOR, CSS_PROP_OUTLINE_STYLE, CSS_PROP_OUTLINE_WIDTH},
        {css__parse_outline_color, css__parse_outline_style, css__parse_outline_width},
        {OUTLINE_COLOR_INVERT, OUTLINE_STYLE_NONE, OUTLINE_WIDTH_MEDIUM},
    };
    return css__parse_border_shorthand(c, vector, ctx, result, &syntax);
}

/*
 * This file is part of LibCSS.
 * Licensed under the MIT License,
 *		  http://www.opensource.org/licenses/mit-license.php
 * Copyright 2012 Michael Drake <tlsa@netsurf-browser.org>
 */

#include "bytecode/bytecode.h"
#include "bytecode/opcodes.h"
#include "parse/properties/properties.h"
#include "parse/properties/utils.h"

css_error css__parse_column_rule(css_language *c, const parserutils_vector *vector, int32_t *ctx, css_style *result)
{
    static const struct css_border_shorthand syntax = {
        {CSS_PROP_COLUMN_RULE_COLOR, CSS_PROP_COLUMN_RULE_STYLE, CSS_PROP_COLUMN_RULE_WIDTH},
        {css__parse_column_rule_color, css__parse_column_rule_style, css__parse_column_rule_width},
        {COLUMN_RULE_COLOR_CURRENT_COLOR, COLUMN_RULE_STYLE_NONE, COLUMN_RULE_WIDTH_MEDIUM},
    };
    return css__parse_border_shorthand(c, vector, ctx, result, &syntax);
}

/*
 * This file is part of LibCSS.
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 * Copyright 2012 Michael Drake <tlsa@netsurf-browser.org>
 */

#include "bytecode/bytecode.h"
#include "parse/properties/properties.h"
#include "parse/properties/utils.h"

/* CSS Overflow 3: one value sets both axes; a second value sets overflow-y.
 * CSS-wide keywords must stand alone. Generated value-only helpers use the
 * same accepted keyword grammar as the corresponding longhand parsers. */
css_error css__parse_overflow(css_language *c, const parserutils_vector *vector, int32_t *ctx, css_style *result)
{
    int32_t original = *ctx;
    uint32_t original_used = result->used;
    const css_token *token = parserutils_vector_peek(vector, *ctx);
    if (token == NULL)
        return CSS_INVALID;
    enum flag_value flag = get_css_flag_value(c, token);
    css_error error;
    if (flag != FLAG_VALUE__NONE) {
        parserutils_vector_iterate(vector, ctx);
        error = css_stylesheet_style_flag_value(result, flag, CSS_PROP_OVERFLOW_X);
        if (error == CSS_OK)
            error = css_stylesheet_style_flag_value(result, flag, CSS_PROP_OVERFLOW_Y);
    } else {
        error = css__parse_overflow_x_value(c, vector, ctx, result);
        if (error != CSS_OK)
            goto fail;
        int32_t after_first = *ctx;
        consumeWhitespace(vector, ctx);
        token = parserutils_vector_peek(vector, *ctx);
        if (token == NULL || token->type == CSS_TOKEN_EOF || tokenIsChar(token, '!')) {
            int32_t replay = original;
            error = css__parse_overflow_y_value(c, vector, &replay, result);
            *ctx = after_first;
        } else {
            error = css__parse_overflow_y_value(c, vector, ctx, result);
        }
    }
    if (error == CSS_OK)
        return CSS_OK;
fail:
    *ctx = original;
    result->used = original_used;
    return error;
}

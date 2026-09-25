/*
 * This file is part of LibCSS.
 * Licensed under the MIT License, http://www.opensource.org/licenses/mit-license.php
 * Copyright 2009 John-Mark Bell <jmb@netsurf-browser.org>
 */
#include "parse/properties/properties.h"
#include "parse/properties/utils.h"

/* Parse one or two values using the ordinary side parsers. Replaying a single
 * value also handles CSS-wide keywords and calc bytecode without special cases.
 * Mapping follows the engine's current horizontal-tb logical property support. */
static css_error parse_pair(css_language *c, const parserutils_vector *vector, int32_t *ctx, css_style *result,
    css_prop_handler start, css_prop_handler end)
{
    int32_t original = *ctx;
    uint32_t original_used = result->used;
    const css_token *token = parserutils_vector_peek(vector, *ctx);
    if (token == NULL)
        return CSS_INVALID;
    bool wide_keyword = get_css_flag_value(c, token) != FLAG_VALUE__NONE;
    css_error error = start(c, vector, ctx, result);
    if (error != CSS_OK)
        goto fail;

    int32_t after_first = *ctx;
    consumeWhitespace(vector, ctx);
    token = parserutils_vector_peek(vector, *ctx);
    if (wide_keyword || token == NULL || token->type == CSS_TOKEN_EOF || tokenIsChar(token, '!')) {
        int32_t replay = original;
        error = end(c, vector, &replay, result);
        *ctx = after_first;
    } else if (get_css_flag_value(c, token) != FLAG_VALUE__NONE) {
        /* A CSS-wide keyword must be the whole shorthand value. */
        error = CSS_INVALID;
    } else {
        error = end(c, vector, ctx, result);
    }
    if (error == CSS_OK)
        return CSS_OK;
fail:
    *ctx = original;
    result->used = original_used;
    return error;
}

css_error css__parse_margin_block(css_language *c, const parserutils_vector *vector, int32_t *ctx, css_style *result)
{
    return parse_pair(c, vector, ctx, result, css__parse_margin_top, css__parse_margin_bottom);
}

css_error css__parse_padding_block(css_language *c, const parserutils_vector *vector, int32_t *ctx, css_style *result)
{
    return parse_pair(c, vector, ctx, result, css__parse_padding_top, css__parse_padding_bottom);
}

css_error css__parse_margin_inline(css_language *c, const parserutils_vector *vector, int32_t *ctx, css_style *result)
{
    return parse_pair(c, vector, ctx, result, css__parse_margin_left, css__parse_margin_right);
}

css_error css__parse_padding_inline(css_language *c, const parserutils_vector *vector, int32_t *ctx, css_style *result)
{
    return parse_pair(c, vector, ctx, result, css__parse_padding_left, css__parse_padding_right);
}

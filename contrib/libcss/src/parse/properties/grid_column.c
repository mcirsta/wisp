/*
 * This file is part of LibCSS.
 * Licensed under the MIT License,
 *		  http://www.opensource.org/licenses/mit-license.php
 * Copyright 2024 Neosurf CSS Grid Support
 */

#include <assert.h>
#include <string.h>

#include "bytecode/bytecode.h"
#include "bytecode/opcodes.h"
#include "parse/properties/properties.h"
#include "parse/properties/utils.h"

/**
 * Parse a single grid line value (auto or integer)
 *
 * \param c       Parsing context
 * \param vector  Vector of tokens to process
 * \param ctx     Pointer to vector iteration context
 * \param value   Pointer to receive value type (CSS_GRID_LINE_AUTO/SET)
 * \param integer Pointer to receive integer value (if SET)
 * \return CSS_OK on success, CSS_INVALID if not a valid grid line
 */
static css_error parse_grid_line_value(
    css_language *c, const parserutils_vector *vector, int32_t *ctx, uint16_t *value, css_fixed *integer)
{
    const css_token *token;
    bool match;

    token = parserutils_vector_peek(vector, *ctx);
    if (token == NULL) {
        return CSS_INVALID;
    }

    /* Check for 'auto' */
    if (token->type == CSS_TOKEN_IDENT &&
        lwc_string_caseless_isequal(token->idata, c->strings[AUTO], &match) == lwc_error_ok && match) {
        parserutils_vector_iterate(vector, ctx);
        *value = CSS_GRID_LINE_AUTO;
        *integer = 0;
        return CSS_OK;
    }

    /* Check for 'span' keyword (followed by optional integer) */
    if (token->type == CSS_TOKEN_IDENT &&
        lwc_string_caseless_isequal(token->idata, c->strings[SPAN], &match) == lwc_error_ok && match) {
        css_fixed span_count = INTTOFIX(1); /* default span 1 */

        parserutils_vector_iterate(vector, ctx);
        consumeWhitespace(vector, ctx);

        /* Check for optional integer after 'span' */
        token = parserutils_vector_peek(vector, *ctx);
        if (token != NULL && token->type == CSS_TOKEN_NUMBER) {
            size_t consumed = 0;
            span_count = css__number_from_lwc_string(token->idata, true, &consumed);

            /* Must be a positive integer */
            if (consumed != lwc_string_length(token->idata) || span_count <= 0) {
                return CSS_INVALID;
            }
            parserutils_vector_iterate(vector, ctx);
        }

        *value = CSS_GRID_LINE_SPAN;
        *integer = span_count;
        return CSS_OK;
    }

    /* Check for integer */
    if (token->type == CSS_TOKEN_NUMBER) {
        size_t consumed = 0;
        css_fixed num = css__number_from_lwc_string(token->idata, true, &consumed);

        /* Must be an integer (no fractional part) */
        if (consumed != lwc_string_length(token->idata)) {
            return CSS_INVALID;
        }

        /* Grid line numbers cannot be 0 */
        if (num == 0) {
            return CSS_INVALID;
        }

        parserutils_vector_iterate(vector, ctx);
        *value = CSS_GRID_LINE_SET;
        *integer = num;
        return CSS_OK;
    }

    return CSS_INVALID;
}

/**
 * Parse grid-column shorthand
 *
 * Syntax: grid-column: <grid-line> [ / <grid-line> ]?
 * Where <grid-line> = auto | <integer>
 *
 * If only one value is given, grid-column-end is set to auto.
 */
css_error css__parse_grid_column(css_language *c, const parserutils_vector *vector, int32_t *ctx, css_style *result)
{
    int32_t orig_ctx = *ctx;
    css_error error;
    const css_token *token;
    uint16_t start_value = CSS_GRID_LINE_AUTO;
    uint16_t end_value = CSS_GRID_LINE_AUTO;
    css_fixed start_integer = 0;
    css_fixed end_integer = 0;
    enum flag_value flag_value;

    token = parserutils_vector_peek(vector, *ctx);
    if (token == NULL) {
        *ctx = orig_ctx;
        return CSS_INVALID;
    }

    /* Check for inherit/initial/unset/revert */
    flag_value = get_css_flag_value(c, token);
    if (flag_value != FLAG_VALUE__NONE) {
        parserutils_vector_iterate(vector, ctx);
        error = css_stylesheet_style_flag_value(result, flag_value, CSS_PROP_GRID_COLUMN_START);
        if (error != CSS_OK) {
            *ctx = orig_ctx;
            return error;
        }
        error = css_stylesheet_style_flag_value(result, flag_value, CSS_PROP_GRID_COLUMN_END);
        if (error != CSS_OK)
            *ctx = orig_ctx;
        return error;
    }

    /* Parse start value */
    error = parse_grid_line_value(c, vector, ctx, &start_value, &start_integer);
    if (error != CSS_OK) {
        *ctx = orig_ctx;
        return error;
    }

    consumeWhitespace(vector, ctx);

    /* Check for '/' separator */
    token = parserutils_vector_peek(vector, *ctx);
    if (token != NULL && tokenIsChar(token, '/')) {
        parserutils_vector_iterate(vector, ctx);
        consumeWhitespace(vector, ctx);

        /* Parse end value */
        error = parse_grid_line_value(c, vector, ctx, &end_value, &end_integer);
        if (error != CSS_OK) {
            *ctx = orig_ctx;
            return error;
        }
    }
    /* If no '/', end defaults to auto (already set) */

    /* Emit grid-column-start */
    error = css__stylesheet_style_appendOPV(result, CSS_PROP_GRID_COLUMN_START, 0, start_value);
    if (error != CSS_OK) {
        *ctx = orig_ctx;
        return error;
    }
    if (start_value == CSS_GRID_LINE_SET || start_value == CSS_GRID_LINE_SPAN) {
        error = css__stylesheet_style_append(result, start_integer);
        if (error != CSS_OK) {
            *ctx = orig_ctx;
            return error;
        }
    }

    /* Emit grid-column-end */
    error = css__stylesheet_style_appendOPV(result, CSS_PROP_GRID_COLUMN_END, 0, end_value);
    if (error != CSS_OK) {
        *ctx = orig_ctx;
        return error;
    }
    if (end_value == CSS_GRID_LINE_SET || end_value == CSS_GRID_LINE_SPAN) {
        error = css__stylesheet_style_append(result, end_integer);
        if (error != CSS_OK)
            *ctx = orig_ctx;
    }

    return error;
}

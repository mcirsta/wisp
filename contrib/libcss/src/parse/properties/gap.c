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
 * Parse gap shorthand
 *
 * Syntax: gap: <row-gap> <column-gap>?
 * If only one value, it applies to both row-gap and column-gap.
 *
 * \param c       Parsing context
 * \param vector  Vector of tokens to process
 * \param ctx     Pointer to vector iteration context
 * \param result  Pointer to location to receive resulting style
 * \return CSS_OK on success,
 *         CSS_NOMEM on memory exhaustion,
 *         CSS_INVALID if the input is not valid
 *
 * Post condition: \a *ctx is updated with the next token to process
 *                 If the input is invalid, then \a *ctx remains unchanged.
 */
css_error css__parse_gap(css_language *c, const parserutils_vector *vector, int32_t *ctx, css_style *result)
{
    int32_t orig_ctx = *ctx;
    css_error error;
    const css_token *token;
    enum flag_value flag_value;

    /* gap: <length-percentage> <length-percentage>? | normal | inherit */
    token = parserutils_vector_peek(vector, *ctx);
    if (token == NULL) {
        *ctx = orig_ctx;
        return CSS_INVALID;
    }

    flag_value = get_css_flag_value(c, token);

    if (flag_value != FLAG_VALUE__NONE) {
        /* inherit/initial/unset/revert */
        parserutils_vector_iterate(vector, ctx);
        error = css_stylesheet_style_flag_value(result, flag_value, CSS_PROP_ROW_GAP);
        if (error != CSS_OK) {
            *ctx = orig_ctx;
            return error;
        }
        error = css_stylesheet_style_flag_value(result, flag_value, CSS_PROP_COLUMN_GAP);
        if (error != CSS_OK)
            *ctx = orig_ctx;
        return error;
    }

    /* Parse first value (row-gap) via auto-generated value helper.
     * Handles: normal keyword, calc(), plain length/percentage. */
    int32_t saved_ctx = *ctx;
    error = css__parse_row_gap_value(c, vector, ctx, result);
    if (error != CSS_OK) {
        *ctx = orig_ctx;
        return error;
    }

    /* Save position after first value */
    int32_t after_first = *ctx;
    consumeWhitespace(vector, ctx);

    /* Try to parse second value (column-gap) */
    token = parserutils_vector_peek(vector, *ctx);
    if (token != NULL && token->type != CSS_TOKEN_EOF) {
        int32_t temp_ctx = *ctx;
        error = css__parse_column_gap_value(c, vector, &temp_ctx, result);
        if (error == CSS_OK) {
            *ctx = temp_ctx;
            return CSS_OK;
        }
    }

    /* Only one value: replay the same tokens for column-gap */
    int32_t replay_ctx = saved_ctx;
    error = css__parse_column_gap_value(c, vector, &replay_ctx, result);
    if (error != CSS_OK) {
        *ctx = orig_ctx;
        return error;
    }
    *ctx = after_first;
    return CSS_OK;
}

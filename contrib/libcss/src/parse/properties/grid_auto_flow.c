/*
 * This file is part of LibCSS
 * Licensed under the MIT License,
 *		  http://www.opensource.org/licenses/mit-license.php
 * Copyright 2024 CSS Grid Support
 */

#include <assert.h>
#include <string.h>

#include "bytecode/bytecode.h"
#include "bytecode/opcodes.h"
#include "parse/properties/properties.h"
#include "parse/properties/utils.h"

/**
 * Parse grid-auto-flow property
 *
 * CSS Syntax:
 *   grid-auto-flow: row | column | dense | row dense | column dense
 *
 * The lexer tokenizes "row dense" as two separate IDENT tokens,
 * so we need a manual parser to handle the two-token case.
 */
css_error css__parse_grid_auto_flow(css_language *c, const parserutils_vector *vector, int32_t *ctx, css_style *result)
{
    int32_t orig_ctx = *ctx;
    const css_token *token;
    bool match;
    uint16_t value = 0;

    token = parserutils_vector_iterate(vector, ctx);
    if (token == NULL || token->type != CSS_TOKEN_IDENT) {
        *ctx = orig_ctx;
        return CSS_INVALID;
    }

    /* Check for inherit/initial/revert/unset first */
    if (lwc_string_caseless_isequal(token->idata, c->strings[INHERIT], &match) == lwc_error_ok && match) {
        return css_stylesheet_style_inherit(result, CSS_PROP_GRID_AUTO_FLOW);
    } else if (lwc_string_caseless_isequal(token->idata, c->strings[INITIAL], &match) == lwc_error_ok && match) {
        return css__stylesheet_style_appendOPV(result, CSS_PROP_GRID_AUTO_FLOW, 0, GRID_AUTO_FLOW_ROW);
    } else if (lwc_string_caseless_isequal(token->idata, c->strings[REVERT], &match) == lwc_error_ok && match) {
        return css_stylesheet_style_revert(result, CSS_PROP_GRID_AUTO_FLOW);
    } else if (lwc_string_caseless_isequal(token->idata, c->strings[UNSET], &match) == lwc_error_ok && match) {
        return css__stylesheet_style_appendOPV(result, CSS_PROP_GRID_AUTO_FLOW, FLAG_VALUE_UNSET, 0);
    }

    /* Parse first keyword: row | column | dense */
    if (lwc_string_caseless_isequal(token->idata, c->strings[ROW], &match) == lwc_error_ok && match) {
        value = GRID_AUTO_FLOW_ROW;
    } else if (lwc_string_caseless_isequal(token->idata, c->strings[COLUMN], &match) == lwc_error_ok && match) {
        value = GRID_AUTO_FLOW_COLUMN;
    } else if (lwc_string_caseless_isequal(token->idata, c->strings[DENSE], &match) == lwc_error_ok && match) {
        /* "dense" alone is equivalent to "row dense" per CSS spec */
        value = GRID_AUTO_FLOW_ROW_DENSE;
    } else {
        *ctx = orig_ctx;
        return CSS_INVALID;
    }

    /* Check for optional second keyword: dense */
    consumeWhitespace(vector, ctx);
    token = parserutils_vector_peek(vector, *ctx);

    if (token != NULL && token->type == CSS_TOKEN_IDENT) {
        if (lwc_string_caseless_isequal(token->idata, c->strings[DENSE], &match) == lwc_error_ok && match) {
            parserutils_vector_iterate(vector, ctx); /* consume "dense" */

            if (value == GRID_AUTO_FLOW_ROW) {
                value = GRID_AUTO_FLOW_ROW_DENSE;
            } else if (value == GRID_AUTO_FLOW_COLUMN) {
                value = GRID_AUTO_FLOW_COLUMN_DENSE;
            }
            /* If already dense, stay as row_dense */
        }
    }

    return css__stylesheet_style_appendOPV(result, CSS_PROP_GRID_AUTO_FLOW, 0, value);
}

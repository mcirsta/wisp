/*
 * This file is part of LibCSS.
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 */

#include "bytecode/bytecode.h"
#include "bytecode/opcodes.h"
#include "parse/properties/properties.h"
#include "parse/properties/utils.h"

/* CSS Grid 2 section 7.7: [ row | column ] || dense, in either order and
 * without duplicate components. An omitted axis defaults to row. */
css_error css__parse_grid_auto_flow(css_language *c, const parserutils_vector *vector, int32_t *ctx, css_style *result)
{
    int32_t start = *ctx;
    const css_token *token = parserutils_vector_peek(vector, *ctx);
    if (token == NULL)
        return CSS_INVALID;
    enum flag_value flag = get_css_flag_value(c, token);
    css_error error;
    if (flag != FLAG_VALUE__NONE) {
        parserutils_vector_iterate(vector, ctx);
        error = css_stylesheet_style_flag_value(result, flag, CSS_PROP_GRID_AUTO_FLOW);
    } else {
        bool axis = false, column = false, dense = false;
        while (token != NULL && token->type == CSS_TOKEN_IDENT) {
            if (!axis && (css__keyword_equal(token->idata, "row") || css__keyword_equal(token->idata, "column"))) {
                axis = true;
                column = css__keyword_equal(token->idata, "column");
            } else if (!dense && css__keyword_equal(token->idata, "dense")) {
                dense = true;
            } else {
                *ctx = start;
                return CSS_INVALID;
            }
            parserutils_vector_iterate(vector, ctx);
            consumeWhitespace(vector, ctx);
            token = parserutils_vector_peek(vector, *ctx);
        }
        if (!axis && !dense) {
            *ctx = start;
            return CSS_INVALID;
        }
        uint16_t value = column ? (dense ? GRID_AUTO_FLOW_COLUMN_DENSE : GRID_AUTO_FLOW_COLUMN) :
            (dense ? GRID_AUTO_FLOW_ROW_DENSE : GRID_AUTO_FLOW_ROW);
        error = css__stylesheet_style_appendOPV(result, CSS_PROP_GRID_AUTO_FLOW, 0, value);
    }
    if (error != CSS_OK)
        *ctx = start;
    return error;
}

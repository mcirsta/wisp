/*
 * This file is part of LibCSS.
 * Licensed under the MIT License,
 *		  http://www.opensource.org/licenses/mit-license.php
 * Copyright 2009 John-Mark Bell <jmb@netsurf-browser.org>
 */

#include <assert.h>
#include <string.h>

#include "bytecode/bytecode.h"
#include "bytecode/opcodes.h"
#include "parse/font.h"
#include "parse/properties/properties.h"
#include "parse/properties/utils.h"

/**
 * Parse font-weight
 *
 * \param c	  Parsing context
 * \param vector  Vector of tokens to process
 * \param ctx	  Pointer to vector iteration context
 * \param result  Pointer to location to receive resulting style
 * \return CSS_OK on success,
 *	   CSS_NOMEM on memory exhaustion,
 *	   CSS_INVALID if the input is not valid
 *
 * Post condition: \a *ctx is updated with the next token to process
 *		   If the input is invalid, then \a *ctx remains unchanged.
 */
css_error css__parse_font_weight(css_language *c, const parserutils_vector *vector, int32_t *ctx, css_style *result)
{
    int32_t orig_ctx = *ctx;
    css_error error;
    const css_token *token;
    token = parserutils_vector_iterate(vector, ctx);
    if (token == NULL)
        return CSS_INVALID;
    enum flag_value flag = get_css_flag_value(c, token);
    if (flag != FLAG_VALUE__NONE) {
        error = css_stylesheet_style_flag_value(result, flag, CSS_PROP_FONT_WEIGHT);
    } else {
        int value = css__font_weight_value(token->type, token->idata);
        error = value < 0 ? CSS_INVALID :
            css__stylesheet_style_appendOPV(result, CSS_PROP_FONT_WEIGHT, 0, value);
    }
    if (error != CSS_OK)
        *ctx = orig_ctx;
    return error;
}

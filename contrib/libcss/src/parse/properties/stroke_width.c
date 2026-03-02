/*
 * Licensed under the MIT License,
 *		  http://www.opensource.org/licenses/mit-license.php
 */

#include <assert.h>
#include <string.h>

#include "bytecode/bytecode.h"
#include "bytecode/opcodes.h"
#include "parse/properties/properties.h"
#include "parse/properties/utils.h"

/**
 * Parse stroke-width
 *
 * \param c	  Parsing context
 * \param vector  Vector of tokens to process
 * \param ctx	  Pointer to vector iteration context
 * \param result  resulting style
 * \return CSS_OK on success,
 *	   CSS_NOMEM on memory exhaustion,
 *	   CSS_INVALID if the input is not valid
 *
 * Post condition: \a *ctx is updated with the next token to process
 *		   If the input is invalid, then \a *ctx remains unchanged.
 */
css_error css__parse_stroke_width(css_language *c, const parserutils_vector *vector, int32_t *ctx, css_style *result)
{
    int32_t orig_ctx = *ctx;
    css_error error;
    const css_token *token;
    enum flag_value flag_value;

    token = parserutils_vector_iterate(vector, ctx);
    if (token == NULL) {
        *ctx = orig_ctx;
        return CSS_INVALID;
    }

    flag_value = get_css_flag_value(c, token);

    if (flag_value != FLAG_VALUE__NONE) {
        error = css_stylesheet_style_flag_value(result, flag_value, CSS_PROP_STROKE_WIDTH);

    } else {
        css_fixed length = 0;
        uint32_t unit = 0;

        *ctx = orig_ctx;

        error = css__parse_unit_specifier(c, vector, ctx, UNIT_PX, &length, &unit);
        if (error != CSS_OK) {
            *ctx = orig_ctx;
            return error;
        }

        if (length < 0) {
            *ctx = orig_ctx;
            return CSS_INVALID;
        }

        error = css__stylesheet_style_appendOPV(result, CSS_PROP_STROKE_WIDTH, 0, STROKE_WIDTH_SET);
        if (error != CSS_OK) {
            *ctx = orig_ctx;
            return error;
        }

        error = css__stylesheet_style_vappend(result, 2, length, unit);
    }

    if (error != CSS_OK)
        *ctx = orig_ctx;

    return error;
}

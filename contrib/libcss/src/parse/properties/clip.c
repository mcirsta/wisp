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
#include "parse/properties/properties.h"
#include "parse/properties/utils.h"

/**
 * Parse clip
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
css_error css__parse_clip(css_language *c, const parserutils_vector *vector, int32_t *ctx, css_style *result)
{
    int32_t orig_ctx = *ctx;
    css_error error;
    const css_token *token;
    int num_lengths = 0;
    css_fixed length[4] = {0};
    uint32_t unit[4] = {0};
    enum flag_value flag_value;
    bool match;
    bool commas = false;

    /* FUNCTION(rect) [ [ IDENT(auto) | length ] CHAR(,)? ]{3}
     *                [ IDENT(auto) | length ] CHAR{)} |
     * IDENT(auto, inherit) */
    token = parserutils_vector_iterate(vector, ctx);
    if (token == NULL) {
        *ctx = orig_ctx;
        return CSS_INVALID;
    }

    flag_value = get_css_flag_value(c, token);

    if (flag_value != FLAG_VALUE__NONE) {
        error = css_stylesheet_style_flag_value(result, flag_value, CSS_PROP_CLIP);

    } else if ((token->type == CSS_TOKEN_IDENT) &&
        (lwc_string_caseless_isequal(token->idata, c->strings[AUTO], &match) == lwc_error_ok && match)) {
        error = css__stylesheet_style_appendOPV(result, CSS_PROP_CLIP, 0, CLIP_AUTO);
    } else if ((token->type == CSS_TOKEN_FUNCTION) &&
        (lwc_string_caseless_isequal(token->idata, c->strings[RECT], &match) == lwc_error_ok && match)) {
        int i;
        uint16_t value = CLIP_SHAPE_RECT;

        for (i = 0; i < 4; i++) {
            consumeWhitespace(vector, ctx);

            token = parserutils_vector_peek(vector, *ctx);
            if (token == NULL) {
                *ctx = orig_ctx;
                return CSS_INVALID;
            }

            if (token->type == CSS_TOKEN_IDENT) {
                /* Slightly magical way of generating the auto
                 * values. These are bits 3-6 of the value. */
                if ((lwc_string_caseless_isequal(token->idata, c->strings[AUTO], &match) == lwc_error_ok && match))
                    value |= 1 << (i + 3);
                else {
                    *ctx = orig_ctx;
                    return CSS_INVALID;
                }

                parserutils_vector_iterate(vector, ctx);
            } else {
                error = css__parse_length(c, vector, ctx, false, &length[num_lengths], &unit[num_lengths]);
                if (error != CSS_OK) {
                    *ctx = orig_ctx;
                    return error;
                }

                num_lengths++;
            }

            consumeWhitespace(vector, ctx);

            /* CSS 2.2 §11.1.2 allows commas or the legacy space-separated
             * syntax, but explicitly forbids mixing the two. */
            if (i < 3) {
                token = parserutils_vector_peek(vector, *ctx);
                if (token == NULL) {
                    *ctx = orig_ctx;
                    return CSS_INVALID;
                }

                bool comma = tokenIsChar(token, ',');
                if (i == 0) {
                    commas = comma;
                } else if (comma != commas) {
                    *ctx = orig_ctx;
                    return CSS_INVALID;
                }
                if (comma)
                    parserutils_vector_iterate(vector, ctx);
            }
        }

        consumeWhitespace(vector, ctx);

        /* Finally, consume closing parenthesis */
        token = parserutils_vector_iterate(vector, ctx);
        if (token == NULL || tokenIsChar(token, ')') == false) {
            *ctx = orig_ctx;
            return CSS_INVALID;
        }

        /* output bytecode */
        error = css__stylesheet_style_appendOPV(result, CSS_PROP_CLIP, 0, value);
        if (error != CSS_OK) {
            *ctx = orig_ctx;
            return error;
        }

        for (i = 0; i < num_lengths; i++) {
            error = css__stylesheet_style_vappend(result, 2, length[i], unit[i]);
            if (error != CSS_OK)
                break;
        }


    } else {
        error = CSS_INVALID;
    }

    if (error != CSS_OK) {
        *ctx = orig_ctx;
    }

    return error;
}

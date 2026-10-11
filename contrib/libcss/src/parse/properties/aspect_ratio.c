/* This file is part of LibCSS. Licensed under the MIT License. */
#include "bytecode/bytecode.h"
#include "bytecode/opcodes.h"
#include "parse/properties/properties.h"
#include "parse/properties/utils.h"

static bool ratio_number(const css_token *token, css_fixed *value)
{
    size_t consumed = 0;
    if (token == NULL || token->type != CSS_TOKEN_NUMBER) {
        return false;
    }
    *value = css__number_from_lwc_string(token->idata, false, &consumed);
    return consumed == lwc_string_length(token->idata) && css__number_sign(token->idata) >= 0;
}

css_error css__parse_aspect_ratio(css_language *c, const parserutils_vector *vector, int32_t *ctx, css_style *result)
{
    int32_t orig = *ctx;
    const css_token *token = parserutils_vector_peek(vector, *ctx);
    if (token == NULL) {
        return CSS_INVALID;
    }
    enum flag_value flag = get_css_flag_value(c, token);
    if (flag != FLAG_VALUE__NONE) {
        parserutils_vector_iterate(vector, ctx);
        return css_stylesheet_style_flag_value(result, flag, CSS_PROP_ASPECT_RATIO);
    }

    bool automatic = false, ratio = false;
    css_fixed width = 0, height = INTTOFIX(1);
    /* auto || <number [0,infinity]> [ / <number [0,infinity]> ]? */
    for (int i = 0; i < 2; i++) {
        bool match = false;
        token = parserutils_vector_peek(vector, *ctx);
        if (!automatic && token && token->type == CSS_TOKEN_IDENT &&
            lwc_string_caseless_isequal(token->idata, c->strings[AUTO], &match) == lwc_error_ok && match) {
            automatic = true;
            parserutils_vector_iterate(vector, ctx);
        } else if (!ratio && ratio_number(token, &width)) {
            ratio = true;
            parserutils_vector_iterate(vector, ctx);
            consumeWhitespace(vector, ctx);
            token = parserutils_vector_peek(vector, *ctx);
            if (token && token->type == CSS_TOKEN_CHAR && lwc_string_length(token->idata) == 1 &&
                lwc_string_data(token->idata)[0] == '/') {
                parserutils_vector_iterate(vector, ctx);
                consumeWhitespace(vector, ctx);
                if (!ratio_number(parserutils_vector_peek(vector, *ctx), &height)) {
                    *ctx = orig;
                    return CSS_INVALID;
                }
                parserutils_vector_iterate(vector, ctx);
            }
        } else {
            break;
        }
        consumeWhitespace(vector, ctx);
    }
    if (!automatic && !ratio) {
        *ctx = orig;
        return CSS_INVALID;
    }
    uint16_t op = ratio ? (automatic ? ASPECT_RATIO_AUTO_RATIO : ASPECT_RATIO_RATIO) : ASPECT_RATIO_AUTO;
    css_error error = css__stylesheet_style_appendOPV(result, CSS_PROP_ASPECT_RATIO, 0, op);
    if (error == CSS_OK && ratio) {
        error = css__stylesheet_style_vappend(result, 2, width, height);
    }
    if (error != CSS_OK) {
        *ctx = orig;
    }
    return error;
}

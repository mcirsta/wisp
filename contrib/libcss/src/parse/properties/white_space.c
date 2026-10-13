/* CSS Text 4 white-space shorthand. Licensed under the MIT License. */
#include "bytecode/bytecode.h"
#include "parse/properties/utils.h"
#include "parse/white_space.h"

css_error css__parse_white_space(css_language *c, const parserutils_vector *v, int32_t *ctx, css_style *result)
{
    int32_t original = *ctx;
    uint32_t used = result->used;
    const css_token *token = parserutils_vector_peek(v, *ctx);
    if (token == NULL || token->type != CSS_TOKEN_IDENT)
        return CSS_INVALID;
    enum flag_value flag = get_css_flag_value(c, token);
    uint16_t values[2];
    if (flag != FLAG_VALUE__NONE) {
        parserutils_vector_iterate(v, ctx);
    } else {
        lwc_string *parts[2];
        size_t count = 0;
        while (token != NULL && token->type == CSS_TOKEN_IDENT && count < 2) {
            parts[count++] = token->idata;
            parserutils_vector_iterate(v, ctx);
            consumeWhitespace(v, ctx);
            token = parserutils_vector_peek(v, *ctx);
        }
        if (!css__white_space_values(parts, count, values)) {
            *ctx = original;
            return CSS_INVALID;
        }
    }
    const opcode_t properties[] = {CSS_PROP_WHITE_SPACE_COLLAPSE, CSS_PROP_TEXT_WRAP_MODE};
    css_error error = CSS_OK;
    for (size_t i = 0; error == CSS_OK && i < 2; i++) {
        error = flag != FLAG_VALUE__NONE ? css_stylesheet_style_flag_value(result, flag, properties[i]) :
            css__stylesheet_style_appendOPV(result, properties[i], 0, values[i]);
    }
    if (error != CSS_OK) {
        *ctx = original;
        result->used = used;
    }
    return error;
}

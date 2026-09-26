/* Background keyword lists. Licensed under the MIT License. */
#include "bytecode/bytecode.h"
#include "bytecode/opcodes.h"
#include "parse/properties/background.h"
#include "parse/properties/utils.h"

css_error
css__parse_background_repeat_item(css_language *c, const parserutils_vector *v, int32_t *ctx, css_style *result)
{
    const css_token *token = parserutils_vector_peek(v, *ctx);
    bool match;
    if (token == NULL || token->type != CSS_TOKEN_IDENT)
        return CSS_INVALID;
    enum flag_value flag = get_css_flag_value(c, token);
    css_error error;
    if (flag != FLAG_VALUE__NONE) {
        error = css_stylesheet_style_flag_value(result, flag, CSS_PROP_BACKGROUND_REPEAT);
    } else if (lwc_string_caseless_isequal(token->idata, c->strings[NO_REPEAT], &match) == lwc_error_ok && match) {
        error = css__stylesheet_style_appendOPV(result, CSS_PROP_BACKGROUND_REPEAT, 0, BACKGROUND_REPEAT_NO_REPEAT);
    } else if (lwc_string_caseless_isequal(token->idata, c->strings[REPEAT_X], &match) == lwc_error_ok && match) {
        error = css__stylesheet_style_appendOPV(result, CSS_PROP_BACKGROUND_REPEAT, 0, BACKGROUND_REPEAT_REPEAT_X);
    } else if (lwc_string_caseless_isequal(token->idata, c->strings[REPEAT_Y], &match) == lwc_error_ok && match) {
        error = css__stylesheet_style_appendOPV(result, CSS_PROP_BACKGROUND_REPEAT, 0, BACKGROUND_REPEAT_REPEAT_Y);
    } else if (lwc_string_caseless_isequal(token->idata, c->strings[REPEAT], &match) == lwc_error_ok && match) {
        error = css__stylesheet_style_appendOPV(result, CSS_PROP_BACKGROUND_REPEAT, 0, BACKGROUND_REPEAT_REPEAT);
    } else {
        return CSS_INVALID;
    }
    if (error == CSS_OK)
        parserutils_vector_iterate(v, ctx);
    return error;
}

css_error css__parse_background_repeat(css_language *c, const parserutils_vector *v, int32_t *ctx, css_style *result)
{
    return css__parse_background_list(c, v, ctx, result, CSS_PROP_BACKGROUND_REPEAT, css__parse_background_repeat_item);
}

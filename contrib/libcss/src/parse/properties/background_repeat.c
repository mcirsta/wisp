/* Background keyword lists. Licensed under the MIT License. */
#include <assert.h>
#include "bytecode/bytecode.h"
#include "bytecode/opcodes.h"
#include "parse/properties/background.h"
#include "parse/properties/utils.h"
#include "parse/repeat.h"

css_error
css__parse_background_repeat_item(css_language *c, const parserutils_vector *v, int32_t *ctx, css_style *result)
{
    const css_token *token = parserutils_vector_peek(v, *ctx);
    if (token == NULL || token->type != CSS_TOKEN_IDENT)
        return CSS_INVALID;
    int32_t original = *ctx;
    enum flag_value flag = get_css_flag_value(c, token);
    css_error error;
    if (flag != FLAG_VALUE__NONE) {
        error = css_stylesheet_style_flag_value(result, flag, CSS_PROP_BACKGROUND_REPEAT_X);
        if (error == CSS_OK)
            error = css_stylesheet_style_flag_value(result, flag, CSS_PROP_BACKGROUND_REPEAT_Y);
    } else {
        int value = css__repeat_keyword(token->idata);
        if (value < 0)
            return CSS_INVALID;
        parserutils_vector_iterate(v, ctx);
        consumeWhitespace(v, ctx);
        token = parserutils_vector_peek(v, *ctx);
        if (css__repeat_axis(value) && token != NULL && token->type == CSS_TOKEN_IDENT) {
            int second = css__repeat_keyword(token->idata);
            if (css__repeat_axis(second)) {
                value = css__repeat_pair(value, second);
                parserutils_vector_iterate(v, ctx);
            }
        }
        error = css__stylesheet_style_appendOPV(result, CSS_PROP_BACKGROUND_REPEAT_X, 0,
            value & BACKGROUND_REPEAT_REPEAT_X ? BACKGROUND_REPEAT_REPEAT : BACKGROUND_REPEAT_NO_REPEAT);
        if (error == CSS_OK)
            error = css__stylesheet_style_appendOPV(result, CSS_PROP_BACKGROUND_REPEAT_Y, 0,
                value & BACKGROUND_REPEAT_REPEAT_Y ? BACKGROUND_REPEAT_REPEAT : BACKGROUND_REPEAT_NO_REPEAT);
    }
    if (error != CSS_OK)
        *ctx = original;
    else if (flag != FLAG_VALUE__NONE)
        parserutils_vector_iterate(v, ctx);
    return error;
}

css_error css__parse_background_repeat(css_language *c, const parserutils_vector *v, int32_t *ctx, css_style *result)
{
    return css__parse_background_list(
        c, v, ctx, result, css__parse_background_repeat_item, css__background_repeat_append);
}

/* Each parsed layer supplies X/Y scalars. Store independent lists, so a later
 * longhand can replace one axis without changing the other's list length. */
css_error css__background_repeat_append(css_style *result, css_style *items, uint32_t count)
{
    assert(count > 0 && items->used == 2 * count);
    if (count == 1)
        return css__stylesheet_merge_style(result, items);
    for (unsigned axis = 0; axis < 2; axis++) {
        css_error error = css__stylesheet_style_appendOPV(result, getOpcode(items->bytecode[axis]), 0,
            BACKGROUND_LAYER_LIST);
        if (error == CSS_OK)
            error = css__stylesheet_style_append(result, count + 1);
        if (error == CSS_OK)
            error = css__stylesheet_style_append(result, count);
        if (error != CSS_OK)
            return error;
        for (uint32_t i = 0; i < count; i++) {
            error = css__stylesheet_style_append(result, items->bytecode[2 * i + axis]);
            if (error != CSS_OK)
                return error;
        }
    }
    return CSS_OK;
}

static css_error parse_axis(css_language *c, const parserutils_vector *v, int32_t *ctx, css_style *result, opcode_t op)
{
    const css_token *token = parserutils_vector_peek(v, *ctx);
    if (token == NULL || token->type != CSS_TOKEN_IDENT)
        return CSS_INVALID;
    enum flag_value flag = get_css_flag_value(c, token);
    css_error error;
    if (flag != FLAG_VALUE__NONE) {
        error = css_stylesheet_style_flag_value(result, flag, op);
    } else {
        int value = css__repeat_keyword(token->idata);
        if (!css__repeat_axis(value))
            return CSS_INVALID;
        error = css__stylesheet_style_appendOPV(result, op, 0, value);
    }
    if (error == CSS_OK)
        parserutils_vector_iterate(v, ctx);
    return error;
}

static css_error parse_x(css_language *c, const parserutils_vector *v, int32_t *ctx, css_style *result)
{
    return parse_axis(c, v, ctx, result, CSS_PROP_BACKGROUND_REPEAT_X);
}

static css_error parse_y(css_language *c, const parserutils_vector *v, int32_t *ctx, css_style *result)
{
    return parse_axis(c, v, ctx, result, CSS_PROP_BACKGROUND_REPEAT_Y);
}

css_error css__parse_background_repeat_x(css_language *c, const parserutils_vector *v, int32_t *ctx, css_style *result)
{
    return css__parse_background_list(c, v, ctx, result, parse_x, css__background_list_append);
}

css_error css__parse_background_repeat_y(css_language *c, const parserutils_vector *v, int32_t *ctx, css_style *result)
{
    return css__parse_background_list(c, v, ctx, result, parse_y, css__background_list_append);
}

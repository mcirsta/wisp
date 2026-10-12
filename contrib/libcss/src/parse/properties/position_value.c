/*
 * This file is part of LibCSS. Licensed under the MIT License.
 * Copyright 2009 John-Mark Bell <jmb@netsurf-browser.org>
 * Copyright 2024 Marius
 */
#include "bytecode/bytecode.h"
#include "parse/position.h"
#include "parse/properties/background.h"
#include "parse/properties/utils.h"

/* Background and object position share this encoding (see opcodes_manual.h). */
static uint16_t position_code(enum css_position_part part, bool horizontal)
{
    switch (part) {
    case CSS_POSITION_LEFT: return BACKGROUND_POSITION_HORZ_LEFT;
    case CSS_POSITION_RIGHT: return BACKGROUND_POSITION_HORZ_RIGHT;
    case CSS_POSITION_TOP: return BACKGROUND_POSITION_VERT_TOP;
    case CSS_POSITION_BOTTOM: return BACKGROUND_POSITION_VERT_BOTTOM;
    case CSS_POSITION_OFFSET: return horizontal ? BACKGROUND_POSITION_HORZ_SET : BACKGROUND_POSITION_VERT_SET;
    default: return 0; /* center */
    }
}

/* Collect at most two components before emitting, including when used inside
 * background's unordered grammar. The caller validates any remaining tokens. */
static css_error parse_position(css_language *c, const parserutils_vector *vector, int32_t *ctx,
    css_style *result, enum css_properties_e property)
{
    int32_t original = *ctx;
    uint32_t original_used = result->used;
    const css_token *token = parserutils_vector_peek(vector, *ctx);
    if (token == NULL)
        return CSS_INVALID;
    enum flag_value flag = get_css_flag_value(c, token);
    css_error error;
    if (flag != FLAG_VALUE__NONE) {
        parserutils_vector_iterate(vector, ctx);
        error = css_stylesheet_style_flag_value(result, flag, property);
    } else {
        enum css_position_part parts[3] = {CSS_POSITION_CENTER, CSS_POSITION_CENTER, CSS_POSITION_CENTER};
        css_fixed length[2] = {0};
        uint32_t unit[2] = {0};
        size_t count = 0, axes[2];
        for (; count < 2; count++) {
            token = parserutils_vector_peek(vector, *ctx);
            if (token == NULL)
                break;
            if (token->type == CSS_TOKEN_IDENT) {
                enum css_position_part part = css__position_keyword(token->idata);
                if (part == CSS_POSITION_INVALID)
                    break;
                parts[count] = part;
                parserutils_vector_iterate(vector, ctx);
            } else if (token->type == CSS_TOKEN_NUMBER || token->type == CSS_TOKEN_DIMENSION ||
                token->type == CSS_TOKEN_PERCENTAGE) {
                error = css__parse_unit_specifier(c, vector, ctx, UNIT_PX, &length[count], &unit[count]);
                if (error != CSS_OK)
                    goto fail;
                if ((!(unit[count] & UNIT_LENGTH) && unit[count] != UNIT_PCT) || unit[count] == UNIT_FR) {
                    error = CSS_INVALID;
                    goto fail;
                }
                parts[count] = CSS_POSITION_OFFSET;
            } else {
                break;
            }
            consumeWhitespace(vector, ctx);
        }
        if (!css__position_axes(parts, count, axes)) {
            error = CSS_INVALID;
            goto fail;
        }
        uint16_t value = position_code(parts[axes[0]], true) | position_code(parts[axes[1]], false);
        error = css__stylesheet_style_appendOPV(result, property, 0, value);
        for (size_t axis = 0; error == CSS_OK && axis < 2; axis++) {
            size_t index = axes[axis];
            if (parts[index] == CSS_POSITION_OFFSET)
                error = css__stylesheet_style_vappend(result, 2, length[index], unit[index]);
        }
    }
    if (error == CSS_OK)
        return CSS_OK;
fail:
    *ctx = original;
    result->used = original_used;
    return error;
}

css_error css__parse_background_position_item(css_language *c, const parserutils_vector *v, int32_t *ctx, css_style *result)
{
    return parse_position(c, v, ctx, result, CSS_PROP_BACKGROUND_POSITION);
}

css_error css__parse_background_position(css_language *c, const parserutils_vector *v, int32_t *ctx, css_style *result)
{
    return css__parse_background_list(c, v, ctx, result, CSS_PROP_BACKGROUND_POSITION, css__parse_background_position_item);
}

css_error css__parse_object_position(css_language *c, const parserutils_vector *v, int32_t *ctx, css_style *result)
{
    return parse_position(c, v, ctx, result, CSS_PROP_OBJECT_POSITION);
}

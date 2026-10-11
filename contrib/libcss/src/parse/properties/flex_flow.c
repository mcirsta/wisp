/*
 * This file is part of LibCSS.
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 * Copyright 2017 Lucas Neves <lcneves@gmail.com>
 */

#include "bytecode/bytecode.h"
#include "bytecode/opcodes.h"
#include "parse/properties/properties.h"
#include "parse/properties/utils.h"

/* Parse <flex-direction> || <flex-wrap>, without accepting CSS-wide keywords
 * as individual components. Preserve canonical bytecode order in either case. */
css_error css__parse_flex_flow(css_language *c, const parserutils_vector *vector, int32_t *ctx, css_style *result)
{
    int32_t orig_ctx = *ctx;
    uint32_t orig_used = result->used;
    const css_token *token = parserutils_vector_peek(vector, *ctx);
    if (token == NULL)
        return CSS_INVALID;
    css_style *parts[2] = {0};
    const enum css_properties_e properties[] = {CSS_PROP_FLEX_DIRECTION, CSS_PROP_FLEX_WRAP};
    const css_prop_handler handlers[] = {css__parse_flex_direction_value, css__parse_flex_wrap_value};
    const uint16_t defaults[] = {FLEX_DIRECTION_ROW, FLEX_WRAP_NOWRAP};
    css_error error;
    enum flag_value flag = get_css_flag_value(c, token);
    if (flag != FLAG_VALUE__NONE) {
        for (size_t i = 0; i < 2; i++) {
            error = css_stylesheet_style_flag_value(result, flag, properties[i]);
            if (error != CSS_OK)
                goto done;
        }
        parserutils_vector_iterate(vector, ctx);
        goto done;
    }
    for (size_t i = 0; i < 2; i++) {
        error = css__stylesheet_style_create(c->sheet, &parts[i]);
        if (error != CSS_OK)
            goto done;
    }
    size_t first = 0;
    error = handlers[first](c, vector, ctx, parts[first]);
    if (error == CSS_INVALID) {
        first = 1;
        error = handlers[first](c, vector, ctx, parts[first]);
    }
    if (error != CSS_OK)
        goto done;
    consumeWhitespace(vector, ctx);
    error = handlers[1 - first](c, vector, ctx, parts[1 - first]);
    if (error != CSS_OK && error != CSS_INVALID)
        goto done;
    for (size_t i = 0; i < 2; i++) {
        if (parts[i]->used == 0) {
            error = css__stylesheet_style_appendOPV(parts[i], properties[i], 0, defaults[i]);
            if (error != CSS_OK)
                goto done;
        }
        error = css__stylesheet_merge_style(result, parts[i]);
        if (error != CSS_OK)
            goto done;
    }
done:
    for (size_t i = 0; i < 2; i++)
        css__stylesheet_style_destroy(parts[i]);
    if (error != CSS_OK) {
        *ctx = orig_ctx;
        result->used = orig_used;
    }
    return error;
}

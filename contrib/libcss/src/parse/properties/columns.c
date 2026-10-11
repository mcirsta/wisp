/*
 * This file is part of LibCSS.
 * Licensed under the MIT License,
 *		  http://www.opensource.org/licenses/mit-license.php
 * Copyright 2012 Michael Drake <tlsa@netsurf-browser.org>
 */

#include "bytecode/bytecode.h"
#include "bytecode/opcodes.h"
#include "parse/properties/properties.h"
#include "parse/properties/utils.h"
#include "utils/css_utils.h"

/* CSS Multicol 1 §3.3: auto may stand for either omitted component, regardless
 * of token order. Validate concrete values through the generated longhands. */
css_error css__parse_columns(css_language *c, const parserutils_vector *vector, int32_t *ctx, css_style *result)
{
    int32_t orig_ctx = *ctx;
    uint32_t orig_used = result->used;
    const css_token *token = parserutils_vector_peek(vector, *ctx);
    if (token == NULL)
        return CSS_INVALID;
    css_style *parts[2] = {0};
    const enum css_properties_e properties[] = {CSS_PROP_COLUMN_WIDTH, CSS_PROP_COLUMN_COUNT};
    const css_prop_handler handlers[] = {css__parse_column_width_value, css__parse_column_count_value};
    const uint16_t defaults[] = {COLUMN_WIDTH_AUTO, COLUMN_COUNT_AUTO};
    css_error error = CSS_OK;
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
    unsigned supplied = 0;
    while (supplied < 2 && (token = parserutils_vector_peek(vector, *ctx)) != NULL) {
        if (token->type == CSS_TOKEN_IDENT && css__keyword_equal(token->idata, "auto")) {
            parserutils_vector_iterate(vector, ctx);
        } else {
            error = CSS_INVALID;
            for (size_t i = 0; i < 2 && error == CSS_INVALID; i++) {
                if (parts[i]->used == 0)
                    error = handlers[i](c, vector, ctx, parts[i]);
            }
            if (error == CSS_INVALID)
                break;
            if (error != CSS_OK)
                goto done;
        }
        supplied++;
        consumeWhitespace(vector, ctx);
    }
    if (supplied == 0) {
        error = CSS_INVALID;
        goto done;
    }
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

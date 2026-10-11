/*
 * This file is part of LibCSS.
 * Licensed under the MIT License,
 *		  http://www.opensource.org/licenses/mit-license.php
 * Copyright 2009 John-Mark Bell <jmb@netsurf-browser.org>
 */

#include "bytecode/bytecode.h"
#include "bytecode/opcodes.h"
#include "parse/properties/properties.h"
#include "parse/properties/utils.h"
#include "utils/css_utils.h"

/* CSS Lists 3 §3.6: defer ambiguous none values until the explicit image and
 * type are known. Keep the renderer's image/position/type bytecode order. */
css_error css__parse_list_style(css_language *c, const parserutils_vector *vector, int32_t *ctx, css_style *result)
{
    int32_t orig_ctx = *ctx;
    uint32_t orig_used = result->used;
    const css_token *token = parserutils_vector_peek(vector, *ctx);
    if (token == NULL)
        return CSS_INVALID;
    css_style *parts[3] = {0};
    const enum css_properties_e properties[] = {
        CSS_PROP_LIST_STYLE_IMAGE, CSS_PROP_LIST_STYLE_POSITION, CSS_PROP_LIST_STYLE_TYPE};
    uint16_t defaults[] = {LIST_STYLE_IMAGE_NONE, LIST_STYLE_POSITION_OUTSIDE, LIST_STYLE_TYPE_DISC};
    css_error error = CSS_OK;
    enum flag_value flag = get_css_flag_value(c, token);
    if (flag != FLAG_VALUE__NONE) {
        for (size_t i = 0; i < 3; i++) {
            error = css_stylesheet_style_flag_value(result, flag, properties[i]);
            if (error != CSS_OK)
                goto done;
        }
        parserutils_vector_iterate(vector, ctx);
        goto done;
    }
    for (size_t i = 0; i < 3; i++) {
        error = css__stylesheet_style_create(c->sheet, &parts[i]);
        if (error != CSS_OK)
            goto done;
    }
    unsigned nones = 0, supplied = 0;
    while (supplied < 3 && (token = parserutils_vector_peek(vector, *ctx)) != NULL) {
        if (token->type == CSS_TOKEN_IDENT && css__keyword_equal(token->idata, "none")) {
            nones++;
            parserutils_vector_iterate(vector, ctx);
        } else {
            error = CSS_INVALID;
            if (parts[0]->used == 0)
                error = css__parse_list_style_image_value(c, vector, ctx, parts[0]);
            if (error == CSS_INVALID && parts[1]->used == 0)
                error = css__parse_list_style_position_value(c, vector, ctx, parts[1]);
            if (error == CSS_INVALID && parts[2]->used == 0 && token->type == CSS_TOKEN_IDENT) {
                uint16_t type;
                error = css__parse_list_style_type_value(c, token, &type);
                if (error == CSS_OK) {
                    error = css__stylesheet_style_appendOPV(parts[2], properties[2], 0, type);
                    if (error == CSS_OK)
                        parserutils_vector_iterate(vector, ctx);
                }
            }
            if (error == CSS_INVALID)
                break; /* Leave priority or an invalid trailing token to the caller. */
            if (error != CSS_OK)
                goto done;
        }
        supplied++;
        consumeWhitespace(vector, ctx);
    }
    if (supplied == 0 || nones > (unsigned)(parts[0]->used == 0) + (unsigned)(parts[2]->used == 0)) {
        error = CSS_INVALID;
        goto done;
    }
    if (nones != 0)
        defaults[2] = LIST_STYLE_TYPE_NONE;
    for (size_t i = 0; i < 3; i++) {
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
    for (size_t i = 0; i < 3; i++)
        css__stylesheet_style_destroy(parts[i]);
    if (error != CSS_OK) {
        *ctx = orig_ctx;
        result->used = orig_used;
    }
    return error;
}

/*
 * This file is part of LibCSS. Licensed under the MIT License.
 * Copyright 2009 John-Mark Bell <jmb@netsurf-browser.org>
 */
#include "bytecode/bytecode.h"
#include "bytecode/opcodes.h"
#include "parse/properties/background.h"
#include "parse/properties/utils.h"

/* Each longhand keeps its own list. Color belongs only to the last layer. */
css_error css__parse_background(css_language *c, const parserutils_vector *v, int32_t *ctx, css_style *result)
{
    static const opcode_t ops[] = {CSS_PROP_BACKGROUND_ATTACHMENT, CSS_PROP_BACKGROUND_COLOR, CSS_PROP_BACKGROUND_IMAGE,
        CSS_PROP_BACKGROUND_POSITION, CSS_PROP_BACKGROUND_REPEAT_X};
    static const uint16_t defaults[] = {BACKGROUND_ATTACHMENT_SCROLL, BACKGROUND_COLOR_TRANSPARENT,
        BACKGROUND_IMAGE_NONE, BACKGROUND_POSITION_HORZ_LEFT | BACKGROUND_POSITION_VERT_TOP, BACKGROUND_REPEAT_REPEAT};
    static const css_prop_handler parsers[] = {css__parse_background_attachment_item, css__parse_background_color,
        css__parse_background_image_item, css__parse_background_position_item, css__parse_background_repeat_item};
    css_style *items[5] = {NULL};
    int32_t orig = *ctx;
    uint32_t used = result->used, count = 0;
    css_error error = CSS_OK;
    const css_token *token = parserutils_vector_peek(v, *ctx);
    if (token == NULL)
        return CSS_INVALID;
    enum flag_value flag = get_css_flag_value(c, token);
    if (flag != FLAG_VALUE__NONE) {
        for (unsigned i = 0; i < 5; i++) {
            error = css_stylesheet_style_flag_value(result, flag, ops[i]);
            if (error != CSS_OK)
                goto cleanup;
        }
        error = css_stylesheet_style_flag_value(result, flag, CSS_PROP_BACKGROUND_REPEAT_Y);
        if (error != CSS_OK)
            goto cleanup;
        parserutils_vector_iterate(v, ctx);
        return CSS_OK;
    }
    for (unsigned i = 0; i < 5; i++) {
        error = css__stylesheet_style_create(c->sheet, &items[i]);
        if (error != CSS_OK)
            goto cleanup;
    }
    for (;;) {
        bool seen[5] = {false}, any = false;
        for (;;) {
            token = parserutils_vector_peek(v, *ctx);
            if (token == NULL || tokenIsChar(token, ',') || tokenIsChar(token, '!'))
                break;
            if (get_css_flag_value(c, token) != FLAG_VALUE__NONE) {
                error = CSS_INVALID;
                goto cleanup;
            }
            unsigned i;
            for (i = 0; i < 5; i++) {
                if (seen[i])
                    continue;
                uint32_t start = items[i]->used;
                error = parsers[i](c, v, ctx, items[i]);
                if (error == CSS_OK) {
                    seen[i] = any = true;
                    break;
                }
                items[i]->used = start;
                if (error != CSS_INVALID)
                    goto cleanup;
            }
            if (i == 5) {
                error = CSS_INVALID;
                goto cleanup;
            }
            consumeWhitespace(v, ctx);
        }
        bool comma = token != NULL && tokenIsChar(token, ',');
        if (!any || (comma && seen[1])) {
            error = CSS_INVALID;
            goto cleanup;
        }
        count++;
        for (unsigned i = 0; i < 5; i++) {
            if (!seen[i] && (i != 1 || !comma)) {
                error = css__stylesheet_style_appendOPV(items[i], ops[i], 0, defaults[i]);
                if (error == CSS_OK && ops[i] == CSS_PROP_BACKGROUND_REPEAT_X)
                    error = css__stylesheet_style_appendOPV(items[i], CSS_PROP_BACKGROUND_REPEAT_Y, 0,
                        BACKGROUND_REPEAT_REPEAT);
                if (error != CSS_OK)
                    goto cleanup;
            }
        }
        if (!comma)
            break;
        parserutils_vector_iterate(v, ctx);
        consumeWhitespace(v, ctx);
    }
    for (unsigned i = 0; i < 5; i++) {
        error = ops[i] == CSS_PROP_BACKGROUND_REPEAT_X ? css__background_repeat_append(result, items[i], count)
                                                    : css__background_list_append(result, items[i], i == 1 ? 1 : count);
        if (error != CSS_OK)
            break;
    }
cleanup:
    for (unsigned i = 0; i < 5; i++) {
        if (items[i] != NULL)
            css__stylesheet_style_destroy(items[i]);
    }
    if (error != CSS_OK) {
        *ctx = orig;
        result->used = used;
    }
    return error;
}

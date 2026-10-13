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
    static const struct {
        opcode_t op, second_op;
        uint16_t initial;
        css_prop_handler parse;
    } components[] = {
        {CSS_PROP_BACKGROUND_ATTACHMENT, 0, BACKGROUND_ATTACHMENT_SCROLL, css__parse_background_attachment_item},
        {CSS_PROP_BACKGROUND_COLOR, 0, BACKGROUND_COLOR_TRANSPARENT, css__parse_background_color},
        {CSS_PROP_BACKGROUND_IMAGE, 0, BACKGROUND_IMAGE_NONE, css__parse_background_image_item},
        {CSS_PROP_BACKGROUND_POSITION_X, CSS_PROP_BACKGROUND_POSITION_Y,
            BACKGROUND_POSITION_AXIS_START, css__parse_background_position_item},
        {CSS_PROP_BACKGROUND_REPEAT_X, CSS_PROP_BACKGROUND_REPEAT_Y,
            BACKGROUND_REPEAT_REPEAT, css__parse_background_repeat_item},
    };
    enum { COLOUR = 1, COMPONENTS = sizeof(components) / sizeof(components[0]) };
    css_style *items[COMPONENTS] = {NULL};
    int32_t orig = *ctx;
    uint32_t used = result->used, count = 0;
    css_error error = CSS_OK;
    const css_token *token = parserutils_vector_peek(v, *ctx);
    if (token == NULL)
        return CSS_INVALID;
    enum flag_value flag = get_css_flag_value(c, token);
    if (flag != FLAG_VALUE__NONE) {
        for (unsigned i = 0; i < COMPONENTS; i++) {
            error = css_stylesheet_style_flag_value(result, flag, components[i].op);
            if (error == CSS_OK && components[i].second_op != 0)
                error = css_stylesheet_style_flag_value(result, flag, components[i].second_op);
            if (error != CSS_OK)
                goto cleanup;
        }
        parserutils_vector_iterate(v, ctx);
        return CSS_OK;
    }
    for (unsigned i = 0; i < COMPONENTS; i++) {
        error = css__stylesheet_style_create(c->sheet, &items[i]);
        if (error != CSS_OK)
            goto cleanup;
    }
    for (;;) {
        bool seen[COMPONENTS] = {false}, any = false;
        for (;;) {
            token = parserutils_vector_peek(v, *ctx);
            if (token == NULL || tokenIsChar(token, ',') || tokenIsChar(token, '!'))
                break;
            if (get_css_flag_value(c, token) != FLAG_VALUE__NONE) {
                error = CSS_INVALID;
                goto cleanup;
            }
            unsigned i;
            for (i = 0; i < COMPONENTS; i++) {
                if (seen[i])
                    continue;
                uint32_t start = items[i]->used;
                error = components[i].parse(c, v, ctx, items[i]);
                if (error == CSS_OK) {
                    seen[i] = any = true;
                    break;
                }
                items[i]->used = start;
                if (error != CSS_INVALID)
                    goto cleanup;
            }
            if (i == COMPONENTS) {
                error = CSS_INVALID;
                goto cleanup;
            }
            consumeWhitespace(v, ctx);
        }
        bool comma = token != NULL && tokenIsChar(token, ',');
        if (!any || (comma && seen[COLOUR])) {
            error = CSS_INVALID;
            goto cleanup;
        }
        count++;
        for (unsigned i = 0; i < COMPONENTS; i++) {
            if (!seen[i] && (i != COLOUR || !comma)) {
                error = css__stylesheet_style_appendOPV(items[i], components[i].op, 0, components[i].initial);
                if (error == CSS_OK && components[i].second_op != 0)
                    error = css__stylesheet_style_appendOPV(items[i], components[i].second_op, 0, components[i].initial);
                if (error != CSS_OK)
                    goto cleanup;
            }
        }
        if (!comma)
            break;
        parserutils_vector_iterate(v, ctx);
        consumeWhitespace(v, ctx);
    }
    for (unsigned i = 0; i < COMPONENTS; i++) {
        error = components[i].second_op != 0 ? css__background_pair_append(result, items[i], count)
                                            : css__background_list_append(result, items[i], i == COLOUR ? 1 : count);
        if (error != CSS_OK)
            break;
    }
cleanup:
    for (unsigned i = 0; i < COMPONENTS; i++) {
        if (items[i] != NULL)
            css__stylesheet_style_destroy(items[i]);
    }
    if (error != CSS_OK) {
        *ctx = orig;
        result->used = used;
    }
    return error;
}

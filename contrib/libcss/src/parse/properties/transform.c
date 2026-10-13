/*
 * This file is part of LibCSS.
 * Licensed under the MIT License,
 *		  http://www.opensource.org/licenses/mit-license.php
 * Copyright 2026 Neosurf Contributors
 */

#include <string.h>
#include <strings.h>

#include "bytecode/bytecode.h"
#include "bytecode/opcodes.h"
#include "parse/transform.h"
#include "parse/properties/utils.h"

/* Shared names/arity keep specified serialization aligned with the supported
 * grammar without interpreting the renderer's fixed-point bytecode. */
const struct css_transform_syntax *css__transform_syntax(lwc_string *name)
{
    static const struct css_transform_syntax functions[] = {
        {"translate", TRANSFORM_TRANSLATE, CSS_TRANSFORM_LENGTH, 2},
        {"translateX", TRANSFORM_TRANSLATEX, CSS_TRANSFORM_LENGTH, 1},
        {"translateY", TRANSFORM_TRANSLATEY, CSS_TRANSFORM_LENGTH, 1},
        {"scale", TRANSFORM_SCALE, CSS_TRANSFORM_NUMBER, 2},
        {"scaleX", TRANSFORM_SCALEX, CSS_TRANSFORM_NUMBER, 1},
        {"scaleY", TRANSFORM_SCALEY, CSS_TRANSFORM_NUMBER, 1},
        {"rotate", TRANSFORM_ROTATE, CSS_TRANSFORM_ANGLE, 1},
    };
    for (size_t i = 0; i < N_ELEMENTS(functions); i++) {
        /* css__keyword_equal expects a lowercase keyword. The canonical
         * function spelling preserves the uppercase axis for CSSOM. */
        if (lwc_string_length(name) == strlen(functions[i].name) &&
            strncasecmp(lwc_string_data(name), functions[i].name, lwc_string_length(name)) == 0)
            return &functions[i];
    }
    return NULL;
}

/* CSS Transforms 1 section 7 and Transforms 2 section 12.1. Both arguments use
 * identical validation. Legacy unitless-length quirks do not apply in functions. */
static css_error parse_argument(css_language *c, const parserutils_vector *vector, int32_t *ctx,
    enum css_transform_argument kind, css_fixed *value, uint32_t *unit)
{
    consumeWhitespace(vector, ctx);
    int32_t start = *ctx;
    const css_token *token = parserutils_vector_peek(vector, *ctx);
    if (token == NULL)
        return CSS_INVALID;
    if (kind == CSS_TRANSFORM_NUMBER) {
        if (token->type != CSS_TOKEN_NUMBER && token->type != CSS_TOKEN_PERCENTAGE)
            return CSS_INVALID;
        size_t consumed;
        *value = css__number_from_lwc_string(token->idata, false, &consumed);
        if (consumed != lwc_string_length(token->idata))
            return CSS_INVALID;
        *unit = token->type == CSS_TOKEN_PERCENTAGE ? UNIT_PCT : UNIT_NUMBER;
        if (*unit == UNIT_PCT)
            *value = FDIV(*value, F_100);
        parserutils_vector_iterate(vector, ctx);
        return CSS_OK;
    }
    if (token->type == CSS_TOKEN_NUMBER && css__number_sign(token->idata) != 0)
        return CSS_INVALID;
    css_error error = css__parse_unit_specifier(c, vector, ctx,
        kind == CSS_TRANSFORM_ANGLE ? UNIT_DEG : UNIT_PX, value, unit);
    if (error != CSS_OK)
        return error;
    /* A whitespace-separated unit is only accepted by old quirky properties. */
    if (*ctx != start + 1)
        return CSS_INVALID;
    bool allowed = kind == CSS_TRANSFORM_ANGLE ? (*unit & UNIT_ANGLE) != 0 :
        ((*unit & (UNIT_LENGTH | UNIT_PCT)) != 0 && *unit != UNIT_FR);
    return allowed ? CSS_OK : CSS_INVALID;
}

static css_error parse_function(css_language *c, const parserutils_vector *vector, int32_t *ctx,
    css_style *result, const struct css_transform_syntax *syntax)
{
    css_fixed values[2] = {0};
    uint32_t units[2] = {0, UNIT_PX};
    css_error error = parse_argument(c, vector, ctx, syntax->argument, &values[0], &units[0]);
    if (error != CSS_OK)
        return error;
    if (syntax->argument == CSS_TRANSFORM_NUMBER) {
        values[1] = values[0];
        units[1] = units[0];
    }
    consumeWhitespace(vector, ctx);
    const css_token *token = parserutils_vector_peek(vector, *ctx);
    if (syntax->arity == 2 && token != NULL && tokenIsChar(token, ',')) {
        parserutils_vector_iterate(vector, ctx);
        error = parse_argument(c, vector, ctx, syntax->argument, &values[1], &units[1]);
        if (error != CSS_OK)
            return error;
        consumeWhitespace(vector, ctx);
    }
    token = parserutils_vector_iterate(vector, ctx);
    if (token == NULL || !tokenIsChar(token, ')'))
        return CSS_INVALID;
    error = css__stylesheet_style_append(result, syntax->opcode);
    for (size_t i = 0; error == CSS_OK && i < syntax->arity; i++)
        error = css__stylesheet_style_vappend(result, 2, values[i], units[i]);
    return error;
}

css_error css__parse_transform(css_language *c, const parserutils_vector *vector, int32_t *ctx, css_style *result)
{
    int32_t start = *ctx;
    const css_token *token = parserutils_vector_peek(vector, *ctx);
    if (token == NULL)
        return CSS_INVALID;
    enum flag_value flag = get_css_flag_value(c, token);
    if (flag != FLAG_VALUE__NONE) {
        parserutils_vector_iterate(vector, ctx);
        return css_stylesheet_style_flag_value(result, flag, CSS_PROP_TRANSFORM);
    }
    if (token->type == CSS_TOKEN_IDENT && css__keyword_equal(token->idata, "none")) {
        parserutils_vector_iterate(vector, ctx);
        return css__stylesheet_style_appendOPV(result, CSS_PROP_TRANSFORM, 0, TRANSFORM_NONE);
    }
    css_error error = css__stylesheet_style_appendOPV(result, CSS_PROP_TRANSFORM, 0, TRANSFORM_NONE + 1);
    if (error != CSS_OK)
        return error;
    uint32_t count_position = result->used, count = 0;
    error = css__stylesheet_style_append(result, 0);
    while (error == CSS_OK && token != NULL && token->type == CSS_TOKEN_FUNCTION) {
        const struct css_transform_syntax *syntax = css__transform_syntax(token->idata);
        if (syntax == NULL) {
            error = CSS_INVALID;
            break;
        }
        parserutils_vector_iterate(vector, ctx);
        error = parse_function(c, vector, ctx, result, syntax);
        if (error != CSS_OK)
            break;
        count++;
        consumeWhitespace(vector, ctx);
        token = parserutils_vector_peek(vector, *ctx);
    }
    if (error == CSS_OK && count == 0)
        error = CSS_INVALID;
    if (error != CSS_OK) {
        *ctx = start;
        return error;
    }
    result->bytecode[count_position] = count;
    return CSS_OK;
}

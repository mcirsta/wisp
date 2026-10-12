/*
 * This file is part of LibCSS.
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 * Copyright 2024 Neosurf CSS Grid Support
 */

#include "bytecode/bytecode.h"
#include "bytecode/opcodes.h"
#include "parse/properties/properties.h"
#include "parse/properties/utils.h"

struct grid_line {
    uint16_t value;
    css_fixed integer;
};

static bool keyword(const css_token *token, lwc_string *word)
{
    bool match;
    return token != NULL && token->type == CSS_TOKEN_IDENT &&
        lwc_string_caseless_isequal(token->idata, word, &match) == lwc_error_ok && match;
}

/* CSS Grid 2 section 8.3, for the renderer's numeric placement subset:
 * auto | <nonzero-integer> | [span && <positive-integer>]. Named lines are not
 * implemented. Keep this grammar identical for all longhands and shorthands. */
static css_error parse_grid_line(css_language *c, const parserutils_vector *vector, int32_t *ctx,
    struct grid_line *line)
{
    const css_token *token = parserutils_vector_peek(vector, *ctx);
    if (keyword(token, c->strings[AUTO])) {
        parserutils_vector_iterate(vector, ctx);
        *line = (struct grid_line){CSS_GRID_LINE_AUTO, 0};
        return CSS_OK;
    }
    bool span = keyword(token, c->strings[SPAN]);
    if (span) {
        parserutils_vector_iterate(vector, ctx);
        consumeWhitespace(vector, ctx);
        token = parserutils_vector_peek(vector, *ctx);
    }
    if (token == NULL || token->type != CSS_TOKEN_NUMBER)
        return CSS_INVALID;
    size_t consumed;
    css_fixed integer = css__number_from_lwc_string(token->idata, true, &consumed);
    if (consumed != lwc_string_length(token->idata) || integer == 0)
        return CSS_INVALID;
    parserutils_vector_iterate(vector, ctx);
    consumeWhitespace(vector, ctx);
    if (!span && keyword(parserutils_vector_peek(vector, *ctx), c->strings[SPAN])) {
        span = true;
        parserutils_vector_iterate(vector, ctx);
    }
    if (span && integer < 0)
        return CSS_INVALID;
    *line = (struct grid_line){span ? CSS_GRID_LINE_SPAN : CSS_GRID_LINE_SET, integer};
    return CSS_OK;
}

/* Missing numeric-placement components default to auto (section 8.4). Parse
 * into bounded stack storage before emitting; a slash always requires another
 * component. CSS-wide keywords apply only to the complete declaration. */
static css_error parse_grid_placement(css_language *c, const parserutils_vector *vector, int32_t *ctx,
    css_style *result, const enum css_properties_e *properties, size_t count)
{
    int32_t orig_ctx = *ctx;
    uint32_t orig_used = result->used;
    const css_token *token = parserutils_vector_peek(vector, *ctx);
    if (token == NULL)
        return CSS_INVALID;
    css_error error = CSS_OK;
    enum flag_value flag = get_css_flag_value(c, token);
    struct grid_line lines[4] = {
        {CSS_GRID_LINE_AUTO, 0}, {CSS_GRID_LINE_AUTO, 0}, {CSS_GRID_LINE_AUTO, 0}, {CSS_GRID_LINE_AUTO, 0}};
    if (flag != FLAG_VALUE__NONE) {
        parserutils_vector_iterate(vector, ctx);
    } else {
        for (size_t i = 0; i < count; i++) {
            error = parse_grid_line(c, vector, ctx, &lines[i]);
            if (error != CSS_OK)
                goto done;
            consumeWhitespace(vector, ctx);
            token = parserutils_vector_peek(vector, *ctx);
            if (token == NULL || !tokenIsChar(token, '/'))
                break;
            if (i + 1 == count) {
                error = CSS_INVALID;
                goto done;
            }
            parserutils_vector_iterate(vector, ctx);
            consumeWhitespace(vector, ctx);
        }
    }
    for (size_t i = 0; i < count; i++) {
        if (flag != FLAG_VALUE__NONE) {
            error = css_stylesheet_style_flag_value(result, flag, properties[i]);
        } else {
            error = css__stylesheet_style_appendOPV(result, properties[i], 0, lines[i].value);
            if (error == CSS_OK && lines[i].value != CSS_GRID_LINE_AUTO)
                error = css__stylesheet_style_append(result, lines[i].integer);
        }
        if (error != CSS_OK)
            goto done;
    }
done:
    if (error != CSS_OK) {
        *ctx = orig_ctx;
        result->used = orig_used;
    }
    return error;
}

css_error css__parse_grid_line_internal(
    css_language *c, const parserutils_vector *vector, int32_t *ctx, css_style *result, enum css_properties_e id)
{
    return parse_grid_placement(c, vector, ctx, result, &id, 1);
}

css_error css__parse_grid_row(css_language *c, const parserutils_vector *vector, int32_t *ctx, css_style *result)
{
    static const enum css_properties_e properties[] = {CSS_PROP_GRID_ROW_START, CSS_PROP_GRID_ROW_END};
    return parse_grid_placement(c, vector, ctx, result, properties, 2);
}

css_error css__parse_grid_column(css_language *c, const parserutils_vector *vector, int32_t *ctx, css_style *result)
{
    static const enum css_properties_e properties[] = {CSS_PROP_GRID_COLUMN_START, CSS_PROP_GRID_COLUMN_END};
    return parse_grid_placement(c, vector, ctx, result, properties, 2);
}

css_error css__parse_grid_area(css_language *c, const parserutils_vector *vector, int32_t *ctx, css_style *result)
{
    static const enum css_properties_e properties[] = {
        CSS_PROP_GRID_ROW_START, CSS_PROP_GRID_COLUMN_START, CSS_PROP_GRID_ROW_END, CSS_PROP_GRID_COLUMN_END};
    return parse_grid_placement(c, vector, ctx, result, properties, 4);
}

/*
 * This file is part of LibCSS.
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 */

#include <string.h>

#include "bytecode/bytecode.h"
#include "bytecode/opcodes.h"
#include "parse/properties/properties.h"
#include "parse/properties/utils.h"

/* Retain the existing supported-size bound. Reject an oversized declaration
 * as a whole instead of silently truncating repeat() and changing its meaning.
 * This is an implementation limit, not the Grid specification's grid clamp. */
#define MAX_GRID_TRACKS 32

struct grid_track {
    css_fixed min, max;
    uint32_t min_unit, max_unit;
    bool minmax;
};

/* CSS Grid 2 section 7.2.1: lengths/percentages/flex are nonnegative; a minmax
 * minimum cannot be flexible. Unitless-length quirks do not apply to Grid. */
static css_error breadth(css_language *c, const parserutils_vector *vector, int32_t *ctx,
    bool flexible, css_fixed *value, uint32_t *unit)
{
    consumeWhitespace(vector, ctx);
    const css_token *token = parserutils_vector_peek(vector, *ctx);
    if (token == NULL)
        return CSS_INVALID;
    if (token->type == CSS_TOKEN_IDENT) {
        if (css__keyword_equal(token->idata, "auto"))
            *unit = UNIT_GRID_AUTO;
        else if (css__keyword_equal(token->idata, "min-content"))
            *unit = UNIT_MIN_CONTENT;
        else if (css__keyword_equal(token->idata, "max-content"))
            *unit = UNIT_MAX_CONTENT;
        else
            return CSS_INVALID;
        *value = 0;
        parserutils_vector_iterate(vector, ctx);
        return CSS_OK;
    }
    if (token->type != CSS_TOKEN_NUMBER && token->type != CSS_TOKEN_DIMENSION && token->type != CSS_TOKEN_PERCENTAGE)
        return CSS_INVALID;
    int sign = css__number_sign(token->idata);
    if (sign < 0 || (token->type == CSS_TOKEN_NUMBER && sign != 0))
        return CSS_INVALID;
    int32_t start = *ctx;
    css_error error = css__parse_unit_specifier(c, vector, ctx, UNIT_PX, value, unit);
    if (error != CSS_OK)
        return error;
    if (*ctx != start + 1 || !(*unit & (UNIT_LENGTH | UNIT_PCT)) || (!flexible && *unit == UNIT_FR))
        return CSS_INVALID;
    return CSS_OK;
}

static bool delimiter(const parserutils_vector *vector, int32_t *ctx, char character)
{
    consumeWhitespace(vector, ctx);
    const css_token *token = parserutils_vector_iterate(vector, ctx);
    return token != NULL && tokenIsChar(token, character);
}

static css_error track_size(css_language *c, const parserutils_vector *vector, int32_t *ctx, struct grid_track *track)
{
    const css_token *token = parserutils_vector_peek(vector, *ctx);
    track->minmax = token != NULL && token->type == CSS_TOKEN_FUNCTION && css__keyword_equal(token->idata, "minmax");
    if (track->minmax)
        parserutils_vector_iterate(vector, ctx);
    css_error error = breadth(c, vector, ctx, !track->minmax, &track->min, &track->min_unit);
    if (error == CSS_OK && track->minmax) {
        if (!delimiter(vector, ctx, ','))
            return CSS_INVALID;
        error = breadth(c, vector, ctx, true, &track->max, &track->max_unit);
        if (error == CSS_OK && !delimiter(vector, ctx, ')'))
            error = CSS_INVALID;
    }
    return error;
}

/* CSS Grid 2 section 7.2.3: an integer repeat contains one or more track sizes,
 * never another repeat. Keep the pattern in the same bounded scratch array. */
static css_error repeat(css_language *c, const parserutils_vector *vector, int32_t *ctx,
    struct grid_track *tracks, size_t *count)
{
    parserutils_vector_iterate(vector, ctx);
    consumeWhitespace(vector, ctx);
    const css_token *token = parserutils_vector_iterate(vector, ctx);
    if (token == NULL || token->type != CSS_TOKEN_NUMBER)
        return CSS_INVALID;
    size_t consumed;
    int repetitions = FIXTOINT(css__number_from_lwc_string(token->idata, true, &consumed));
    if (consumed != lwc_string_length(token->idata) || repetitions < 1 || repetitions > MAX_GRID_TRACKS ||
        !delimiter(vector, ctx, ','))
        return CSS_INVALID;
    size_t start = *count;
    while (true) {
        consumeWhitespace(vector, ctx);
        if (*count == MAX_GRID_TRACKS)
            return CSS_INVALID;
        css_error error = track_size(c, vector, ctx, &tracks[*count]);
        if (error != CSS_OK)
            return error;
        (*count)++;
        consumeWhitespace(vector, ctx);
        token = parserutils_vector_peek(vector, *ctx);
        if (token != NULL && tokenIsChar(token, ')')) {
            parserutils_vector_iterate(vector, ctx);
            break;
        }
    }
    size_t pattern = *count - start;
    if ((size_t)repetitions > (MAX_GRID_TRACKS - start) / pattern)
        return CSS_INVALID;
    for (int i = 1; i < repetitions; i++) {
        memcpy(tracks + *count, tracks + start, pattern * sizeof(*tracks));
        *count += pattern;
    }
    return CSS_OK;
}

css_error css__parse_grid_template_internal(
    css_language *c, const parserutils_vector *vector, int32_t *ctx, css_style *result, enum css_properties_e id)
{
    int32_t start = *ctx;
    uint32_t used = result->used;
    const css_token *token = parserutils_vector_peek(vector, *ctx);
    if (token == NULL)
        return CSS_INVALID;
    enum flag_value flag = get_css_flag_value(c, token);
    css_error error;
    if (flag != FLAG_VALUE__NONE) {
        parserutils_vector_iterate(vector, ctx);
        error = css_stylesheet_style_flag_value(result, flag, id);
    } else if (token->type == CSS_TOKEN_IDENT && css__keyword_equal(token->idata, "none")) {
        parserutils_vector_iterate(vector, ctx);
        error = css__stylesheet_style_appendOPV(result, id, 0, GRID_TEMPLATE_NONE);
    } else {
        struct grid_track tracks[MAX_GRID_TRACKS] = {0};
        size_t count = 0;
        error = CSS_OK;
        while (token != NULL && !tokenIsChar(token, '!')) {
            if (token->type == CSS_TOKEN_FUNCTION && css__keyword_equal(token->idata, "repeat")) {
                error = repeat(c, vector, ctx, tracks, &count);
            } else if (count == MAX_GRID_TRACKS) {
                error = CSS_INVALID;
            } else {
                error = track_size(c, vector, ctx, &tracks[count]);
                count++;
            }
            if (error != CSS_OK)
                break;
            consumeWhitespace(vector, ctx);
            token = parserutils_vector_peek(vector, *ctx);
        }
        if (error == CSS_OK && count == 0)
            error = CSS_INVALID;
        if (error == CSS_OK)
            error = css__stylesheet_style_appendOPV(result, id, 0, GRID_TEMPLATE_SET);
        if (error == CSS_OK)
            error = css__stylesheet_style_append(result, count);
        for (size_t i = 0; error == CSS_OK && i < count; i++) {
            const struct grid_track *track = &tracks[i];
            if (track->minmax)
                error = css__stylesheet_style_vappend(result, 2, 0, UNIT_MINMAX);
            if (error == CSS_OK)
                error = css__stylesheet_style_vappend(result, 2, track->min, track->min_unit);
            if (error == CSS_OK && track->minmax)
                error = css__stylesheet_style_vappend(result, 2, track->max, track->max_unit);
        }
    }
    if (error != CSS_OK) {
        *ctx = start;
        result->used = used;
    }
    return error;
}

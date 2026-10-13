/*
 * This file is part of LibCSS.
 * Licensed under the MIT License,
 *		  http://www.opensource.org/licenses/mit-license.php
 * Copyright 2009 John-Mark Bell <jmb@netsurf-browser.org>
 */

#include <assert.h>
#include <string.h>

#include "bytecode/bytecode.h"
#include "bytecode/opcodes.h"
#include "parse/font.h"
#include "parse/declarations.h"
#include "parse/properties/properties.h"
#include "parse/properties/utils.h"

/**
 * Convert css_unit to a bytecode unit.
 */
static inline uint32_t css__to_parse_unit(css_unit u)
{
    switch (u) {
    case CSS_UNIT_PX:
        return UNIT_PX;
    case CSS_UNIT_EX:
        return UNIT_EX;
    case CSS_UNIT_EM:
        return UNIT_EM;
    case CSS_UNIT_IN:
        return UNIT_IN;
    case CSS_UNIT_CM:
        return UNIT_CM;
    case CSS_UNIT_MM:
        return UNIT_MM;
    case CSS_UNIT_PT:
        return UNIT_PT;
    case CSS_UNIT_PC:
        return UNIT_PC;
    case CSS_UNIT_CH:
        return UNIT_CH;
    case CSS_UNIT_REM:
        return UNIT_REM;
    case CSS_UNIT_LH:
        return UNIT_LH;
    case CSS_UNIT_VH:
        return UNIT_VH;
    case CSS_UNIT_VW:
        return UNIT_VW;
    case CSS_UNIT_VI:
        return UNIT_VI;
    case CSS_UNIT_VB:
        return UNIT_VB;
    case CSS_UNIT_VMIN:
        return UNIT_VMIN;
    case CSS_UNIT_VMAX:
        return UNIT_VMAX;
    case CSS_UNIT_Q:
        return UNIT_Q;
    case CSS_UNIT_IC:
        return UNIT_IC;
    case CSS_UNIT_CQW:
        return UNIT_CQW;
    case CSS_UNIT_CQH:
        return UNIT_CQH;
    case CSS_UNIT_CQI:
        return UNIT_CQI;
    case CSS_UNIT_CQB:
        return UNIT_CQB;
    case CSS_UNIT_PCT:
        return UNIT_PCT;
    case CSS_UNIT_DEG:
        return UNIT_DEG;
    case CSS_UNIT_GRAD:
        return UNIT_GRAD;
    case CSS_UNIT_RAD:
        return UNIT_RAD;
    case CSS_UNIT_MS:
        return UNIT_MS;
    case CSS_UNIT_S:
        return UNIT_S;
    case CSS_UNIT_HZ:
        return UNIT_HZ;
    case CSS_UNIT_KHZ:
        return UNIT_KHZ;
    case CSS_UNIT_CALC:
        assert(0);
    }

    return 0;
}

static css_error parse_system_font(css_language *c, css_style *result, css_system_font *system_font)
{
    css_error error;
    bool match;

    if (system_font->family == NULL || system_font->size.size < 0 || system_font->line_height.size < 0 ||
        css__system_font_unit(system_font->size.unit) == NULL ||
        css__system_font_unit(system_font->line_height.unit) == NULL)
        return CSS_BADPARM;

    /* style */
    switch (system_font->style) {
    case CSS_FONT_STYLE_NORMAL:
        error = css__stylesheet_style_appendOPV(result, CSS_PROP_FONT_STYLE, 0, FONT_STYLE_NORMAL);
        break;

    case CSS_FONT_STYLE_ITALIC:
        error = css__stylesheet_style_appendOPV(result, CSS_PROP_FONT_STYLE, 0, FONT_STYLE_ITALIC);
        break;

    case CSS_FONT_STYLE_OBLIQUE:
        error = css__stylesheet_style_appendOPV(result, CSS_PROP_FONT_STYLE, 0, FONT_STYLE_OBLIQUE);
        break;

    default:
        error = CSS_BADPARM;
        break;
    }
    if (error != CSS_OK)
        return error;

    /* variant */
    switch (system_font->variant) {
    case CSS_FONT_VARIANT_NORMAL:
        error = css__stylesheet_style_appendOPV(result, CSS_PROP_FONT_VARIANT, 0, FONT_VARIANT_NORMAL);
        break;

    case CSS_FONT_VARIANT_SMALL_CAPS:
        error = css__stylesheet_style_appendOPV(result, CSS_PROP_FONT_VARIANT, 0, FONT_VARIANT_SMALL_CAPS);
        break;

    default:
        error = CSS_BADPARM;
        break;
    }
    if (error != CSS_OK)
        return error;

    /* weight */
    switch (system_font->weight) {
    case CSS_FONT_WEIGHT_NORMAL:
        error = css__stylesheet_style_appendOPV(result, CSS_PROP_FONT_WEIGHT, 0, FONT_WEIGHT_NORMAL);
        break;

    case CSS_FONT_WEIGHT_BOLD:
        error = css__stylesheet_style_appendOPV(result, CSS_PROP_FONT_WEIGHT, 0, FONT_WEIGHT_BOLD);
        break;

    case CSS_FONT_WEIGHT_BOLDER:
        error = css__stylesheet_style_appendOPV(result, CSS_PROP_FONT_WEIGHT, 0, FONT_WEIGHT_BOLDER);
        break;

    case CSS_FONT_WEIGHT_LIGHTER:
        error = css__stylesheet_style_appendOPV(result, CSS_PROP_FONT_WEIGHT, 0, FONT_WEIGHT_LIGHTER);
        break;

    case CSS_FONT_WEIGHT_100:
        error = css__stylesheet_style_appendOPV(result, CSS_PROP_FONT_WEIGHT, 0, FONT_WEIGHT_100);
        break;

    case CSS_FONT_WEIGHT_200:
        error = css__stylesheet_style_appendOPV(result, CSS_PROP_FONT_WEIGHT, 0, FONT_WEIGHT_200);
        break;

    case CSS_FONT_WEIGHT_300:
        error = css__stylesheet_style_appendOPV(result, CSS_PROP_FONT_WEIGHT, 0, FONT_WEIGHT_300);
        break;

    case CSS_FONT_WEIGHT_400:
        error = css__stylesheet_style_appendOPV(result, CSS_PROP_FONT_WEIGHT, 0, FONT_WEIGHT_400);
        break;

    case CSS_FONT_WEIGHT_500:
        error = css__stylesheet_style_appendOPV(result, CSS_PROP_FONT_WEIGHT, 0, FONT_WEIGHT_500);
        break;

    case CSS_FONT_WEIGHT_600:
        error = css__stylesheet_style_appendOPV(result, CSS_PROP_FONT_WEIGHT, 0, FONT_WEIGHT_600);
        break;

    case CSS_FONT_WEIGHT_700:
        error = css__stylesheet_style_appendOPV(result, CSS_PROP_FONT_WEIGHT, 0, FONT_WEIGHT_700);
        break;

    case CSS_FONT_WEIGHT_800:
        error = css__stylesheet_style_appendOPV(result, CSS_PROP_FONT_WEIGHT, 0, FONT_WEIGHT_800);
        break;

    case CSS_FONT_WEIGHT_900:
        error = css__stylesheet_style_appendOPV(result, CSS_PROP_FONT_WEIGHT, 0, FONT_WEIGHT_900);
        break;

    default:
        error = CSS_BADPARM;
        break;
    }
    if (error != CSS_OK)
        return error;

    /* size */
    error = css__stylesheet_style_appendOPV(result, CSS_PROP_FONT_SIZE, 0, FONT_SIZE_DIMENSION);
    if (error != CSS_OK)
        return error;

    error = css__stylesheet_style_vappend(
        result, 2, system_font->size.size, css__to_parse_unit(system_font->size.unit));
    if (error != CSS_OK)
        return error;

    /* line height */
    error = css__stylesheet_style_appendOPV(result, CSS_PROP_LINE_HEIGHT, 0, LINE_HEIGHT_DIMENSION);
    if (error != CSS_OK)
        return error;

    error = css__stylesheet_style_vappend(
        result, 2, system_font->line_height.size, css__to_parse_unit(system_font->line_height.unit));
    if (error != CSS_OK)
        return error;

    /* font family */
    if ((lwc_string_caseless_isequal(system_font->family, c->strings[SERIF], &match) == lwc_error_ok && match))
        error = css__stylesheet_style_appendOPV(result, CSS_PROP_FONT_FAMILY, 0, FONT_FAMILY_SERIF);
    else if ((lwc_string_caseless_isequal(system_font->family, c->strings[SANS_SERIF], &match) == lwc_error_ok &&
                 match))
        error = css__stylesheet_style_appendOPV(result, CSS_PROP_FONT_FAMILY, 0, FONT_FAMILY_SANS_SERIF);
    else if ((lwc_string_caseless_isequal(system_font->family, c->strings[CURSIVE], &match) == lwc_error_ok && match))
        error = css__stylesheet_style_appendOPV(result, CSS_PROP_FONT_FAMILY, 0, FONT_FAMILY_CURSIVE);
    else if ((lwc_string_caseless_isequal(system_font->family, c->strings[FANTASY], &match) == lwc_error_ok && match))
        error = css__stylesheet_style_appendOPV(result, CSS_PROP_FONT_FAMILY, 0, FONT_FAMILY_FANTASY);
    else if ((lwc_string_caseless_isequal(system_font->family, c->strings[MONOSPACE], &match) == lwc_error_ok && match))
        error = css__stylesheet_style_appendOPV(result, CSS_PROP_FONT_FAMILY, 0, FONT_FAMILY_MONOSPACE);
    else {
        uint32_t snumber;

        error = css__stylesheet_string_add(c->sheet, lwc_string_ref(system_font->family), &snumber);
        if (error != CSS_OK)
            return error;

        error = css__stylesheet_style_appendOPV(result, CSS_PROP_FONT_FAMILY, 0, FONT_FAMILY_STRING);
        if (error != CSS_OK)
            return error;

        error = css__stylesheet_style_append(result, snumber);
    }
    if (error != CSS_OK)
        return error;

    error = css__stylesheet_style_append(result, FONT_FAMILY_END);

    return error;
}

/* CSS2 Fonts §15.8's supported style/variant/weight prefix. Defer assigning
 * normal until all concrete values are known: normal italic is just as valid
 * as italic normal. No temporary styles or speculative property parsers. */
static css_error parse_font_prefix(const parserutils_vector *vector, int32_t *ctx, css_style *result)
{
    const enum css_properties_e properties[] = {CSS_PROP_FONT_STYLE, CSS_PROP_FONT_VARIANT, CSS_PROP_FONT_WEIGHT};
    uint16_t values[] = {FONT_STYLE_NORMAL, FONT_VARIANT_NORMAL, FONT_WEIGHT_NORMAL};
    bool seen[N_ELEMENTS(values)] = {false};
    size_t supplied = 0;
    const css_token *token;
    while ((token = parserutils_vector_peek(vector, *ctx)) != NULL) {
        uint16_t value = 0;
        enum css_font_prefix part = css__font_prefix(token->type, token->idata, &value);
        if (part == CSS_FONT_PREFIX_END)
            break;
        if (supplied++ == N_ELEMENTS(values))
            return CSS_INVALID;
        if (part != CSS_FONT_PREFIX_NORMAL) {
            if (seen[part])
                return CSS_INVALID;
            seen[part] = true;
            values[part] = value;
        }
        parserutils_vector_iterate(vector, ctx);
        consumeWhitespace(vector, ctx);
    }
    for (size_t i = 0; i < N_ELEMENTS(values); i++) {
        css_error error = css__stylesheet_style_appendOPV(result, properties[i], 0, values[i]);
        if (error != CSS_OK)
            return error;
    }
    return CSS_OK;
}

/* A failed shorthand restores both the token cursor and its entire bytecode
 * span, including failures while emitting defaults, CSS-wide or system fonts. */
css_error css__parse_font(css_language *c, const parserutils_vector *vector, int32_t *ctx, css_style *result)
{
    int32_t orig_ctx = *ctx;
    uint32_t orig_used = result->used;
    const css_token *token = parserutils_vector_peek(vector, *ctx);
    if (token == NULL)
        return CSS_INVALID;
    css_error error;
    enum flag_value flag = get_css_flag_value(c, token);
    if (flag != FLAG_VALUE__NONE) {
        const enum css_properties_e properties[] = {CSS_PROP_FONT_STYLE, CSS_PROP_FONT_VARIANT,
            CSS_PROP_FONT_WEIGHT, CSS_PROP_FONT_SIZE, CSS_PROP_LINE_HEIGHT, CSS_PROP_FONT_FAMILY};
        for (size_t i = 0; i < N_ELEMENTS(properties); i++) {
            error = css_stylesheet_style_flag_value(result, flag, properties[i]);
            if (error != CSS_OK)
                goto done;
        }
        parserutils_vector_iterate(vector, ctx);
        goto done;
    }

    /* Preserve the client's existing system-font resolver. Only an unknown
     * name permits ordinary parsing; allocation/client errors must propagate. */
    if (token->type == CSS_TOKEN_IDENT && c->sheet->font != NULL) {
        css_system_font system_font = {0};
        error = c->sheet->font(c->sheet->font_pw, token->idata, &system_font);
        if (error == CSS_OK) {
            error = parse_system_font(c, result, &system_font);
            if (error == CSS_OK && c->declarations != NULL)
                error = css__declaration_list_system_font(c->declarations, &system_font);
            if (system_font.family != NULL)
                lwc_string_unref(system_font.family);
            if (error == CSS_OK)
                parserutils_vector_iterate(vector, ctx);
            goto done;
        }
        if (error != CSS_INVALID)
            goto done;
    }

    error = parse_font_prefix(vector, ctx, result);
    if (error != CSS_OK)
        goto done;
    /* Value-only generated handlers exclude CSS-wide keywords from embedded
     * positions; they are only legal as the whole shorthand above. */
    error = css__parse_font_size_value(c, vector, ctx, result);
    if (error != CSS_OK)
        goto done;
    consumeWhitespace(vector, ctx);
    token = parserutils_vector_peek(vector, *ctx);
    if (token != NULL && tokenIsChar(token, '/')) {
        parserutils_vector_iterate(vector, ctx);
        consumeWhitespace(vector, ctx);
        error = css__parse_line_height_value(c, vector, ctx, result);
    } else {
        error = css__stylesheet_style_appendOPV(result, CSS_PROP_LINE_HEIGHT, 0, LINE_HEIGHT_NORMAL);
    }
    if (error != CSS_OK)
        goto done;
    consumeWhitespace(vector, ctx);
    token = parserutils_vector_peek(vector, *ctx);
    if (token == NULL || get_css_flag_value(c, token) != FLAG_VALUE__NONE) {
        error = CSS_INVALID;
        goto done;
    }
    error = css__parse_font_family(c, vector, ctx, result);
done:
    if (error != CSS_OK) {
        *ctx = orig_ctx;
        result->used = orig_used;
    }
    return error;
}

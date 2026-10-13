/*
 * This file is part of LibCSS.
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 */

#include "bytecode/bytecode.h"
#include "bytecode/opcodes.h"
#include "parse/properties/properties.h"
#include "parse/properties/utils.h"

/* SVG 2 section 13.5.3: a nonnegative length, percentage or user-unit number.
 * Bare numbers are valid in standards mode too; unrelated CSS units and the
 * whitespace-separated-unit quirk are not. Check sign before fixed-point
 * rounding so a tiny negative width cannot become a valid zero. */
css_error css__parse_stroke_width(css_language *c, const parserutils_vector *vector, int32_t *ctx, css_style *result)
{
    int32_t start = *ctx;
    uint32_t used = result->used;
    const css_token *token = parserutils_vector_iterate(vector, ctx);
    if (token == NULL)
        return CSS_INVALID;
    enum flag_value flag = get_css_flag_value(c, token);
    css_error error;
    if (flag != FLAG_VALUE__NONE) {
        error = css_stylesheet_style_flag_value(result, flag, CSS_PROP_STROKE_WIDTH);
    } else {
        css_fixed length;
        uint32_t unit = UNIT_PX;
        if ((token->type != CSS_TOKEN_NUMBER && token->type != CSS_TOKEN_DIMENSION &&
                token->type != CSS_TOKEN_PERCENTAGE) || css__number_sign(token->idata) < 0) {
            *ctx = start;
            return CSS_INVALID;
        }
        if (token->type == CSS_TOKEN_NUMBER) {
            size_t consumed;
            length = css__number_from_lwc_string(token->idata, false, &consumed);
            error = consumed == lwc_string_length(token->idata) ? CSS_OK : CSS_INVALID;
        } else {
            *ctx = start;
            error = css__parse_unit_specifier(c, vector, ctx, UNIT_PX, &length, &unit);
            if (error == CSS_OK && ((unit & (UNIT_LENGTH | UNIT_PCT)) == 0 || unit == UNIT_FR))
                error = CSS_INVALID;
        }
        if (error == CSS_OK)
            error = css__stylesheet_style_appendOPV(result, CSS_PROP_STROKE_WIDTH, 0, STROKE_WIDTH_SET);
        if (error == CSS_OK)
            error = css__stylesheet_style_vappend(result, 2, length, unit);
    }
    if (error != CSS_OK) {
        *ctx = start;
        result->used = used;
    }
    return error;
}

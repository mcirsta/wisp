/*
 * This file is part of LibCSS.
 * Licensed under the MIT License,
 *		  http://www.opensource.org/licenses/mit-license.php
 * Copyright 2009 John-Mark Bell <jmb@netsurf-browser.org>
 */

#include <assert.h>
#include <string.h>
#include <strings.h>
#include <math.h>

/* Undefine macros that conflict with CSS enums (e.g. on MinGW) */
#undef OVERFLOW
#undef UNDERFLOW

#include "utils/parserutilserror.h"
#include "bytecode/bytecode.h"
#include "bytecode/opcodes.h"
#include "parse/properties/properties.h"
#include "parse/colour.h"
#include "parse/properties/utils.h"
#include "stylesheet.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif


/* CSS Lists <counter-name> adds none to the custom-identifier exclusions. */
bool css__is_counter_name(const css_token *token)
{
    return token != NULL && token->type == CSS_TOKEN_IDENT && css__is_custom_ident(token->idata) &&
        !css__keyword_equal(token->idata, "none");
}


/**
 * Parse list-style-type value
 *
 * \param c	 Parsing context
 * \param ident	 Identifier to consider
 * \param value	 Pointer to location to receive value
 * \return CSS_OK on success,
 *	   CSS_INVALID if the input is not valid
 */
css_error css__parse_list_style_type_value(css_language *c, const css_token *ident, uint16_t *value)
{
/* IDENT (disc, circle, square, decimal, decimal-leading-zero,
 *	  lower-roman, upper-roman, lower-greek, lower-latin,
 *	  upper-latin, armenian, georgian, lower-alpha, upper-alpha,
 *	  none)
 */
    bool match;
    const struct {
        int stringid;
        int value;
    } mapping[] = {{DISC, LIST_STYLE_TYPE_DISC}, {CIRCLE, LIST_STYLE_TYPE_CIRCLE},
        {SQUARE, LIST_STYLE_TYPE_SQUARE}, {DECIMAL, LIST_STYLE_TYPE_DECIMAL},
        {DECIMAL_LEADING_ZERO, LIST_STYLE_TYPE_DECIMAL_LEADING_ZERO}, {LOWER_ROMAN, LIST_STYLE_TYPE_LOWER_ROMAN},
        {UPPER_ROMAN, LIST_STYLE_TYPE_UPPER_ROMAN}, {LOWER_GREEK, LIST_STYLE_TYPE_LOWER_GREEK},
        {LOWER_LATIN, LIST_STYLE_TYPE_LOWER_LATIN}, {UPPER_LATIN, LIST_STYLE_TYPE_UPPER_LATIN},
        {ARMENIAN, LIST_STYLE_TYPE_ARMENIAN}, {GEORGIAN, LIST_STYLE_TYPE_GEORGIAN},
        {LOWER_ALPHA, LIST_STYLE_TYPE_LOWER_ALPHA}, {UPPER_ALPHA, LIST_STYLE_TYPE_UPPER_ALPHA},
        {NONE, LIST_STYLE_TYPE_NONE}, {BINARY, LIST_STYLE_TYPE_BINARY}, {OCTAL, LIST_STYLE_TYPE_OCTAL},
        {LOWER_HEXADECIMAL, LIST_STYLE_TYPE_LOWER_HEXADECIMAL}, {UPPER_HEXADECIMAL, LIST_STYLE_TYPE_UPPER_HEXADECIMAL},
        {ARABIC_INDIC, LIST_STYLE_TYPE_ARABIC_INDIC}, {LOWER_ARMENIAN, LIST_STYLE_TYPE_LOWER_ARMENIAN},
        {UPPER_ARMENIAN, LIST_STYLE_TYPE_UPPER_ARMENIAN}, {BENGALI, LIST_STYLE_TYPE_BENGALI},
        {CAMBODIAN, LIST_STYLE_TYPE_CAMBODIAN}, {KHMER, LIST_STYLE_TYPE_KHMER},
        {CJK_DECIMAL, LIST_STYLE_TYPE_CJK_DECIMAL}, {DEVANAGARI, LIST_STYLE_TYPE_DEVANAGARI},
        {GUJARATI, LIST_STYLE_TYPE_GUJARATI}, {GURMUKHI, LIST_STYLE_TYPE_GURMUKHI}, {HEBREW, LIST_STYLE_TYPE_HEBREW},
        {KANNADA, LIST_STYLE_TYPE_KANNADA}, {LAO, LIST_STYLE_TYPE_LAO}, {MALAYALAM, LIST_STYLE_TYPE_MALAYALAM},
        {MONGOLIAN, LIST_STYLE_TYPE_MONGOLIAN}, {MYANMAR, LIST_STYLE_TYPE_MYANMAR}, {ORIYA, LIST_STYLE_TYPE_ORIYA},
        {PERSIAN, LIST_STYLE_TYPE_PERSIAN}, {TAMIL, LIST_STYLE_TYPE_TAMIL}, {TELUGU, LIST_STYLE_TYPE_TELUGU},
        {THAI, LIST_STYLE_TYPE_THAI}, {TIBETAN, LIST_STYLE_TYPE_TIBETAN},
        {CJK_EARTHLY_BRANCH, LIST_STYLE_TYPE_CJK_EARTHLY_BRANCH},
        {CJK_HEAVENLY_STEM, LIST_STYLE_TYPE_CJK_HEAVENLY_STEM}, {HIAGANA, LIST_STYLE_TYPE_HIAGANA},
        {HIAGANA_IROHA, LIST_STYLE_TYPE_HIAGANA_IROHA}, {KATAKANA, LIST_STYLE_TYPE_KATAKANA},
        {KATAKANA_IROHA, LIST_STYLE_TYPE_KATAKANA_IROHA}, {JAPANESE_INFORMAL, LIST_STYLE_TYPE_JAPANESE_INFORMAL},
        {JAPANESE_FORMAL, LIST_STYLE_TYPE_JAPANESE_FORMAL},
        {KOREAN_HANGUL_FORMAL, LIST_STYLE_TYPE_KOREAN_HANGUL_FORMAL},
        {KOREAN_HANJA_INFORMAL, LIST_STYLE_TYPE_KOREAN_HANJA_INFORMAL},
        {KOREAN_HANJA_FORMAL, LIST_STYLE_TYPE_KOREAN_HANJA_FORMAL},
        {DISCLOSURE_OPEN, LIST_STYLE_TYPE_DISCLOSURE_OPEN}, {DISCLOSURE_CLOSED, LIST_STYLE_TYPE_DISCLOSURE_CLOSED}};

    for (size_t midx = 0; midx < sizeof(mapping) / sizeof(mapping[0]); midx++) {
        if ((lwc_string_caseless_isequal(ident->idata, c->strings[mapping[midx].stringid], &match) == lwc_error_ok &&
                match)) {
            *value = mapping[midx].value;
            return CSS_OK;
        }
    }

    return CSS_INVALID;
}


/* All three values are optional, but at least one must be supplied. Keep
 * CSS-wide keywords at the shorthand boundary, and do not swallow fatal errors
 * while trying alternative components. The caller validates any trailing input. */
css_error css__parse_border_shorthand(css_language *c, const parserutils_vector *vector, int32_t *ctx,
    css_style *result, const struct css_border_shorthand *syntax)
{
    int32_t orig_ctx = *ctx;
    uint32_t orig_used = result->used;
    const css_token *token = parserutils_vector_peek(vector, *ctx);
    if (token == NULL)
        return CSS_INVALID;
    css_style *parts[3] = {0};
    css_error error = CSS_OK;
    enum flag_value flag = get_css_flag_value(c, token);
    if (flag != FLAG_VALUE__NONE) {
        for (size_t i = 0; i < 3; i++) {
            error = css_stylesheet_style_flag_value(result, flag, syntax->properties[i]);
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
    unsigned supplied = 0;
    while (supplied < 3 && (token = parserutils_vector_peek(vector, *ctx)) != NULL) {
        if (get_css_flag_value(c, token) != FLAG_VALUE__NONE) {
            error = CSS_INVALID;
            goto done;
        }
        error = CSS_INVALID;
        for (size_t i = 0; i < 3 && error == CSS_INVALID; i++) {
            if (parts[i]->used == 0)
                error = syntax->handlers[i](c, vector, ctx, parts[i]);
        }
        if (error == CSS_INVALID)
            break;
        if (error != CSS_OK)
            goto done;
        supplied++;
        consumeWhitespace(vector, ctx);
    }
    if (supplied == 0) {
        error = CSS_INVALID;
        goto done;
    }
    for (size_t i = 0; i < 3; i++) {
        if (parts[i]->used == 0) {
            error = css__stylesheet_style_appendOPV(parts[i], syntax->properties[i], 0, syntax->defaults[i]);
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

css_error css__parse_border_side(
    css_language *c, const parserutils_vector *vector, int32_t *ctx, css_style *result, enum border_side_e side)
{
    static const struct css_border_shorthand syntax[] = {
        {{CSS_PROP_BORDER_TOP_COLOR, CSS_PROP_BORDER_TOP_STYLE, CSS_PROP_BORDER_TOP_WIDTH},
            {css__parse_border_top_color, css__parse_border_top_style, css__parse_border_top_width},
            {BORDER_COLOR_CURRENT_COLOR, BORDER_STYLE_NONE, BORDER_WIDTH_MEDIUM}},
        {{CSS_PROP_BORDER_RIGHT_COLOR, CSS_PROP_BORDER_RIGHT_STYLE, CSS_PROP_BORDER_RIGHT_WIDTH},
            {css__parse_border_right_color, css__parse_border_right_style, css__parse_border_right_width},
            {BORDER_COLOR_CURRENT_COLOR, BORDER_STYLE_NONE, BORDER_WIDTH_MEDIUM}},
        {{CSS_PROP_BORDER_BOTTOM_COLOR, CSS_PROP_BORDER_BOTTOM_STYLE, CSS_PROP_BORDER_BOTTOM_WIDTH},
            {css__parse_border_bottom_color, css__parse_border_bottom_style, css__parse_border_bottom_width},
            {BORDER_COLOR_CURRENT_COLOR, BORDER_STYLE_NONE, BORDER_WIDTH_MEDIUM}},
        {{CSS_PROP_BORDER_LEFT_COLOR, CSS_PROP_BORDER_LEFT_STYLE, CSS_PROP_BORDER_LEFT_WIDTH},
            {css__parse_border_left_color, css__parse_border_left_style, css__parse_border_left_width},
            {BORDER_COLOR_CURRENT_COLOR, BORDER_STYLE_NONE, BORDER_WIDTH_MEDIUM}},
    };
    return css__parse_border_shorthand(c, vector, ctx, result, &syntax[side]);
}

static inline uint8_t linear_srgb_to_srgb8(double linear) {
    double srgb;
    if (linear <= 0.0) srgb = 0.0;
    else if (linear >= 1.0) srgb = 1.0;
    else if (linear <= 0.0031308) srgb = 12.92 * linear;
    else srgb = 1.055 * pow(linear, 1.0 / 2.4) - 0.055;
    
    int val = (int)(srgb * 255.0 + 0.5);
    if (val < 0) return 0;
    if (val > 255) return 255;
    return val;
}

static void OKLAB_to_RGB(double L, double a, double b, uint8_t *r_out, uint8_t *g_out, uint8_t *b_out) {
    double l_ = L + 0.3963377774 * a + 0.2158037573 * b;
    double m_ = L - 0.1055613458 * a - 0.0638541728 * b;
    double s_ = L - 0.0894841775 * a - 1.2914855480 * b;

    l_ = l_ * l_ * l_;
    m_ = m_ * m_ * m_;
    s_ = s_ * s_ * s_;

    double r_lin = +4.0767416621 * l_ - 3.3077115913 * m_ + 0.2309699292 * s_;
    double g_lin = -1.2684380046 * l_ + 2.6097574011 * m_ - 0.3413193965 * s_;
    double b_lin = -0.0041960863 * l_ - 0.7034186147 * m_ + 1.7076147010 * s_;

    *r_out = linear_srgb_to_srgb8(r_lin);
    *g_out = linear_srgb_to_srgb8(g_lin);
    *b_out = linear_srgb_to_srgb8(b_lin);
}

static void LAB_to_RGB(double L, double a, double b, uint8_t *r_out, uint8_t *g_out, uint8_t *b_out) {
    // 1. Convert LAB to D50 XYZ
    double y = (L + 16.0) / 116.0;
    double x = a / 500.0 + y;
    double z = y - b / 200.0;
    
    double x3 = x * x * x;
    double z3 = z * z * z;
    
    x = (x3 > 0.008856) ? x3 : (x - 16.0 / 116.0) / 7.787;
    y = (L > 8.0) ? (y * y * y) : L / 903.3;
    z = (z3 > 0.008856) ? z3 : (z - 16.0 / 116.0) / 7.787;
    
    // D50 reference white
    x *= 0.96422;
    y *= 1.00000;
    z *= 0.82521;
    
    // 2. Convert D50 XYZ to D65 XYZ (Bradford adaptation)
    double x65 =  0.956456 * x - 0.022641 * y + 0.045987 * z;
    double y65 = -0.027050 * x + 1.000000 * y + 0.029864 * z;
    double z65 =  0.003920 * x - 0.015690 * y + 1.096894 * z;
    
    // 3. Convert D65 XYZ to Linear sRGB
    double r_lin =  3.2404542 * x65 - 1.5371385 * y65 - 0.4985314 * z65;
    double g_lin = -0.9692660 * x65 + 1.8760108 * y65 + 0.0415560 * z65;
    double b_lin =  0.0556434 * x65 - 0.2040259 * y65 + 1.0572252 * z65;
    
    *r_out = linear_srgb_to_srgb8(r_lin);
    *g_out = linear_srgb_to_srgb8(g_lin);
    *b_out = linear_srgb_to_srgb8(b_lin);
}

/**
 * Parse a lab/oklab colour specifier
 *
 * \param vector  Vector of tokens to process
 * \param ctx     Pointer to vector iteration context
 * \param result  Pointer to location to receive result (AARRGGBB)
 * \return true on success, false on error.
 */
static bool parse_lab(const parserutils_vector *vector, int32_t *ctx, uint32_t *result, bool is_ok)
{
    const css_token *token;
    size_t consumed = 0;
    int i;
    double L = 0.0, A = 0.0, B = 0.0;
    int32_t alpha = 255;

    consumeWhitespace(vector, ctx);

    /* Expect 3 values (L, A, B) */
    for (i = 0; i < 3; i++) {
        token = parserutils_vector_iterate(vector, ctx);
        if (token == NULL || (token->type != CSS_TOKEN_NUMBER && token->type != CSS_TOKEN_PERCENTAGE))
            return false;

        css_fixed num = css__number_from_lwc_string(token->idata, false, &consumed);
        if (consumed != lwc_string_length(token->idata))
            return false;
            
        double val = FIXTOINT(num) + ((double)(num & ((1<<10)-1)) / (1<<10));
        
        // CSS spec says LAB Lightness percentage is 0-100%, A/B are unbounded but often scaled around 125
        // OKLAB Lightness percentage is 0-100% mapped to 0.0-1.0, A/B are percentage mapped to -0.4 to 0.4
        if (token->type == CSS_TOKEN_PERCENTAGE) {
            if (is_ok) {
                if (i == 0) val = val / 100.0;
                else val = (val / 100.0) * 0.4;
            } else {
                if (i > 0) val = (val / 100.0) * 125.0;
            }
        }
        
        if (i == 0) L = val;
        else if (i == 1) A = val;
        else if (i == 2) B = val;

        consumeWhitespace(vector, ctx);
    }

    /* Optional alpha */
    token = parserutils_vector_peek(vector, *ctx);
    if (tokenIsChar(token, '/')) {
        parserutils_vector_iterate(vector, ctx);
        consumeWhitespace(vector, ctx);

        token = parserutils_vector_iterate(vector, ctx);
        if (token == NULL || (token->type != CSS_TOKEN_NUMBER && token->type != CSS_TOKEN_PERCENTAGE))
            return false;

        css_fixed num = css__number_from_lwc_string(token->idata, false, &consumed);
        if (consumed != lwc_string_length(token->idata))
            return false;
            
        if (token->type == CSS_TOKEN_NUMBER) {
            alpha = FIXTOINT(FMUL(num, F_255));
        } else {
            alpha = FIXTOINT(FDIV(FMUL(num, F_255), F_100));
        }

        consumeWhitespace(vector, ctx);
    }

    token = parserutils_vector_iterate(vector, ctx);
    if (!tokenIsChar(token, ')'))
        return false;

    uint8_t r, g, b, a;
    
    if (alpha > 255) {
        a = 255;
    } else if (alpha < 0) {
        a = 0;
    } else {
        a = alpha;
    }

    if (is_ok) {
        OKLAB_to_RGB(L, A, B, &r, &g, &b);
    } else {
        LAB_to_RGB(L, A, B, &r, &g, &b);
    }

    *result = ((unsigned)a << 24) | (r << 16) | (g << 8) | b;
    return true;
}

/**
 * Parse a lch/oklch colour specifier
 *
 * \param vector  Vector of tokens to process
 * \param ctx     Pointer to vector iteration context
 * \param result  Pointer to location to receive result (AARRGGBB)
 * \return true on success, false on error.
 */
static bool parse_lch(const parserutils_vector *vector, int32_t *ctx, uint32_t *result, bool is_ok)
{
    const css_token *token;
    size_t consumed = 0;
    int i;
    double L = 0.0, C = 0.0, H = 0.0;
    int32_t alpha = 255;

    consumeWhitespace(vector, ctx);

    /* Expect 3 values (L, C, H) */
    for (i = 0; i < 3; i++) {
        token = parserutils_vector_iterate(vector, ctx);
        /* H can be angle (DIMENSION) or number */
        if (token == NULL ||
            (token->type != CSS_TOKEN_NUMBER && token->type != CSS_TOKEN_PERCENTAGE &&
                (i == 2 && token->type != CSS_TOKEN_DIMENSION)))
            return false;

        double val = 0.0;

        if (token->type == CSS_TOKEN_DIMENSION) {
            /* Parse angle unit */
            size_t len = lwc_string_length(token->idata);
            const char *data = lwc_string_data(token->idata);
            uint32_t unit = UNIT_DEG;
            css_fixed num = css__number_from_lwc_string(token->idata, false, &consumed);
            if (css__parse_unit_keyword(data + consumed, len - consumed, &unit) != CSS_OK)
                return false;
                
            val = FIXTOINT(num) + ((double)(num & ((1<<10)-1)) / (1<<10));
            if (unit == UNIT_RAD) val = val * 180.0 / M_PI;
            else if (unit == UNIT_GRAD) val = val * 0.9;
            else if (unit == UNIT_TURN) val = val * 360.0;
        } else {
            css_fixed num = css__number_from_lwc_string(token->idata, false, &consumed);
            if (consumed != lwc_string_length(token->idata))
                return false;
            val = FIXTOINT(num) + ((double)(num & ((1<<10)-1)) / (1<<10));
        }
        
        if (token->type == CSS_TOKEN_PERCENTAGE) {
            if (is_ok) {
                if (i == 0) val = val / 100.0;
                else if (i == 1) val = (val / 100.0) * 0.4;
            } else {
                if (i == 1) val = (val / 100.0) * 150.0;
            }
        }
        
        if (i == 0) L = val;
        else if (i == 1) C = val;
        else if (i == 2) H = val;

        consumeWhitespace(vector, ctx);
    }

    /* Optional alpha */
    token = parserutils_vector_peek(vector, *ctx);
    if (tokenIsChar(token, '/')) {
        parserutils_vector_iterate(vector, ctx);
        consumeWhitespace(vector, ctx);

        token = parserutils_vector_iterate(vector, ctx);
        if (token == NULL || (token->type != CSS_TOKEN_NUMBER && token->type != CSS_TOKEN_PERCENTAGE))
            return false;

        css_fixed num = css__number_from_lwc_string(token->idata, false, &consumed);
        if (consumed != lwc_string_length(token->idata))
            return false;
            
        if (token->type == CSS_TOKEN_NUMBER) {
            alpha = FIXTOINT(FMUL(num, F_255));
        } else {
            alpha = FIXTOINT(FDIV(FMUL(num, F_255), F_100));
        }

        consumeWhitespace(vector, ctx);
    }

    token = parserutils_vector_iterate(vector, ctx);
    if (!tokenIsChar(token, ')'))
        return false;

    uint8_t r, g, b, a;
    
    if (alpha > 255) {
        a = 255;
    } else if (alpha < 0) {
        a = 0;
    } else {
        a = alpha;
    }

    // Convert Polar to Cartesian
    double A = C * cos(H * M_PI / 180.0);
    double B = C * sin(H * M_PI / 180.0);

    if (is_ok) {
        OKLAB_to_RGB(L, A, B, &r, &g, &b);
    } else {
        LAB_to_RGB(L, A, B, &r, &g, &b);
    }

    *result = ((unsigned)a << 24) | (r << 16) | (g << 8) | b;
    return true;
}

/**
 * Parse a colour specifier
 *
 * \param c       Parsing context
 * \param vector  Vector of tokens to process
 * \param ctx     Pointer to vector iteration context
 * \param value   Pointer to location to receive value
 * \param result  Pointer to location to receive result (AARRGGBB)
 * \return CSS_OK      on success,
 *         CSS_INVALID if the input is invalid
 *
 * Post condition: \a *ctx is updated with the next token to process
 *                 If the input is invalid, then \a *ctx remains unchanged.
 */
css_error css__parse_colour_specifier(
    css_language *c, const parserutils_vector *vector, int32_t *ctx, uint16_t *value, uint32_t *result)
{
    int32_t orig_ctx = *ctx;
    const css_token *token;
    bool match;
    css_error error;

    consumeWhitespace(vector, ctx);

    /* IDENT(<colour name>) |
     * HASH(rgb | rgba | rrggbb | rrggbbaa) |
     * FUNCTION(rgb) [ [ NUMBER | PERCENTAGE ] ',' ] {3} ')'
     * FUNCTION(rgba) [ [ NUMBER | PERCENTAGE ] ',' ] {4} ')'
     * FUNCTION(hsl) ANGLE ',' PERCENTAGE ',' PERCENTAGE  ')'
     * FUNCTION(hsla) ANGLE ',' PERCENTAGE ',' PERCENTAGE ',' NUMBER ')'
     *
     * For quirks, NUMBER | DIMENSION | IDENT, too
     * I.E. "123456" -> NUMBER, "1234f0" -> DIMENSION, "f00000" -> IDENT
     */
    token = parserutils_vector_iterate(vector, ctx);
    if (token == NULL ||
        (token->type != CSS_TOKEN_IDENT && token->type != CSS_TOKEN_HASH && token->type != CSS_TOKEN_FUNCTION)) {
        if (c->sheet->quirks_allowed == false || token == NULL ||
            (token->type != CSS_TOKEN_NUMBER && token->type != CSS_TOKEN_DIMENSION))
            goto invalid;
    }

    if (token->type == CSS_TOKEN_IDENT) {
        if ((lwc_string_caseless_isequal(token->idata, c->strings[TRANSPARENT], &match) == lwc_error_ok && match)) {
            *value = COLOR_TRANSPARENT;
            *result = 0; /* black transparent */
            return CSS_OK;
        } else if ((lwc_string_caseless_isequal(token->idata, c->strings[CURRENTCOLOR], &match) == lwc_error_ok &&
                       match)) {
            *value = COLOR_CURRENT_COLOR;
            *result = 0;
            return CSS_OK;
        }

        error = css__parse_named_colour(c, token->idata, result);
        if (error != CSS_OK && c->sheet->quirks_allowed) {
            error = css__parse_hash_colour(token->idata, result);
            if (error == CSS_OK)
                c->sheet->quirks_used = true;
        }

        if (error != CSS_OK)
            goto invalid;
    } else if (token->type == CSS_TOKEN_HASH) {
        error = css__parse_hash_colour(token->idata, result);
        if (error != CSS_OK)
            goto invalid;
    } else if (c->sheet->quirks_allowed && token->type == CSS_TOKEN_NUMBER) {
        error = css__parse_hash_colour(token->idata, result);
        if (error == CSS_OK)
            c->sheet->quirks_used = true;
        else
            goto invalid;
    } else if (c->sheet->quirks_allowed && token->type == CSS_TOKEN_DIMENSION) {
        error = css__parse_hash_colour(token->idata, result);
        if (error == CSS_OK)
            c->sheet->quirks_used = true;
        else
            goto invalid;
    } else if (token->type == CSS_TOKEN_FUNCTION) {
        if (css__keyword_equal(token->idata, "rgb") || css__keyword_equal(token->idata, "rgba") ||
            css__keyword_equal(token->idata, "hsl") || css__keyword_equal(token->idata, "hsla") ||
            css__keyword_equal(token->idata, "hwb")) {
            if (!css__parse_colour_function(token->idata, vector, ctx, result))
                goto invalid;
        } else if ((lwc_string_caseless_isequal(token->idata, c->strings[LAB], &match) == lwc_error_ok && match)) {
            if (!parse_lab(vector, ctx, result, false)) {
                goto invalid;
            }
        } else if ((lwc_string_caseless_isequal(token->idata, c->strings[OKLAB], &match) == lwc_error_ok && match)) {
            if (!parse_lab(vector, ctx, result, true)) {
                goto invalid;
            }
        } else if ((lwc_string_caseless_isequal(token->idata, c->strings[LCH], &match) == lwc_error_ok && match)) {
            if (!parse_lch(vector, ctx, result, false)) {
                goto invalid;
            }
        } else if ((lwc_string_caseless_isequal(token->idata, c->strings[OKLCH], &match) == lwc_error_ok && match)) {
            if (!parse_lch(vector, ctx, result, true)) {
                goto invalid;
            }
        } else {
            /* Unsupported functions must reject the declaration so an earlier
             * fallback can win. In particular, color() needs real colour-space
             * handling; reporting success with transparent black is incorrect. */
            goto invalid;
        }
    }

    *value = COLOR_SET;

    return CSS_OK;

invalid:
    *ctx = orig_ctx;
    return CSS_INVALID;
}

/**
 * Parse a named colour
 *
 * \param c       Parsing context
 * \param data    Colour name string
 * \param result  Pointer to location to receive result
 * \return CSS_OK      on success,
 *         CSS_INVALID if the colour name is unknown
 */
css_error css__parse_named_colour(css_language *c, lwc_string *data, uint32_t *result)
{
    static const uint32_t colourmap[LAST_COLOUR + 1 - FIRST_COLOUR] = {
        0xfff0f8ff, /* ALICEBLUE */
        0xfffaebd7, /* ANTIQUEWHITE */
        0xff00ffff, /* AQUA */
        0xff7fffd4, /* AQUAMARINE */
        0xfff0ffff, /* AZURE */
        0xfff5f5dc, /* BEIGE */
        0xffffe4c4, /* BISQUE */
        0xff000000, /* BLACK */
        0xffffebcd, /* BLANCHEDALMOND */
        0xff0000ff, /* BLUE */
        0xff8a2be2, /* BLUEVIOLET */
        0xffa52a2a, /* BROWN */
        0xffdeb887, /* BURLYWOOD */
        0xff5f9ea0, /* CADETBLUE */
        0xff7fff00, /* CHARTREUSE */
        0xffd2691e, /* CHOCOLATE */
        0xffff7f50, /* CORAL */
        0xff6495ed, /* CORNFLOWERBLUE */
        0xfffff8dc, /* CORNSILK */
        0xffdc143c, /* CRIMSON */
        0xff00ffff, /* CYAN */
        0xff00008b, /* DARKBLUE */
        0xff008b8b, /* DARKCYAN */
        0xffb8860b, /* DARKGOLDENROD */
        0xffa9a9a9, /* DARKGRAY */
        0xff006400, /* DARKGREEN */
        0xffa9a9a9, /* DARKGREY */
        0xffbdb76b, /* DARKKHAKI */
        0xff8b008b, /* DARKMAGENTA */
        0xff556b2f, /* DARKOLIVEGREEN */
        0xffff8c00, /* DARKORANGE */
        0xff9932cc, /* DARKORCHID */
        0xff8b0000, /* DARKRED */
        0xffe9967a, /* DARKSALMON */
        0xff8fbc8f, /* DARKSEAGREEN */
        0xff483d8b, /* DARKSLATEBLUE */
        0xff2f4f4f, /* DARKSLATEGRAY */
        0xff2f4f4f, /* DARKSLATEGREY */
        0xff00ced1, /* DARKTURQUOISE */
        0xff9400d3, /* DARKVIOLET */
        0xffff1493, /* DEEPPINK */
        0xff00bfff, /* DEEPSKYBLUE */
        0xff696969, /* DIMGRAY */
        0xff696969, /* DIMGREY */
        0xff1e90ff, /* DODGERBLUE */
        0xffd19275, /* FELDSPAR */
        0xffb22222, /* FIREBRICK */
        0xfffffaf0, /* FLORALWHITE */
        0xff228b22, /* FORESTGREEN */
        0xffff00ff, /* FUCHSIA */
        0xffdcdcdc, /* GAINSBORO */
        0xfff8f8ff, /* GHOSTWHITE */
        0xffffd700, /* GOLD */
        0xffdaa520, /* GOLDENROD */
        0xff808080, /* GRAY */
        0xff008000, /* GREEN */
        0xffadff2f, /* GREENYELLOW */
        0xff808080, /* GREY */
        0xfff0fff0, /* HONEYDEW */
        0xffff69b4, /* HOTPINK */
        0xffcd5c5c, /* INDIANRED */
        0xff4b0082, /* INDIGO */
        0xfffffff0, /* IVORY */
        0xfff0e68c, /* KHAKI */
        0xffe6e6fa, /* LAVENDER */
        0xfffff0f5, /* LAVENDERBLUSH */
        0xff7cfc00, /* LAWNGREEN */
        0xfffffacd, /* LEMONCHIFFON */
        0xffadd8e6, /* LIGHTBLUE */
        0xfff08080, /* LIGHTCORAL */
        0xffe0ffff, /* LIGHTCYAN */
        0xfffafad2, /* LIGHTGOLDENRODYELLOW */
        0xffd3d3d3, /* LIGHTGRAY */
        0xff90ee90, /* LIGHTGREEN */
        0xffd3d3d3, /* LIGHTGREY */
        0xffffb6c1, /* LIGHTPINK */
        0xffffa07a, /* LIGHTSALMON */
        0xff20b2aa, /* LIGHTSEAGREEN */
        0xff87cefa, /* LIGHTSKYBLUE */
        0xff8470ff, /* LIGHTSLATEBLUE */
        0xff778899, /* LIGHTSLATEGRAY */
        0xff778899, /* LIGHTSLATEGREY */
        0xffb0c4de, /* LIGHTSTEELBLUE */
        0xffffffe0, /* LIGHTYELLOW */
        0xff00ff00, /* LIME */
        0xff32cd32, /* LIMEGREEN */
        0xfffaf0e6, /* LINEN */
        0xffff00ff, /* MAGENTA */
        0xff800000, /* MAROON */
        0xff66cdaa, /* MEDIUMAQUAMARINE */
        0xff0000cd, /* MEDIUMBLUE */
        0xffba55d3, /* MEDIUMORCHID */
        0xff9370db, /* MEDIUMPURPLE */
        0xff3cb371, /* MEDIUMSEAGREEN */
        0xff7b68ee, /* MEDIUMSLATEBLUE */
        0xff00fa9a, /* MEDIUMSPRINGGREEN */
        0xff48d1cc, /* MEDIUMTURQUOISE */
        0xffc71585, /* MEDIUMVIOLETRED */
        0xff191970, /* MIDNIGHTBLUE */
        0xfff5fffa, /* MINTCREAM */
        0xffffe4e1, /* MISTYROSE */
        0xffffe4b5, /* MOCCASIN */
        0xffffdead, /* NAVAJOWHITE */
        0xff000080, /* NAVY */
        0xfffdf5e6, /* OLDLACE */
        0xff808000, /* OLIVE */
        0xff6b8e23, /* OLIVEDRAB */
        0xffffa500, /* ORANGE */
        0xffff4500, /* ORANGERED */
        0xffda70d6, /* ORCHID */
        0xffeee8aa, /* PALEGOLDENROD */
        0xff98fb98, /* PALEGREEN */
        0xffafeeee, /* PALETURQUOISE */
        0xffdb7093, /* PALEVIOLETRED */
        0xffffefd5, /* PAPAYAWHIP */
        0xffffdab9, /* PEACHPUFF */
        0xffcd853f, /* PERU */
        0xffffc0cb, /* PINK */
        0xffdda0dd, /* PLUM */
        0xffb0e0e6, /* POWDERBLUE */
        0xff800080, /* PURPLE */
        0xffff0000, /* RED */
        0xffbc8f8f, /* ROSYBROWN */
        0xff4169e1, /* ROYALBLUE */
        0xff8b4513, /* SADDLEBROWN */
        0xfffa8072, /* SALMON */
        0xfff4a460, /* SANDYBROWN */
        0xff2e8b57, /* SEAGREEN */
        0xfffff5ee, /* SEASHELL */
        0xffa0522d, /* SIENNA */
        0xffc0c0c0, /* SILVER */
        0xff87ceeb, /* SKYBLUE */
        0xff6a5acd, /* SLATEBLUE */
        0xff708090, /* SLATEGRAY */
        0xff708090, /* SLATEGREY */
        0xfffffafa, /* SNOW */
        0xff00ff7f, /* SPRINGGREEN */
        0xff4682b4, /* STEELBLUE */
        0xffd2b48c, /* TAN */
        0xff008080, /* TEAL */
        0xffd8bfd8, /* THISTLE */
        0xffff6347, /* TOMATO */
        0xff40e0d0, /* TURQUOISE */
        0xffee82ee, /* VIOLET */
        0xffd02090, /* VIOLETRED */
        0xfff5deb3, /* WHEAT */
        0xffffffff, /* WHITE */
        0xfff5f5f5, /* WHITESMOKE */
        0xffffff00, /* YELLOW */
        0xff9acd32, /* YELLOWGREEN */
        0xff663399 /* REBECCAPURPLE */
    };
    /** Legacy system colour mapping */
    static const int deprecatedmap[LAST_DEPRECATEDCOLOUR + 1 - FIRST_DEPRECATEDCOLOUR] = {
        BUTTONBORDER, /* ACTIVEBORDER */
        CANVAS, /* ACTIVECAPTION */
        CANVAS, /* APPWORKSPACE */
        CANVAS, /* BACKGROUND */
        BUTTONFACE, /* BUTTONHIGHLIGHT */
        BUTTONFACE, /* BUTTONSHADOW */
        CANVASTEXT, /* CAPTIONTEXT */
        BUTTONBORDER, /* INACTIVEBORDER */
        CANVAS, /* INACTIVECAPTION */
        GRAYTEXT, /* INACTIVECAPTIONTEXT */
        CANVAS, /* INFOBACKGROUND */
        CANVASTEXT, /* INFOTEXT */
        CANVAS, /* MENU */
        CANVASTEXT, /* MENUTEXT */
        CANVAS, /* SCROLLBAR */
        BUTTONBORDER, /* THREEDDARKSHADOW */
        BUTTONFACE, /* THREEDFACE */
        BUTTONBORDER, /* THREEDHIGHLIGHT */
        BUTTONBORDER, /* THREEDLIGHTSHADOW */
        BUTTONBORDER, /* THREEDSHADOW */
        CANVAS, /* WINDOW */
        BUTTONBORDER, /* WINDOWFRAME */
        CANVASTEXT, /* WINDOWTEXT */
    };
    int i;
    bool match;

    /* try to match a named colour */
    for (i = FIRST_COLOUR; i <= LAST_COLOUR; i++) {
        if (lwc_string_caseless_isequal(data, c->strings[i], &match) == lwc_error_ok && match) {
            /* Known named colour */
            *result = colourmap[i - FIRST_COLOUR];
            return CSS_OK;
        }
    }

    /* map deprecated system colours to current system colours */
    for (i = FIRST_DEPRECATEDCOLOUR; i <= LAST_DEPRECATEDCOLOUR; i++) {
        if (lwc_string_caseless_isequal(data, c->strings[i], &match) == lwc_error_ok && match) {
            /* Known legacy system named colour */
            data = c->strings[deprecatedmap[i - FIRST_DEPRECATEDCOLOUR]];
            break;
        }
    }

    /* attempt to get client to map colour */
    if (c->sheet->color != NULL) {
        css_error err = c->sheet->color(c->sheet->color_pw, data, result);
        if (err == CSS_OK)
            return CSS_OK;
    }

    for (i = FIRST_SYSTEMCOLOUR; i <= LAST_SYSTEMCOLOUR; i++) {
        if (lwc_string_caseless_isequal(data, c->strings[i], &match) == lwc_error_ok && match) {
            static const uint32_t systemcolourmap[LAST_SYSTEMCOLOUR + 1 - FIRST_SYSTEMCOLOUR] = {0xff0078d7, 0xffffffff,
                0xff000000, 0xff767676, 0xffd4d4d4, 0xff000000, 0xffffffff, 0xff000000, 0xffffffff, 0xff000000,
                0xff6d6d6d, 0xff3399ff, 0xffffffff, 0xff0000ee, 0xffffff00, 0xff000000, 0xff3399ff, 0xffffffff,
                0xff551a8b};
            *result = systemcolourmap[i - FIRST_SYSTEMCOLOUR];
            return CSS_OK;
        }
    }

    /* Invalid colour name */
    return CSS_INVALID;
}

/**
 * Parse a hash colour (#rgb, #rgba, #rrggbb or #rrggbbaa)
 *
 * \param data    Pointer to colour string
 * \param result  Pointer to location to receive result (AARRGGBB)
 * \return CSS_OK      on success,
 *         CSS_INVALID if the input is invalid
 */
css_error css__parse_hash_colour(lwc_string *data, uint32_t *result)
{
    uint8_t r = 0, g = 0, b = 0, a = 0xff;
    size_t len = lwc_string_length(data);
    const char *input = lwc_string_data(data);

    switch (len) {
    case 4:
        if (!isHex(input[3])) {
            return CSS_INVALID;
        }
        a = charToHex(input[3]);
        a |= (a << 4);
        /* Fall through */
    case 3:
        if (!isHex(input[0]) || !isHex(input[1]) || !isHex(input[2])) {
            return CSS_INVALID;
        }
        r = charToHex(input[0]);
        g = charToHex(input[1]);
        b = charToHex(input[2]);

        r |= (r << 4);
        g |= (g << 4);
        b |= (b << 4);
        break;
    case 8:
        if (!isHex(input[6]) || !isHex(input[7])) {
            return CSS_INVALID;
        }
        a = (charToHex(input[6]) << 4);
        a |= charToHex(input[7]);
        /* Fall through */
    case 6:
        if (!isHex(input[0]) || !isHex(input[1]) || !isHex(input[2]) || !isHex(input[3]) || !isHex(input[4]) ||
            !isHex(input[5])) {
            return CSS_INVALID;
        }
        r = (charToHex(input[0]) << 4);
        r |= charToHex(input[1]);
        g = (charToHex(input[2]) << 4);
        g |= charToHex(input[3]);
        b = (charToHex(input[4]) << 4);
        b |= charToHex(input[5]);
        break;
    default:
        return CSS_INVALID;
    }

    *result = ((unsigned)a << 24) | (r << 16) | (g << 8) | b;

    return CSS_OK;
}

/**
 * Parse a unit specifier
 *
 * \param c             Parsing context
 * \param vector        Vector of tokens to process
 * \param ctx           Pointer to current vector iteration context
 * \param default_unit  The default unit to use if none specified
 * \param length        Pointer to location to receive length
 * \param unit          Pointer to location to receive unit
 * \return CSS_OK      on success,
 *         CSS_INVALID if the tokens do not form a valid unit
 *
 * Post condition: \a *ctx is updated with the next token to process
 *                 If the input is invalid, then \a *ctx remains unchanged.
 */
css_error css__parse_unit_specifier(css_language *c, const parserutils_vector *vector, int32_t *ctx,
    uint32_t default_unit, css_fixed *length, uint32_t *unit)
{
    int32_t orig_ctx = *ctx;
    const css_token *token;
    css_fixed num;
    size_t consumed = 0;
    css_error error;

    consumeWhitespace(vector, ctx);

    token = parserutils_vector_iterate(vector, ctx);
    if (token == NULL ||
        (token->type != CSS_TOKEN_DIMENSION && token->type != CSS_TOKEN_NUMBER &&
            token->type != CSS_TOKEN_PERCENTAGE)) {
        *ctx = orig_ctx;
        return CSS_INVALID;
    }

    num = css__number_from_lwc_string(token->idata, false, &consumed);

    if (token->type == CSS_TOKEN_DIMENSION) {
        size_t len = lwc_string_length(token->idata);
        const char *data = lwc_string_data(token->idata);
        uint32_t temp_unit = CSS_UNIT_PX;

        error = css__parse_unit_keyword(data + consumed, len - consumed, &temp_unit);
        if (error != CSS_OK) {
            *ctx = orig_ctx;
            return error;
        }

        *unit = temp_unit;
    } else if (token->type == CSS_TOKEN_NUMBER) {
        /* Non-zero values are permitted in quirks mode */
        if (css__number_sign(token->idata) != 0) {
            if (c->sheet->quirks_allowed) {
                c->sheet->quirks_used = true;
            } else {
                *ctx = orig_ctx;
                return CSS_INVALID;
            }
        }

        *unit = default_unit;

        if (c->sheet->quirks_allowed) {
            /* Also, in quirks mode, we need to cater for
             * dimensions separated from their units by whitespace
             * (e.g. "0 px")
             */
            int32_t temp_ctx = *ctx;
            uint32_t temp_unit;

            consumeWhitespace(vector, &temp_ctx);

            /* Try to parse the unit keyword, ignoring errors */
            token = parserutils_vector_iterate(vector, &temp_ctx);
            if (token != NULL && token->type == CSS_TOKEN_IDENT) {
                error = css__parse_unit_keyword(
                    lwc_string_data(token->idata), lwc_string_length(token->idata), &temp_unit);
                if (error == CSS_OK) {
                    c->sheet->quirks_used = true;
                    *ctx = temp_ctx;
                    *unit = temp_unit;
                }
            }
        }
    } else {
        /* Percentage -- number must be entire token data */
        if (consumed != lwc_string_length(token->idata)) {
            *ctx = orig_ctx;
            return CSS_INVALID;
        }
        *unit = UNIT_PCT;
    }

    *length = num;

    return CSS_OK;
}

css_error css__parse_length(css_language *c, const parserutils_vector *vector, int32_t *ctx,
    bool non_negative, css_fixed *length, uint32_t *unit)
{
    int32_t orig_ctx = *ctx;
    consumeWhitespace(vector, ctx);
    const css_token *token = parserutils_vector_peek(vector, *ctx);
    css_error error = css__parse_unit_specifier(c, vector, ctx, UNIT_PX, length, unit);
    if (error == CSS_OK && (!(*unit & UNIT_LENGTH) || *unit == UNIT_FR ||
            (non_negative && css__number_sign(token->idata) < 0)))
        error = CSS_INVALID;
    if (error != CSS_OK)
        *ctx = orig_ctx;
    return error;
}

/**
 * Parse a unit keyword
 *
 * \param ptr   Pointer to keyword string
 * \param len   Length, in bytes, of string
 * \param unit  Pointer to location to receive computed unit
 * \return CSS_OK      on success,
 *         CSS_INVALID on encountering an unknown keyword
 */
css_error css__parse_unit_keyword(const char *ptr, size_t len, uint32_t *unit)
{
    if (len == 4) {
        if (strncasecmp(ptr, "grad", 4) == 0)
            *unit = UNIT_GRAD;
        else if (strncasecmp(ptr, "turn", 4) == 0)
            *unit = UNIT_TURN;
        else if (strncasecmp(ptr, "dppx", 4) == 0)
            *unit = UNIT_DPPX;
        else if (strncasecmp(ptr, "dpcm", 4) == 0)
            *unit = UNIT_DPCM;
        else if (strncasecmp(ptr, "vmin", 4) == 0)
            *unit = UNIT_VMIN;
        else if (strncasecmp(ptr, "vmax", 4) == 0)
            *unit = UNIT_VMAX;
        else
            return CSS_INVALID;
    } else if (len == 3) {
        if (strncasecmp(ptr, "kHz", 3) == 0)
            *unit = UNIT_KHZ;
        else if (strncasecmp(ptr, "deg", 3) == 0)
            *unit = UNIT_DEG;
        else if (strncasecmp(ptr, "rad", 3) == 0)
            *unit = UNIT_RAD;
        else if (strncasecmp(ptr, "rem", 3) == 0)
            *unit = UNIT_REM;
        else if (strncasecmp(ptr, "dpi", 3) == 0)
            *unit = UNIT_DPI;
        else if (strncasecmp(ptr, "cqw", 3) == 0)
            *unit = UNIT_CQW;
        else if (strncasecmp(ptr, "cqh", 3) == 0)
            *unit = UNIT_CQH;
        else if (strncasecmp(ptr, "cqi", 3) == 0)
            *unit = UNIT_CQI;
        else if (strncasecmp(ptr, "cqb", 3) == 0)
            *unit = UNIT_CQB;
        else
            return CSS_INVALID;
    } else if (len == 2) {
        if (strncasecmp(ptr, "Hz", 2) == 0)
            *unit = UNIT_HZ;
        else if (strncasecmp(ptr, "ms", 2) == 0)
            *unit = UNIT_MS;
        else if (strncasecmp(ptr, "px", 2) == 0)
            *unit = UNIT_PX;
        else if (strncasecmp(ptr, "ex", 2) == 0)
            *unit = UNIT_EX;
        else if (strncasecmp(ptr, "em", 2) == 0)
            *unit = UNIT_EM;
        else if (strncasecmp(ptr, "in", 2) == 0)
            *unit = UNIT_IN;
        else if (strncasecmp(ptr, "cm", 2) == 0)
            *unit = UNIT_CM;
        else if (strncasecmp(ptr, "mm", 2) == 0)
            *unit = UNIT_MM;
        else if (strncasecmp(ptr, "pt", 2) == 0)
            *unit = UNIT_PT;
        else if (strncasecmp(ptr, "pc", 2) == 0)
            *unit = UNIT_PC;
        else if (strncasecmp(ptr, "ch", 2) == 0)
            *unit = UNIT_CH;
        else if (strncasecmp(ptr, "ic", 2) == 0)
            *unit = UNIT_IC;
        else if (strncasecmp(ptr, "lh", 2) == 0)
            *unit = UNIT_LH;
        else if (strncasecmp(ptr, "vh", 2) == 0)
            *unit = UNIT_VH;
        else if (strncasecmp(ptr, "vw", 2) == 0)
            *unit = UNIT_VW;
        else if (strncasecmp(ptr, "vi", 2) == 0)
            *unit = UNIT_VI;
        else if (strncasecmp(ptr, "vb", 2) == 0)
            *unit = UNIT_VB;
        else if (strncasecmp(ptr, "fr", 2) == 0)
            *unit = UNIT_FR;
        else
            return CSS_INVALID;
    } else if (len == 1) {
        if (strncasecmp(ptr, "s", 1) == 0)
            *unit = UNIT_S;
        else if (strncasecmp(ptr, "q", 1) == 0)
            *unit = UNIT_Q;
        else
            return CSS_INVALID;
    } else
        return CSS_INVALID;

    return CSS_OK;
}

/**
 * Create a string from a list of IDENT/S tokens if the next token is IDENT
 * or references the next token's string if it is a STRING
 *
 * \param c          Parsing context
 * \param vector     Vector containing tokens
 * \param ctx        Vector iteration context
 * \param reserved   Callback to determine if an identifier is reserved
 * \param result     Pointer to location to receive resulting string
 * \return CSS_OK on success, appropriate error otherwise.
 *
 * Post condition: \a *ctx is updated with the next token to process
 *                 If the input is invalid, then \a *ctx remains unchanged.
 *
 *                 The resulting string's reference is passed to the caller
 */
css_error css__ident_list_or_string_to_string(css_language *c, const parserutils_vector *vector, int32_t *ctx,
    bool (*reserved)(css_language *c, const css_token *ident), lwc_string **result)
{
    const css_token *token;

    token = parserutils_vector_peek(vector, *ctx);
    if (token == NULL)
        return CSS_INVALID;

    if (token->type == CSS_TOKEN_STRING) {
        token = parserutils_vector_iterate(vector, ctx);
        *result = lwc_string_ref(token->idata);
        return CSS_OK;
    } else if (token->type == CSS_TOKEN_IDENT) {
        return css__ident_list_to_string(c, vector, ctx, reserved, result);
    }

    return CSS_INVALID;
}

/**
 * Create a string from a list of IDENT/S tokens
 *
 * \param c          Parsing context
 * \param vector     Vector containing tokens
 * \param ctx        Vector iteration context
 * \param reserved   Callback to determine if an identifier is reserved
 * \param result     Pointer to location to receive resulting string
 * \return CSS_OK on success, appropriate error otherwise.
 *
 * Post condition: \a *ctx is updated with the next token to process
 *                 If the input is invalid, then \a *ctx remains unchanged.
 *
 *                 The resulting string's reference is passed to the caller
 */
css_error css__ident_list_to_string(css_language *c, const parserutils_vector *vector, int32_t *ctx,
    bool (*reserved)(css_language *c, const css_token *ident), lwc_string **result)
{
    int32_t start = *ctx, end = start;
    size_t length = 0, count = 0;
    const css_token *token;
    /* Join identifier tokens, not whitespace tokens: comments may separate
     * identifiers without producing S, and spaces inside an escape are data.
     * Measure without changing ctx so invalid input/allocation failure is atomic. */
    while ((token = parserutils_vector_peek(vector, end)) != NULL) {
        if (token->type == CSS_TOKEN_S && count != 0) {
            end++;
            continue;
        }
        if (token->type != CSS_TOKEN_IDENT)
            break;
        if (!css__is_custom_ident(token->idata) || (reserved != NULL && reserved(c, token)))
            return CSS_INVALID;
        size_t part = lwc_string_length(token->idata);
        if (length > SIZE_MAX - (count != 0) || part > SIZE_MAX - length - (count != 0))
            return CSS_NOMEM;
        length += part + (count != 0);
        count++;
        end++;
    }
    if (count == 0)
        return CSS_INVALID;
    if (count == 1) {
        token = parserutils_vector_peek(vector, start);
        *result = lwc_string_ref(token->idata);
        *ctx = end;
        return CSS_OK;
    }
    char *buffer = malloc(length);
    if (buffer == NULL)
        return CSS_NOMEM;
    size_t position = 0;
    for (int32_t i = start; i < end; i++) {
        token = parserutils_vector_peek(vector, i);
        if (token->type != CSS_TOKEN_IDENT)
            continue;
        if (position != 0)
            buffer[position++] = ' ';
        size_t part = lwc_string_length(token->idata);
        memcpy(buffer + position, lwc_string_data(token->idata), part);
        position += part;
    }
    lwc_error error = lwc_intern_string(buffer, length, result);
    free(buffer);
    if (error != lwc_error_ok)
        return css_error_from_lwc_error(error);
    *ctx = end;
    return CSS_OK;
}

/**
 * Parse a comma separated list, converting to bytecode
 *
 * \param c          Parsing context
 * \param vector     Vector of tokens to process
 * \param ctx        Pointer to vector iteration context
 * \param reserved   Callback to determine if an identifier is reserved
 * \param get_value  Callback to retrieve bytecode value for a token
 * \param style      Pointer to output style
 * \return CSS_OK      on success,
 *         CSS_INVALID if the input is invalid
 *
 * Post condition: \a *ctx is updated with the next token to process
 *                 If the input is invalid, then \a *ctx remains unchanged.
 */
css_error css__comma_list_to_style(css_language *c, const parserutils_vector *vector, int32_t *ctx,
    bool (*reserved)(css_language *c, const css_token *ident),
    css_code_t (*get_value)(css_language *c, const css_token *token, bool first), css_style *result)
{
    int32_t orig_ctx = *ctx;
    int prev_ctx = orig_ctx;
    const css_token *token;
    bool first = true;
    css_error error = CSS_OK;

    token = parserutils_vector_iterate(vector, ctx);
    if (token == NULL) {
        *ctx = orig_ctx;
        return CSS_INVALID;
    }

    while (token != NULL) {
        if (token->type == CSS_TOKEN_IDENT) {
            css_code_t value = get_value(c, token, first);

            if (reserved(c, token) == false) {
                lwc_string *str = NULL;
                uint32_t snumber;

                *ctx = prev_ctx;

                error = css__ident_list_to_string(c, vector, ctx, reserved, &str);
                if (error != CSS_OK)
                    goto cleanup;

                error = css__stylesheet_string_add(c->sheet, str, &snumber);
                if (error != CSS_OK)
                    goto cleanup;

                error = css__stylesheet_style_append(result, value);
                if (error != CSS_OK)
                    goto cleanup;

                error = css__stylesheet_style_append(result, snumber);
                if (error != CSS_OK)
                    goto cleanup;
            } else {
                error = css__stylesheet_style_append(result, value);
                if (error != CSS_OK)
                    goto cleanup;
            }
        } else if (token->type == CSS_TOKEN_STRING) {
            css_code_t value = get_value(c, token, first);
            uint32_t snumber;

            error = css__stylesheet_string_add(c->sheet, lwc_string_ref(token->idata), &snumber);
            if (error != CSS_OK)
                goto cleanup;

            error = css__stylesheet_style_append(result, value);
            if (error != CSS_OK)
                goto cleanup;

            error = css__stylesheet_style_append(result, snumber);
            if (error != CSS_OK)
                goto cleanup;
        } else {
            error = CSS_INVALID;
            goto cleanup;
        }

        consumeWhitespace(vector, ctx);

        token = parserutils_vector_peek(vector, *ctx);
        if (token != NULL && tokenIsChar(token, ',')) {
            parserutils_vector_iterate(vector, ctx);

            consumeWhitespace(vector, ctx);

            token = parserutils_vector_peek(vector, *ctx);
            if (token == NULL || (token->type != CSS_TOKEN_IDENT && token->type != CSS_TOKEN_STRING)) {
                error = CSS_INVALID;
                goto cleanup;
            }
        } else {
            break;
        }

        first = false;

        prev_ctx = *ctx;

        token = parserutils_vector_iterate(vector, ctx);
    }

cleanup:
    if (error != CSS_OK)
        *ctx = orig_ctx;

    return error;
}

/******************************************************************************/

/* CALC
 *
 * calc( <calc-sum> )
 *
 * where
 * <calc-sum> = <calc-product> [ [ '+' | '-' ] <calc-product> ]*
 *
 * where
 * <calc-product> = <calc-value> [ '*' <calc-value> | '/' <number> ]*
 *
 * where
 * <calc-value> = <number> | <dimension> | <percentage> | ( <calc-sum> )
 *
 *
 * Once a calc() expression is parsed, it generates into the bytecode as
 * an opV where the V is VALUE_IS_CALC, then a unit kind which is the
 * expected resolved type for the calc, and a string index.  The string is
 * another kind of bytecode, essentially it's a sequence of stack machine
 * operations which are one of the calc_opcodes enum.  They are:
 *
 * * CALC_PUSH_NUMBER (N)
 *   - takes a css_fixed and pushes it onto the operand stack
 * * CALC_PUSH_VALUE (V)
 *   - takes a css_fixed and a unit, and pushes them
 * * CALC_{ADD,SUBTRACT,MULTIPLY,DIVIDE} (+ - * /)
 *   - pop two values, perform the operation, push the result
 * * CALC_FINISH (=)
 *   - pop the top value from the stack and return it.
 *
 * As an example:
 *
 * calc(10px + (4rem / 2)) =>
 *   V 10 px
 *   V 4 rem
 *   N 2
 *   /
 *   +
 *   =
 */

static css_error css__parse_calc_sum(css_language *c, enum css_properties_e property, const parserutils_vector *vector,
    int *ctx, parserutils_buffer *result);

static css_error css__parse_calc_number(const parserutils_vector *vector, int *ctx, parserutils_buffer *result)
{
    const css_token *token;
    css_fixed num;
    size_t consumed;
    css_code_t push = CALC_PUSH_NUMBER;

    /* Consume the number token */
    token = parserutils_vector_iterate(vector, ctx);
    if (token == NULL || token->type != CSS_TOKEN_NUMBER) {
        return CSS_INVALID;
    }

    num = css__number_from_string(
        (const uint8_t *)lwc_string_data(token->idata), lwc_string_length(token->idata), false, &consumed);

    if (consumed != lwc_string_length(token->idata)) {
        return CSS_INVALID;
    }

    return css_error_from_parserutils_error(
        parserutils_buffer_appendv(result, 2, &push, sizeof(push), &num, sizeof(num)));
}

static css_error css__parse_calc_value(css_language *c, enum css_properties_e property,
    const parserutils_vector *vector, int *ctx, parserutils_buffer *result)
{
    css_error error;
    int orig_ctx = *ctx;
    const css_token *token;

    /* On entry, we are already pointing at the value to parse, so peek it
     */
    token = parserutils_vector_peek(vector, *ctx);
    if (tokenIsChar(token, '(')) {
        parserutils_vector_iterate(vector, ctx);
        consumeWhitespace(vector, ctx);
        error = css__parse_calc_sum(c, property, vector, ctx, result);
        if (error != CSS_OK) {
            return error;
        }

        token = parserutils_vector_peek(vector, *ctx);
        if (!tokenIsChar(token, ')')) {
            return CSS_INVALID;
        }
        /* Consume the close-paren to complete this value */
        parserutils_vector_iterate(vector, ctx);
    } else
        switch (token->type) {
        case CSS_TOKEN_NUMBER:
            error = css__parse_calc_number(vector, ctx, result);
            if (error != CSS_OK) {
                return error;
            }
            break;
        case CSS_TOKEN_DIMENSION: /* Fall through */
        case CSS_TOKEN_PERCENTAGE: {
            css_fixed length = 0;
            uint32_t unit = 0;
            css_code_t push = CALC_PUSH_VALUE;
            *ctx = orig_ctx;

            error = css__parse_unit_specifier(c, vector, ctx, UNIT_CALC_NUMBER, &length, &unit);
            if (error != CSS_OK) {
                *ctx = orig_ctx;
                return error;
            }

            if (!(unit & property_unit_mask[property])) {
                /* This unit is not valid for this property. */
                return CSS_INVALID;
            }

            error = css_error_from_parserutils_error(parserutils_buffer_appendv(
                result, 3, &push, sizeof(push), &length, sizeof(length), &unit, sizeof(unit)));

        } break;

        default:
            error = CSS_INVALID;
            break;
        }

    consumeWhitespace(vector, ctx);
    return error;
}

/* Both this, and css_parse_calc_sum must stop when it encounters a close-paren.
 * If it hasn't had any useful tokens before that, it's an error.  It does not
 * need to restore ctx before returning an error but it does need to ensure that
 * the close paren has not been consumed
 */
static css_error css__parse_calc_product(css_language *c, enum css_properties_e property,
    const parserutils_vector *vector, int *ctx, parserutils_buffer *result)
{
    css_error error = CSS_OK;
    const css_token *token;
    css_code_t operator;

    /* First parse a value */
    error = css__parse_calc_value(c, property, vector, ctx, result);
    if (error != CSS_OK) {
        return error;
    }

    do {
        /* What is our next token? */
        token = parserutils_vector_peek(vector, *ctx);
        if (token == NULL) {
            error = CSS_INVALID;
            break;
        } else if (tokenIsChar(token, ')') || tokenIsChar(token, '+') || tokenIsChar(token, '-'))
            break;
        else if (tokenIsChar(token, '*'))
            operator= CALC_MULTIPLY;
        else if (tokenIsChar(token, '/'))
            operator= CALC_DIVIDE;
        else {
            error = CSS_INVALID;
            break;
        }
        /* Consume that * or / now */
        parserutils_vector_iterate(vector, ctx);

        consumeWhitespace(vector, ctx);

        if (operator== CALC_MULTIPLY) {
            /* parse another value */
            error = css__parse_calc_value(c, property, vector, ctx, result);
        } else {
            error = css__parse_calc_number(vector, ctx, result);
        }
        if (error != CSS_OK)
            break;

        /* emit the multiplication/division operator */
        error = css_error_from_parserutils_error(
            parserutils_buffer_append(result, (const uint8_t *)&operator, sizeof(operator)));
    } while (1);
    /* We've fallen off, either we had an error or we're left with ')' */
    return error;
}


css_error css__parse_calc_sum(css_language *c, enum css_properties_e property, const parserutils_vector *vector,
    int *ctx, parserutils_buffer *result)
{
    css_error error = CSS_OK;
    const css_token *token;
    css_code_t operator;

    /* First parse a product */
    error = css__parse_calc_product(c, property, vector, ctx, result);
    if (error != CSS_OK) {
        return error;
    }

    do {
        /* What is our next token? */
        token = parserutils_vector_peek(vector, *ctx);
        if (token == NULL) {
            error = CSS_INVALID;
            break;
        } else if (tokenIsChar(token, ')'))
            break;
        else if (tokenIsChar(token, '+'))
            operator= CALC_ADD;
        else if (tokenIsChar(token, '-'))
            operator= CALC_SUBTRACT;
        else {
            error = CSS_INVALID;
            break;
        }
        /* Consume that + or - now */
        parserutils_vector_iterate(vector, ctx);
        consumeWhitespace(vector, ctx);

        /* parse another product */
        error = css__parse_calc_product(c, property, vector, ctx, result);
        if (error != CSS_OK)
            break;

        /* emit the addition/subtraction operator */
        error = css_error_from_parserutils_error(
            parserutils_buffer_append(result, (const uint8_t *)&operator, sizeof(operator)));
    } while (1);
    /* We've fallen off, either we had an error or we're left with ')' */
    return error;
}

/* Documented in utils.h */
css_error css__parse_calc(
    css_language *c, const parserutils_vector *vector, int *ctx, css_style *result, css_code_t OPV, uint32_t unit)
{
    int orig_ctx = *ctx;
    const css_token *token;
    css_error error = CSS_OK;
    css_style *calc_style = NULL;
    parserutils_buffer *calc_buffer = NULL;
    lwc_string *calc_expr = NULL;
    uint32_t expr_index = 0;
    css_code_t finish = CALC_FINISH;
    enum css_properties_e property = getOpcode(OPV);

    consumeWhitespace(vector, ctx);

    token = parserutils_vector_peek(vector, *ctx);
    if (token == NULL) {
        *ctx = orig_ctx;
        return CSS_INVALID;
    }

    if (parserutils_buffer_create(&calc_buffer) != PARSERUTILS_OK) {
        /* Since &calc_buffer is valid, the only error case is NONMEM */
        *ctx = orig_ctx;
        return CSS_NOMEM;
    }

    error = css__stylesheet_style_create(c->sheet, &calc_style);
    if (error != CSS_OK)
        goto cleanup;

    error = css__stylesheet_style_append(calc_style, OPV);
    if (error != CSS_OK)
        goto cleanup;

    error = css__stylesheet_style_append(calc_style, (css_code_t)unit);
    if (error != CSS_OK)
        goto cleanup;

    error = css__parse_calc_sum(c, property, vector, ctx, calc_buffer);
    if (error != CSS_OK)
        goto cleanup;

    consumeWhitespace(vector, ctx);
    token = parserutils_vector_peek(vector, *ctx);
    if (!tokenIsChar(token, ')')) {
        /* If we don't get a close-paren, give up now */
        error = CSS_INVALID;
        goto cleanup;
    }

    /* Append the indicator that the calc is finished */
    error = css_error_from_parserutils_error(
        parserutils_buffer_append(calc_buffer, (const uint8_t *)&finish, sizeof(finish)));
    if (error != CSS_OK)
        goto cleanup;

    /* Swallow that close paren */
    parserutils_vector_iterate(vector, ctx);

    /* Create the lwc string representing the calculation and store it in */
    error = css_error_from_lwc_error(
        lwc_intern_string((const char *)calc_buffer->data, calc_buffer->length, &calc_expr));
    if (error != CSS_OK)
        goto cleanup;

    /* This always takes ownership of calc_expr, so we should not use after
     * this */
    error = css__stylesheet_string_add(calc_style->sheet, calc_expr, &expr_index);
    if (error != CSS_OK)
        goto cleanup;

    error = css__stylesheet_style_append(calc_style, (css_code_t)expr_index);
    if (error != CSS_OK) {
        goto cleanup;
    }

    error = css__stylesheet_merge_style(result, calc_style);
cleanup:
    css__stylesheet_style_destroy(calc_style);
    parserutils_buffer_destroy(calc_buffer);
    /* We do not need to clean up calc_expr, it will never leak */
    if (error != CSS_OK) {
        *ctx = orig_ctx;
    }

    return error;
}

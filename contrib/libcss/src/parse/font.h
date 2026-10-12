/* Shared supported font prefix grammar. Licensed under the MIT License. */
#ifndef css_parse_font_h_
#define css_parse_font_h_

#include "bytecode/opcodes.h"
#include "lex/lex.h"
#include "utils/css_utils.h"

/* Keep the current discrete CSS2 weight model shared by font-weight, font,
 * and specified-value readback. Variable weights need a separate storage change. */
static inline int css__font_weight_value(css_token_type type, lwc_string *text)
{
    if (type == CSS_TOKEN_NUMBER) {
        size_t consumed = 0;
        css_fixed number = css__number_from_lwc_string(text, true, &consumed);
        if (consumed != lwc_string_length(text))
            return -1;
        switch (FIXTOINT(number)) {
        case 100:
            return FONT_WEIGHT_100;
        case 200:
            return FONT_WEIGHT_200;
        case 300:
            return FONT_WEIGHT_300;
        case 400:
            return FONT_WEIGHT_400;
        case 500:
            return FONT_WEIGHT_500;
        case 600:
            return FONT_WEIGHT_600;
        case 700:
            return FONT_WEIGHT_700;
        case 800:
            return FONT_WEIGHT_800;
        case 900:
            return FONT_WEIGHT_900;
        default:
            return -1;
        }
    }
    if (type == CSS_TOKEN_IDENT) {
        if (css__keyword_equal(text, "normal"))
            return FONT_WEIGHT_NORMAL;
        if (css__keyword_equal(text, "bold"))
            return FONT_WEIGHT_BOLD;
        if (css__keyword_equal(text, "bolder"))
            return FONT_WEIGHT_BOLDER;
        if (css__keyword_equal(text, "lighter"))
            return FONT_WEIGHT_LIGHTER;
    }
    return -1;
}

enum css_font_prefix {
    CSS_FONT_PREFIX_STYLE,
    CSS_FONT_PREFIX_VARIANT,
    CSS_FONT_PREFIX_WEIGHT,
    CSS_FONT_PREFIX_NORMAL,
    CSS_FONT_PREFIX_END
};

/* normal has no assigned component: any still-omitted prefix can consume it.
 * Concrete values identify their own component without speculative parsing. */
static inline enum css_font_prefix css__font_prefix(css_token_type type, lwc_string *text, uint16_t *value)
{
    if (type == CSS_TOKEN_IDENT) {
        if (css__keyword_equal(text, "normal"))
            return CSS_FONT_PREFIX_NORMAL;
        if (css__keyword_equal(text, "italic") || css__keyword_equal(text, "oblique")) {
            *value = css__keyword_equal(text, "italic") ? FONT_STYLE_ITALIC : FONT_STYLE_OBLIQUE;
            return CSS_FONT_PREFIX_STYLE;
        }
        if (css__keyword_equal(text, "small-caps")) {
            *value = FONT_VARIANT_SMALL_CAPS;
            return CSS_FONT_PREFIX_VARIANT;
        }
    }
    int weight = css__font_weight_value(type, text);
    if (weight >= 0) {
        *value = weight;
        return CSS_FONT_PREFIX_WEIGHT;
    }
    return CSS_FONT_PREFIX_END;
}

#endif

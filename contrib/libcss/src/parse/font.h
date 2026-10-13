/* Shared supported font prefix grammar. Licensed under the MIT License. */
#ifndef css_parse_font_h_
#define css_parse_font_h_

#include <libcss/stylesheet.h>
#include "bytecode/opcodes.h"
#include "lex/lex.h"
#include "utils/css_utils.h"

/* Lengths (or percentages) accepted by both font-size and line-height. Reject
 * invalid client results before bytecode emission, including non-length units
 * and CALC, which cannot be represented by this callback's scalar descriptor. */
static inline const char *css__system_font_unit(css_unit unit)
{
    static const char *const units[] = {
        [CSS_UNIT_PX] = "px", [CSS_UNIT_EX] = "ex", [CSS_UNIT_EM] = "em", [CSS_UNIT_IN] = "in",
        [CSS_UNIT_CM] = "cm", [CSS_UNIT_MM] = "mm", [CSS_UNIT_PT] = "pt", [CSS_UNIT_PC] = "pc",
        [CSS_UNIT_CH] = "ch", [CSS_UNIT_REM] = "rem", [CSS_UNIT_LH] = "lh", [CSS_UNIT_VH] = "vh",
        [CSS_UNIT_VW] = "vw", [CSS_UNIT_VI] = "vi", [CSS_UNIT_VB] = "vb", [CSS_UNIT_VMIN] = "vmin",
        [CSS_UNIT_VMAX] = "vmax", [CSS_UNIT_Q] = "q", [CSS_UNIT_IC] = "ic", [CSS_UNIT_CQW] = "cqw",
        [CSS_UNIT_CQH] = "cqh", [CSS_UNIT_CQI] = "cqi", [CSS_UNIT_CQB] = "cqb", [CSS_UNIT_PCT] = "%"};
    return (unsigned)unit < N_ELEMENTS(units) ? units[unit] : NULL;
}

static inline bool css__system_font_equal(const css_system_font *a, const css_system_font *b)
{
    if (a == NULL || b == NULL)
        return a == b;
    return a->style == b->style && a->variant == b->variant && a->weight == b->weight &&
           a->size.size == b->size.size && a->size.unit == b->size.unit &&
           a->line_height.size == b->line_height.size && a->line_height.unit == b->line_height.unit &&
           a->family == b->family;
}

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

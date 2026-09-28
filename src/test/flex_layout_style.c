/* Keep libcss generated setters separate from Wisp's conflicting enum names. */
#include "select/propset.h"
#include "select/propget.h"
#include "flex_layout_style.h"

uint8_t flex_test_style_grow(const css_computed_style *style, css_fixed *grow)
{
    return get_flex_grow(style, grow);
}

css_computed_style *flex_test_style_create(const struct flex_test_style *v)
{
    css_computed_style *style;
    css_calculator *calc;
    if (css_calculator_create(&calc) != CSS_OK) {
        return NULL;
    }
    css_error error = css__computed_style_create(&style, calc);
    css_calculator_unref(calc);
    if (error != CSS_OK) {
        return NULL;
    }
    set_display(style, v->container ? CSS_DISPLAY_FLEX : CSS_DISPLAY_BLOCK);
    set_position(style, v->position ? v->position : CSS_POSITION_STATIC);
    set_float(style, CSS_FLOAT_NONE);
    set_box_sizing(style, v->border_box ? CSS_BOX_SIZING_BORDER_BOX : CSS_BOX_SIZING_CONTENT_BOX);
    set_font_size(style, CSS_FONT_SIZE_DIMENSION, (css_fixed_or_calc)INTTOFIX(16), CSS_UNIT_PX);
    set_height(style, CSS_HEIGHT_AUTO, (css_fixed_or_calc)0, CSS_UNIT_PX);
    set_min_height(style, CSS_MIN_HEIGHT_AUTO, (css_fixed_or_calc)0, CSS_UNIT_PX);
    set_max_height(style, CSS_MAX_HEIGHT_NONE, (css_fixed_or_calc)0, CSS_UNIT_PX);
    set_width(style, v->width < 0 ? CSS_WIDTH_AUTO : CSS_WIDTH_SET,
        (css_fixed_or_calc)INTTOFIX(v->width < 0 ? 0 : v->width), v->width_unit);
    set_min_width(style, v->min_width < 0 ? CSS_MIN_WIDTH_AUTO : CSS_MIN_WIDTH_SET,
        (css_fixed_or_calc)INTTOFIX(v->min_width < 0 ? 0 : v->min_width), v->min_unit);
    set_max_width(style, v->max_width < 0 ? CSS_MAX_WIDTH_NONE : CSS_MAX_WIDTH_SET,
        (css_fixed_or_calc)INTTOFIX(v->max_width < 0 ? 0 : v->max_width), CSS_UNIT_PX);
    set_flex_direction(style, v->reverse ? CSS_FLEX_DIRECTION_ROW_REVERSE : CSS_FLEX_DIRECTION_ROW);
    set_flex_wrap(style, CSS_FLEX_WRAP_NOWRAP);
    set_flex_basis(style, v->basis < 0 ? CSS_FLEX_BASIS_AUTO : CSS_FLEX_BASIS_SET,
        (css_fixed_or_calc)INTTOFIX(v->basis < 0 ? 0 : v->basis), CSS_UNIT_PX);
    set_flex_grow(style, CSS_FLEX_GROW_SET, INTTOFIX(v->grow));
    set_flex_shrink(style, CSS_FLEX_SHRINK_SET, INTTOFIX(1));
    set_align_items(style, CSS_ALIGN_ITEMS_FLEX_START);
    set_align_self(style, CSS_ALIGN_SELF_AUTO);
    set_justify_content(style, CSS_JUSTIFY_CONTENT_FLEX_START);
    set_overflow_x(style, v->overflow);
    set_overflow_y(style, v->overflow);

#define SET_EDGES(side)                                                                                                \
    set_margin_##side(style, CSS_MARGIN_SET, (css_fixed_or_calc)INTTOFIX(v->margin), CSS_UNIT_PX);                     \
    set_padding_##side(style, CSS_PADDING_SET, (css_fixed_or_calc)INTTOFIX(v->padding), CSS_UNIT_PX);                  \
    set_border_##side##_width(style, CSS_BORDER_WIDTH_WIDTH, (css_fixed_or_calc)INTTOFIX(v->border), CSS_UNIT_PX);     \
    set_border_##side##_style(style, v->border ? CSS_BORDER_STYLE_SOLID : CSS_BORDER_STYLE_NONE)
    SET_EDGES(top);
    SET_EDGES(right);
    SET_EDGES(bottom);
    SET_EDGES(left);
#undef SET_EDGES
    return style;
}

void flex_test_style_ratio(css_computed_style *style, int height, int numerator, int denominator)
{
    set_aspect_ratio(style, CSS_ASPECT_RATIO_RATIO, INTTOFIX(numerator), INTTOFIX(denominator));
    set_height(style, CSS_HEIGHT_SET, (css_fixed_or_calc)INTTOFIX(height), CSS_UNIT_PX);
}

void flex_test_style_alignment(css_computed_style *style, uint8_t direction, uint8_t justify, int height)
{
    set_flex_direction(style, direction);
    set_justify_content(style, justify);
    set_height(style, CSS_HEIGHT_SET, (css_fixed_or_calc)INTTOFIX(height), CSS_UNIT_PX);
}

void flex_test_style_stretch(css_computed_style *style, int minimum, int maximum, bool auto_margin)
{
    set_align_self(style, CSS_ALIGN_SELF_STRETCH);
    set_min_height(style, minimum < 0 ? CSS_MIN_HEIGHT_AUTO : CSS_MIN_HEIGHT_SET,
        (css_fixed_or_calc)INTTOFIX(minimum < 0 ? 0 : minimum), CSS_UNIT_PX);
    set_max_height(style, maximum < 0 ? CSS_MAX_HEIGHT_NONE : CSS_MAX_HEIGHT_SET,
        (css_fixed_or_calc)INTTOFIX(maximum < 0 ? 0 : maximum), CSS_UNIT_PX);
    set_margin_top(style, auto_margin ? CSS_MARGIN_AUTO : CSS_MARGIN_SET, (css_fixed_or_calc)0, CSS_UNIT_PX);
}

void flex_test_style_wrap(css_computed_style *style, uint8_t wrap)
{
    set_flex_wrap(style, wrap);
}

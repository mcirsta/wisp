/* Keep libcss internal setters separate from Wisp's conflicting enum names.
 * Use the generated setters, not assumptions about computed-style bitfields. */
#include <limits.h>
#include "select/propset.h"
#include "grid_layout_style.h"

void grid_test_set_horizontal_margins(css_computed_style *style, int left, int right)
{
    set_margin_left(style, left == INT_MIN ? CSS_MARGIN_AUTO : CSS_MARGIN_SET,
        (css_fixed_or_calc)INTTOFIX(left == INT_MIN ? 0 : left), CSS_UNIT_PX);
    set_margin_right(style, right == INT_MIN ? CSS_MARGIN_AUTO : CSS_MARGIN_SET,
        (css_fixed_or_calc)INTTOFIX(right == INT_MIN ? 0 : right), CSS_UNIT_PX);
}

css_computed_style *grid_item_test_style_create(const struct grid_item_test_style *v)
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
    set_display(style, CSS_DISPLAY_BLOCK);
    set_position(style, v->position ? v->position : CSS_POSITION_STATIC);
    set_float(style, CSS_FLOAT_NONE);
    set_box_sizing(style, v->border_box ? CSS_BOX_SIZING_BORDER_BOX : CSS_BOX_SIZING_CONTENT_BOX);
    set_height(style, CSS_HEIGHT_SET, (css_fixed_or_calc)INTTOFIX(40), CSS_UNIT_PX);
    set_font_size(style, CSS_FONT_SIZE_DIMENSION, (css_fixed_or_calc)INTTOFIX(16), CSS_UNIT_PX);
    set_width(style, v->width == INT_MIN ? CSS_WIDTH_AUTO : CSS_WIDTH_SET,
        (css_fixed_or_calc)INTTOFIX(v->width == INT_MIN ? 0 : v->width), v->percent_width ? CSS_UNIT_PCT : CSS_UNIT_PX);
    set_min_width(style, CSS_MIN_WIDTH_SET, (css_fixed_or_calc)INTTOFIX(v->min_width), CSS_UNIT_PX);
    set_max_width(style, v->max_width < 0 ? CSS_MAX_WIDTH_NONE : CSS_MAX_WIDTH_SET,
        (css_fixed_or_calc)INTTOFIX(v->max_width < 0 ? 0 : v->max_width), CSS_UNIT_PX);
    grid_test_set_horizontal_margins(style, v->margin_left, v->margin_right);
    set_margin_top(style, CSS_MARGIN_SET, (css_fixed_or_calc)0, CSS_UNIT_PX);
    set_margin_bottom(style, CSS_MARGIN_SET, (css_fixed_or_calc)0, CSS_UNIT_PX);
    set_align_self(style, CSS_ALIGN_SELF_AUTO);

#define SET_EDGES(side)                                                                                                \
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

void grid_test_set_block_alignment(css_computed_style *style, uint8_t align, int height, int top, int bottom)
{
    set_align_self(style, align);
    set_height(style, height == INT_MIN ? CSS_HEIGHT_AUTO : CSS_HEIGHT_SET,
        (css_fixed_or_calc)INTTOFIX(height == INT_MIN ? 0 : height), CSS_UNIT_PX);
    set_margin_top(style, top == INT_MIN ? CSS_MARGIN_AUTO : CSS_MARGIN_SET,
        (css_fixed_or_calc)INTTOFIX(top == INT_MIN ? 0 : top), CSS_UNIT_PX);
    set_margin_bottom(style, bottom == INT_MIN ? CSS_MARGIN_AUTO : CSS_MARGIN_SET,
        (css_fixed_or_calc)INTTOFIX(bottom == INT_MIN ? 0 : bottom), CSS_UNIT_PX);
    set_overflow_y(style, CSS_OVERFLOW_VISIBLE);
}

/* Generated libcss setters are isolated from Wisp's conflicting enum names. */
#include "select/propset.h"

css_computed_style *sticky_test_style(uint8_t position, uint8_t overflow_x, uint8_t overflow_y)
{
    css_computed_style *style;
    css_calculator *calc;
    if (css_calculator_create(&calc) != CSS_OK)
        return NULL;
    css_error error = css__computed_style_create(&style, calc);
    css_calculator_unref(calc);
    if (error != CSS_OK)
        return NULL;
    set_display(style, CSS_DISPLAY_BLOCK);
    set_position(style, position);
    set_float(style, CSS_FLOAT_NONE);
    set_writing_mode(style, CSS_WRITING_MODE_HORIZONTAL_TB);
    set_direction(style, CSS_DIRECTION_LTR);
    set_overflow_x(style, overflow_x);
    set_overflow_y(style, overflow_y);
    set_top(style, CSS_TOP_SET, (css_fixed_or_calc)INTTOFIX(10), CSS_UNIT_PCT);
    set_bottom(style, CSS_BOTTOM_AUTO, (css_fixed_or_calc)0, CSS_UNIT_PX);
    set_left(style, CSS_LEFT_AUTO, (css_fixed_or_calc)0, CSS_UNIT_PX);
    set_right(style, CSS_RIGHT_AUTO, (css_fixed_or_calc)0, CSS_UNIT_PX);
    return style;
}

void sticky_test_set_clip(css_computed_style *style, int top, int right, int bottom, int left)
{
    css_computed_clip_rect clip = {.top = INTTOFIX(top), .right = INTTOFIX(right),
        .bottom = INTTOFIX(bottom), .left = INTTOFIX(left),
        .tunit = CSS_UNIT_PX, .runit = CSS_UNIT_PX, .bunit = CSS_UNIT_PX, .lunit = CSS_UNIT_PX};
    set_clip(style, CSS_CLIP_RECT, &clip);
}

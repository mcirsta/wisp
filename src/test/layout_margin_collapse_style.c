/*
 * Copyright 2025 Wisp Contributors
 *
 * This file is part of Wisp.
 *
 * Style factory for margin collapse tests.
 *
 * This translation unit ONLY includes libcss internal headers.
 * It must NOT include any Wisp headers (box.h, content_type.h, etc.)
 * to avoid enum name collisions (COLUMN_WIDTH_AUTO, CONTENT_NONE, etc.).
 */

#include <assert.h>
#include <limits.h>
#include <stdlib.h>
#include <stdint.h>

#include "select/propset.h"

#include "layout_margin_collapse_style.h"

css_computed_style *create_block_style(void)
{
    css_computed_style *s = calloc(1, sizeof(css_computed_style));
    assert(s != NULL);

    /* Match css__computed_style_create: set bin to UINT32_MAX */
    s->bin = UINT32_MAX;

    /* display: block */
    set_display(s, CSS_DISPLAY_BLOCK);

    /* position: static */
    set_position(s, CSS_POSITION_STATIC);

    /* overflow-x: visible */
    set_overflow_x(s, CSS_OVERFLOW_VISIBLE);

    /* overflow-y: visible */
    set_overflow_y(s, CSS_OVERFLOW_VISIBLE);

    /* height: auto */
    set_height(s, CSS_HEIGHT_AUTO, (css_fixed_or_calc)0, CSS_UNIT_PX);

    /* font-size: 16px (needed for CSS unit conversions) */
    set_font_size(s, CSS_FONT_SIZE_DIMENSION, (css_fixed_or_calc)INTTOFIX(16), CSS_UNIT_PX);

    return s;
}

void style_set_margins(css_computed_style *s, int top, int right, int bottom, int left)
{
    if (top != INT_MIN)
        set_margin_top(s, CSS_MARGIN_SET, (css_fixed_or_calc)INTTOFIX(top), CSS_UNIT_PX);
    if (right != INT_MIN)
        set_margin_right(s, CSS_MARGIN_SET, (css_fixed_or_calc)INTTOFIX(right), CSS_UNIT_PX);
    if (bottom != INT_MIN)
        set_margin_bottom(s, CSS_MARGIN_SET, (css_fixed_or_calc)INTTOFIX(bottom), CSS_UNIT_PX);
    if (left != INT_MIN)
        set_margin_left(s, CSS_MARGIN_SET, (css_fixed_or_calc)INTTOFIX(left), CSS_UNIT_PX);
}

void style_set_border_top(css_computed_style *s, int width)
{
    set_border_top_width(s, CSS_BORDER_WIDTH_WIDTH, (css_fixed_or_calc)INTTOFIX(width), CSS_UNIT_PX);
    if (width > 0)
        set_border_top_style(s, CSS_BORDER_STYLE_SOLID);
    else
        set_border_top_style(s, CSS_BORDER_STYLE_NONE);
}

void style_set_padding_top(css_computed_style *s, int px)
{
    set_padding_top(s, CSS_PADDING_SET, (css_fixed_or_calc)INTTOFIX(px), CSS_UNIT_PX);
}

void style_set_height_px(css_computed_style *s, int px)
{
    if (px < 0)
        set_height(s, CSS_HEIGHT_AUTO, (css_fixed_or_calc)0, CSS_UNIT_PX);
    else
        set_height(s, CSS_HEIGHT_SET, (css_fixed_or_calc)INTTOFIX(px), CSS_UNIT_PX);
}

void style_set_overflow_hidden(css_computed_style *s)
{
    set_overflow_x(s, CSS_OVERFLOW_HIDDEN);
    set_overflow_y(s, CSS_OVERFLOW_HIDDEN);
}

void style_set_position_absolute(css_computed_style *s)
{
    set_position(s, CSS_POSITION_ABSOLUTE);
}

void style_set_border_bottom(css_computed_style *s, int width)
{
    set_border_bottom_width(s, CSS_BORDER_WIDTH_WIDTH, (css_fixed_or_calc)INTTOFIX(width), CSS_UNIT_PX);
    if (width > 0)
        set_border_bottom_style(s, CSS_BORDER_STYLE_SOLID);
    else
        set_border_bottom_style(s, CSS_BORDER_STYLE_NONE);
}

void style_set_padding_bottom(css_computed_style *s, int px)
{
    set_padding_bottom(s, CSS_PADDING_SET, (css_fixed_or_calc)INTTOFIX(px), CSS_UNIT_PX);
}

void style_set_min_height(css_computed_style *s, int px)
{
    if (px > 0)
        set_min_height(s, CSS_MIN_HEIGHT_SET, (css_fixed_or_calc)INTTOFIX(px), CSS_UNIT_PX);
    else
        set_min_height(s, CSS_MIN_HEIGHT_AUTO, (css_fixed_or_calc)0, CSS_UNIT_PX);
}

void destroy_mock_style(css_computed_style *s)
{
    free(s);
}

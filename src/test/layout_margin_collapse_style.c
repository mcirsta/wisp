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

    /* Mock styles reach layout directly, without selection/composition. */
    set_white_space_collapse(s, CSS_WHITE_SPACE_COLLAPSE_COLLAPSE);
    set_text_wrap_mode(s, CSS_TEXT_WRAP_MODE_WRAP);

    /* font-size: 16px (needed for CSS unit conversions) */
    set_font_size(s, CSS_FONT_SIZE_DIMENSION, (css_fixed_or_calc)INTTOFIX(16), CSS_UNIT_PX);

    return s;
}

void style_set_flex_or_grid(css_computed_style *s, bool grid)
{
    set_display(s, grid ? CSS_DISPLAY_GRID : CSS_DISPLAY_FLEX);
}

void style_set_table_display(css_computed_style *s, uint8_t display)
{
    set_display(s, display);
    set_border_collapse(s, CSS_BORDER_COLLAPSE_SEPARATE);
    set_border_spacing(s, CSS_BORDER_SPACING_SET, (css_fixed_or_calc)0, CSS_UNIT_PX, (css_fixed_or_calc)0, CSS_UNIT_PX);
}

void style_set_box_sizing(css_computed_style *s, uint8_t sizing)
{
    set_box_sizing(s, sizing);
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

void style_set_clear(css_computed_style *s, uint8_t clear)
{
    set_clear(s, clear);
}

void style_set_overflow_hidden(css_computed_style *s)
{
    set_overflow_x(s, CSS_OVERFLOW_HIDDEN);
    set_overflow_y(s, CSS_OVERFLOW_HIDDEN);
}

void style_set_flow_root(css_computed_style *s)
{
    set_display(s, CSS_DISPLAY_FLOW_ROOT);
}

void style_set_width(css_computed_style *s, int value, bool percent)
{
    set_width(s, value < 0 ? CSS_WIDTH_AUTO : CSS_WIDTH_SET,
        (css_fixed_or_calc)INTTOFIX(value < 0 ? 0 : value), percent ? CSS_UNIT_PCT : CSS_UNIT_PX);
}

void style_set_min_width(css_computed_style *s, int px)
{
    set_min_width(s, CSS_MIN_WIDTH_SET, (css_fixed_or_calc)INTTOFIX(px), CSS_UNIT_PX);
}

void style_set_max_width(css_computed_style *s, int px)
{
    set_max_width(
        s, px < 0 ? CSS_MAX_WIDTH_NONE : CSS_MAX_WIDTH_SET, (css_fixed_or_calc)INTTOFIX(px < 0 ? 0 : px), CSS_UNIT_PX);
}

void style_set_horizontal_edges(css_computed_style *s, int padding, int border)
{
    set_padding_left(s, CSS_PADDING_SET, (css_fixed_or_calc)INTTOFIX(padding), CSS_UNIT_PX);
    set_padding_right(s, CSS_PADDING_SET, (css_fixed_or_calc)INTTOFIX(padding), CSS_UNIT_PX);
    set_border_left_width(s, CSS_BORDER_WIDTH_WIDTH, (css_fixed_or_calc)INTTOFIX(border), CSS_UNIT_PX);
    set_border_right_width(s, CSS_BORDER_WIDTH_WIDTH, (css_fixed_or_calc)INTTOFIX(border), CSS_UNIT_PX);
    set_border_left_style(s, border ? CSS_BORDER_STYLE_SOLID : CSS_BORDER_STYLE_NONE);
    set_border_right_style(s, border ? CSS_BORDER_STYLE_SOLID : CSS_BORDER_STYLE_NONE);
}

void style_set_position_absolute(css_computed_style *s)
{
    set_position(s, CSS_POSITION_ABSOLUTE);
}

void style_set_percentage_padding(css_computed_style *s, int percent)
{
    set_padding_left(s, CSS_PADDING_SET, (css_fixed_or_calc)INTTOFIX(percent), CSS_UNIT_PCT);
    set_padding_right(s, CSS_PADDING_SET, (css_fixed_or_calc)INTTOFIX(percent), CSS_UNIT_PCT);
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

void style_set_max_height(css_computed_style *s, int px)
{
    set_max_height(s, px < 0 ? CSS_MAX_HEIGHT_NONE : CSS_MAX_HEIGHT_SET, (css_fixed_or_calc)INTTOFIX(px < 0 ? 0 : px),
        CSS_UNIT_PX);
}

void destroy_mock_style(css_computed_style *s)
{
    free(s);
}

void style_set_inline_metrics(css_computed_style *s, int font_px, int line_px, uint8_t align)
{
    set_font_size(s, CSS_FONT_SIZE_DIMENSION, (css_fixed_or_calc)INTTOFIX(font_px), CSS_UNIT_PX);
    set_line_height(s, CSS_LINE_HEIGHT_DIMENSION, (css_fixed_or_calc)INTTOFIX(line_px), CSS_UNIT_PX);
    set_vertical_align(s, align, (css_fixed_or_calc)0, CSS_UNIT_PX);
}

void style_set_text_alignment(css_computed_style *s, uint8_t align, uint8_t direction)
{
    set_text_align(s, align);
    set_direction(s, direction);
}

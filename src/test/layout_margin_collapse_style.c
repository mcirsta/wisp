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
    set_height(s, CSS_HEIGHT_AUTO, 0, CSS_UNIT_PX);

    /* font-size: 16px (needed for CSS unit conversions) */
    set_font_size(s, CSS_FONT_SIZE_DIMENSION, INTTOFIX(16), CSS_UNIT_PX);

    return s;
}

void style_set_margins(css_computed_style *s, int top, int right, int bottom, int left)
{
    if (top != INT_MIN)
        set_margin_top(s, CSS_MARGIN_SET, INTTOFIX(top), CSS_UNIT_PX);
    if (right != INT_MIN)
        set_margin_right(s, CSS_MARGIN_SET, INTTOFIX(right), CSS_UNIT_PX);
    if (bottom != INT_MIN)
        set_margin_bottom(s, CSS_MARGIN_SET, INTTOFIX(bottom), CSS_UNIT_PX);
    if (left != INT_MIN)
        set_margin_left(s, CSS_MARGIN_SET, INTTOFIX(left), CSS_UNIT_PX);
}

void style_set_border_top(css_computed_style *s, int width)
{
    set_border_top_width(s, CSS_BORDER_WIDTH_WIDTH, INTTOFIX(width), CSS_UNIT_PX);
    if (width > 0)
        set_border_top_style(s, CSS_BORDER_STYLE_SOLID);
    else
        set_border_top_style(s, CSS_BORDER_STYLE_NONE);
}

void style_set_padding_top(css_computed_style *s, int px)
{
    set_padding_top(s, CSS_PADDING_SET, INTTOFIX(px), CSS_UNIT_PX);
}

void style_set_height_px(css_computed_style *s, int px)
{
    if (px < 0)
        set_height(s, CSS_HEIGHT_AUTO, 0, CSS_UNIT_PX);
    else
        set_height(s, CSS_HEIGHT_SET, INTTOFIX(px), CSS_UNIT_PX);
}

void destroy_mock_style(css_computed_style *s)
{
    free(s);
}

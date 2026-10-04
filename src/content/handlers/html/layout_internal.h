/*
 * Copyright 2003 James Bursa <bursa@users.sourceforge.net>
 *
 * This file is part of NetSurf, http://www.netsurf-browser.org/
 *
 * NetSurf is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * NetSurf is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * \file
 * HTML layout private interface.
 */

#ifndef WISP_HTML_LAYOUT_INTERNAL_H
#define WISP_HTML_LAYOUT_INTERNAL_H

#include <assert.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>

#include <wisp/content/handlers/html/box.h>
#include <wisp/utils/utils.h>

struct html_content;
struct gui_layout_table;

#define AUTO INT_MIN

/* For assertions at the point a layout value is consumed as pixels.
 * Negative margins/coordinates are valid. This does not resolve auto or
 * validate an available-size parameter whose API uses -1 for indefinite. */
static inline bool lh__is_resolved(int value)
{
    return value != AUTO && value != UNKNOWN_WIDTH;
}

/* Note: enum css_size_type and struct css_size are defined in box.h */

/**
 * Layout a block formatting context.
 *
 * \param  block            BLOCK, INLINE_BLOCK, or TABLE_CELL to layout
 * \param  viewport_height  Height of viewport in pixels or -ve if unknown
 * \param  content          Memory pool for any new boxes
 * \return  true on success, false on memory exhaustion
 *
 * This function carries out layout of a block and its children, as described
 * in CSS 2.1 9.4.1.
 */
bool layout_block_context(struct box *block, int viewport_height, struct html_content *content);

/**
 * Layout a table.
 *
 * \param  table            table to layout
 * \param  available_width  width of containing block
 * \param  content          memory pool for any new boxes
 * \return  true on success, false on memory exhaustion
 */
bool layout_table(struct box *table, int available_width, struct html_content *content);

/**
 * Layout a flex container.
 *
 * \param[in] flex             table to layout
 * \param[in] available_width  width of containing block
 * \param[in] content          memory pool for any new boxes
 * \return  true on success, false on memory exhaustion
 */
bool layout_flex(struct box *flex, int available_width, struct html_content *content);

/**
 * Redistribute auto margin space for a column flex container.
 *
 * Called after the container's height becomes definite to adjust positions
 * of children with margin-top/bottom: auto.
 *
 * \param[in] flex  The column flex container box
 * \return true on success
 */
bool layout_flex_redistribute_auto_margins_vertical(struct box *flex);

/**
 * Calculate minimum and maximum width of any box type.
 *
 * \param box       box to calculate min/max width for
 * \param font_func font functions
 * \param content   The HTML content being laid out
 * \return true on success, false on memory exhaustion
 */
bool layout_minmax_box(struct box *box, const struct gui_layout_table *font_func, const struct html_content *content);

/**
 * Perform grid layout on a grid container.
 *
 * \param grid            grid container box
 * \param available_width available width in pixels
 * \param content         html content
 * \return true on success, false on failure
 */
bool layout_grid(struct box *grid, int available_width, struct html_content *content);

typedef uint8_t (*css_len_func)(const css_computed_style *style, css_fixed_or_calc *length, css_unit *unit);
typedef uint8_t (*css_border_style_func)(const css_computed_style *style);
typedef uint8_t (*css_border_color_func)(const css_computed_style *style, css_color *color);

/** Array of per-side access functions for computed style margins. */
extern const css_len_func margin_funcs[4];

/** Array of per-side access functions for computed style paddings. */
extern const css_len_func padding_funcs[4];

/** Array of per-side access functions for computed style border_widths. */
extern const css_len_func border_width_funcs[4];

/** Array of per-side access functions for computed style border styles. */
extern const css_border_style_func border_style_funcs[4];

/** Array of per-side access functions for computed style border colors. */
extern const css_border_color_func border_color_funcs[4];

/** Layout helper: Check whether box is a float. */
static inline bool lh__box_is_float_box(const struct box *b)
{
    return b->type == BOX_FLOAT_LEFT || b->type == BOX_FLOAT_RIGHT;
}

/** Layout helper: Check whether box takes part in inline flow. */
static inline bool lh__box_is_inline_flow(const struct box *b)
{
    return b->type == BOX_INLINE || b->type == BOX_INLINE_FLEX || b->type == BOX_INLINE_BLOCK ||
        b->type == BOX_INLINE_GRID || b->type == BOX_TEXT || b->type == BOX_INLINE_END;
}

/** Layout helper: Check whether box takes part in inline flow. */
static inline bool lh__box_is_flex_container(const struct box *b)
{
    return b->type == BOX_FLEX || b->type == BOX_INLINE_FLEX;
}

static inline bool lh__box_is_grid_container(const struct box *b)
{
    return b->type == BOX_GRID || b->type == BOX_INLINE_GRID;
}

/** Layout helper: Check whether box is inline level. (Includes BR.) */
static inline bool lh__box_is_inline_level(const struct box *b)
{
    return lh__box_is_inline_flow(b) || b->type == BOX_BR;
}

static inline bool lh__box_is_absolute(const struct box *b)
{
    return css_computed_position(b->style) == CSS_POSITION_ABSOLUTE ||
        css_computed_position(b->style) == CSS_POSITION_FIXED;
}

/** Only in-flow children participate in flex sizing and stretch. */
static inline bool lh__box_is_flex_item(const struct box *b)
{
    return b->parent != NULL && lh__box_is_flex_container(b->parent) && !(b->style && lh__box_is_absolute(b));
}

/* Out-of-flow boxes in an inline container retain their inner formatter.
 * A table can occur here only when positioned, never as an in-flow table. */
static inline bool lh__box_is_inline_positioned(const struct box *b)
{
    return (b->type == BOX_INLINE_BLOCK || b->type == BOX_INLINE_FLEX || b->type == BOX_INLINE_GRID ||
               b->type == BOX_TABLE) &&
        lh__box_is_absolute(b);
}

/** Layout helper: Check whether box is inline level. (Includes BR, floats.) */
static inline bool lh__box_is_inline_content(const struct box *b)
{
    return lh__box_is_float_box(b) || lh__box_is_inline_level(b) || lh__box_is_inline_positioned(b);
}

/** Layout helper: Check whether box is an object. */
static inline bool lh__box_is_object(const struct box *b)
{
    return b->object || (b->flags & (IFRAME | REPLACE_DIM));
}

/** Layout helper: Check whether box is replaced. */
static inline bool lh__box_is_replace(const struct box *b)
{
    return b->gadget || lh__box_is_object(b);
}

/** Layout helper: Check for CSS border on given side. */
static inline bool lh__have_border(enum box_side side, const css_computed_style *style)
{
    return border_style_funcs[side](style) != CSS_BORDER_STYLE_NONE;
}

static inline bool lh__flex_main_is_horizontal(const struct box *flex)
{
    const css_computed_style *style = flex->style;

    assert(style != NULL);

    switch (css_computed_flex_direction(style)) {
    default: /* Fallthrough. */
    case CSS_FLEX_DIRECTION_ROW: /* Fallthrough. */
    case CSS_FLEX_DIRECTION_ROW_REVERSE:
        return true;
    case CSS_FLEX_DIRECTION_COLUMN: /* Fallthrough. */
    case CSS_FLEX_DIRECTION_COLUMN_REVERSE:
        return false;
    }
}

static inline bool lh__flex_direction_reversed(const struct box *flex)
{
    switch (css_computed_flex_direction(flex->style)) {
    default: /* Fallthrough. */
    case CSS_FLEX_DIRECTION_ROW_REVERSE: /* Fallthrough. */
    case CSS_FLEX_DIRECTION_COLUMN_REVERSE:
        return true;
    case CSS_FLEX_DIRECTION_ROW: /* Fallthrough. */
    case CSS_FLEX_DIRECTION_COLUMN:
        return false;
    }
}

static inline int lh__non_auto_margin(const struct box *b, enum box_side side)
{
    assert(b->margin[side] != UNKNOWN_WIDTH);
    return (b->margin[side] == AUTO) ? 0 : b->margin[side];
}

static inline int lh__delta_outer_height(const struct box *b)
{
    assert(lh__is_resolved(b->padding[TOP]));
    assert(lh__is_resolved(b->padding[BOTTOM]));
    assert(lh__is_resolved(b->border[TOP].width));
    assert(lh__is_resolved(b->border[BOTTOM].width));
    return b->padding[TOP] + b->padding[BOTTOM] + b->border[TOP].width + b->border[BOTTOM].width +
        lh__non_auto_margin(b, TOP) + lh__non_auto_margin(b, BOTTOM);
}

static inline int lh__delta_outer_width(const struct box *b)
{
    assert(lh__is_resolved(b->padding[LEFT]));
    assert(lh__is_resolved(b->padding[RIGHT]));
    assert(lh__is_resolved(b->border[LEFT].width));
    assert(lh__is_resolved(b->border[RIGHT].width));
    return b->padding[LEFT] + b->padding[RIGHT] + b->border[LEFT].width + b->border[RIGHT].width +
        lh__non_auto_margin(b, LEFT) + lh__non_auto_margin(b, RIGHT);
}

static inline int lh__delta_outer_main(const struct box *flex, const struct box *b)
{
    if (lh__flex_main_is_horizontal(flex)) {
        return lh__delta_outer_width(b);
    } else {
        return lh__delta_outer_height(b);
    }
}

static inline int lh__delta_outer_cross(const struct box *flex, const struct box *b)
{
    if (lh__flex_main_is_horizontal(flex) == false) {
        return lh__delta_outer_width(b);
    } else {
        return lh__delta_outer_height(b);
    }
}

static inline int *lh__box_size_main_ptr(bool horizontal, struct box *b)
{
    return horizontal ? &b->width : &b->height;
}

static inline int *lh__box_size_cross_ptr(bool horizontal, struct box *b)
{
    return horizontal ? &b->height : &b->width;
}

static inline int lh__box_size_main(bool horizontal, const struct box *b)
{
    return horizontal ? b->width : b->height;
}

static inline int lh__box_size_cross(bool horizontal, const struct box *b)
{
    return horizontal ? b->height : b->width;
}

static inline bool lh__box_size_cross_is_auto(bool horizontal, struct box *b)
{
    css_fixed_or_calc length = (css_fixed_or_calc)0;
    css_unit unit;

    if (horizontal) {
        return css_computed_height(b->style, &length, &unit) == CSS_HEIGHT_AUTO;
    } else {
        return css_computed_width(b->style, &length, &unit) == CSS_WIDTH_AUTO;
    }
}

static inline enum css_align_self_e lh__box_align_self(const struct box *flex, const struct box *item)
{
    enum css_align_self_e align_self = css_computed_align_self(item->style);

    if (align_self == CSS_ALIGN_SELF_AUTO) {
        align_self = css_computed_align_items(flex->style);
    }

    return align_self == CSS_ALIGN_SELF_NORMAL ? CSS_ALIGN_SELF_STRETCH : align_self;
}

/* Horizontal writing: only row items without auto cross margins participate
 * in a shared first-baseline group. Column baseline alignment falls back to start. */
static inline bool lh__flex_baseline_item(const struct box *flex, const struct box *item)
{
    return lh__flex_main_is_horizontal(flex) &&
        lh__box_align_self(flex, item) == CSS_ALIGN_SELF_BASELINE &&
        item->margin[TOP] != AUTO && item->margin[BOTTOM] != AUTO;
}

/* Record first baselines only for their consumers. Formatting contexts select
 * their contributing item after placement; ordinary blocks propagate it. */
static inline bool layout_needs_first_baseline(const struct box *box)
{
    for (; box &&
        (box->type == BOX_BLOCK || box->type == BOX_TABLE_CELL || box->type == BOX_TABLE ||
            lh__box_is_flex_container(box) || lh__box_is_grid_container(box));
        box = box->parent) {
        if (box_has_outside_marker(box) || box->type == BOX_TABLE_CELL || box->type == BOX_INLINE_FLEX ||
            box->type == BOX_INLINE_GRID)
            return true;
        if (box->style && lh__box_is_absolute(box))
            break;
        if (lh__box_is_flex_item(box) && lh__flex_baseline_item(box->parent, box))
            return true;
        if (box->parent && lh__box_is_grid_container(box->parent) && box->style &&
            lh__box_align_self(box->parent, box) == CSS_ALIGN_SELF_BASELINE && box->margin[TOP] != AUTO &&
            box->margin[BOTTOM] != AUTO)
            return true;
    }
    return false;
}

/* Baseline relative to the padding-edge origin. A box without one uses its
 * bottom border edge. A scrolling block's baseline cannot escape that edge. */
static inline int lh__box_first_baseline(const struct box *box)
{
    int bottom = box->padding[TOP] + box->height + box->padding[BOTTOM] + box->border[BOTTOM].width;
    if (!(box->flags & HAS_BASELINE))
        return bottom;
    return css_computed_overflow_y(box->style) == CSS_OVERFLOW_VISIBLE ? box->baseline : min(box->baseline, bottom);
}

/**
 * Determine width of margin, borders, and padding on one side of a box.
 *
 * \param unit_len_ctx  CSS length conversion context for document
 * \param style    style to measure
 * \param side     side of box to measure
 * \param margin   whether margin width is required
 * \param border   whether border width is required
 * \param padding  whether padding width is required
 * \param fixed    increased by sum of fixed margin, border, and padding
 * \param frac     increased by sum of fractional margin and padding
 */
static inline void calculate_mbp_width(const css_unit_ctx *unit_len_ctx, const css_computed_style *style,
    unsigned int side, bool margin, bool border, bool padding, int *fixed, float *frac)
{
    css_fixed_or_calc value = (css_fixed_or_calc)0;
    css_unit unit = CSS_UNIT_PX;

    assert(style);

    /* margin */
    if (margin) {
        enum css_margin_e type;

        type = margin_funcs[side](style, &value, &unit);
        if (type == CSS_MARGIN_SET) {
            if (unit == CSS_UNIT_PCT) {
                *frac += FIXTOFLT(FDIV(value.value, F_100));
            } else if (unit == CSS_UNIT_CALC) {
                int px = 0;
                if (css_computed_length_to_px(style, unit_len_ctx, -1, value, unit, &px) == CSS_OK) {
                    *fixed += px;
                }
            } else {
                *fixed += FIXTOINT(css_unit_len2device_px(style, unit_len_ctx, value.value, unit));
            }
        }
    }

    /* border */
    if (border) {
        if (lh__have_border(side, style)) {
            border_width_funcs[side](style, &value, &unit);

            if (unit == CSS_UNIT_CALC) {
                int px = 0;
                if (css_computed_length_to_px(style, unit_len_ctx, -1, value, unit, &px) == CSS_OK) {
                    *fixed += px;
                }
            } else {
                *fixed += FIXTOINT(css_unit_len2device_px(style, unit_len_ctx, value.value, unit));
            }
        }
    }

    /* padding */
    if (padding) {
        padding_funcs[side](style, &value, &unit);
        if (unit == CSS_UNIT_PCT) {
            *frac += FIXTOFLT(FDIV(value.value, F_100));
        } else if (unit == CSS_UNIT_CALC) {
            int px = 0;
            if (css_computed_length_to_px(style, unit_len_ctx, -1, value, unit, &px) == CSS_OK) {
                *fixed += px;
            }
        } else {
            *fixed += FIXTOINT(css_unit_len2device_px(style, unit_len_ctx, value.value, unit));
        }
    }
}

static inline bool lh__length_to_px(const css_computed_style *style, const css_unit_ctx *unit_len_ctx,
    int available_px, css_fixed_or_calc length, css_unit unit, int *px_out)
{
    if (unit == CSS_UNIT_CALC) {
        return css_computed_length_to_px(style, unit_len_ctx, available_px, length, unit, px_out) == CSS_OK;
    }
    if (unit == CSS_UNIT_PCT) {
        if (available_px < 0) {
            return false;
        }
        *px_out = FPCT_OF_INT_TOINT(length.value, available_px);
        return true;
    }
    *px_out = FIXTOINT(css_unit_len2device_px(style, unit_len_ctx, length.value, unit));
    return true;
}

static inline css_unit_ctx lh__with_container_dimensions(
    const css_unit_ctx *unit_len_ctx, int container_width, int container_height)
{
    css_unit_ctx ctx = *unit_len_ctx;

    if (container_width >= 0) {
        ctx.container_width = css_unit_device2css_px(INTTOFIX(container_width), unit_len_ctx->device_dpi);
    }

    if (container_height >= 0) {
        ctx.container_height = css_unit_device2css_px(INTTOFIX(container_height), unit_len_ctx->device_dpi);
    }

    return ctx;
}

static inline int lh__available_height_for_box(const struct box *box, int viewport_height)
{
    struct box *containing_block = NULL;
    enum css_height_e cbhtype = CSS_HEIGHT_AUTO;

    if (box == NULL || box->style == NULL) {
        return -1;
    }

    if ((css_computed_position(box->style) == CSS_POSITION_ABSOLUTE ||
            css_computed_position(box->style) == CSS_POSITION_FIXED) && box->parent) {
        assert(box->float_container);
        containing_block = box->float_container;
    } else if (box->parent && (box->parent->type == BOX_FLOAT_LEFT || box->parent->type == BOX_FLOAT_RIGHT)) {
        struct box *cb = box->parent;

        while (cb != NULL &&
            (cb->style == NULL || cb->type == BOX_INLINE_CONTAINER || cb->type == BOX_FLOAT_LEFT ||
                cb->type == BOX_FLOAT_RIGHT)) {
            cb = cb->parent;
        }
        containing_block = cb;
    } else if (box->parent && box->parent->type != BOX_INLINE_CONTAINER) {
        containing_block = box->parent;
    } else if (box->parent && box->parent->type == BOX_INLINE_CONTAINER) {
        assert(box->parent->parent);
        containing_block = box->parent->parent;
    }

    if (containing_block != NULL && containing_block->style != NULL) {
        css_fixed_or_calc f = (css_fixed_or_calc)0;
        css_unit u = CSS_UNIT_PX;

        cbhtype = css_computed_height(containing_block->style, &f, &u);
    }

    if (containing_block != NULL && containing_block->height != AUTO &&
        (css_computed_position(box->style) == CSS_POSITION_ABSOLUTE ||
            css_computed_position(box->style) == CSS_POSITION_FIXED || cbhtype == CSS_HEIGHT_SET ||
            (containing_block->flags & (ASPECT_RATIO_HEIGHT | HEIGHT_STRETCHED)))) {
        return containing_block->height;
    }

    if ((!box->parent || !box->parent->parent) && viewport_height >= 0) {
        return viewport_height;
    }

    return -1;
}

/* Ratio calculations retain their fraction until conversion to integer layout
 * pixels. Truncation biases every scaled image downward and can turn an almost
 * integral result (including CSS fixed-point error) into a whole missing pixel. */
static inline int layout_round_ratio_dimension(double result)
{
    return result < 0 ? 0 : result >= INT_MAX ? INT_MAX : (int)(result + 0.5);
}

/** Transfer a content-box dimension through the specified preferred ratio.
 * Border-box ratios include padding/borders on both axes, never margins.
 * The caller selects between this and a replaced element's natural ratio. */
static inline int layout_ratio_dimension(const struct box *box, int size, bool to_height)
{
    css_fixed width = 0, height = 0;
    if (box->style == NULL || size < 0) {
        return AUTO;
    }
    uint8_t type = css_computed_aspect_ratio(box->style, &width, &height);
    if ((type != CSS_ASPECT_RATIO_RATIO && type != CSS_ASPECT_RATIO_AUTO_RATIO) || width <= 0 || height <= 0) {
        return AUTO;
    }
    double horizontal = 0, vertical = 0;
    if (css_computed_box_sizing(box->style) == CSS_BOX_SIZING_BORDER_BOX) {
        horizontal = (double)box->padding[LEFT] + box->padding[RIGHT] + box->border[LEFT].width +
            box->border[RIGHT].width;
        vertical = (double)box->padding[TOP] + box->padding[BOTTOM] + box->border[TOP].width +
            box->border[BOTTOM].width;
    }
    double result = to_height ? (size + horizontal) * height / width - vertical
                              : (size + vertical) * width / height - horizontal;
    return layout_round_ratio_dimension(result);
}

/** Called after layout has chosen the used width and refreshed CSS height. */
static inline void layout_prepare_aspect_ratio_height(struct box *box)
{
    box->flags &= ~ASPECT_RATIO_HEIGHT;
    if (box->height != AUTO || box->object != NULL || box->type == BOX_TABLE || box->type == BOX_TABLE_CELL) {
        return;
    }
    int height = layout_ratio_dimension(box, box->width, true);
    if (height != AUTO) {
        box->height = height;
        box->flags |= ASPECT_RATIO_HEIGHT;
    }
}

/** A preferred ratio can yield to the automatic content minimum. */
static inline void layout_ratio_content_height(struct box *box, int content_height)
{
    if ((box->flags & ASPECT_RATIO_HEIGHT) && content_height > box->height &&
        css_computed_overflow_y(box->style) == CSS_OVERFLOW_VISIBLE) {
        css_fixed_or_calc value = (css_fixed_or_calc)0;
        css_unit unit = CSS_UNIT_PX;
        if (css_computed_min_height(box->style, &value, &unit) == CSS_MIN_HEIGHT_AUTO) {
            box->height = content_height;
        }
    }
}

/**
 * Adjust a specified width or height for the box-sizing property.
 *
 * This turns the specified dimension into a content-box dimension.
 *
 * \param  unit_len_ctx          Length conversion context
 * \param  box		    gadget to adjust dimensions of
 * \param  available_width  width of containing block
 * \param  setwidth	    set true if the dimension to be tweaked is a width,
 *				else set false for a height
 * \param  dimension	    current value for given width/height dimension.
 *				updated to new value after consideration of
 *				gadget properties.
 */
static inline void layout_handle_box_sizing(
    const css_unit_ctx *unit_len_ctx, const struct box *box, int available_width, bool setwidth, int *dimension)
{
    enum css_box_sizing_e bs;

    assert(box && box->style);

    bs = css_computed_box_sizing(box->style);

    if (bs == CSS_BOX_SIZING_BORDER_BOX) {
        int orig = *dimension;
        int fixed = 0;
        float frac = 0;

        calculate_mbp_width(unit_len_ctx, box->style, setwidth ? LEFT : TOP, false, true, true, &fixed, &frac);
        calculate_mbp_width(unit_len_ctx, box->style, setwidth ? RIGHT : BOTTOM, false, true, true, &fixed, &frac);
        orig -= frac * available_width + fixed;
        *dimension = orig > 0 ? orig : 0;
    }
}

static inline bool lh__intrinsic_width_unit(css_unit unit)
{
    return unit == CSS_UNIT_MIN_CONTENT || unit == CSS_UNIT_MAX_CONTENT || unit == CSS_UNIT_FIT_CONTENT;
}

/* Cached intrinsic widths include the outer edges. Sizing keywords specify
 * content sizes, regardless of box-sizing (CSS Sizing 3.3). */
static inline int
lh__intrinsic_width(const struct box *box, const css_unit_ctx *unit_ctx, int available_width, css_unit unit)
{
    assert(box->max_width != UNKNOWN_MAX_WIDTH);
    int edges = 0;
    float fraction = 0;
    calculate_mbp_width(unit_ctx, box->style, LEFT, true, true, true, &edges, &fraction);
    calculate_mbp_width(unit_ctx, box->style, RIGHT, true, true, true, &edges, &fraction);
    if (available_width >= 0)
        edges += fraction * available_width;
    int minimum = max(0, box->min_width.value - edges);
    int maximum = max(minimum, box->max_width - edges);
    if (unit == CSS_UNIT_MIN_CONTENT)
        return minimum;
    if (unit == CSS_UNIT_MAX_CONTENT || available_width < 0)
        return maximum;
    return min(maximum, max(minimum, available_width - edges));
}

/* Optional destinations for CSS dimensions. Omitted members are not computed;
 * callers may write directly into box fields or keep separate working values. */
struct layout_dimension_outputs {
    int *width;
    int *height;
    int *max_width;
    struct css_size *min_width;
    int *max_height;
    struct css_size *min_height;
    int *margin;
    int *padding;
    struct box_border *border;
};

/* One containing-block resolution and length context per measurement. */
struct layout_dimension_context {
    const struct box *box;
    css_unit_ctx unit_len_ctx;
    int available_width;
    int available_height;
};

static inline void
layout_find_preferred_dimensions(const struct layout_dimension_context *ctx, const struct layout_dimension_outputs *out)
{
    const struct box *box = ctx->box;
    const css_computed_style *style = ctx->box->style;
    const css_unit_ctx *box_unit_len_ctx = &ctx->unit_len_ctx;
    int available_width = ctx->available_width;
    int available_height = ctx->available_height;
    int *width = out->width;
    int *height = out->height;

    if (width) {
        css_fixed_or_calc value = (css_fixed_or_calc)0;
        css_unit unit = CSS_UNIT_PX;
        if (css_computed_width(style, &value, &unit) == CSS_WIDTH_SET && lh__intrinsic_width_unit(unit) &&
            box->max_width != UNKNOWN_MAX_WIDTH) {
            *width = lh__intrinsic_width(box, box_unit_len_ctx, available_width, unit);
        } else if (css_computed_width_px(style, box_unit_len_ctx, available_width, width) == CSS_WIDTH_SET) {
            layout_handle_box_sizing(box_unit_len_ctx, box, available_width, true, width);
        } else {
            *width = AUTO;
        }
    }

    if (height) {
        enum css_height_e htype;
        css_fixed_or_calc value = (css_fixed_or_calc)0;
        css_unit unit = CSS_UNIT_PX;

        htype = css_computed_height(style, &value, &unit);

        /* Percentage-dependent heights need a definite containing height. */
        if (htype != CSS_HEIGHT_SET ||
            !lh__length_to_px(style, box_unit_len_ctx, available_height, value, unit, height)) {
            *height = AUTO;
        }

        if (*height != AUTO) {
            layout_handle_box_sizing(box_unit_len_ctx, box, available_width, false, height);
        }
    }
}

static inline void
layout_find_width_limits(const struct layout_dimension_context *ctx, const struct layout_dimension_outputs *out)
{
    const struct box *box = ctx->box;
    const css_computed_style *style = ctx->box->style;
    const css_unit_ctx *box_unit_len_ctx = &ctx->unit_len_ctx;
    int available_width = ctx->available_width;
    int *max_width = out->max_width;
    struct css_size *min_width = out->min_width;

    if (max_width) {
        enum css_max_width_e type;
        css_fixed_or_calc value = (css_fixed_or_calc)0;
        css_unit unit = CSS_UNIT_PX;

        type = css_computed_max_width(style, &value, &unit);

        if (type == CSS_MAX_WIDTH_SET) {
            if (lh__intrinsic_width_unit(unit)) {
                *max_width = box->max_width == UNKNOWN_MAX_WIDTH ? -1 :
                    lh__intrinsic_width(box, box_unit_len_ctx, available_width, unit);
            } else if (unit == CSS_UNIT_PCT) {
                *max_width = FPCT_OF_INT_TOINT(value.value, available_width);
            } else if (unit == CSS_UNIT_CALC) {
                if (!lh__length_to_px(style, box_unit_len_ctx, available_width, value, unit, max_width)) {
                    *max_width = -1;
                }
            } else {
                *max_width = FIXTOINT(css_unit_len2device_px(style, box_unit_len_ctx, value.value, unit));
            }
        } else {
            /* Inadmissible */
            *max_width = -1;
        }

        if (*max_width != -1 && !lh__intrinsic_width_unit(unit)) {
            layout_handle_box_sizing(box_unit_len_ctx, box, available_width, true, max_width);
        }
    }

    if (min_width) {
        enum css_min_width_e type;
        css_fixed_or_calc value = (css_fixed_or_calc)0;
        css_unit unit = CSS_UNIT_PX;

        type = ns_computed_min_width(style, &value, &unit);

        if (type == CSS_MIN_WIDTH_SET) {
            min_width->type = CSS_SIZE_SET;
            if (lh__intrinsic_width_unit(unit)) {
                min_width->value = box->max_width == UNKNOWN_MAX_WIDTH ? 0 :
                    lh__intrinsic_width(box, box_unit_len_ctx, available_width, unit);
            } else if (unit == CSS_UNIT_PCT) {
                min_width->value = FPCT_OF_INT_TOINT(value.value, available_width);
            } else if (unit == CSS_UNIT_CALC) {
                if (!lh__length_to_px(style, box_unit_len_ctx, available_width, value, unit, &min_width->value)) {
                    min_width->type = CSS_SIZE_AUTO;
                    min_width->value = 0;
                }
            } else {
                min_width->value = FIXTOINT(css_unit_len2device_px(style, box_unit_len_ctx, value.value, unit));
            }
        } else {
            /* min-width: auto */
            min_width->type = CSS_SIZE_AUTO;
            min_width->value = 0;
        }

        if (min_width->value != 0 && !lh__intrinsic_width_unit(unit)) {
            layout_handle_box_sizing(box_unit_len_ctx, box, available_width, true, &min_width->value);
        }
    }
}

static inline void
layout_find_height_limits(const struct layout_dimension_context *ctx, const struct layout_dimension_outputs *out)
{
    const struct box *box = ctx->box;
    const css_computed_style *style = ctx->box->style;
    const css_unit_ctx *box_unit_len_ctx = &ctx->unit_len_ctx;
    int available_width = ctx->available_width;
    int available_height = ctx->available_height;
    int *max_height = out->max_height;
    struct css_size *min_height = out->min_height;

    if (max_height) {
        css_fixed_or_calc value = (css_fixed_or_calc)0;
        css_unit unit = CSS_UNIT_PX;
        *max_height = -1;
        if (css_computed_max_height(style, &value, &unit) == CSS_MAX_HEIGHT_SET &&
            lh__length_to_px(style, box_unit_len_ctx, available_height, value, unit, max_height)) {
            layout_handle_box_sizing(box_unit_len_ctx, box, available_width, false, max_height);
        }
    }

    if (min_height) {
        css_fixed_or_calc value = (css_fixed_or_calc)0;
        css_unit unit = CSS_UNIT_PX;
        min_height->type = CSS_SIZE_AUTO;
        min_height->value = 0;
        if (ns_computed_min_height(style, &value, &unit) == CSS_MIN_HEIGHT_SET &&
            lh__length_to_px(style, box_unit_len_ctx, available_height, value, unit, &min_height->value)) {
            min_height->type = CSS_SIZE_SET;
            layout_handle_box_sizing(box_unit_len_ctx, box, available_width, false, &min_height->value);
        }
    }
}

static inline void
layout_find_box_edges(const struct layout_dimension_context *ctx, const struct layout_dimension_outputs *out)
{
    const struct box *box = ctx->box;
    const css_computed_style *style = ctx->box->style;
    const css_unit_ctx *box_unit_len_ctx = &ctx->unit_len_ctx;
    int available_width = ctx->available_width;
    int *margin = out->margin;
    int *padding = out->padding;
    struct box_border *border = out->border;

    for (unsigned int i = 0; i != 4; i++) {
        if (margin) {
            enum css_margin_e type = CSS_MARGIN_AUTO;
            css_fixed_or_calc value = (css_fixed_or_calc)0;
            css_unit unit = CSS_UNIT_PX;

            type = margin_funcs[i](style, &value, &unit);

            if (type == CSS_MARGIN_SET) {
                if (unit == CSS_UNIT_PCT) {
                    margin[i] = FPCT_OF_INT_TOINT(value.value, available_width);
                } else if (unit == CSS_UNIT_CALC) {
                    if (!lh__length_to_px(style, box_unit_len_ctx, available_width, value, unit, &margin[i])) {
                        margin[i] = AUTO;
                    }
                } else {
                    margin[i] = FIXTOINT(css_unit_len2device_px(style, box_unit_len_ctx, value.value, unit));
                }
            } else {
                margin[i] = AUTO;
            }
        }

        if (padding) {
            css_fixed_or_calc value = (css_fixed_or_calc)0;
            css_unit unit = CSS_UNIT_PX;

            padding_funcs[i](style, &value, &unit);

            if (unit == CSS_UNIT_PCT) {
                padding[i] = FPCT_OF_INT_TOINT(value.value, available_width);
            } else if (unit == CSS_UNIT_CALC) {
                if (!lh__length_to_px(style, box_unit_len_ctx, available_width, value, unit, &padding[i])) {
                    padding[i] = 0;
                }
            } else {
                padding[i] = FIXTOINT(css_unit_len2device_px(style, box_unit_len_ctx, value.value, unit));
            }
        }

        /* Table cell borders are populated in table.c */
        if (border && box->type != BOX_TABLE_CELL) {
            enum css_border_style_e bstyle = CSS_BORDER_STYLE_NONE;
            css_color color = 0;
            css_fixed_or_calc value = (css_fixed_or_calc)0;
            css_unit unit = CSS_UNIT_PX;

            border_width_funcs[i](style, &value, &unit);
            bstyle = border_style_funcs[i](style);
            border_color_funcs[i](style, &color);

            border[i].style = bstyle;
            border[i].c = color;

            if (bstyle == CSS_BORDER_STYLE_HIDDEN || bstyle == CSS_BORDER_STYLE_NONE)
                /* spec unclear: following Mozilla */
                border[i].width = 0;
            else {
                if (unit == CSS_UNIT_CALC) {
                    if (!lh__length_to_px(style, box_unit_len_ctx, -1, value, unit, &border[i].width)) {
                        border[i].width = 0;
                    }
                } else {
                    border[i].width = FIXTOINT(css_unit_len2device_px(style, box_unit_len_ctx, value.value, unit));
                }
            }

            /* Special case for border-collapse: make all borders
             * on table/table-row-group/table-row zero width. */
            if (css_computed_border_collapse(style) == CSS_BORDER_COLLAPSE_COLLAPSE &&
                (box->type == BOX_TABLE || box->type == BOX_TABLE_ROW_GROUP || box->type == BOX_TABLE_ROW))
                border[i].width = 0;
        }
    }
}

static inline void
layout_find_ratio_width(const struct layout_dimension_context *ctx, const struct layout_dimension_outputs *out)
{
    const struct box *box = ctx->box;
    int *width = out->width;
    int *height = out->height;
    int *max_height = out->max_height;
    struct css_size *min_height = out->min_height;

    if (width && height && *width == AUTO && *height != AUTO && box->object == NULL) {
        int source_height = *height;
        if (max_height && *max_height >= 0 && source_height > *max_height) {
            source_height = *max_height;
        }
        if (min_height && min_height->type == CSS_SIZE_SET && source_height < min_height->value) {
            source_height = min_height->value;
        }
        *width = layout_ratio_dimension(box, source_height, false);
    }
}

/**
 * Resolve the requested CSS sizes, constraints and box edges.
 *
 * available_width is the containing block width; viewport_height is negative
 * when unknown. Use box->style consistently for lengths and box sizing.
 * Destinations may alias box members: preserve the measurement order, and
 * transfer aspect-ratio only after the requested edges have been written.
 */
static inline void layout_find_dimensions(const css_unit_ctx *unit_len_ctx, int available_width, int viewport_height,
    const struct box *box, const struct layout_dimension_outputs *out)
{
    int available_height = lh__available_height_for_box(box, viewport_height);
    const struct layout_dimension_context ctx = {
        .box = box,
        .unit_len_ctx = lh__with_container_dimensions(unit_len_ctx, available_width, available_height),
        .available_width = available_width,
        .available_height = available_height,
    };

    layout_find_preferred_dimensions(&ctx, out);
    layout_find_width_limits(&ctx, out);
    layout_find_height_limits(&ctx, out);
    layout_find_box_edges(&ctx, out);
    layout_find_ratio_width(&ctx, out);
}

#endif

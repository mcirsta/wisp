/*
 * Copyright 2005 Richard Wilson <info@tinct.net>
 * Copyright 2006 James Bursa <bursa@users.sourceforge.net>
 * Copyright 2008 Michael Drake <tlsa@netsurf-browser.org>
 * Copyright 2003 Phil Mellor <monkeyson@users.sourceforge.net>
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
 * HTML layout implementation.
 *
 * Layout is carried out in two stages:
 *
 * 1. + calculation of minimum / maximum box widths, and
 *    + determination of whether block level boxes will have >zero height
 *
 * 2. + layout (position and dimensions)
 *
 * In most cases the functions for the two stages are a corresponding pair
 * layout_minmax_X() and layout_X().
 */

#include <dom/dom.h>
#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "content/handlers/html/restyle_profile.h"
#include "content/handlers/html/box_sticky.h"

#include <wisp/browser.h>
#include <wisp/browser_window.h>
#include <wisp/content.h>
#include <wisp/content/content.h>
#include <wisp/content/content_protected.h>
#include <wisp/content/handlers/css/utils.h>
#include <wisp/desktop/textarea.h>
#include <wisp/ns_inttypes.h>
#include <wisp/layout.h>
#include <wisp/utils/corestrings.h>
#include <wisp/utils/log.h>
#include <wisp/utils/nsoption.h>
#include <wisp/utils/nsurl.h>
#include <wisp/utils/utils.h>
#include "utils/talloc.h"
#include "desktop/scrollbar.h"

#include <wisp/content/handlers/html/box.h>
#include <wisp/content/handlers/html/box_inspect.h>
#include <wisp/content/handlers/html/form_internal.h>
#include <wisp/content/handlers/html/html.h>
#include <wisp/content/handlers/html/html_save.h>
#include <wisp/content/handlers/html/private.h>
#include "content/handlers/html/box_manipulate.h"
#include "content/handlers/html/font.h"
#include "content/handlers/html/layout.h"
#include "content/handlers/html/layout_grid.h"
#include "content/handlers/html/layout_internal.h"
#include "content/handlers/html/table.h"
#include <svgtiny.h>

/** Array of per-side access functions for computed style margins. */
const css_len_func margin_funcs[4] = {
    [TOP] = css_computed_margin_top,
    [RIGHT] = css_computed_margin_right,
    [BOTTOM] = css_computed_margin_bottom,
    [LEFT] = css_computed_margin_left,
};

/** Array of per-side access functions for computed style paddings. */
const css_len_func padding_funcs[4] = {
    [TOP] = css_computed_padding_top,
    [RIGHT] = css_computed_padding_right,
    [BOTTOM] = css_computed_padding_bottom,
    [LEFT] = css_computed_padding_left,
};

/** Array of per-side access functions for computed style border_widths. */
const css_len_func border_width_funcs[4] = {
    [TOP] = css_computed_border_top_width,
    [RIGHT] = css_computed_border_right_width,
    [BOTTOM] = css_computed_border_bottom_width,
    [LEFT] = css_computed_border_left_width,
};

/** Array of per-side access functions for computed style border styles. */
const css_border_style_func border_style_funcs[4] = {
    [TOP] = css_computed_border_top_style,
    [RIGHT] = css_computed_border_right_style,
    [BOTTOM] = css_computed_border_bottom_style,
    [LEFT] = css_computed_border_left_style,
};

/** Array of per-side access functions for computed style border colors. */
const css_border_color_func border_color_funcs[4] = {
    [TOP] = css_computed_border_top_color,
    [RIGHT] = css_computed_border_right_color,
    [BOTTOM] = css_computed_border_bottom_color,
    [LEFT] = css_computed_border_left_color,
};

/* forward declaration to break cycles */
static bool
layout_minmax_block(struct box *block, const struct gui_layout_table *font_func, const html_content *content);

/* Resolved CSS constraints, separate from a box's cached intrinsic widths. */
struct layout_size_limits {
    struct css_size min_width;
    int max_width;
    struct css_size min_height;
    int max_height;
};

/**
 * Resolve replaced content's auto dimensions using its natural size or ratio.
 * Width/height are updated in place; limits are the resolved CSS constraints.
 * containing_block_width is negative during the intrinsic-width pass.
 * See CSS 2.1 sections 10.3 and 10.6.
 */
static void layout_get_object_dimensions(
    struct box *box, int *width, int *height, const struct layout_size_limits *limits, int containing_block_width)
{
    assert(box->object != NULL);
    assert(width != NULL && height != NULL);

    /* A nested document has no natural dimensions: its laid-out extent depends
     * on the viewport we give it. Only image content supplies a natural size.
     * Keep the default 300x150 size separate from an intrinsic aspect ratio. */
    bool document = content_get_type(box->object) == CONTENT_HTML;
    int intrinsic_width = document ? 0 : content_get_width(box->object);
    int intrinsic_height = document ? 0 : content_get_height(box->object);
    bool contents_skipped = (box->flags & CONTENTS_SKIPPED) != 0;
    if (contents_skipped)
        intrinsic_width = intrinsic_height = 0;

    /* Check for an explicit intrinsic ratio from the content handler.
     * This is set only when the content has no real intrinsic dimensions
     * but has an aspect ratio (e.g., SVG with viewBox but no width/height).
     * The ratio acts as the signal to use containing-block sizing. */
    int ratio_w = 0;
    int ratio_h = 0;
    bool has_ratio_only = !document && !contents_skipped &&
        content_get_intrinsic_ratio(box->object, &ratio_w, &ratio_h);

    /* For all ratio-based calculations below, we use either the explicit
     * ratio (viewBox-only) or the intrinsic dimensions as the ratio source. */
    int eff_ratio_w = has_ratio_only ? ratio_w : intrinsic_width;
    int eff_ratio_h = has_ratio_only ? ratio_h : intrinsic_height;

    css_fixed css_ratio_w = 0, css_ratio_h = 0;
    uint8_t ratio_type = box->style ? css_computed_aspect_ratio(box->style, &css_ratio_w, &css_ratio_h)
                                    : CSS_ASPECT_RATIO_AUTO;
    bool css_ratio = layout_ratio_dimension(box, 0, true) != AUTO &&
        !(ratio_type == CSS_ASPECT_RATIO_AUTO_RATIO && eff_ratio_w > 0 && eff_ratio_h > 0);

    /* DIAG: Log image dimensions before calculation */
    NSLOG(layout, DEBUG,
        "OBJ_DIM box %p: input w=%d h=%d intrinsic=%dx%d ratio=%dx%d has_ratio_only=%d cb_w=%d limits w[%d,%d] h[%d,%d]",
        box, *width, *height, intrinsic_width, intrinsic_height, ratio_w, ratio_h, has_ratio_only,
        containing_block_width, limits->min_width.value, limits->max_width, limits->min_height.value,
        limits->max_height);

    if (*width == AUTO && *height == AUTO) {
        if (has_ratio_only && ratio_w > 0 && ratio_h > 0 && containing_block_width > 0) {
            /* Content has intrinsic ratio but no real intrinsic dimensions
             * (e.g., SVG with viewBox only).  Per CSS Images 3 §5.2:
             * use containing block width, compute height from ratio. */
            *width = containing_block_width;
            *height = layout_round_ratio_dimension((double)*width * ratio_h / ratio_w);
        } else if (intrinsic_width > 0 && intrinsic_height > 0) {
            /* Has intrinsic dimensions — use them (existing behavior) */

#ifdef WISP_DEVICE_PIXEL_LAYOUT
            /* Windows frontend uses device pixels for layout (GDI doesn't auto-scale).
             * Only scale here when intrinsic dimensions are used to determine box size.
             * If CSS specifies explicit width/height, those values are already in the
             * correct coordinate space and should not be scaled. */
            {
                int dpi = browser_get_dpi();
                if (dpi > 0 && dpi != 96) {
                    intrinsic_width = (intrinsic_width * dpi) / 96;
                    intrinsic_height = (intrinsic_height * dpi) / 96;
                }
            }
#endif

            *width = intrinsic_width;
            *height = intrinsic_height;
        } else {
            /* No intrinsic dimensions or ratio —
             * CSS default object size (300×150). */
            *width = contents_skipped ? 0 : 300;
            *height = contents_skipped ? 0 : 150;
        }

        if (css_ratio) {
            *height = layout_ratio_dimension(box, *width, true);
        }

        /* Apply min/max-width constraints */
        bool scaled = false;
        if (limits->min_width.type == CSS_SIZE_SET && limits->min_width.value > 0 && limits->min_width.value > *width) {
            *width = limits->min_width.value;
            scaled = true;
        }
        if (limits->max_width >= 0 && limits->max_width < *width) {
            *width = limits->max_width;
            scaled = true;
        }
        if (scaled && (css_ratio || eff_ratio_w != 0)) {
            *height = css_ratio ? layout_ratio_dimension(box, *width, true)
                                : layout_round_ratio_dimension((double)*width * eff_ratio_h / eff_ratio_w);
        }

        /* Apply min/max-height constraints */
        scaled = false;
        if (limits->min_height.type == CSS_SIZE_SET && limits->min_height.value > 0 &&
            limits->min_height.value > *height) {
            *height = limits->min_height.value;
            scaled = true;
        }
        if (limits->max_height >= 0 && limits->max_height < *height) {
            *height = limits->max_height;
            scaled = true;
        }
        if (scaled && (css_ratio || eff_ratio_h != 0)) {
            *width = css_ratio ? layout_ratio_dimension(box, *height, false)
                               : layout_round_ratio_dimension((double)*height * eff_ratio_w / eff_ratio_h);
        }

    } else if (*width == AUTO) {
        /* Resolve the given height's limits before transferring through the
         * ratio. This also covers percentages resolved by flex stretching. */
        if (limits->max_height >= 0)
            *height = min(*height, limits->max_height);
        if (limits->min_height.type == CSS_SIZE_SET)
            *height = max(*height, limits->min_height.value);
        if (css_ratio)
            *width = layout_ratio_dimension(box, *height, false);
        else if (eff_ratio_h != 0)
            *width = layout_round_ratio_dimension((double)*height * eff_ratio_w / eff_ratio_h);
        else
            *width = document && !contents_skipped ? 300 : intrinsic_width;

        if (limits->min_width.type == CSS_SIZE_SET && limits->min_width.value > 0 && limits->min_width.value > *width)
            *width = limits->min_width.value;
        if (limits->max_width >= 0 && limits->max_width < *width)
            *width = limits->max_width;

    } else if (*height == AUTO) {
        /* Have given width; height is calculated from ratio */

        if (limits->min_width.type == CSS_SIZE_SET && limits->min_width.value > 0 && limits->min_width.value > *width)
            *width = limits->min_width.value;
        if (limits->max_width >= 0 && limits->max_width < *width)
            *width = limits->max_width;

        if (css_ratio)
            *height = layout_ratio_dimension(box, *width, true);
        else if (eff_ratio_w != 0)
            *height = layout_round_ratio_dimension((double)*width * eff_ratio_h / eff_ratio_w);
        else
            *height = document && !contents_skipped ? 150 : intrinsic_height;

        /* CSS 2.2 10.7: height constraints do not change a resolved width.
         * With both dimensions auto, proportional scaling is handled above.
         * object-fit affects painting, not sizing. */
        if (limits->max_height >= 0 && *height > limits->max_height) {
            *height = limits->max_height;
        }
        if (limits->min_height.type == CSS_SIZE_SET && limits->min_height.value > 0 &&
            *height < limits->min_height.value) {
            *height = limits->min_height.value;
        }
    } else {
        /* Specified replaced sizes obey the same constraints as auto sizes. */
        if (limits->max_width >= 0)
            *width = min(*width, limits->max_width);
        if (limits->min_width.type == CSS_SIZE_SET)
            *width = max(*width, limits->min_width.value);
        if (limits->max_height >= 0)
            *height = min(*height, limits->max_height);
        if (limits->min_height.type == CSS_SIZE_SET)
            *height = max(*height, limits->min_height.value);
    }

    /* DIAG: Log final computed dimensions */
    NSLOG(layout, INFO, "OBJ_DIM_OUT box %p: computed w=%d h=%d (intrinsic was %dx%d, ratio=%dx%d has_ratio_only=%d)",
        box, *width, *height, intrinsic_width, intrinsic_height, eff_ratio_w, eff_ratio_h, has_ratio_only);
}


/**
 * Calculate the text-indent length.
 *
 * \param  style  style of block
 * \param  width  width of containing block
 * \return  length of indent
 */
static int layout_text_indent(const css_unit_ctx *unit_len_ctx, const css_computed_style *style, int width)
{
    css_fixed_or_calc value = (css_fixed_or_calc)0;
    css_unit unit = CSS_UNIT_PX;

    css_computed_text_indent(style, &value, &unit);

    if (unit == CSS_UNIT_PCT) {
        return FPCT_OF_INT_TOINT(value.value, width);
    } else if (unit == CSS_UNIT_CALC) {
        int px = 0;
        if (lh__length_to_px(style, unit_len_ctx, width, value, unit, &px)) {
            return px;
        }
        return 0;
    } else {
        return FIXTOINT(css_unit_len2device_px(style, unit_len_ctx, value.value, unit));
    }
}


/**
 * Calculate minimum and maximum width of a table.
 *
 * \param table box of type TABLE
 * \param font_func Font functions
 * \param content  The HTML content we are laying out.
 * \post  table->min_width.value and table->max_width filled in,
 *        0 <= table->min_width.value <= table->max_width
 */
static bool
layout_minmax_table(struct box *table, const struct gui_layout_table *font_func, const html_content *content)
{
    unsigned int i, j;
    int width;
    int border_spacing_h = 0;
    int table_min = 0, table_max = 0;
    int extra_fixed = 0;
    float extra_frac = 0;
    struct column *col;
    struct box *row_group, *row, *cell;

    /* check if the widths have already been calculated */
    if (table->max_width != UNKNOWN_MAX_WIDTH)
        return true;

    if (table_calculate_column_types(&content->unit_len_ctx, table) == false) {
        NSLOG(wisp, ERROR, "Could not establish table column types.");
        return false;
    }
    col = table->table->columns;
    if (!table_prepare_borders(&content->unit_len_ctx, table))
        return false;

    /* start with 0 except for fixed-width columns */
    for (i = 0; i != table->columns; i++) {
        if (col[i].type == COLUMN_WIDTH_FIXED)
            col[i].min = col[i].max = col[i].width;
        else
            col[i].min = col[i].max = 0;
    }

    /* border-spacing is used in the separated borders model */
    if (css_computed_border_collapse(table->style) == CSS_BORDER_COLLAPSE_SEPARATE) {
        css_fixed_or_calc h = (css_fixed_or_calc)0, v = (css_fixed_or_calc)0;
        css_unit hu = CSS_UNIT_PX, vu = CSS_UNIT_PX;

        css_computed_border_spacing(table->style, &h, &hu, &v, &vu);

        if (!lh__length_to_px(table->style, &content->unit_len_ctx, -1, h, hu, &border_spacing_h)) {
            border_spacing_h = 0;
        }
    }

    /* 1st pass: consider cells with colspan 1 only */
    for (row_group = table->children; row_group; row_group = row_group->next)
        for (row = row_group->children; row; row = row->next)
            for (cell = row->children; cell; cell = cell->next) {
                assert(cell->type == BOX_TABLE_CELL);
                assert(cell->style);
                /** TODO: Handle colspan="0" correctly.
                 *        It's currently converted to 1 in box
                 * normaisation */
                assert(cell->columns != 0);

                if (cell->columns != 1)
                    continue;

                if (!layout_minmax_block(cell, font_func, content))
                    return false;
                i = cell->start_column;

                if (col[i].positioned)
                    continue;

                /* update column min, max widths using cell
                 * widths */
                if (col[i].min < cell->min_width.value)
                    col[i].min = cell->min_width.value;
                if (col[i].max < cell->max_width)
                    col[i].max = cell->max_width;
            }

    /* 2nd pass: cells which span multiple columns */
    for (row_group = table->children; row_group; row_group = row_group->next)
        for (row = row_group->children; row; row = row->next)
            for (cell = row->children; cell; cell = cell->next) {
                unsigned int flexible_columns = 0;
                int min = 0, max = 0, fixed_width = 0, extra;

                if (cell->columns == 1)
                    continue;

                if (!layout_minmax_block(cell, font_func, content))
                    return false;
                i = cell->start_column;

                /* find min width so far of spanned columns, and
                 * count number of non-fixed spanned columns and
                 * total fixed width */
                for (j = 0; j != cell->columns; j++) {
                    min += col[i + j].min;
                    if (col[i + j].type == COLUMN_WIDTH_FIXED)
                        fixed_width += col[i + j].width;
                    else
                        flexible_columns++;
                }
                min += (cell->columns - 1) * border_spacing_h;

                /* distribute extra min to spanned columns */
                if (min < cell->min_width.value) {
                    if (flexible_columns == 0) {
                        extra = 1 + (cell->min_width.value - min) / cell->columns;
                        for (j = 0; j != cell->columns; j++) {
                            col[i + j].min += extra;
                            if (col[i + j].max < col[i + j].min)
                                col[i + j].max = col[i + j].min;
                        }
                    } else {
                        extra = 1 + (cell->min_width.value - min) / flexible_columns;
                        for (j = 0; j != cell->columns; j++) {
                            if (col[i + j].type != COLUMN_WIDTH_FIXED) {
                                col[i + j].min += extra;
                                if (col[i + j].max < col[i + j].min)
                                    col[i + j].max = col[i + j].min;
                            }
                        }
                    }
                }

                /* find max width so far of spanned columns */
                for (j = 0; j != cell->columns; j++)
                    max += col[i + j].max;
                max += (cell->columns - 1) * border_spacing_h;

                /* distribute extra max to spanned columns */
                if (max < cell->max_width && flexible_columns) {
                    extra = 1 + (cell->max_width - max) / flexible_columns;
                    for (j = 0; j != cell->columns; j++)
                        if (col[i + j].type != COLUMN_WIDTH_FIXED)
                            col[i + j].max += extra;
                }
            }

    for (i = 0; i != table->columns; i++) {
        if (col[i].max < col[i].min) {
            box_dump(stderr, table, 0, true);
            assert(0);
        }
        table_min += col[i].min;
        table_max += col[i].max;
    }

    /* Spacing belongs inside the table's content width. A specified width
     * includes it; only intrinsic cell widths need it added separately. */
    table_min += (table->columns + 1) * border_spacing_h;
    table_max += (table->columns + 1) * border_spacing_h;

    /* fixed width takes priority, unless it is too narrow */
    if (css_computed_width_px(table->style, &content->unit_len_ctx, -1, &width) == CSS_WIDTH_SET) {
        layout_handle_box_sizing(&content->unit_len_ctx, table, 0, true, &width);
        if (table_min < width)
            table_min = width;
        if (table_max < width)
            table_max = width;
    }

    /* add margins, border, padding to min, max widths */
    layout_used_mbp_width(&content->unit_len_ctx, table, LEFT, true, &extra_fixed, &extra_frac);
    layout_used_mbp_width(&content->unit_len_ctx, table, RIGHT, true, &extra_fixed, &extra_frac);
    if (extra_fixed < 0)
        extra_fixed = 0;
    if (extra_frac < 0)
        extra_frac = 0;
    if (1.0 <= extra_frac)
        extra_frac = 0.9;
    table->min_width.value = (table_min + extra_fixed) / (1.0 - extra_frac);
    table->max_width = (table_max + extra_fixed) / (1.0 - extra_frac);

    assert(0 <= table->min_width.value && table->min_width.value <= table->max_width);
    return true;
}

/**
 * Helper to check if a box has percentage max width.
 *
 * \param[in]  b  Box to check.
 * \return true iff box has percnetage max width.
 */
static inline bool box_has_percentage_max_width(struct box *b)
{
    css_unit unit = CSS_UNIT_PX;
    enum css_max_width_e type;
    css_fixed_or_calc value = (css_fixed_or_calc)0;

    assert(b != NULL);

    type = css_computed_max_width(b->style, &value, &unit);
    return ((type == CSS_MAX_WIDTH_SET) && (unit == CSS_UNIT_PCT));
}

/* Intrinsic image widths can depend on a definite percentage height. The
 * minmax pass precedes layout, so use CSS sizes, never a previous box->height.
 * A nowrap row's stretched height is definite already (Flexbox 9.8). Stop at
 * content-dependent heights; they must not feed back into intrinsic widths. */
static int layout_minmax_definite_height(const struct box *box, const css_unit_ctx *unit_ctx)
{
    if (box == NULL || box->style == NULL)
        return AUTO;

    const struct box *parent = box->parent;
    if (parent && parent->type == BOX_INLINE_CONTAINER)
        parent = parent->parent;

    css_fixed_or_calc value = (css_fixed_or_calc)0;
    css_unit unit = CSS_UNIT_PX;
    uint8_t type = css_computed_height(box->style, &value, &unit);
    bool stretch = type == CSS_HEIGHT_AUTO && lh__box_is_flex_item(box) && lh__flex_main_is_horizontal(parent) &&
        css_computed_flex_wrap(parent->style) == CSS_FLEX_WRAP_NOWRAP &&
        lh__box_align_self(parent, box) == CSS_ALIGN_SELF_STRETCH;
    if (type != CSS_HEIGHT_SET && !stretch)
        return AUTO;

    int available = layout_minmax_definite_height(parent, unit_ctx);
    int height = AUTO;
    int edges = 0;
    float fraction = 0;
    if (stretch || css_computed_box_sizing(box->style) == CSS_BOX_SIZING_BORDER_BOX) {
        for (unsigned int side = TOP; side <= BOTTOM; side += 2) {
            if (stretch && margin_funcs[side](box->style, &value, &unit) == CSS_MARGIN_AUTO)
                return AUTO;
            calculate_mbp_width(unit_ctx, box->style, side, stretch, true, true, &edges, &fraction);
        }
        /* Percentage edges need an inline size that this pass does not know. */
        if (fraction != 0)
            return AUTO;
    }

    if (stretch) {
        if (available == AUTO)
            return AUTO;
        height = max(0, available - edges);
    } else {
        css_computed_height(box->style, &value, &unit);
        if (!lh__length_to_px(box->style, unit_ctx, available, value, unit, &height))
            return AUTO;
        height = max(0, height - edges);
    }

    /* Stretch limits are content-box sizes; margins are not part of them. */
    edges = 0;
    fraction = 0;
    if (css_computed_box_sizing(box->style) == CSS_BOX_SIZING_BORDER_BOX) {
        calculate_mbp_width(unit_ctx, box->style, TOP, false, true, true, &edges, &fraction);
        calculate_mbp_width(unit_ctx, box->style, BOTTOM, false, true, true, &edges, &fraction);
    }
    int limit;
    if (css_computed_max_height(box->style, &value, &unit) == CSS_MAX_HEIGHT_SET &&
        lh__length_to_px(box->style, unit_ctx, available, value, unit, &limit))
        height = min(height, max(0, limit - edges));
    if (css_computed_min_height(box->style, &value, &unit) == CSS_MIN_HEIGHT_SET &&
        lh__length_to_px(box->style, unit_ctx, available, value, unit, &limit))
        height = max(height, max(0, limit - edges));
    return height;
}

/**
 * Compute the constrained intrinsic width of a replaced element (image, etc.)
 * for the min/max width pass.
 *
 * Reads CSS min/max width/height constraints from the element's style and
 * calls layout_get_object_dimensions() so that e.g. max-height on an image
 * correctly limits the intrinsic width contribution via aspect-ratio scaling.
 * Percentage heights use a definite ancestor height where one is available;
 * percentage width constraints still have no containing width in this pass.
 *
 * \param box      The box with an object (box->object must be non-NULL).
 * \param content  The HTML content being laid out.
 * \param css_width   CSS-computed width for the element (AUTO or set).
 * \param css_height  CSS-computed height for the element (AUTO or set).
 * \return  The constrained intrinsic width in device pixels.
 */
static int layout_minmax_object_width(struct box *box, const html_content *content, int css_width, int css_height)
{
    int width = css_width;
    int height = css_height;
    struct css_size obj_min_w = {.type = CSS_SIZE_AUTO, .value = 0};
    struct css_size obj_min_h = {.type = CSS_SIZE_AUTO, .value = 0};
    int obj_max_w = INT_MAX;
    int obj_max_h = INT_MAX;

    assert(box->object != NULL);

    if (width == AUTO) {
        int definite_height = layout_minmax_definite_height(box, &content->unit_len_ctx);
        if (definite_height != AUTO)
            height = definite_height;
    }

    if (box->style) {
        css_fixed_or_calc v = (css_fixed_or_calc)0;
        css_unit u = CSS_UNIT_PX;

        if (css_computed_max_height(box->style, &v, &u) == CSS_MAX_HEIGHT_SET && u != CSS_UNIT_PCT) {
            if (!lh__length_to_px(box->style, &content->unit_len_ctx, -1, v, u, &obj_max_h))
                obj_max_h = INT_MAX;
        }
        if (css_computed_max_width(box->style, &v, &u) == CSS_MAX_WIDTH_SET && u != CSS_UNIT_PCT &&
            !lh__intrinsic_width_unit(u)) {
            if (!lh__length_to_px(box->style, &content->unit_len_ctx, -1, v, u, &obj_max_w))
                obj_max_w = INT_MAX;
        }
        if (css_computed_min_height(box->style, &v, &u) == CSS_MIN_HEIGHT_SET && u != CSS_UNIT_PCT) {
            obj_min_h.type = CSS_SIZE_SET;
            if (!lh__length_to_px(box->style, &content->unit_len_ctx, -1, v, u, &obj_min_h.value)) {
                obj_min_h.type = CSS_SIZE_AUTO;
                obj_min_h.value = 0;
            }
        }
        if (css_computed_min_width(box->style, &v, &u) == CSS_MIN_WIDTH_SET && u != CSS_UNIT_PCT &&
            !lh__intrinsic_width_unit(u)) {
            obj_min_w.type = CSS_SIZE_SET;
            if (!lh__length_to_px(box->style, &content->unit_len_ctx, -1, v, u, &obj_min_w.value)) {
                obj_min_w.type = CSS_SIZE_AUTO;
                obj_min_w.value = 0;
            }
        }
    }

    layout_get_object_dimensions(box, &width, &height,
        &(struct layout_size_limits){
            .min_width = obj_min_w,
            .max_width = obj_max_w,
            .min_height = obj_min_h,
            .max_height = obj_max_h,
        },
        -1 /* containing block width unknown during minmax pass */);

    return width;
}

/* Empty inline markers do not interrupt a run of collapsible spaces. */
static bool layout_inline_has_content(const struct box *box)
{
    return (box->text && box->length != 0) || lh__box_is_replace(box) || lh__box_is_atomic_inline(box);
}

/**
 * Resolve a collapsed space after any in-flow inline box. Construction can
 * attach it to an opening inline or an atomic inline, not just text/end boxes.
 * Suppress leading and duplicate spaces across empty markers (CSS 2.1 16.6.1).
 */
static int layout_inline_space(struct box *box, bool *has_content, bool *after_space,
    const struct gui_layout_table *font_func, const html_content *content)
{
    if (layout_inline_has_content(box)) {
        *has_content = true;
        *after_space = false;
    }

    if (box->space == 0 || !*has_content || *after_space)
        return 0;

    *after_space = true;
    if (box->space == UNKNOWN_WIDTH) {
        plot_font_style_t style;
        font_plot_style_from_css(content, box->space_style ? box->space_style : box->style, &style);
        font_func->width(&style, " ", 1, &box->space);
    }
    return box->space;
}

/* Find the first space in the trailing run of empty inline markers. A space
 * after the final box is still pending, so it has no width to remove. Walking
 * just this suffix keeps trimming linear across all lines of a container. */
static struct box *layout_trailing_space(struct box *first, struct box *end)
{
    struct box *space = NULL;
    bool following_inline = false;

    if (end == first)
        return NULL;

    for (struct box *b = end ? end->prev : first->parent->last; b; b = b->prev) {
        if (lh__box_is_inline_flow(b) && !lh__box_is_inline_positioned(b)) {
            /* Ignored duplicates may still have an unmeasured width. */
            if (following_inline && b->space != 0 && b->space != UNKNOWN_WIDTH)
                space = b;
            if (layout_inline_has_content(b))
                return space;
            following_inline = true;
        }
        if (b == first)
            break;
    }
    return NULL;
}

/* Inline box boundaries are transparent to wrapping (CSS Text 3, 5.1).
 * Keep the existing whitespace/atomic-inline opportunities, but do not let an
 * opening/closing decoration become a break merely because it is a box. */
struct inline_wrap_state {
    bool content;
    bool space;
    bool atomic;
    const struct box *empty_end;
    bool empty_space, empty_atomic;
};

static bool layout_inline_wraps(const struct box *box)
{
    enum css_white_space_e ws = css_computed_white_space(box->style);
    return ws != CSS_WHITE_SPACE_NOWRAP && ws != CSS_WHITE_SPACE_PRE && !box_is_inside_marker(box);
}

static bool layout_inline_atomic(const struct box *box)
{
    return lh__box_is_replace(box) || lh__box_is_atomic_inline(box);
}

/* Closing edges stay with the preceding content. A pending space or atomic
 * boundary is consumed before the next opening edge or content box. */
static bool layout_inline_break_before(struct inline_wrap_state *state, const struct box *box)
{
    if (!lh__box_is_inline_flow(box) || lh__box_is_inline_positioned(box))
        return false;

    /* Generated inside markers have an unbreakable internal string, but the
     * list item's whitespace mode governs wrapping between marker and label. */
    bool marker = box_is_inside_marker(box);
    bool atomic = layout_inline_atomic(box) || marker;
    bool wrap = layout_inline_wraps(marker ? box->parent->parent : box);
    if (box == state->empty_end) {
        /* A space can break after intervening empty spans as well as before
         * them. Real content cancels this pending opportunity below. */
        state->space |= state->empty_space;
        state->atomic |= state->empty_atomic;
        state->empty_end = NULL;
    }
    bool boundary = box->type != BOX_INLINE_END && state->content &&
        (state->space || (wrap && (state->atomic || atomic)));
    /* Empty elements may use a single BOX_INLINE rather than paired markers. */
    bool empty_inline = box->type == BOX_INLINE && !box->inline_end && !layout_inline_has_content(box);
    if (boundary && !empty_inline) {
        if (box->type == BOX_INLINE && box->inline_end && !atomic && !state->empty_end) {
            state->empty_end = box->inline_end;
            state->empty_space = state->space;
            state->empty_atomic = state->atomic;
        }
        state->space = state->atomic = false;
    }
    if (layout_inline_has_content(box)) {
        state->empty_end = NULL;
        state->content = true;
        state->space = false;
        state->atomic = atomic && wrap;
    }
    /* A folded space follows its source's white-space policy as well as its
     * font; an adjacent atomic inline may override either inherited value. */
    if (state->content && box->space != 0) {
        uint8_t ws = css_computed_white_space(box->space_style ? box->space_style : box->style);
        state->space |= marker ? wrap : ws != CSS_WHITE_SPACE_NOWRAP && ws != CSS_WHITE_SPACE_PRE;
    }
    return boundary;
}

/**
 * Calculate minimum and maximum width of a line.
 *
 * \param first       a box in an inline container
 * \param line_min    updated to minimum width of line starting at first
 * \param line_max    updated to maximum width of line starting at first
 * \param first_line  true iff this is the first line in the inline container
 * \param line_has_height  updated to true or false, depending on line
 * \param font_func Font functions.
 * \param next receives the first box in the next line, or NULL at the end
 * \return true on success, false on memory exhaustion
 * \post  0 <= *line_min <= *line_max
 */
static bool layout_minmax_line(struct box *first, int *line_min, int *line_max, bool first_line,
    bool *line_has_height, const struct gui_layout_table *font_func, const html_content *content, struct box **next)
{
    int min = 0, max = 0, width, height, fixed;
    float frac;
    size_t i, j;
    struct box *b;
    struct box *block;
    plot_font_style_t fstyle;
    bool no_wrap;
    bool has_content = false, after_space = false;
    int space_after = 0;
    int min_run = 0, trailing_min_space = 0;
    struct inline_wrap_state wrap_state = {0};

    assert(first->parent);
    assert(first->parent->parent);
    assert(first->parent->parent->style);

    block = first->parent->parent;
    no_wrap = (css_computed_white_space(block->style) == CSS_WHITE_SPACE_NOWRAP ||
        css_computed_white_space(block->style) == CSS_WHITE_SPACE_PRE);

    *line_has_height = false;

    /* corresponds to the pass 1 loop in layout_line() */
    for (b = first; b; b = b->next) {
        enum css_width_e wtype;
        enum css_height_e htype;
        enum css_box_sizing_e bs;
        css_fixed_or_calc value = (css_fixed_or_calc)0;
        css_unit unit = CSS_UNIT_PX;

        assert(lh__box_is_inline_content(b));

        NSLOG(layout, DEBUG, "%p: min %i, max %i", b, min, max);

        if (b->type == BOX_BR) {
            /* A forced break creates a line even without text. */
            *line_has_height = true;
            b = b->next;
            break;
        }

        if (lh__box_is_float_box(b)) {
            assert(b->children);
            if (b->children->type == BOX_TABLE) {
                if (!layout_minmax_table(b->children, font_func, content))
                    return false;
            } else {
                if (!layout_minmax_block(b->children, font_func, content))
                    return false;
            }
            b->min_width.value = b->children->min_width.value;
            b->max_width = b->children->max_width;
            if (min < b->min_width.value)
                min = b->min_width.value;
            max += b->max_width;
            continue;
        }

        if (lh__box_is_inline_positioned(b)) {
            /* Compute the positioned box's own intrinsic size, without
             * contributing to its parent's line width or height. */
            if (!layout_minmax_box(b, font_func, content))
                return false;
            continue;
        }

        /* Min-content takes every legal break. Inline fragments and adjacent
         * text nodes remain in the same run unless line layout can break there.
         * A collapsed space may precede closing edges; trim it at the eventual
         * break without dropping those edges from the run. */
        if (layout_inline_break_before(&wrap_state, b)) {
            min = max(min, min_run - trailing_min_space);
            min_run = trailing_min_space = 0;
        } else {
            min_run += space_after;
            trailing_min_space += space_after;
        }
        if (layout_inline_has_content(b))
            trailing_min_space = 0;
        max += space_after;
        space_after = layout_inline_space(b, &has_content, &after_space, font_func, content);

        if (lh__box_is_atomic_inline(b)) {
            if (!layout_minmax_box(b, font_func, content))
                return false;
            min_run += b->min_width.value;
            /* Log if adding a huge max_width that will cause overflow */
            if (b->max_width > 1000000000) {
                NSLOG(layout, WARNING,
                    "OVERFLOW_TRACE: line accumulator adding inline %p type=%d max_width=%d (current max=%d)", b,
                    b->type, b->max_width, max);
            }
            max += b->max_width;

            if ((b->flags & HAS_HEIGHT) || b->type == BOX_TABLE)
                *line_has_height = true;
            continue;
        }

        assert(b->style);
        font_plot_style_from_css(content, b->style, &fstyle);

        if (b->type == BOX_INLINE && !b->object && !(b->flags & REPLACE_DIM) && !(b->flags & IFRAME)) {
            fixed = frac = 0;
            calculate_mbp_width(&content->unit_len_ctx, b->style, LEFT, true, true, true, &fixed, &frac);
            if (!b->inline_end)
                calculate_mbp_width(&content->unit_len_ctx, b->style, RIGHT, true, true, true, &fixed, &frac);
            if (0 < fixed) {
                max += fixed;
                min_run += fixed;
            }
            *line_has_height = true;
        } else if (b->type == BOX_INLINE_END) {
            fixed = frac = 0;
            calculate_mbp_width(&content->unit_len_ctx, b->inline_end->style, RIGHT, true, true, true, &fixed, &frac);
            if (0 < fixed) {
                max += fixed;
                min_run += fixed;
            }

            *line_has_height = true;
            continue;
        }

        if (lh__box_is_replace(b) == false) {
            /* inline non-replaced, 10.3.1 and 10.6.1 */
            if (!b->text)
                continue;

            if (b->width == UNKNOWN_WIDTH) {
                /** \todo handle errors */

                /* If it's a select element, we must use the
                 * width of the widest option text */
                if (b->parent->parent->gadget && b->parent->parent->gadget->type == GADGET_SELECT) {
                    int opt_maxwidth = 0;
                    struct form_option *o;

                    for (o = b->parent->parent->gadget->data.select.items; o; o = o->next) {
                        int opt_width;
                        font_func->width(&fstyle, o->text, strlen(o->text), &opt_width);

                        if (opt_maxwidth < opt_width)
                            opt_maxwidth = opt_width;
                    }

                    b->width = opt_maxwidth;
                    if (nsoption_bool(core_select_menu))
                        b->width += SCROLLBAR_WIDTH;

                } else {
                    font_func->width(&fstyle, b->text, b->length, &b->width);
                    b->flags |= MEASURED;
                }
            }
            max += b->width;
            if (no_wrap || !layout_inline_wraps(b)) {
                min_run += b->width;
            } else if (b->parent->flags & NEED_MIN) {
                /* Only measure words when an ancestor needs min-content.
                 * The first/last word can join text or edges in adjacent boxes.
                 * Reuse the existing width for a single-word text box. */
                i = 0;
                do {
                    for (j = i; j != b->length && b->text[j] != ' '; j++)
                        ;
                    if (i == 0 && j == b->length && (b->flags & MEASURED))
                        width = b->width;
                    else
                        font_func->width(&fstyle, b->text + i, j - i, &width);
                    min_run += width;
                    if (j != b->length) {
                        min = max(min, min_run);
                        min_run = 0;
                    }
                    i = j + 1;
                } while (j != b->length);
            }

            *line_has_height = true;

            continue;
        }

        /* inline replaced, 10.3.2 and 10.6.2 */
        assert(b->style);

        bs = css_computed_box_sizing(b->style);

        /* calculate box width */
        wtype = css_computed_width_px(b->style, &content->unit_len_ctx, -1, &width);
        if (wtype == CSS_WIDTH_SET) {
            if (bs == CSS_BOX_SIZING_BORDER_BOX) {
                fixed = frac = 0;
                calculate_mbp_width(&content->unit_len_ctx, b->style, LEFT, false, true, true, &fixed, &frac);
                calculate_mbp_width(&content->unit_len_ctx, b->style, RIGHT, false, true, true, &fixed, &frac);
                if (width < fixed) {
                    width = fixed;
                }
            }
            if (width < 0)
                width = 0;
        } else {
            width = AUTO;
        }

        /* height */
        htype = css_computed_height(b->style, &value, &unit);
        if (htype == CSS_HEIGHT_SET) {
            if (!lh__length_to_px(b->style, &content->unit_len_ctx, -1, value, unit, &height)) {
                height = AUTO;
            }
        } else {
            height = AUTO;
        }

        if (b->object || (b->flags & REPLACE_DIM)) {
            if (b->object) {
                width = layout_minmax_object_width(b, content, width, height);
            }

            fixed = frac = 0;
            if (bs == CSS_BOX_SIZING_BORDER_BOX) {
                calculate_mbp_width(&content->unit_len_ctx, b->style, LEFT, true, false, false, &fixed, &frac);
                calculate_mbp_width(&content->unit_len_ctx, b->style, RIGHT, true, false, false, &fixed, &frac);
            } else {
                calculate_mbp_width(&content->unit_len_ctx, b->style, LEFT, true, true, true, &fixed, &frac);
                calculate_mbp_width(&content->unit_len_ctx, b->style, RIGHT, true, true, true, &fixed, &frac);
            }
            if (0 < width + fixed)
                width += fixed;
        } else if (b->flags & IFRAME) {
            /* TODO: handle percentage widths properly */
            if (width == AUTO)
                width = 400;

            fixed = frac = 0;
            if (bs == CSS_BOX_SIZING_BORDER_BOX) {
                calculate_mbp_width(&content->unit_len_ctx, b->style, LEFT, true, false, false, &fixed, &frac);
                calculate_mbp_width(&content->unit_len_ctx, b->style, RIGHT, true, false, false, &fixed, &frac);
            } else {
                calculate_mbp_width(&content->unit_len_ctx, b->style, LEFT, true, true, true, &fixed, &frac);
                calculate_mbp_width(&content->unit_len_ctx, b->style, RIGHT, true, true, true, &fixed, &frac);
            }

            if (0 < width + fixed)
                width += fixed;

        } else {
            /* form control with no object */
            if (width == AUTO)
                width = FIXTOINT(css_unit_len2device_px(b->style, &content->unit_len_ctx, INTTOFIX(1), CSS_UNIT_EM));
        }

        if (width > 0 && !box_has_percentage_max_width(b))
            min_run += width;
        if (width > 0)
            max += width;

        *line_has_height = true;
    }

    min = max(min, min_run - trailing_min_space);
    struct box *trailing_space = layout_trailing_space(first, b);
    if (trailing_space) {
        max -= trailing_space->space;
        /* This is a paragraph/BR boundary, not a soft wrap. These spaces
         * can never reappear on reflow; discard them so an exact-fit line
         * cannot push its closing inline markers onto an empty next line. */
        for (struct box *d = trailing_space; d != b; d = d->next) {
            d->space = 0;
            d->space_style = NULL;
        }
    }
    if (no_wrap)
        min = max;

    if (first_line) {
        /* todo: handle percentage values properly */
        /* todo: handle text-indent interaction with floats */
        int text_indent = layout_text_indent(&content->unit_len_ctx, first->parent->parent->style, 100);
        min = (min + text_indent < 0) ? 0 : min + text_indent;
        max = (max + text_indent < 0) ? 0 : max + text_indent;
    }

    *line_min = min;
    *line_max = max;

    NSLOG(layout, DEBUG, "line_min %i, line_max %i", min, max);

    assert(b != first);
    assert(0 <= *line_min);
    assert(*line_min <= *line_max);
    *next = b;
    return true;
}


static bool layout_can_reuse_inline_intrinsics(const struct box *box);
static void layout_discard_inline_layout(struct box *box);

/**
 * Calculate minimum and maximum width of an inline container.
 *
 * \param inline_container  box of type INLINE_CONTAINER
 * \param[out] has_height set to true if container has height
 * \param font_func Font functions.
 * \post  inline_container->min_width.value and inline_container->max_width filled in,
 *        0 <= inline_container->min_width.value <= inline_container->max_width
 */
static bool layout_minmax_inline_container(struct box *inline_container, bool *has_height,
    const struct gui_layout_table *font_func, const html_content *content)
{
    struct box *child;
    int line_min = 0, line_max = 0;
    int min = 0, max = 0;
    bool first_line = true;
    bool line_has_height;

    assert(inline_container->type == BOX_INLINE_CONTAINER);

    if (inline_container->flags & REUSE_INLINE_INTRINSICS) {
        inline_container->flags &= ~REUSE_INLINE_INTRINSICS;
        /* NEED_MIN comes from the freshly measured ancestors. A cache measured
         * with another requirement must not change their intrinsic contribution. */
        if (!!(inline_container->flags & NEED_MIN) == !!(inline_container->flags & INLINE_INTRINSIC_NEED_MIN)) {
            if (has_height != NULL)
                *has_height = (inline_container->flags & INLINE_INTRINSIC_HEIGHT) != 0;
            HTML_PROFILE(if (content->restyling)
                ((html_content *)content)->restyle_profile.intrinsic_inline_reuses++);
            return true;
        }
        inline_container->max_width = UNKNOWN_MAX_WIDTH;
        if (inline_container->flags & REUSE_INLINE_LAYOUT) {
            /* Intrinsic measurement must see the original text runs, not
             * fragments whose breaks came from the previous used width. */
            layout_discard_inline_layout(inline_container);
            HTML_PROFILE(if (content->restyling)
                ((html_content *)content)->restyle_profile.inline_layout_intrinsic_misses++);
        }
    }
    /* check if the widths have already been calculated */
    if (inline_container->max_width != UNKNOWN_MAX_WIDTH)
        return true;

    HTML_PROFILE(if (content->restyling)
        ((html_content *)content)->restyle_profile.intrinsic_inline_calculations++);
    bool height = false;

    for (child = inline_container->children; child;) {
        if (!layout_minmax_line(child, &line_min, &line_max, first_line, &line_has_height, font_func, content, &child))
            return false;
        if (min < line_min)
            min = line_min;
        if (max < line_max)
            max = line_max;
        first_line = false;
        height |= line_has_height;
    }

    if (has_height != NULL)
        *has_height = height;
    inline_container->min_width.value = min;
    inline_container->max_width = max;
    inline_container->flags &= ~(INLINE_INTRINSIC_HEIGHT | INLINE_INTRINSIC_NEED_MIN);
    if (height)
        inline_container->flags |= INLINE_INTRINSIC_HEIGHT;
    if (inline_container->flags & NEED_MIN)
        inline_container->flags |= INLINE_INTRINSIC_NEED_MIN;

    assert(0 <= inline_container->min_width.value && inline_container->min_width.value <= inline_container->max_width);
    return true;
}

/* Apply preferred/min/max inline sizes to content-based contributions. Keep
 * both bounds in content-box coordinates, and let the minimum win conflicts.
 * Flex items use the preferred width as a floor (Flexbox 9.9.3). */
static void layout_minmax_constraints(
    const struct box *box, const css_unit_ctx *unit_ctx, int border_padding, int *minimum, int *maximum)
{
    bool flex_item = lh__box_is_flex_item(box);
    int inset = css_computed_box_sizing(box->style) == CSS_BOX_SIZING_BORDER_BOX ? border_padding : 0;
    css_fixed_or_calc value = (css_fixed_or_calc)0;
    css_unit unit = CSS_UNIT_PX;
    int pixels;

    uint8_t type = css_computed_width(box->style, &value, &unit);
    if (type == CSS_WIDTH_SET && unit == CSS_UNIT_PCT) {
        if (!flex_item)
            *minimum = 0;
    } else if (css_computed_width_px(box->style, unit_ctx, -1, &pixels) == CSS_WIDTH_SET) {
        pixels = max(0, pixels - inset);
        if (flex_item) {
            *minimum = max(*minimum, pixels);
            *maximum = max(*maximum, pixels);
        } else {
            *minimum = *maximum = pixels;
        }
    }

    type = css_computed_max_width(box->style, &value, &unit);
    if (type == CSS_MAX_WIDTH_SET) {
        if (unit == CSS_UNIT_PCT) {
            if (!flex_item)
                *minimum = 0;
        } else if (!lh__intrinsic_width_unit(unit) &&
            lh__length_to_px(box->style, unit_ctx, -1, value, unit, &pixels)) {
            pixels = max(0, pixels - inset);
            *minimum = min(*minimum, pixels);
            *maximum = min(*maximum, pixels);
        }
    }

    type = css_computed_min_width(box->style, &value, &unit);
    if (type == CSS_MIN_WIDTH_SET && unit != CSS_UNIT_PCT && !lh__intrinsic_width_unit(unit) &&
        lh__length_to_px(box->style, unit_ctx, -1, value, unit, &pixels)) {
        pixels = max(0, pixels - inset);
        *minimum = max(*minimum, pixels);
        *maximum = max(*maximum, pixels);
    }
}

/**
 * Calculate minimum and maximum width of a block.
 *
 * \param block  box of type BLOCK, INLINE_BLOCK, or TABLE_CELL
 * \param font_func font functions
 * \param content The HTML content being layed out.
 * \post  block->min_width.value and block->max_width filled in,
 *        0 <= block->min_width.value <= block->max_width
 */
static bool
layout_minmax_block(struct box *block, const struct gui_layout_table *font_func, const html_content *content)
{
    struct box *child;
    int min = 0, max = 0;
    int extra_fixed = 0;
    float extra_frac = 0;
    enum css_width_e wtype = CSS_WIDTH_AUTO;
    css_fixed_or_calc width = (css_fixed_or_calc)0;
    css_unit wunit = CSS_UNIT_PX;
    enum css_height_e htype = CSS_HEIGHT_AUTO;
    css_fixed_or_calc height = (css_fixed_or_calc)0;
    css_unit hunit = CSS_UNIT_PX;
    bool child_has_height = false;
    int flex_item_count = 0; /* Count of flex items for gap calculation */
    int flex_gap = 0; /* Gap between flex items in pixels */

    assert(block->type == BOX_BLOCK || block->type == BOX_FLEX || block->type == BOX_GRID ||
        block->type == BOX_INLINE_FLEX || block->type == BOX_INLINE_GRID || block->type == BOX_INLINE_BLOCK ||
        block->type == BOX_TABLE_CELL);

    /* check if the widths have already been calculated */
    if (block->max_width != UNKNOWN_MAX_WIDTH)
        return true;

    if (block->style != NULL) {
        wtype = css_computed_width(block->style, &width, &wunit);
        htype = css_computed_height(block->style, &height, &hunit);
    }

    /* set whether the minimum width is of any interest for this box */
    if (((block->parent && lh__box_is_float_box(block->parent)) || block->type == BOX_INLINE_BLOCK ||
            block->type == BOX_INLINE_FLEX || block->type == BOX_INLINE_GRID) &&
        wtype != CSS_WIDTH_SET) {
        /* box shrinks to fit; need minimum width */
        block->flags |= NEED_MIN;
    } else if (block->type == BOX_TABLE_CELL) {
        /* box shrinks to fit; need minimum width */
        block->flags |= NEED_MIN;
    } else if (block->parent && (block->parent->flags & NEED_MIN) && wtype != CSS_WIDTH_SET) {
        /* box inside shrink-to-fit context; need minimum width */
        block->flags |= NEED_MIN;
    } else if (block->parent &&
        (lh__box_is_flex_container(block->parent) || block->parent->type == BOX_GRID ||
            block->parent->type == BOX_INLINE_GRID)) {
        /* Flex/grid items need their content minimum even with a set width. */
        block->flags |= NEED_MIN;
    }

    if (block->gadget &&
        (block->gadget->type == GADGET_TEXTBOX || block->gadget->type == GADGET_PASSWORD ||
            block->gadget->type == GADGET_FILE || block->gadget->type == GADGET_TEXTAREA) &&
        block->style) {
        /* For text inputs, use intrinsic width (10em ≈ 20 chars) for minmax:
         * - Always when width is AUTO
         * - Also when the element is a flex item, since the intrinsic size
         *   should be used for the parent's shrink-to-fit calculation.
         *   This matches Chrome/Firefox behavior where inputs have an intrinsic
         *   size based on their type, even with explicit width like "width: 1%". */
        bool use_intrinsic = (wtype == CSS_WIDTH_AUTO);
        if (!use_intrinsic && block->parent &&
            (block->parent->type == BOX_FLEX || block->parent->type == BOX_INLINE_FLEX)) {
            use_intrinsic = true;
        }
        if (use_intrinsic) {
            css_fixed size = INTTOFIX(10);
            css_unit unit = CSS_UNIT_EM;

            min = max = FIXTOINT(css_unit_len2device_px(block->style, &content->unit_len_ctx, size, unit));

            block->flags |= HAS_HEIGHT;
        }
    }

    if (block->gadget && (block->gadget->type == GADGET_RADIO || block->gadget->type == GADGET_CHECKBOX) &&
        block->style && wtype == CSS_WIDTH_AUTO) {
        css_fixed size = INTTOFIX(1);
        css_unit unit = CSS_UNIT_EM;

        /* form checkbox or radio button
         * if width is AUTO, set it to 1em */
        min = max = FIXTOINT(css_unit_len2device_px(block->style, &content->unit_len_ctx, size, unit));

        block->flags |= HAS_HEIGHT;
    }

    if (block->object) {
        /* Use constrained intrinsic width, respecting
         * max-height/max-width via aspect-ratio scaling */
        int obj_w = AUTO, obj_h = AUTO;

        if (wtype == CSS_WIDTH_SET && wunit != CSS_UNIT_PCT) {
            if (wunit == CSS_UNIT_CALC) {
                int px = 0;
                if (lh__length_to_px(block->style, &content->unit_len_ctx, -1, width, wunit, &px)) {
                    obj_w = px;
                }
            } else {
                obj_w = FIXTOINT(css_unit_len2device_px(block->style, &content->unit_len_ctx, width.value, wunit));
            }
        }
        if (htype == CSS_HEIGHT_SET && hunit != CSS_UNIT_PCT) {
            if (hunit == CSS_UNIT_CALC) {
                int px = 0;
                if (lh__length_to_px(block->style, &content->unit_len_ctx, -1, height, hunit, &px)) {
                    obj_h = px;
                }
            } else {
                obj_h = FIXTOINT(css_unit_len2device_px(block->style, &content->unit_len_ctx, height.value, hunit));
            }
        }

        min = max = layout_minmax_object_width(block, content, obj_w, obj_h);
        NSLOG(layout, DEBUG, "MINMAX_BLOCK object %p: after helper min=%d max=%d", block, min, max);

        block->flags |= HAS_HEIGHT;
    } else if (block->flags & IFRAME) {
        /** \todo do we need to know the min/max width of the iframe's
         * content? */
        block->flags |= HAS_HEIGHT;
    } else if (block->type == BOX_GRID || block->type == BOX_INLINE_GRID) {
        /* Tracks determine content sizes; the common block path below applies
         * the container's constraints and outer edges exactly once. */
        if (!layout_minmax_grid_content(block, font_func, content, &min, &max))
            return false;
        block->flags |= HAS_HEIGHT;
    } else {
        /* For horizontal flex containers, get the column-gap for intrinsic sizing.
         * Per CSS spec, gaps contribute to the intrinsic main size. */
        if (lh__box_is_flex_container(block) && lh__flex_main_is_horizontal(block) && block->style != NULL) {
            css_fixed_or_calc gap_len = (css_fixed_or_calc)0;
            css_unit gap_unit = CSS_UNIT_PX;
            if (css_computed_column_gap(block->style, &gap_len, &gap_unit) == CSS_COLUMN_GAP_SET) {
                if (!lh__length_to_px(block->style, &content->unit_len_ctx, -1, gap_len, gap_unit, &flex_gap)) {
                    flex_gap = 0;
                }
            }
        }

        /* recurse through children */
        for (child = block->children; child; child = child->next) {
            switch (child->type) {
            case BOX_FLEX:
            case BOX_GRID:
            case BOX_BLOCK:
                if (!layout_minmax_block(child, font_func, content))
                    return false;
                if (child->flags & HAS_HEIGHT)
                    child_has_height = true;
                break;
            case BOX_INLINE_CONTAINER:
                if (block->flags & NEED_MIN)
                    child->flags |= NEED_MIN;

                if (!layout_minmax_inline_container(child, &child_has_height, font_func, content))
                    return false;
                /* Intrinsic height is only a conservative hint for empty
                 * inline fragments. Margin lookahead checks whether their
                 * edges create a line; content does not set MAKE_HEIGHT. */
                break;
            case BOX_TABLE:
                if (!layout_minmax_table(child, font_func, content))
                    return false;
                /* todo: fix for zero height tables */
                child_has_height = true;
                child->flags |= MAKE_HEIGHT;
                break;
            default:
                /* Inline-level boxes (INLINE_BLOCK, INLINE_FLEX, INLINE_GRID)
                 * should be wrapped in INLINE_CONTAINER by box construction.
                 * If we reach here, box construction has a bug. */
                NSLOG(layout, ERROR,
                    "Unexpected child type %d in layout_minmax_block - "
                    "box construction should have wrapped this in INLINE_CONTAINER",
                    child->type);
                assert(0 && "Invalid box tree: inline-level box not wrapped in INLINE_CONTAINER");
            }
            assert(child->max_width != UNKNOWN_MAX_WIDTH);

            /* Detect children with huge max_width values */
            if (child->max_width > 1000000000) {
                NSLOG(layout, WARNING, "OVERFLOW_TRACE: child %p type=%d max_width=%d in block %p type=%d", child,
                    child->type, child->max_width, block, block->type);
            }

            if (child->style &&
                (css_computed_position(child->style) == CSS_POSITION_ABSOLUTE ||
                    css_computed_position(child->style) == CSS_POSITION_FIXED)) {
                /* This child is positioned out of normal flow,
                 * so it will have no affect on width */
                continue;
            }

            if (lh__box_is_flex_container(block) && lh__flex_main_is_horizontal(block)) {
                if (block->style != NULL && css_computed_flex_wrap(block->style) == CSS_FLEX_WRAP_NOWRAP) {
                    min += child->min_width.value;
                } else {
                    if (min < child->min_width.value)
                        min = child->min_width.value;
                }
                max += child->max_width;
                flex_item_count++;

            } else {
                if (min < child->min_width.value)
                    min = child->min_width.value;
                if (max < child->max_width)
                    max = child->max_width;
            }

            if (child_has_height)
                block->flags |= HAS_HEIGHT;
        }
    }

    /* Add gap contribution for horizontal flex containers.
     * Per CSS spec, intrinsic main size includes gaps between items.
     * For n items, there are (n-1) gaps. */
    if (flex_item_count > 1 && flex_gap > 0) {
        int total_gap = (flex_item_count - 1) * flex_gap;
        /* For nowrap flex, min is sum of items so add gaps to min too */
        if (block->style != NULL && css_computed_flex_wrap(block->style) == CSS_FLEX_WRAP_NOWRAP) {
            min += total_gap;
        }
        max += total_gap;
    }

    if (max < min) {
        box_dump(stderr, block, 0, true);
        assert(0);
    }

    /* Preserve the content minimum for flex's automatic minimum (§4.5).
     * The preferred-width floor below belongs only to the item's intrinsic
     * contribution (§9.9.3); using it as an auto minimum prevents shrinking. */
    block->content_min_width = min;

    /* Intrinsic contributions use content widths until constraints have been
     * applied. Resolve border-box values before comparing them with content. */
    layout_used_mbp_width(&content->unit_len_ctx, block, LEFT, false, &extra_fixed, &extra_frac);
    layout_used_mbp_width(&content->unit_len_ctx, block, RIGHT, false, &extra_fixed, &extra_frac);
    if (block->type != BOX_TABLE_CELL)
        layout_minmax_constraints(block, &content->unit_len_ctx, extra_fixed, &min, &max);

    if (htype == CSS_HEIGHT_SET && hunit != CSS_UNIT_PCT) {
        bool positive = false;
        if (hunit == CSS_UNIT_CALC) {
            int hpx = 0;
            if (lh__length_to_px(block->style, &content->unit_len_ctx, -1, height, hunit, &hpx)) {
                positive = hpx > 0;
            }
        } else {
            positive = height.value > INTTOFIX(0);
        }
        if (positive) {
            block->flags |= MAKE_HEIGHT;
            block->flags |= HAS_HEIGHT;
        }
    }

    /* Add the outer edges after constraining the content size. */
    calculate_mbp_width(&content->unit_len_ctx, block->style, LEFT, true, false, false, &extra_fixed, &extra_frac);
    calculate_mbp_width(&content->unit_len_ctx, block->style, RIGHT, true, false, false, &extra_fixed, &extra_frac);

    if (extra_fixed < 0)
        extra_fixed = 0;
    if (extra_frac < 0)
        extra_frac = 0;
    if (1.0 <= extra_frac)
        extra_frac = 0.9;
    if (block->style != NULL &&
        (css_computed_float(block->style) == CSS_FLOAT_LEFT || css_computed_float(block->style) == CSS_FLOAT_RIGHT)) {
        /* floated boxs */
        block->min_width.value = min + extra_fixed;
        block->max_width = max + extra_fixed;
    } else {
        /* not floated */
        block->min_width.value = (min + extra_fixed) / (1.0 - extra_frac);
        block->max_width = (max + extra_fixed) / (1.0 - extra_frac);
    }
    NSLOG(layout, DEBUG, "MINMAX_BLOCK %p: FINAL stored min=%d max=%d (extra_fixed=%d)", block, block->min_width.value,
        block->max_width, extra_fixed);

    /* Detect overflow: if resulting max_width is huge, log debug info */
    if (block->max_width > 1000000000) {
        NSLOG(layout, WARNING,
            "OVERFLOW_TRACE: block %p type=%d max_width=%d (min=%d max=%d extra_fixed=%d extra_frac=%f)", block,
            block->type, block->max_width, min, max, extra_fixed, extra_frac);
    }

    assert(0 <= block->min_width.value);
    assert(block->min_width.value <= block->max_width);
    return true;
}

/**
 * Calculate minimum and maximum width of any box type
 *
 * This is a dispatcher function that routes to the appropriate
 * minmax function based on the box type.
 *
 * \param box       box to calculate min/max width for
 * \param font_func font functions
 * \param content   The HTML content being laid out
 */
bool layout_minmax_box(struct box *box, const struct gui_layout_table *font_func, const html_content *content)
{

    /* Check if already calculated */
    if (box->max_width != UNKNOWN_MAX_WIDTH)
        return true;

    switch (box->type) {
    case BOX_BLOCK:
    case BOX_INLINE_BLOCK:
    case BOX_TABLE_CELL:
    case BOX_FLEX:
    case BOX_INLINE_FLEX:
    case BOX_GRID:
    case BOX_INLINE_GRID:
        if (!layout_minmax_block(box, font_func, content))
            return false;
        break;
    case BOX_TABLE:
        if (!layout_minmax_table(box, font_func, content))
            return false;
        break;
    case BOX_INLINE_CONTAINER:
        if (!layout_minmax_inline_container(box, NULL, font_func, content))
            return false;
        break;
    default:
        NSLOG(layout, WARNING, "layout_minmax_box: unhandled box type %d", box->type);
        box->min_width.value = 0;
        box->max_width = 0;
        break;
    }
    return true;
}


/**
 * Check if a box is out of normal flow (absolute or fixed positioned).
 * CSS 2.1 §9.3: Out-of-flow elements don't participate in margin collapsing.
 */
static inline bool box_is_out_of_flow(const struct box *box)
{
    /* Text borrows its parent's style, including position, but remains in
     * that parent's line. It is not an independently positioned element. */
    return box->type != BOX_TEXT && box->style != NULL &&
        (css_computed_position(box->style) == CSS_POSITION_ABSOLUTE ||
            css_computed_position(box->style) == CSS_POSITION_FIXED);
}

/**
 * Check if a box has vertical padding or borders that separate margins.
 * CSS 2.1 §8.3.1: Padding and borders prevent adjoining margins.
 */
static inline bool box_has_vertical_separator(const struct box *box)
{
    return box->padding[TOP] || box->padding[BOTTOM] || box->border[TOP].width || box->border[BOTTOM].width;
}

/* CSS Inline 3, phantom line boxes: only inline-axis edges make an empty
 * fragment participate in a line. In horizontal layout the opening fragment
 * owns the left edge and its closing fragment owns the right edge, including
 * when a block splits them into separate inline containers. Use the same
 * computed-value test for margin lookahead and final line sizing: a nonzero
 * percentage is not zero just because its containing width makes it so. */
static bool box_inline_has_edges(const css_unit_ctx *unit_len_ctx, const struct box *box)
{
    if (box->style == NULL)
        return false;
    const unsigned int sides[] = {LEFT, RIGHT};
    for (size_t i = 0; i < sizeof(sides) / sizeof(sides[0]); i++) {
        unsigned int side = sides[i];
        if ((side == LEFT && box->type == BOX_INLINE_END) ||
            (side == RIGHT && box->type == BOX_INLINE && box->inline_end))
            continue;
        css_fixed_or_calc value = (css_fixed_or_calc)0;
        css_unit unit = CSS_UNIT_PX;
        if (margin_funcs[side](box->style, &value, &unit) == CSS_MARGIN_SET &&
            !css_computed_length_is_zero(box->style, unit_len_ctx, value, unit))
            return true;
        if (padding_funcs[side](box->style, &value, &unit) == CSS_PADDING_SET &&
            !css_computed_length_is_zero(box->style, unit_len_ctx, value, unit))
            return true;
        if (border_style_funcs[side](box->style) > CSS_BORDER_STYLE_HIDDEN) {
            uint8_t type = border_width_funcs[side](box->style, &value, &unit);
            if (type != CSS_BORDER_WIDTH_WIDTH ||
                !css_computed_length_is_zero(box->style, unit_len_ctx, value, unit))
                return true;
        }
    }
    return false;
}

/* Whether an inline container interrupts margin lookahead. Floats also need
 * positioning before looking past their container. Collapsed whitespace is
 * stored in box->space, while preserved whitespace has its own text box. */
static inline bool box_inline_container_has_line_boxes(const css_unit_ctx *unit_len_ctx, const struct box *box)
{
    for (struct box *c = box->children; c != NULL; c = c->next) {
        if (box_is_out_of_flow(c))
            continue;
        if ((c->type == BOX_INLINE || c->type == BOX_INLINE_END) && !lh__box_is_replace(c) &&
            !(c->text && c->length) && !box_inline_has_edges(unit_len_ctx, c))
            continue;
        return true;
    }
    return false;
}

/* Float wrappers interrupt lookahead until positioned, but do not create
 * in-flow line boxes or separate the surrounding blocks' margins. */
static bool box_inline_container_only_floats(const css_unit_ctx *unit_len_ctx, const struct box *box)
{
    bool floats = false;
    for (const struct box *child = box->children; child; child = child->next) {
        if (child->type == BOX_FLOAT_LEFT || child->type == BOX_FLOAT_RIGHT) {
            floats = true;
        } else if (!box_is_out_of_flow(child) &&
            !((child->type == BOX_INLINE || child->type == BOX_INLINE_END) && !lh__box_is_replace(child) &&
                !(child->text && child->length) && !box_inline_has_edges(unit_len_ctx, child))) {
            return false;
        }
    }
    return floats;
}


/**
 * Check if a block box has at least one in-flow block-level child.
 * CSS 2.1 §8.3.1: For margin collapse descent, only in-flow block-level
 * children have margins that participate in the block margin chain.
 * Inline containers (anonymous wrappers) do not have block margins and
 * should not trigger descent — their content is evaluated separately
 * via box_prevents_collapse_through.
 */
static inline bool box_has_in_flow_block_children(const struct box *box)
{
    for (struct box *c = box->children; c != NULL; c = c->next) {
        if ((c->type == BOX_BLOCK || c->type == BOX_FLEX || c->type == BOX_GRID) && !box_is_out_of_flow(c))
            return true;
    }
    return false;
}

static bool box_has_outside_marker_content(const struct box *box)
{
    return box_has_outside_marker(box) && (box->list_marker->object || box->list_marker->length) &&
        !(box->flags & CONTENTS_SKIPPED);
}

/* An outside marker supplies a line even when its inline formatting context
 * contains only floats, positioned boxes or collapsed whitespace. Block-child
 * marker fallback has different sizing and margin rules. */
static bool box_has_outside_marker_line(const struct box *box)
{
    if (!box_has_outside_marker_content(box))
        return false;
    for (const struct box *child = box->children; child; child = child->next) {
        if (child->type != BOX_INLINE_CONTAINER && !box_is_out_of_flow(child))
            return false;
    }
    return true;
}

/* A nonzero used minimum separates an otherwise empty block's margins.
 * Resolve percentages against the same definite containing height as sizing. */
static bool box_has_positive_min_height(const css_unit_ctx *unit_len_ctx, const struct box *box)
{
    css_fixed_or_calc value = (css_fixed_or_calc)0;
    css_unit unit = CSS_UNIT_PX;
    int height;
    if (box->style == NULL || css_computed_min_height(box->style, &value, &unit) != CSS_MIN_HEIGHT_SET)
        return false;
    int available = unit == CSS_UNIT_PCT || unit == CSS_UNIT_CALC ? lh__available_height_for_box(box, -1) : -1;
    return lh__length_to_px(box->style, unit_len_ctx, available, value, unit, &height) && height > 0;
}

/* Intrinsic measurement cannot set MAKE_HEIGHT for percentages: their
 * containing height may only become definite during layout (for example,
 * from an aspect ratio or flex stretch). Resolve it before calling a box
 * self-collapsing; using a previous layout's box->height would be stale. */
static bool box_has_positive_percentage_height(const css_unit_ctx *unit_len_ctx, const struct box *box)
{
    css_fixed_or_calc value = (css_fixed_or_calc)0;
    css_unit unit = CSS_UNIT_PX;
    int height;
    if (box->style == NULL || css_computed_height(box->style, &value, &unit) != CSS_HEIGHT_SET ||
        (unit != CSS_UNIT_PCT && unit != CSS_UNIT_CALC))
        return false;
    int available = lh__available_height_for_box(box, -1);
    return lh__length_to_px(box->style, unit_len_ctx, available, value, unit, &height) && height > 0;
}

/* Style/containment reasons for giving an ordinary block its own formatting
 * context. Keep this separate from overflow clipping: flow-root does not clip.
 * Atomic inline boxes, floats, flex and grid have their own layout paths. */
static bool box_style_creates_block_context(const struct box *box)
{
    return box->style && (css_computed_display(box->style, false) == CSS_DISPLAY_FLOW_ROOT ||
        css_computed_overflow_x(box->style) != CSS_OVERFLOW_VISIBLE ||
        css_computed_overflow_y(box->style) != CSS_OVERFLOW_VISIBLE || (box->flags & CONTENTS_SKIPPED));
}

/**
 * Check if a box prevents margins from collapsing through it.
 * CSS 2.1 §8.3.1: A box's own margins collapse only if it does not
 * establish an independent formatting context, has zero min-height,
 * zero/auto height, no vertical padding/border, and no line box.
 */
static inline bool box_prevents_collapse_through(const css_unit_ctx *unit_len_ctx, const struct box *box)
{
    if (box_has_outside_marker_content(box))
        return true;
    if (box->flags & CONTENTS_SKIPPED)
        return true;
    /* Replaced content has its own intrinsic dimensions; having no box
     * children does not make an image or form control an empty block. */
    if (lh__box_is_replace(box))
        return true;
    /* Flex/grid/table containers establish independent formatting contexts,
     * even when empty. Their top and bottom margins are never adjoining. */
    if (box->type == BOX_FLEX || box->type == BOX_GRID || box->type == BOX_TABLE)
        return true;
    /* Independent block contexts keep top and bottom margins separate even
     * when empty, so collect their bottom margin after layout. */
    if (box->type == BOX_BLOCK && box_style_creates_block_context(box))
        return true;
    if (box->flags & (MAKE_HEIGHT | ASPECT_RATIO_HEIGHT))
        return true;
    if (box_has_positive_percentage_height(unit_len_ctx, box))
        return true;
    if (box_has_vertical_separator(box) || box_has_positive_min_height(unit_len_ctx, box))
        return true;
    /* Floats stop lookahead until positioned, but do not supply an in-flow
     * line that could separate this block's own top and bottom margins. */
    if (box->type == BOX_INLINE_CONTAINER)
        return box_inline_container_has_line_boxes(unit_len_ctx, box) &&
            !box_inline_container_only_floats(unit_len_ctx, box);
    /* In-flow descendants can separate the margins even before layout sets
     * MAKE_HEIGHT. This also covers zero-height independent contexts inside wrappers. */
    if (box->type == BOX_BLOCK) {
        for (struct box *c = box->children; c != NULL; c = c->next) {
            if (!box_is_out_of_flow(c) &&
                (c->type == BOX_BLOCK || c->type == BOX_INLINE_CONTAINER || c->type == BOX_FLEX ||
                    c->type == BOX_GRID || c->type == BOX_TABLE) &&
                box_prevents_collapse_through(unit_len_ctx, c))
                return true;
        }
    }
    return false;
}

static bool layout_clear(struct box *fl, enum css_clear_e clear, int *y);

static bool box_is_descendant_of(const struct box *box, const struct box *ancestor)
{
    for (box = box->parent; box; box = box->parent) {
        if (box == ancestor)
            return true;
    }
    return false;
}

/* Flow scans decide clearance using floats already positioned in this BFC.
 * Top scans find a hypothetical/bounded top without applying clearance; they
 * can cross float-only containers and report a deferred clearance decision.
 * Resumed top scans also decide clearance against the floats now positioned. */
enum margin_scan_mode {
    MARGIN_SCAN_FLOW,
    MARGIN_SCAN_TOP,
    MARGIN_SCAN_RESUME_TOP,
};

struct margin_scan {
    const css_unit_ctx *unit_len_ctx;
    struct box *block;             /* Formatting-context root. */
    int viewport_height;
    int flow_y;                   /* Position before the adjoining margins. */
    enum margin_scan_mode mode;
    struct box *boundary;          /* Stop before this box's bottom, or NULL. */
    struct box *adjoining_parent;  /* Positioned top still adjoining the chain. */
    int applied_margin;           /* Provisional offset when resuming a parent top. */
};

struct margin_scan_result {
    /* Start with the caller's pending margins; the scan adds adjoining edges. */
    unsigned int positive, negative;
    struct box *top;               /* Edge receiving the margin, or NULL at the BFC boundary. */
    struct box *clearance;         /* Box requiring clearance from positioned floats. */
    struct box *pending_clearance; /* Box requiring a decision after float placement. */
};

/* Internal floats move with an adjoining parent's hypothetical top. External
 * floats keep their formatting-context coordinates. Compare against both in
 * that hypothetical position, including zero-height and negative-margin floats. */
static int layout_clear_adjoining_floor(const struct margin_scan *scan, const struct box *box, int margin, int floor)
{
    const struct box *parent = scan->adjoining_parent;
    enum css_clear_e clear = css_computed_clear(box->style);
    int shift = margin - scan->applied_margin;
    if (!parent || !box_is_descendant_of(box, parent))
        return floor;
    floor = INT_MIN;
    for (const struct box *fl = scan->block->float_children; fl; fl = fl->next_float) {
        if ((fl->type == BOX_FLOAT_LEFT && (clear == CSS_CLEAR_LEFT || clear == CSS_CLEAR_BOTH)) ||
            (fl->type == BOX_FLOAT_RIGHT && (clear == CSS_CLEAR_RIGHT || clear == CSS_CLEAR_BOTH))) {
            int bottom = fl->y + fl->height;
            if (box_is_descendant_of(fl, parent))
                bottom += shift;
            floor = max(floor, bottom);
        }
    }
    return floor;
}

/* Find the end of the adjoining-margin chain in tree order. The result's
 * margin totals accumulate; its three box outputs are reset for each scan. */
static struct box *layout_next_margin_block(struct box *box, const struct margin_scan *scan,
    struct margin_scan_result *result)
{
    assert(scan->block != NULL);
    result->top = box;
    result->clearance = NULL;
    result->pending_clearance = NULL;
    bool left_float = false, right_float = false;

    while (box != NULL) {

        if (box->type == BOX_INLINE_CONTAINER ||
            (box->style &&
                (css_computed_position(box->style) != CSS_POSITION_ABSOLUTE &&
                    css_computed_position(box->style) != CSS_POSITION_FIXED))) {
            /* Not positioned */

            /* Get margins */
            if (box->style) {
                layout_find_dimensions(scan->unit_len_ctx, box->parent->width, scan->viewport_height, box,
                    &(struct layout_dimension_outputs){
                        .margin = box->margin,
                        .padding = box->padding,
                        .border = box->border,
                    });

                /* A hypothetical parent top may cross float-only containers,
                 * but clearance needs their positions first. End this chain
                 * before borrowing the potentially cleared child's margins. */
                enum css_clear_e clear = css_computed_clear(box->style);
                if ((left_float && (clear == CSS_CLEAR_LEFT || clear == CSS_CLEAR_BOTH)) ||
                    (right_float && (clear == CSS_CLEAR_RIGHT || clear == CSS_CLEAR_BOTH))) {
                    result->pending_clearance = box;
                    return box;
                }

                /* CSS 2.1 section 9.5.2: decide clearance using the top edge
                 * with clear:none, before joining this margin to its parent
                 * or preceding sibling. Bound the hypothetical search at this
                 * box so a later sibling cannot move its empty top edge. */
                if (scan->mode != MARGIN_SCAN_TOP && clear != CSS_CLEAR_NONE) {
                    int floor;
                    if (layout_clear(scan->block->float_children, css_computed_clear(box->style), &floor)) {
                        struct margin_scan hypothetical = *scan;
                        hypothetical.mode = MARGIN_SCAN_TOP;
                        hypothetical.boundary = box;
                        hypothetical.adjoining_parent = NULL;
                        struct margin_scan_result top = {
                            .positive = result->positive, .negative = result->negative};
                        layout_next_margin_block(box, &hypothetical, &top);
                        int margin = (int)top.positive - (int)top.negative;
                        floor = layout_clear_adjoining_floor(scan, box, margin, floor);
                        if (scan->flow_y + margin < floor) {
                            result->clearance = box;
                            return box;
                        }
                    }
                }

                /* Apply top margin */
                if (box->margin[TOP] > 0 && (unsigned int)box->margin[TOP] > result->positive) {
                    result->positive = (unsigned int)box->margin[TOP];
                } else if (box->margin[TOP] != AUTO && box->margin[TOP] < 0 &&
                    -(unsigned int)box->margin[TOP] > result->negative) {
                    result->negative = -(unsigned int)box->margin[TOP];
                }
            }


            /* Check whether box is the box current margin collapses to.
             * Position inline content (including floats) before collecting
             * a following sibling's top margin. */
            bool float_only = scan->boundary && scan->mode != MARGIN_SCAN_FLOW && box->type == BOX_INLINE_CONTAINER &&
                box_inline_container_only_floats(scan->unit_len_ctx, box);
            if (float_only) {
                for (const struct box *child = box->children; child; child = child->next) {
                    left_float |= child->type == BOX_FLOAT_LEFT;
                    right_float |= child->type == BOX_FLOAT_RIGHT;
                }
            }
            /* Content or a specified height separates the bottom edge, not a
             * first block child's top. Stop at an inline-content leaf instead
             * of losing the chain's end and recollecting its ancestor margins. */
            if (box->type == BOX_FLEX || box->type == BOX_GRID || box->flags & CONTENTS_SKIPPED ||
                (!box_has_in_flow_block_children(box) && ((box->flags & MAKE_HEIGHT) ||
                    (box->type == BOX_BLOCK && box_prevents_collapse_through(scan->unit_len_ctx, box)))) ||
                box_has_outside_marker_line(box) || lh__box_is_replace(box) || box->border[TOP].width ||
                box->padding[TOP] || box_style_creates_block_context(box) ||
                (box->type == BOX_INLINE_CONTAINER && !float_only &&
                    box_inline_container_has_line_boxes(scan->unit_len_ctx, box)) ||
                (box_has_positive_min_height(scan->unit_len_ctx, box) && !box_has_in_flow_block_children(box))) {
                /* Collapse to this box; return it */
                return box;
            }
        }


        /* Find next box */
        if (box->type == BOX_BLOCK && !box->object && box->children && box->style &&
            !box_style_creates_block_context(box) && box_has_in_flow_block_children(box)) {
            /* Down into children — crossing a parent-child boundary.
             * CSS 2.1 §8.3.1: margin may collapse with first child.
             * Only descend for in-flow block-level children; inline
             * containers are anonymous wrappers without block margins
             * and are handled via box_prevents_collapse_through. */
            box = box->children;
        } else {
            bool walked_up = false;

            /* CSS 2.1 §8.3.1: If the box does NOT collapse through
             * (has height/content), then its Top margin does NOT
             * collapse with its Bottom margin (and thus does not
             * collapse with Parent Bottom or Next Sibling Top).
             * We should NOT walk up in this case. */
            bool collapses_through = !box_prevents_collapse_through(scan->unit_len_ctx, box);

            if (box == scan->boundary)
                return box;
            if (!box->next && collapses_through) {
                /* No more siblings AND box collapses through:
                 * Go up to first ancestor with a sibling. */
                walked_up = true;
                do {
                    /* Empty descendants' bottoms join the top chain too.
                     * The boundary's own bottom is excluded above. */
                    if (box->margin[BOTTOM] > 0)
                        result->positive = max(result->positive, (unsigned int)box->margin[BOTTOM]);
                    else if (box->margin[BOTTOM] != AUTO && box->margin[BOTTOM] < 0)
                        result->negative = max(result->negative, -(unsigned int)box->margin[BOTTOM]);
                    struct box *child = box;
                    bool includes_parent_top = box != result->top;
                    if (box == result->top)
                        result->top = box->parent;
                    box = box->parent;
                    if (box == scan->boundary)
                        return box;

                    /* Check if we've reached the block boundary BEFORE
                     * accumulating any margins from it */
                    if (box == scan->block) {
                        /* Margins don't collapse with stuff
                         * outside the block formatting context.
                         * Don't accumulate this block's margin-bottom
                         * as it belongs to the outer context. */
                        result->top = NULL;
                        return scan->block;
                    }

                    /* First-child margins can join a parent's top without
                     * joining its bottom. Stop when that parent cannot itself
                     * collapse through, including one with a specified height. */
                    if (box_prevents_collapse_through(scan->unit_len_ctx, box)) {
                        /* If this scan began at an ancestor's top, the last
                         * adjoining edge is the child, before the parent's
                         * non-adjoining content. Keep that end distinct from
                         * the parent which receives the collapsed offset. */
                        return includes_parent_top ? child : box;
                    }

                    /* NOTE: We do NOT accumulate parent's margin[BOTTOM] here.
                     * When walking up from children looking for the next margin
                     * collapse target, the parent's bottom margin is at the
                     * BOTTOM of the parent box, not adjacent to children's
                     * top margins. The parent's bottom margin should only
                     * collapse with what comes AFTER the parent, not with
                     * content inside it. */

                } while (!box->next);
            } else if (!box->next) {
                /* No more siblings and NO collapse through:
                 * This box has content/height, so its top margin/margin chain
                 * ends here. It does not collapse with parent's bottom or
                 * valid next siblings of parent. */
                return NULL;
            }

            /* CSS 2.1 §8.3.1: "bottom margin of box and top margin of its
             * next in-flow following sibling" — accumulate margin[BOTTOM]
             * when moving to the next sibling.
             *
             * If we walked up from inside a child, the ancestor's
             * margin-bottom is normally NOT adjacent to the child's top
             * margin chain (it's at the bottom of the ancestor).
             * EXCEPTION: if the ancestor itself collapses through
             * (self-collapsing — no height, no in-flow content), then
             * its top and bottom margins ARE adjoining, and the bottom
             * margin is part of the same collapsing chain. */
            if ((!walked_up || !box_prevents_collapse_through(scan->unit_len_ctx, box)) && !box->padding[BOTTOM]) {
                if (box->margin[BOTTOM] > 0 && (unsigned int)box->margin[BOTTOM] > result->positive) {
                    result->positive = (unsigned int)box->margin[BOTTOM];
                } else if (box->margin[BOTTOM] != AUTO && box->margin[BOTTOM] < 0 &&
                    -(unsigned int)box->margin[BOTTOM] > result->negative)
                    result->negative = -(unsigned int)box->margin[BOTTOM];
            }

            /* To next sibling.
             * CSS 2.1 §8.3.1 only allows collapse with the next in-flow
             * sibling, so skip out-of-flow boxes here and let the top of
             * the loop compute margins for the next valid participant. */
            struct box *previous = box;
            box = box->next;
            while (box != NULL && box_is_out_of_flow(box)) {
                NSLOG(layout, DEBUG, "margin collapse: skipping out-of-flow sibling %p type=%d", box, box->type);
                box = box->next;
            }
            /* Leaving an empty branch transfers the receiving edge to the
             * next sibling. Moving between children keeps their enclosing
             * parent's top edge as the receiver. */
            if (previous == result->top)
                result->top = box;
        }
    }

    return NULL;
}


/**
 * Find y coordinate which clears all floats on left and/or right.
 *
 * \param  fl	  first float in float list
 * \param  clear  type of clear
 * \param  y     receives the bottom edge in formatting-context coordinates
 * \return  whether a matching float supplied a clearance edge
 */
static bool layout_clear(struct box *fl, enum css_clear_e clear, int *y)
{
    bool found = false;
    for (; fl; fl = fl->next_float) {
        if (((clear == CSS_CLEAR_LEFT || clear == CSS_CLEAR_BOTH) && fl->type == BOX_FLOAT_LEFT) ||
            ((clear == CSS_CLEAR_RIGHT || clear == CSS_CLEAR_BOTH) && fl->type == BOX_FLOAT_RIGHT)) {
            if (!found || *y < fl->y + fl->height)
                *y = fl->y + fl->height;
            found = true;
        }
    }
    return found;
}


/**
 * Find left and right edges in a vertical range.
 *
 * \param  fl	  first float in float list
 * \param  y0	  start of y range to search
 * \param  y1	  end of y range to search
 * \param  x0	  start left edge, updated to available left edge
 * \param  x1	  start right edge, updated to available right edge
 * \param  left	  returns float on left if present
 * \param  right  returns float on right if present
 */
static void find_sides(struct box *fl, int y0, int y1, int *x0, int *x1, struct box **left, struct box **right)
{
    int fy0, fy1, fx0, fx1;

    NSLOG(layout, DEBUG, "y0 %i, y1 %i, x0 %i, x1 %i", y0, y1, *x0, *x1);

    *left = *right = 0;
    for (; fl; fl = fl->next_float) {
        fy1 = fl->y + fl->height;
        if (fy1 < y0) {
            /* Floats are sorted in order of decreasing bottom pos.
             * Past here, all floats will be too high to concern us.
             */
            return;
        }
        fy0 = fl->y;
        /* CSS 2.2 9.5: zero/negative outer heights have no exclusion area,
         * even when their position falls inside the queried line range. */
        if (fl->height > 0 && y0 < fy1 && fy0 <= y1) {
            if (fl->type == BOX_FLOAT_LEFT) {
                fx1 = fl->x + fl->width;
                if (*x0 < fx1) {
                    *x0 = fx1;
                    *left = fl;
                }
            } else {
                fx0 = fl->x;
                if (fx0 < *x1) {
                    *x1 = fx0;
                    *right = fl;
                }
            }
        }
    }

    NSLOG(layout, DEBUG, "x0 %i, x1 %i, left %p, right %p", *x0, *x1, *left, *right);
}


/**
 * Solve the width constraint as given in CSS 2.1 section 10.3.3.
 *
 * \param  box              Box to solve constraint for
 * \param  available_width  Max width available in pixels
 * \param  width	    Current box width
 * \param  lm		    Min left margin required to avoid floats in px.
 *				zero if not applicable
 * \param  rm		    Min right margin required to avoid floats in px.
 *				zero if not applicable
 * \param  max_width	    Box max-width ( -ve means no max-width to apply)
 * \param  min_width	    Box min-width ( <=0 means no min-width to apply)
 * \return		    New box width
 *
 * \post \a box's left/right margins will be updated.
 */
static int
layout_solve_width(struct box *box, int available_width, int width, int lm, int rm, int max_width, int min_width)
{
    bool auto_width = false;

    /* Increase specified left/right margins */
    if (box->margin[LEFT] != AUTO && box->margin[LEFT] < lm && box->margin[LEFT] >= 0)
        box->margin[LEFT] = lm;
    if (box->margin[RIGHT] != AUTO && box->margin[RIGHT] < rm && box->margin[RIGHT] >= 0)
        box->margin[RIGHT] = rm;

    /* Find width */
    if (width == AUTO) {
        int margin_left = box->margin[LEFT];
        int margin_right = box->margin[RIGHT];

        if (margin_left == AUTO) {
            margin_left = lm;
        }
        if (margin_right == AUTO) {
            margin_right = rm;
        }

        width = available_width -
            (margin_left + box->border[LEFT].width + box->padding[LEFT] + box->padding[RIGHT] +
                box->border[RIGHT].width + margin_right);
        width = width < 0 ? 0 : width;
        auto_width = true;
    }

    if (max_width >= 0 && width > max_width) {
        /* max-width is admissable and width exceeds max-width */
        width = max_width;
        auto_width = false;
    }

    if (min_width > 0 && width < min_width) {
        /* min-width is admissable and width is less than max-width */
        width = min_width;
        auto_width = false;
    }

    /* Width was auto, and unconstrained by min/max width, so we're done */
    if (auto_width) {
        /* any other 'auto' become 0 or the minimum required values */
        if (box->margin[LEFT] == AUTO) {
            box->margin[LEFT] = lm;
        }
        if (box->margin[RIGHT] == AUTO) {
            box->margin[RIGHT] = rm;
        }
        return width;
    }

    /* Width was not auto, or was constrained by min/max width
     * Need to compute left/right margins */

    /* HTML alignment (only applies to over-constrained boxes) */
    if (box->margin[LEFT] != AUTO && box->margin[RIGHT] != AUTO && box->parent != NULL && box->parent->style != NULL) {
        switch (css_computed_text_align(box->parent->style)) {
        case CSS_TEXT_ALIGN_LIBCSS_RIGHT:
            box->margin[LEFT] = AUTO;
            box->margin[RIGHT] = 0;
            break;
        case CSS_TEXT_ALIGN_LIBCSS_CENTER:
            box->margin[LEFT] = box->margin[RIGHT] = AUTO;
            break;
        case CSS_TEXT_ALIGN_LIBCSS_LEFT:
            box->margin[LEFT] = 0;
            box->margin[RIGHT] = AUTO;
            break;
        default:
            /* Leave it alone; no HTML alignment */
            break;
        }
    }

    if (box->margin[LEFT] == AUTO && box->margin[RIGHT] == AUTO) {
        /* make the margins equal, centering the element */
        box->margin[LEFT] = box->margin[RIGHT] = (available_width - lm - rm -
                                                     (box->border[LEFT].width + box->padding[LEFT] + width +
                                                         box->padding[RIGHT] + box->border[RIGHT].width)) /
            2;

        if (box->margin[LEFT] < 0) {
            box->margin[RIGHT] += box->margin[LEFT];
            box->margin[LEFT] = 0;
        }

        box->margin[LEFT] += lm;

    } else if (box->margin[LEFT] == AUTO) {
        box->margin[LEFT] = available_width - lm -
            (box->border[LEFT].width + box->padding[LEFT] + width + box->padding[RIGHT] + box->border[RIGHT].width +
                box->margin[RIGHT]);
        box->margin[LEFT] = box->margin[LEFT] < lm ? lm : box->margin[LEFT];
    } else {
        /* margin-right auto or "over-constrained" */
        box->margin[RIGHT] = available_width - rm -
            (box->margin[LEFT] + box->border[LEFT].width + box->padding[LEFT] + width + box->padding[RIGHT] +
                box->border[RIGHT].width);
    }

    return width;
}


/**
 * Compute dimensions of box, margins, paddings, and borders for a block-level
 * element.
 *
 * \param  unit_len_ctx          Length conversion context
 * \param  available_width  Max width available in pixels
 * \param  viewport_height  Height of viewport in pixels or -ve if unknown
 * \param  lm		    min left margin required to avoid floats in px.
 *				zero if not applicable
 * \param  rm		    min right margin required to avoid floats in px.
 *				zero if not applicable
 * \param  box		    box to find dimensions of. updated with new width,
 *			    height, margins, borders and paddings
 *
 * See CSS 2.1 10.3.3, 10.3.4, 10.6.2, and 10.6.3.
 */
static void layout_block_find_dimensions(
    const css_unit_ctx *unit_len_ctx, int available_width, int viewport_height, int lm, int rm, struct box *box)
{
    int width, max_width;
    struct css_size min_width;
    int height, max_height;
    struct css_size min_height;
    int *margin = box->margin;
    int *padding = box->padding;
    struct box_border *border = box->border;
    const css_computed_style *style = box->style;

    layout_find_dimensions(unit_len_ctx, available_width, viewport_height, box,
        &(struct layout_dimension_outputs){
            .width = &width,
            .height = &height,
            .max_width = &max_width,
            .min_width = &min_width,
            .max_height = &max_height,
            .min_height = &min_height,
            .margin = margin,
            .padding = padding,
            .border = border,
        });

    /* Debug: Log what layout_find_dimensions computed */
    NSLOG(layout, DEBUG,
        "layout_block_find_dimensions: AFTER layout_find_dimensions box=%p margins=[%d,%d,%d,%d] (AUTO=%d)", box,
        margin[TOP], margin[RIGHT], margin[BOTTOM], margin[LEFT], AUTO);

    /* Debug: Log computed margins for specific elements */
    {
        const char *tag = "";
        const char *cls = "";
        const char *id = "";
        dom_string *name = NULL;
        dom_string *class_attr = NULL;
        dom_string *id_attr = NULL;
        if (box->node != NULL) {
            if (dom_node_get_node_name(box->node, &name) == DOM_NO_ERR && name != NULL) {
                tag = dom_string_data(name);
            }
            if (dom_element_get_attribute(box->node, corestring_dom_class, &class_attr) == DOM_NO_ERR &&
                class_attr != NULL) {
                cls = dom_string_data(class_attr);
            }
            if (dom_element_get_attribute(box->node, corestring_dom_id, &id_attr) == DOM_NO_ERR && id_attr != NULL) {
                id = dom_string_data(id_attr);
            }
        }
        /* Log for article elements or DIVs with class/id */
        if ((strcasecmp(tag, "ARTICLE") == 0) || (strcasecmp(tag, "DIV") == 0 && (cls[0] != '\0' || id[0] != '\0'))) {
            NSLOG(layout, DEBUG,
                "layout_block_find_dimensions: tag=%s id=%s class=%s box=%p margins=[%d,%d,%d,%d] padding=[%d,%d,%d,%d]",
                tag, id, cls, box, margin[TOP], margin[RIGHT], margin[BOTTOM], margin[LEFT], padding[TOP],
                padding[RIGHT], padding[BOTTOM], padding[LEFT]);
        }
        if (id_attr != NULL)
            dom_string_unref(id_attr);
        if (class_attr != NULL)
            dom_string_unref(class_attr);
        if (name != NULL)
            dom_string_unref(name);
    }

    if (box->object && !(box->flags & REPLACE_DIM)) {
        /* block-level replaced element, see 10.3.4 and 10.6.2 */
        layout_get_object_dimensions(box, &width, &height,
            &(struct layout_size_limits){
                .min_width = min_width,
                .max_width = max_width,
                .min_height = min_height,
                .max_height = max_height,
            },
            available_width);
    }

    box->width = layout_solve_width(box, available_width, width, lm, rm, max_width, min_width.value);
    box->height = height;
    layout_prepare_aspect_ratio_height(box);
    if (box->flags & ASPECT_RATIO_HEIGHT) {
        if (max_height >= 0)
            box->height = min(box->height, max_height);
        if (min_height.type == CSS_SIZE_SET)
            box->height = max(box->height, min_height.value);
    }

    if (margin[TOP] == AUTO)
        margin[TOP] = 0;
    if (margin[BOTTOM] == AUTO)
        margin[BOTTOM] = 0;
}


/**
 * Size a new formatting context beside floats, moving down if it cannot fit.
 * Coordinates are relative to the containing block formatting context.
 * Return the chosen top border edge; dimensions and horizontal margins are
 * recomputed at each float boundary, keeping percentages relative to the parent.
 */
static int layout_block_avoid_floats(const css_unit_ctx *unit_len_ctx, struct box *box,
    int viewport_height, struct box *floats, int cx, int top)
{
    int containing_left = cx + box->parent->padding[LEFT];
    int containing_right = containing_left + box->parent->width;

    for (;;) {
        int x0 = containing_left, x1 = containing_right;
        struct box *left, *right;
        find_sides(floats, top, top, &x0, &x1, &left, &right);
        layout_block_find_dimensions(unit_len_ctx, box->parent->width, viewport_height,
            x0 - containing_left, containing_right - x1, box);

        /* The width solver can produce a negative right margin for a fixed
         * width that will not fit. Test the border box, not the margin box. */
        int border_left = containing_left + box->margin[LEFT];
        int border_right = border_left + box->border[LEFT].width + box->padding[LEFT] + box->width +
            box->padding[RIGHT] + box->border[RIGHT].width;
        if ((!left && !right) || (border_left >= x0 && border_right <= x1))
            return top;

        /* Try the next boundary, rather than clearing every float. The box
         * may fit beside a taller float once a shorter one has ended. Each
         * step advances past an obstructing float, including oversized boxes. */
        if (!left)
            top = right->y + right->height;
        else if (!right)
            top = left->y + left->height;
        else
            top = min(left->y + left->height, right->y + right->height);
    }
}


/**
 * Manipulate a block's [RB]padding/height/width to accommodate scrollbars
 *
 * \param  box	  Box to apply scrollbar space too. Must be BOX_BLOCK.
 * \param  which  Which scrollbar to make space for. Must be RIGHT or BOTTOM.
 */
static void layout_block_add_scrollbar(struct box *box, int which)
{
    enum css_overflow_e overflow_x, overflow_y;

    assert(box->type == BOX_BLOCK && (which == RIGHT || which == BOTTOM));

    if (box->style == NULL)
        return;

    overflow_x = css_computed_overflow_x(box->style);
    overflow_y = css_computed_overflow_y(box->style);

    if (which == BOTTOM &&
        (overflow_x == CSS_OVERFLOW_SCROLL || overflow_x == CSS_OVERFLOW_AUTO ||
            (box->object && content_get_type(box->object) == CONTENT_HTML))) {
        /* make space for scrollbar, unless height is AUTO */
        if (box->height != AUTO && (overflow_x == CSS_OVERFLOW_SCROLL || box_hscrollbar_present(box))) {
            int space = SCROLLBAR_WIDTH;
            if (lh__box_is_replace(box)) {
                /* Replaced sizing already fixed the outer content area.
                 * Reserve the scrollbar inside it, as for the width below. */
                space = min(space, max(0, box->height));
                box->height -= space;
            }
            box->padding[BOTTOM] += space;
        }

    } else if (which == RIGHT &&
        (overflow_y == CSS_OVERFLOW_SCROLL || overflow_y == CSS_OVERFLOW_AUTO ||
            (box->object && content_get_type(box->object) == CONTENT_HTML))) {
        /* make space for scrollbars, unless width is AUTO */
        enum css_height_e htype;
        css_fixed_or_calc height = (css_fixed_or_calc)0;
        css_unit hunit = CSS_UNIT_PX;
        htype = css_computed_height(box->style, &height, &hunit);

        if (which == RIGHT && box->width != AUTO && (htype == CSS_HEIGHT_SET || lh__box_is_replace(box)) &&
            (overflow_y == CSS_OVERFLOW_SCROLL || box_vscrollbar_present(box))) {
            int space = min(SCROLLBAR_WIDTH, max(0, box->width));
            box->width -= space;
            box->padding[RIGHT] += space;
        }
    }
}


/* CSS 2.2 §17.5.3: inline-only vertical-align values fall back to baseline
 * on table cells. Keep selection identical during row sizing and placement. */
static uint8_t layout_table_cell_alignment(const struct box *cell)
{
    css_fixed_or_calc value = (css_fixed_or_calc)0;
    css_unit unit = CSS_UNIT_PX;
    uint8_t align = css_computed_vertical_align(cell->style, &value, &unit);
    switch (align) {
    case CSS_VERTICAL_ALIGN_TOP:
    case CSS_VERTICAL_ALIGN_MIDDLE:
    case CSS_VERTICAL_ALIGN_BOTTOM:
        return align;
    default:
        return CSS_VERTICAL_ALIGN_BASELINE;
    }
}

/* Measure natural contents before minimum heights or row-span padding change
 * the cell's geometry. Empty cells synthesize at their natural content bottom. */
static bool layout_table_measure_row(struct box *row, const int *xs, int spacing, html_content *content)
{
    const struct box *table = row->parent->parent;
    row->flags &= ~HAS_BASELINE;
    for (struct box *cell = row->children; cell; cell = cell->next) {
        unsigned int column = table_cell_start_column(table, cell);
        cell->width = xs[column + cell->columns] - xs[column] - spacing - cell->border[LEFT].width -
            cell->padding[LEFT] - cell->padding[RIGHT] - cell->border[RIGHT].width;
        cell->height = AUTO;
        if (!layout_block_context(cell, -1, content))
            return false;
        /* These existing scratch fields remain valid until final alignment. */
        cell->descendant_y0 = cell->height;
        cell->descendant_y1 = cell->padding[BOTTOM];
        if (!(cell->flags & HAS_BASELINE)) {
            cell->baseline = cell->padding[TOP] + cell->height;
            cell->flags |= HAS_BASELINE;
        }
        if (layout_table_cell_alignment(cell) == CSS_VERTICAL_ALIGN_BASELINE) {
            int baseline = cell->border[TOP].width + cell->baseline;
            row->baseline = (row->flags & HAS_BASELINE) ? max(row->baseline, baseline) : baseline;
            row->flags |= HAS_BASELINE;
        }
        css_fixed_or_calc value = (css_fixed_or_calc)0;
        css_unit unit = CSS_UNIT_PX;
        int height;
        /* Cell height is a minimum for row sizing, not a content baseline. */
        if (css_computed_height(cell->style, &value, &unit) == CSS_HEIGHT_SET &&
            lh__length_to_px(cell->style, &content->unit_len_ctx, -1, value, unit, &height))
            cell->height = max(cell->height, height);
        cell->x = xs[column] + cell->border[LEFT].width;
        cell->y = cell->border[TOP].width;
    }
    return true;
}

/* Moving every float in this formatting context by the same amount preserves
 * its exclusion-list order. Independent nested contexts move with their box. */
static void layout_table_shift_cell(struct box *cell, int shift)
{
    if (shift == 0)
        return;
    cell->padding[TOP] += shift;
    cell->padding[BOTTOM] -= shift;
    cell->baseline += shift;
    for (struct box *child = cell->children; child; child = child->next)
        child->y += shift;
    for (struct box *floating = cell->float_children; floating; floating = floating->next_float)
        floating->y += shift;
}

/* Row-span heights are final now. Baseline cells align to their starting row;
 * middle/bottom cells use their complete span. Rows without a baseline group
 * export the lowest natural content bottom after alignment. */
static void layout_table_align_row(struct box *row)
{
    bool baseline_group = row->flags & HAS_BASELINE;
    int fallback = row->children ? 0 : row->height;
    for (struct box *cell = row->children; cell; cell = cell->next) {
        int spare = (cell->padding[BOTTOM] - cell->descendant_y1) + (cell->height - cell->descendant_y0);
        int shift = 0;
        switch (layout_table_cell_alignment(cell)) {
        case CSS_VERTICAL_ALIGN_BASELINE:
            shift = row->baseline - cell->border[TOP].width - cell->baseline;
            break;
        case CSS_VERTICAL_ALIGN_MIDDLE:
            shift = spare / 2;
            break;
        case CSS_VERTICAL_ALIGN_BOTTOM:
            shift = spare;
            break;
        default:
            break;
        }
        layout_table_shift_cell(cell, shift);
        fallback = max(fallback, cell->y + cell->padding[TOP] + cell->descendant_y0);
    }
    if (!baseline_group)
        row->baseline = fallback;
    row->flags |= HAS_BASELINE;
}


/* Documented in layout_internal.h */
bool layout_table(struct box *table, int available_width, html_content *content)
{
    unsigned int columns = table->columns; /* total columns */
    unsigned int i;
    unsigned int *row_span;
    int *excess_y;
    int table_width, min_width = 0, max_width = 0;
    int required_width = 0;
    int x, remainder = 0, count = 0;
    int table_height = 0;
    int min_height = 0;
    int *xs; /* array of column x positions */
    int auto_width;
    int spare_width;
    int relative_sum = 0;
    int border_spacing_h = 0, border_spacing_v = 0;
    int positioned_columns = 0;
    struct box *containing_block = NULL;
    struct box *c;
    struct box *row;
    struct box *row_group;
    struct box **row_span_cell;
    struct column *col;
    const css_computed_style *style = table->style;
    enum css_height_e htype;
    css_fixed_or_calc value = (css_fixed_or_calc)0;
    css_unit unit = CSS_UNIT_PX;

    assert(table->type == BOX_TABLE);
    assert(style);
    assert(table->children && table->children->children);
    assert(columns);

    table->flags &= ~HAS_BASELINE;

    if (!table_prepare_borders(&content->unit_len_ctx, table))
        return false;

    /* allocate working buffers */
    col = malloc(columns * sizeof col[0]);
    excess_y = malloc(columns * sizeof excess_y[0]);
    row_span = malloc(columns * sizeof row_span[0]);
    row_span_cell = malloc(columns * sizeof row_span_cell[0]);
    xs = malloc((columns + 1) * sizeof xs[0]);
    if (!col || !xs || !row_span || !excess_y || !row_span_cell) {
        free(col);
        free(excess_y);
        free(row_span);
        free(row_span_cell);
        free(xs);
        return false;
    }

    memcpy(col, table->table->columns, sizeof(col[0]) * columns);

    /* find margins, paddings, and borders for table and cells */
    layout_find_dimensions(&content->unit_len_ctx, available_width, -1, table,
        &(struct layout_dimension_outputs){
            .margin = table->margin,
            .padding = table->padding,
            .border = table->border,
        });
    for (row_group = table->children; row_group; row_group = row_group->next) {
        for (row = row_group->children; row; row = row->next) {
            for (c = row->children; c; c = c->next) {
                enum css_overflow_e overflow_x;
                enum css_overflow_e overflow_y;

                assert(c->style);
                layout_find_dimensions(&content->unit_len_ctx, available_width, -1, c,
                    &(struct layout_dimension_outputs){
                        .padding = c->padding,
                        .border = c->border,
                    });

                overflow_x = css_computed_overflow_x(c->style);
                overflow_y = css_computed_overflow_y(c->style);

                if (overflow_x == CSS_OVERFLOW_SCROLL || overflow_x == CSS_OVERFLOW_AUTO) {
                    c->padding[BOTTOM] += SCROLLBAR_WIDTH;
                }
                if (overflow_y == CSS_OVERFLOW_SCROLL || overflow_y == CSS_OVERFLOW_AUTO) {
                    c->padding[RIGHT] += SCROLLBAR_WIDTH;
                }
            }
        }
    }

    /* border-spacing is used in the separated borders model */
    if (css_computed_border_collapse(style) == CSS_BORDER_COLLAPSE_SEPARATE) {
        css_fixed_or_calc h = (css_fixed_or_calc)0, v = (css_fixed_or_calc)0;
        css_unit hu = CSS_UNIT_PX, vu = CSS_UNIT_PX;

        css_computed_border_spacing(style, &h, &hu, &v, &vu);

        if (!lh__length_to_px(style, &content->unit_len_ctx, -1, h, hu, &border_spacing_h)) {
            border_spacing_h = 0;
        }
        if (!lh__length_to_px(style, &content->unit_len_ctx, -1, v, vu, &border_spacing_v)) {
            border_spacing_v = 0;
        }
    }

    /* find specified table width, or available width if auto-width */
    if (css_computed_width_px(style, &content->unit_len_ctx, available_width, &table_width) == CSS_WIDTH_SET) {
        layout_handle_box_sizing(&content->unit_len_ctx, table, available_width, true, &table_width);

        auto_width = table_width;
    } else {
        table_width = AUTO;
        auto_width = available_width -
            ((table->margin[LEFT] == AUTO ? 0 : table->margin[LEFT]) + table->border[LEFT].width +
                table->padding[LEFT] + table->padding[RIGHT] + table->border[RIGHT].width +
                (table->margin[RIGHT] == AUTO ? 0 : table->margin[RIGHT]));
    }

    /* Find any table height specified within CSS/HTML */
    htype = css_computed_height(style, &value, &unit);
    if (htype == CSS_HEIGHT_SET) {
        if (unit == CSS_UNIT_PCT || unit == CSS_UNIT_CALC) {
            int available_height = -1;
            enum css_height_e cb_htype = CSS_HEIGHT_AUTO;
            css_fixed_or_calc ignored = (css_fixed_or_calc)0;
            css_unit cb_unit = CSS_UNIT_PX;

            /* This is the minimum height for the table (see 17.5.3). */
            if (css_computed_position(table->style) == CSS_POSITION_ABSOLUTE) {
                /* Table is absolutely positioned */
                assert(table->float_container);
                containing_block = table->float_container;
            } else if (table->float_container && css_computed_position(table->style) != CSS_POSITION_ABSOLUTE &&
                (css_computed_float(table->style) == CSS_FLOAT_LEFT ||
                    css_computed_float(table->style) == CSS_FLOAT_RIGHT)) {
                /* Table is a float */
                assert(table->parent && table->parent->parent && table->parent->parent->parent);
                containing_block = table->parent->parent->parent;
            } else if (table->parent && table->parent->type != BOX_INLINE_CONTAINER) {
                /* Table is a block level element */
                containing_block = table->parent;
            } else if (table->parent && table->parent->type == BOX_INLINE_CONTAINER) {
                /* Table is an inline block */
                assert(table->parent->parent);
                containing_block = table->parent->parent;
            }

            if (containing_block) {
                cb_htype = css_computed_height(containing_block->style, &ignored, &cb_unit);
            }

            if (containing_block && containing_block->height != AUTO &&
                (css_computed_position(table->style) == CSS_POSITION_ABSOLUTE || cb_htype == CSS_HEIGHT_SET)) {
                /* Table is absolutely positioned or its containing block has a valid specified height
                 * (CSS 2.1 Section 10.5). */
                available_height = containing_block->height;
            }

            if (available_height >= 0) {
                int px = 0;
                if (lh__length_to_px(style, &content->unit_len_ctx, available_height, value, unit, &px)) {
                    min_height = px;
                }
            } else if (unit == CSS_UNIT_CALC) {
                int px = 0;
                if (lh__length_to_px(style, &content->unit_len_ctx, -1, value, unit, &px)) {
                    min_height = px;
                }
            }
        } else {
            /* This is the minimum height for the table (see 17.5.3). */
            min_height = FIXTOINT(css_unit_len2device_px(style, &content->unit_len_ctx, value.value, unit));
        }
    }

    /* calculate width required by cells */
    for (i = 0; i != columns; i++) {

        NSLOG(layout, DEBUG, "table %p, column %u: type %s, width %i, min %i, max %i", table, i,
            ((const char *[]){
                "UNKNOWN",
                "FIXED",
                "AUTO",
                "PERCENT",
                "RELATIVE",
            })[col[i].type],
            col[i].width, col[i].min, col[i].max);


        if (col[i].positioned) {
            positioned_columns++;
            continue;
        } else if (col[i].type == COLUMN_WIDTH_FIXED) {
            if (col[i].width < col[i].min)
                col[i].width = col[i].max = col[i].min;
            else
                col[i].min = col[i].max = col[i].width;
            required_width += col[i].width;
        } else if (col[i].type == COLUMN_WIDTH_PERCENT) {
            int width = col[i].width * auto_width / 100;
            required_width += col[i].min < width ? width : col[i].min;
        } else
            required_width += col[i].min;

        NSLOG(layout, DEBUG, "required_width %i", required_width);
    }
    required_width += (columns + 1 - positioned_columns) * border_spacing_h;

    NSLOG(layout, DEBUG, "width %i, min %i, max %i, auto %i, required %i", table_width, table->min_width.value,
        table->max_width, auto_width, required_width);

    if (auto_width < required_width) {
        /* table narrower than required width for columns:
         * treat percentage widths as maximums */
        for (i = 0; i != columns; i++) {
            if (col[i].type == COLUMN_WIDTH_RELATIVE)
                continue;
            if (col[i].type == COLUMN_WIDTH_PERCENT) {
                col[i].max = auto_width * col[i].width / 100;
                if (col[i].max < col[i].min)
                    col[i].max = col[i].min;
            }
            min_width += col[i].min;
            max_width += col[i].max;
        }
    } else {
        /* take percentages exactly */
        for (i = 0; i != columns; i++) {
            if (col[i].type == COLUMN_WIDTH_RELATIVE)
                continue;
            if (col[i].type == COLUMN_WIDTH_PERCENT) {
                int width = auto_width * col[i].width / 100;
                if (width < col[i].min)
                    width = col[i].min;
                col[i].min = col[i].width = col[i].max = width;
                col[i].type = COLUMN_WIDTH_FIXED;
            }
            min_width += col[i].min;
            max_width += col[i].max;
        }
    }

    /* allocate relative widths */
    spare_width = auto_width;
    for (i = 0; i != columns; i++) {
        if (col[i].type == COLUMN_WIDTH_RELATIVE)
            relative_sum += col[i].width;
        else if (col[i].type == COLUMN_WIDTH_FIXED)
            spare_width -= col[i].width;
        else
            spare_width -= col[i].min;
    }
    spare_width -= (columns + 1) * border_spacing_h;
    if (relative_sum != 0) {
        if (spare_width < 0)
            spare_width = 0;
        for (i = 0; i != columns; i++) {
            if (col[i].type == COLUMN_WIDTH_RELATIVE) {
                col[i].min = ceil(col[i].max = (float)spare_width * (float)col[i].width / relative_sum);
                min_width += col[i].min;
                max_width += col[i].max;
            }
        }
    }
    min_width += (columns + 1) * border_spacing_h;
    max_width += (columns + 1) * border_spacing_h;

    if (auto_width <= min_width) {
        /* not enough space: minimise column widths */
        for (i = 0; i < columns; i++) {
            col[i].width = col[i].min;
        }
        table_width = min_width;
    } else if (max_width <= auto_width) {
        /* more space than maximum width */
        if (table_width == AUTO) {
            /* for auto-width tables, make columns max width */
            for (i = 0; i < columns; i++) {
                col[i].width = col[i].max;
            }
            table_width = max_width;
        } else {
            /* for fixed-width tables, distribute the extra space
             * too */
            unsigned int flexible_columns = 0;
            for (i = 0; i != columns; i++)
                if (col[i].type != COLUMN_WIDTH_FIXED)
                    flexible_columns++;
            if (flexible_columns == 0) {
                int extra = (table_width - max_width) / columns;
                remainder = (table_width - max_width) - (extra * columns);
                for (i = 0; i != columns; i++) {
                    col[i].width = col[i].max + extra;
                    count -= remainder;
                    if (count < 0) {
                        col[i].width++;
                        count += columns;
                    }
                }

            } else {
                int extra = (table_width - max_width) / flexible_columns;
                remainder = (table_width - max_width) - (extra * flexible_columns);
                for (i = 0; i != columns; i++)
                    if (col[i].type != COLUMN_WIDTH_FIXED) {
                        col[i].width = col[i].max + extra;
                        count -= remainder;
                        if (count < 0) {
                            col[i].width++;
                            count += flexible_columns;
                        }
                    }
            }
        }
    } else {
        /* space between min and max: fill it exactly */
        float scale = (float)(auto_width - min_width) / (float)(max_width - min_width);
        /* fprintf(stderr, "filling, scale %f\n", scale); */
        for (i = 0; i < columns; i++) {
            col[i].width = col[i].min + (int)(0.5 + (col[i].max - col[i].min) * scale);
        }
        table_width = auto_width;
    }

    /* Grid coordinates are physical left-to-right, while column widths and
     * row-span bookkeeping remain indexed in source order. */
    bool rtl = css_computed_direction(style) == CSS_DIRECTION_RTL;
    xs[0] = x = border_spacing_h;
    for (i = 0; i != columns; i++) {
        unsigned int column = rtl ? columns - 1 - i : i;
        if (!col[column].positioned)
            x += col[column].width + border_spacing_h;
        xs[i + 1] = x;
        row_span[i] = 0;
        excess_y[i] = 0;
        row_span_cell[i] = 0;
    }

    /* position cells */
    table_height = border_spacing_v + table->padding[TOP];
    for (row_group = table->children; row_group; row_group = row_group->next) {
        int row_group_height = 0;
        for (row = row_group->children; row; row = row->next) {
            int row_height = 0;

            htype = css_computed_height(row->style, &value, &unit);
            if (htype == CSS_HEIGHT_SET && unit != CSS_UNIT_PCT) {
                if (!lh__length_to_px(row->style, &content->unit_len_ctx, -1, value, unit, &row_height)) {
                    row_height = 0;
                }
            }
            if (!layout_table_measure_row(row, xs, border_spacing_h, content)) {
                free(col);
                free(excess_y);
                free(row_span);
                free(row_span_cell);
                free(xs);
                return false;
            }
            /* A spanning baseline cell still reserves its ascent in the first
             * row. Its remaining descent can be accommodated by later rows. */
            if (row->flags & HAS_BASELINE)
                row_height = max(row_height, row->baseline);
            for (c = row->children; c; c = c->next) {
                int shift = layout_table_cell_alignment(c) == CSS_VERTICAL_ALIGN_BASELINE
                    ? row->baseline - c->border[TOP].width - c->baseline
                    : 0;
                int required = max(c->height, c->descendant_y0 + shift);
                for (i = 0; i != c->columns; i++) {
                    row_span[c->start_column + i] = c->rows;
                    excess_y[c->start_column + i] = c->border[TOP].width + c->padding[TOP] + required +
                        c->padding[BOTTOM] + c->border[BOTTOM].width;
                    row_span_cell[c->start_column + i] = 0;
                }
                row_span_cell[c->start_column] = c;
                c->padding[BOTTOM] = -border_spacing_v - c->border[TOP].width - c->padding[TOP] - c->height -
                    c->border[BOTTOM].width;
            }
            for (i = 0; i != columns; i++)
                if (row_span[i] != 0)
                    row_span[i]--;
                else
                    row_span_cell[i] = 0;
            if (row->next || row_group->next) {
                /* row height is greatest excess of a cell
                 * which ends in this row */
                for (i = 0; i != columns; i++)
                    if (row_span[i] == 0 && row_height < excess_y[i])
                        row_height = excess_y[i];
            } else {
                /* except in the last row */
                for (i = 0; i != columns; i++)
                    if (row_height < excess_y[i])
                        row_height = excess_y[i];
            }
            for (i = 0; i != columns; i++) {
                if (row_height < excess_y[i])
                    excess_y[i] -= row_height;
                else
                    excess_y[i] = 0;
                if (row_span_cell[i] != 0)
                    row_span_cell[i]->padding[BOTTOM] += row_height + border_spacing_v;
            }

            row->x = 0;
            row->y = row_group_height;
            row->width = table_width;
            row->height = row_height;
            row_group_height += row_height + border_spacing_v;
        }
        row_group->x = table->padding[LEFT];
        row_group->y = table_height;
        row_group->width = table_width;
        row_group->height = row_group_height;
        table_height += row_group_height;
    }
    /* Row positions include leading padding, but box->height stores content
     * height. Outer layout adds both padding edges separately. */
    table_height = max(table_height - table->padding[TOP], min_height);
    /** \todo distribute spare height over the row groups / rows / cells */

    for (row_group = table->children; row_group; row_group = row_group->next) {
        for (row = row_group->children; row; row = row->next) {
            layout_table_align_row(row);
            if (!(table->flags & HAS_BASELINE)) {
                table->baseline = row_group->y + row->y + row->baseline;
                table->flags |= HAS_BASELINE;
            }
        }
    }

    /* Top and bottom margins of 'auto' are set to 0.  CSS2.1 10.6.3 */
    if (table->margin[TOP] == AUTO)
        table->margin[TOP] = 0;
    if (table->margin[BOTTOM] == AUTO)
        table->margin[BOTTOM] = 0;

    table_position_borders(table, xs);
    free(col);
    free(excess_y);
    free(row_span);
    free(row_span_cell);
    free(xs);

    table->width = table_width;
    table->height = table_height;

    return true;
}


/**
 * Manimpulate box height according to CSS min-height and max-height properties
 *
 * \param  unit_len_ctx      CSS length conversion context for document.
 * \param  box		block to modify with any min-height or max-height
 * \param  container	containing block for absolutely positioned elements, or
 *			NULL for non absolutely positioned elements.
 * \return		whether the height has been changed
 */
static bool layout_apply_minmax_height(const css_unit_ctx *unit_len_ctx, struct box *box, struct box *container)
{
    if (box->style == NULL)
        return false;
    const struct box *cb = container ? container : lh__containing_block_for_box(box);
    int width = cb ? cb->width : box->width;
    int height = container ? container->height : lh__available_height_for_box(box, -1);
    if (box->containing_size) {
        width = box->containing_size->width;
        height = box->containing_size->height;
    }
    const struct layout_dimension_context ctx = {
        .box = box,
        .unit_len_ctx = lh__with_container_dimensions(unit_len_ctx, width, height),
        .available_width = width,
        .available_height = height,
    };
    struct css_size minimum;
    int maximum;
    layout_find_height_limits(&ctx, &(struct layout_dimension_outputs){.min_height = &minimum, .max_height = &maximum});
    int previous = box->height;
    if (maximum >= 0)
        box->height = min(box->height, maximum);
    if (minimum.type == CSS_SIZE_SET)
        box->height = max(box->height, minimum.value);
    return previous != box->height;
}


/**
 * Layout a block which contains an object.
 *
 * \param  block  box of type BLOCK, INLINE_BLOCK, TABLE, or TABLE_CELL
 * \return  true on success, false on memory exhaustion
 */
static bool layout_block_object(struct box *block)
{
    assert(block);
    assert(block->type == BOX_BLOCK || block->type == BOX_FLEX || block->type == BOX_GRID ||
        block->type == BOX_INLINE_BLOCK || block->type == BOX_INLINE_FLEX || block->type == BOX_INLINE_GRID ||
        block->type == BOX_TABLE || block->type == BOX_TABLE_CELL);
    assert(block->object);

    NSLOG(layout, DEBUG, "block %p, object %p, width %i", block, hlcache_handle_get_url(block->object), block->width);

    if (content_can_reformat(block->object)) {
        /* The replaced box establishes the embedded document's viewport;
         * the document's resulting extent must not size that box in return. */
        content_reformat(block->object, false, block->width, block->height);
    } else {
        /* Non-HTML objects */
        /* this case handled already in
         * layout_block_find_dimensions() */
    }

    return true;
}


/**
 * Insert a float into a container.
 *
 * \param  cont	  block formatting context block, used to contain float
 * \param  b      box to add to float
 *
 * This sorts floats in order of descending bottom edges.
 */
static void add_float_to_container(struct box *cont, struct box *b)
{
    struct box *box = cont->float_children;
    int b_bottom = b->y + b->height;

    assert(b->type == BOX_FLOAT_LEFT || b->type == BOX_FLOAT_RIGHT);

    /* These limits exist only after a float has been placed. Its top can be
     * negative in the shared formatting context; zero is not a lower bound.
     * Every placed float also constrains later floats' top edges (CSS 2.2 9.5.1). */
    cont->cached_place_below_level = box ? max(cont->cached_place_below_level, b->y) : b->y;
    if (box == NULL) {
        /* No other float children */
        cont->clear_level = b->y;
        b->next_float = NULL;
        cont->float_children = b;
        return;
    } else if (b_bottom >= box->y + box->height) {
        /* Goes at start of list */
        b->next_float = cont->float_children;
        cont->float_children = b;
    } else {
        struct box *prev = NULL;
        while (box != NULL && b_bottom < box->y + box->height) {
            prev = box;
            box = box->next_float;
        }
        if (prev != NULL) {
            b->next_float = prev->next_float;
            prev->next_float = b;
        }
    }
}


/* Float positions use formatting-context coordinates. Moving a completed line
 * or block must keep the exclusion list and later float placement consistent. */
static void layout_shift_float(struct box *box, int shift)
{
    struct box *cont = box->float_container;
    struct box **link = &cont->float_children;
    while (*link && *link != box)
        link = &(*link)->next_float;
    assert(*link == box);
    *link = box->next_float;
    box->y += shift;
    add_float_to_container(cont, box);
    if (css_computed_clear(box->children->style) != CSS_CLEAR_NONE)
        cont->clear_level = max(cont->clear_level, box->y);
}


/**
 * Split a text box.
 *
 * \param  content     memory pool for any new boxes
 * \param  fstyle      style for text in text box
 * \param  split_box   box with text to split
 * \param  new_length  new length for text in split_box, after splitting
 * \param  new_width   new width for text in split_box, after splitting
 * \return  true on success, false on memory exhaustion
 *
 * A new box is created and inserted into the box tree after split_box,
 * containing the text after new_length excluding the initial space character.
 */
static bool layout_text_box_split(
    html_content *content, plot_font_style_t *fstyle, struct box *split_box, size_t new_length, int new_width)
{
    int space_width = 0;
    struct box *c2;
    const struct gui_layout_table *font_func = content->font_func;
    bool space = (split_box->text[new_length] == ' ');
    int used_length = new_length + (space ? 1 : 0);

    if (space) {
        /* This is an internal text break. The original trailing space can
         * come from a different DOM style and belongs to the last fragment. */
        if (split_box->space_style == NULL && split_box->space != UNKNOWN_WIDTH && split_box->space != 0)
            space_width = split_box->space;
        else
            font_func->width(fstyle, " ", 1, &space_width);
    }

    /* Fragments share their inline container's lifetime. Keep them as allocation
     * siblings: rejoining a fragment must not free any later live fragments. */
    assert(split_box->parent != NULL && split_box->parent->type == BOX_INLINE_CONTAINER);
    c2 = talloc_memdup(split_box->parent, split_box, sizeof *c2);
    if (!c2)
        return false;
    c2->flags |= CLONE;
    c2->sticky = NULL;
    c2->flags &= ~(HAS_STICKY_DESCENDANT | HAS_STICKY_ANCESTOR | HAS_IFRAME_DESCENDANT);

    /* Set remaining text in c2 */
    c2->text += used_length;

    /* Set c2 according to the remaining text */
    c2->width -= new_width + space_width;
    c2->flags &= ~MEASURED; /* width has been estimated */
    c2->length = split_box->length - used_length;

    /* Update split_box for its reduced text */
    split_box->width = new_width;
    split_box->flags |= MEASURED;
    split_box->length = new_length;
    split_box->space = space_width;
    split_box->space_style = NULL;

    /* Insert c2 into box list */
    c2->next = split_box->next;
    split_box->next = c2;
    c2->prev = split_box;
    if (c2->next)
        c2->next->prev = c2;
    else
        c2->parent->last = c2;

    NSLOG(layout, DEBUG, "split_box %p len: %" PRIsizet " \"%.*s\"", split_box, split_box->length,
        (int)split_box->length, split_box->text);
    NSLOG(layout, DEBUG, "  new_box %p len: %" PRIsizet " \"%.*s\"", c2, c2->length, (int)c2->length, c2->text);

    return true;
}


/**
 * Compute dimensions of box, margins, paddings, and borders for a floating
 * element using shrink-to-fit. Also used for inline-blocks.
 *
 * \param  unit_len_ctx          CSS length conversion context for document.
 * \param  available_width  Max width available in pixels
 * \param  box		    Box for which to find dimensions
 *				Box margins, borders, paddings, width and
 *				height are updated.
 */
static void layout_float_find_dimensions(const css_unit_ctx *unit_len_ctx, int available_width, struct box *box)
{
    const css_computed_style *style = box->style;
    int width, height, max_width, max_height;
    struct css_size min_width;
    struct css_size min_height;
    int *margin = box->margin;
    int *padding = box->padding;
    struct box_border *border = box->border;
    enum css_overflow_e overflow_x = css_computed_overflow_x(style);
    enum css_overflow_e overflow_y = css_computed_overflow_y(style);
    int scrollbar_width_x = (overflow_x == CSS_OVERFLOW_SCROLL || overflow_x == CSS_OVERFLOW_AUTO) ? SCROLLBAR_WIDTH
                                                                                                   : 0;
    int scrollbar_width_y = (overflow_y == CSS_OVERFLOW_SCROLL || overflow_y == CSS_OVERFLOW_AUTO) ? SCROLLBAR_WIDTH
                                                                                                   : 0;

    layout_find_dimensions(unit_len_ctx, available_width, -1, box,
        &(struct layout_dimension_outputs){
            .width = &width,
            .height = &height,
            .max_width = &max_width,
            .min_width = &min_width,
            .max_height = &max_height,
            .min_height = &min_height,
            .margin = margin,
            .padding = padding,
            .border = border,
        });

    if (margin[LEFT] == AUTO)
        margin[LEFT] = 0;
    if (margin[RIGHT] == AUTO)
        margin[RIGHT] = 0;

    if (box->gadget == NULL) {
        padding[RIGHT] += scrollbar_width_y;
        padding[BOTTOM] += scrollbar_width_x;
    }

    if (box->object && !(box->flags & REPLACE_DIM)) {
        /* Floating replaced element, with intrinsic width or height.
         * See 10.3.6 and 10.6.2 */
        layout_get_object_dimensions(box, &width, &height,
            &(struct layout_size_limits){
                .min_width = min_width,
                .max_width = max_width,
                .min_height = min_height,
                .max_height = max_height,
            },
            available_width);
    } else if (box->gadget &&
        (box->gadget->type == GADGET_TEXTBOX || box->gadget->type == GADGET_PASSWORD ||
            box->gadget->type == GADGET_FILE || box->gadget->type == GADGET_TEXTAREA)) {
        css_fixed size = 0;
        css_unit unit = CSS_UNIT_EM;

        /* Give sensible dimensions to gadgets, with auto width/height,
         * that don't shrink to fit contained text. */
        assert(box->style);

        if (box->gadget->type == GADGET_TEXTBOX || box->gadget->type == GADGET_PASSWORD ||
            box->gadget->type == GADGET_FILE) {
            if (width == AUTO) {
                size = INTTOFIX(10);
                width = FIXTOINT(css_unit_len2device_px(box->style, unit_len_ctx, size, unit));
            }
            if (box->gadget->type == GADGET_FILE && height == AUTO) {
                size = FLTTOFIX(1.5);
                height = FIXTOINT(css_unit_len2device_px(box->style, unit_len_ctx, size, unit));
            }
        }
        if (box->gadget->type == GADGET_TEXTAREA) {
            if (width == AUTO) {
                size = INTTOFIX(10);
                width = FIXTOINT(css_unit_len2device_px(box->style, unit_len_ctx, size, unit));
            }
            if (height == AUTO) {
                size = INTTOFIX(4);
                height = FIXTOINT(css_unit_len2device_px(box->style, unit_len_ctx, size, unit));
            }
        }
    } else if (width == AUTO) {
        /* CSS 2.1 section 10.3.5 */
        width = min(max(box->min_width.value, available_width), box->max_width);

        /* width includes margin, borders and padding */
        if (width == available_width) {
            width -= box->margin[LEFT] + box->border[LEFT].width + box->padding[LEFT] + box->padding[RIGHT] +
                box->border[RIGHT].width + box->margin[RIGHT];
        } else {
            /* width was obtained from a min_width or max_width
             * value, so need to use the same method for calculating
             * mbp as was used in layout_minmax_block() */
            int fixed = 0;
            float frac = 0;
            calculate_mbp_width(unit_len_ctx, box->style, LEFT, true, true, true, &fixed, &frac);
            calculate_mbp_width(unit_len_ctx, box->style, RIGHT, true, true, true, &fixed, &frac);
            if (fixed < 0)
                fixed = 0;

            width -= fixed;
        }

        if (max_width >= 0 && width > max_width)
            width = max_width;
        if (min_width.type == CSS_SIZE_SET && min_width.value > 0 && width < min_width.value)
            width = min_width.value;

    } else {
        if (max_width >= 0 && width > max_width)
            width = max_width;
        if (min_width.type == CSS_SIZE_SET && min_width.value > 0 && width < min_width.value)
            width = min_width.value;
        width -= scrollbar_width_y;
    }

    box->width = width;
    box->height = height;
    layout_prepare_aspect_ratio_height(box);

    if (margin[TOP] == AUTO)
        margin[TOP] = 0;
    if (margin[BOTTOM] == AUTO)
        margin[BOTTOM] = 0;
}


/**
 * Layout the contents of a float or inline block.
 *
 * \param  b	  float or inline block box
 * \param  width  available width
 * \param  content  memory pool for any new boxes
 * \return  true on success, false on memory exhaustion
 */
static bool layout_float(struct box *b, int width, html_content *content)
{
    assert(b->type == BOX_TABLE || b->type == BOX_BLOCK || b->type == BOX_INLINE_BLOCK || b->type == BOX_FLEX ||
        b->type == BOX_GRID || b->type == BOX_INLINE_FLEX || b->type == BOX_INLINE_GRID);
    layout_float_find_dimensions(&content->unit_len_ctx, width, b);

    if (b->type == BOX_TABLE || b->type == BOX_INLINE_FLEX || b->type == BOX_INLINE_GRID || b->type == BOX_GRID ||
        b->type == BOX_FLEX ||
        (b->type == BOX_BLOCK && b->style &&
            (css_computed_display(b->style, false) == CSS_DISPLAY_GRID ||
                css_computed_display(b->style, false) == CSS_DISPLAY_INLINE_GRID ||
                css_computed_display(b->style, false) == CSS_DISPLAY_FLEX ||
                css_computed_display(b->style, false) == CSS_DISPLAY_INLINE_FLEX))) {
        if (b->type == BOX_TABLE) {
            if (!layout_table(b, width, content))
                return false;
        } else if (b->type == BOX_GRID || b->type == BOX_INLINE_GRID ||
            (b->type == BOX_BLOCK &&
                (css_computed_display(b->style, false) == CSS_DISPLAY_GRID ||
                    css_computed_display(b->style, false) == CSS_DISPLAY_INLINE_GRID))) {
            NSLOG(layout, INFO, "calling layout_grid for grid %p width %i (type %d)", b, width, b->type);
            if (!layout_grid(b, width, content))
                return false;
        } else {
            NSLOG(layout, INFO, "calling layout_flex for flex %p width %i (type %d)", b, width, b->type);
            if (!layout_flex(b, width, content))
                return false;
        }
        if (b->margin[LEFT] == AUTO)
            b->margin[LEFT] = 0;
        if (b->margin[RIGHT] == AUTO)
            b->margin[RIGHT] = 0;
        if (b->margin[TOP] == AUTO)
            b->margin[TOP] = 0;
        if (b->margin[BOTTOM] == AUTO)
            b->margin[BOTTOM] = 0;
    } else {
        return layout_block_context(b, -1, content);
    }
    return true;
}


/**
 * Position a float in the first available space.
 *
 * \param  c	  float box to position
 * \param  width  available width
 * \param  cx	  x coordinate relative to cont to place float right of
 * \param  y	  y coordinate relative to cont to place float below
 * \param  cont	  ancestor box which defines horizontal space, for floats
 */
static void place_float_below(struct box *c, int width, int cx, int y, struct box *cont)
{
    int x0, x1, yy;
    struct box *left;
    struct box *right;

    yy = cont->float_children ? max(y, cont->cached_place_below_level) : y;

    NSLOG(layout, DEBUG, "c %p, width %i, cx %i, y %i, cont %p", c, width, cx, y, cont);

    do {
        y = yy;
        x0 = cx;
        x1 = cx + width;
        find_sides(cont->float_children, y, y + c->height, &x0, &x1, &left, &right);
        if (left != 0 && right != 0) {
            yy = (left->y + left->height < right->y + right->height ? left->y + left->height
                                                                    : right->y + right->height);
        } else if (left == 0 && right != 0) {
            yy = right->y + right->height;
        } else if (left != 0 && right == 0) {
            yy = left->y + left->height;
        }
    } while ((left != 0 || right != 0) && (c->width > x1 - x0));

    if (c->type == BOX_FLOAT_LEFT) {
        c->x = x0;
    } else {
        c->x = x1 - c->width;
    }
    c->y = y;
    cont->cached_place_below_level = y;
}


/**
 * Calculate line height from a style.
 */
static int line_height(const css_unit_ctx *unit_len_ctx, const css_computed_style *style)
{
    enum css_line_height_e lhtype;
    css_fixed_or_calc lhvalue = (css_fixed_or_calc)0;
    css_unit lhunit = CSS_UNIT_PX;

    assert(style);

    lhtype = css_computed_line_height(style, &lhvalue, &lhunit);
    if (lhtype == CSS_LINE_HEIGHT_NORMAL) {
        /* Normal => use a constant of 1.2 * font-size */
        lhvalue.value = FLTTOFIX(1.2);
        lhtype = CSS_LINE_HEIGHT_NUMBER;
    }

    if (lhtype == CSS_LINE_HEIGHT_NUMBER) {
        css_fixed line_height = css_unit_len2device_px(style, unit_len_ctx, lhvalue.value, CSS_UNIT_EM);
        return FIXTOINT(line_height);
    }

    /* Length or percentage (including calc) */
    {
        int font_px = FIXTOINT(css_unit_len2device_px(style, unit_len_ctx, INTTOFIX(1), CSS_UNIT_EM));
        int px = 0;
        if (!lh__length_to_px(style, unit_len_ctx, font_px, lhvalue, lhunit, &px)) {
            px = font_px;
        }
        return px;
    }
}


static bool box_needs_inline_alignment(const struct box *box)
{
    return lh__box_is_atomic_inline(box) || (box->type == BOX_INLINE && lh__box_is_object(box));
}

static bool box_needs_line_metrics(const css_unit_ctx *unit_len_ctx, const struct box *box,
    const css_computed_style *parent_style)
{
    /* Identical styles have identical ascent/descent and need no font lookup.
     * A different text style may extend above or below the parent's strut.
     * Empty inline fragments also contribute their font's strut on real lines;
     * a line containing only undecorated fragments still collapses to zero. */
    return box_needs_inline_alignment(box) ||
        (box->type == BOX_TEXT && box->length && box->style != parent_style) ||
        ((box->type == BOX_INLINE || box->type == BOX_INLINE_END) &&
            (box->style != parent_style || box_inline_has_edges(unit_len_ctx, box)));
}

/* Reuse measurements across the sizing and positioning passes of one line.
 * Styles and fonts stay unchanged during that call; nothing survives reflow
 * or a font load. Key by height too, to preserve half-leading rounding. */
struct line_baselines {
    struct {
        const css_computed_style *style;
        int height, baseline;
    } entries[4];
    unsigned int next;
};

static int layout_text_baseline(const html_content *content, const css_computed_style *style, int height,
    struct line_baselines *baselines)
{
    for (size_t i = 0; i < sizeof(baselines->entries) / sizeof(baselines->entries[0]); i++) {
        if (baselines->entries[i].style == style && baselines->entries[i].height == height)
            return baselines->entries[i].baseline;
    }
    plot_font_style_t fstyle;
    font_plot_style_from_css(content, style, &fstyle);
    int baseline = font_baseline(content->font_func, &fstyle, height);
    unsigned int slot = baselines->next;
    baselines->entries[slot].style = style;
    baselines->entries[slot].height = height;
    baselines->entries[slot].baseline = baseline;
    baselines->next = (slot + 1) % (sizeof(baselines->entries) / sizeof(baselines->entries[0]));
    return baseline;
}

/* Outside markers align to the first content baseline. A taller ascent pushes
 * that content down; marker descent does not enlarge the content's line box.
 * Offset is the content's top relative to the list item, before the push. */
static int layout_align_outside_marker(struct box *owner, const html_content *content,
    int offset, int baseline, struct line_baselines *baselines)
{
    struct box *marker = owner->list_marker;
    if (!box_has_outside_marker(owner) || (!marker->object && !marker->length))
        return 0;
    int ascent = marker->object ? marker->height :
        layout_text_baseline(content, marker->style, marker->height, baselines);
    marker->y = offset + max(0, baseline - ascent);
    return max(0, ascent - baseline);
}

/* A marker without a content baseline still needs space. Inline-only items
 * use the ordinary strut; block children keep their own positions and margins.
 * Their fallback marker occupies the top of the border box, without exporting
 * a synthetic content baseline. CSS height constraints are applied afterward. */
static int layout_marker_min_height(struct box *box, const html_content *content)
{
    if ((box->flags & HAS_BASELINE) || !box_has_outside_marker_content(box))
        return 0;
    if (!box_has_outside_marker_line(box)) {
        struct box *marker = box->list_marker;
        marker->y = -box->border[TOP].width;
        int height = marker->height;
        if (marker->object) {
            struct line_baselines baselines = {0};
            int strut = line_height(&content->unit_len_ctx, marker->style);
            int baseline = layout_text_baseline(content, marker->style, strut, &baselines);
            marker->y += max(0, baseline - marker->height);
            height = max(strut, marker->height + max(0, strut - baseline));
        }
        return max(0, height - box->border[TOP].width - box->padding[TOP] -
            box->padding[BOTTOM] - box->border[BOTTOM].width);
    }
    struct line_baselines baselines = {0};
    int height = line_height(&content->unit_len_ctx, box->style);
    int baseline = layout_text_baseline(content, box->style, height, &baselines);
    int push = layout_align_outside_marker(box, content, box->padding[TOP], baseline, &baselines);
    box->baseline = box->padding[TOP] + baseline + push;
    box->flags |= HAS_BASELINE;
    return max(0, height + push);
}

/* Find the last normal-flow line, looking through block descendants but not
 * into floats, positioned boxes or independent flex/grid/table contexts. The
 * inline container records its baseline during layout, so no text is remeasured. */
static bool layout_last_line_baseline(const struct box *box, int *baseline)
{
    for (const struct box *child = box->last; child; child = child->prev) {
        if (box_is_out_of_flow(child) || lh__box_is_float_box(child))
            continue;
        if (child->type == BOX_INLINE_CONTAINER && (child->flags & HAS_BASELINE)) {
            *baseline = child->y + child->baseline;
            return true;
        }
        if (child->type == BOX_BLOCK && layout_last_line_baseline(child, baseline)) {
            *baseline += child->y;
            return true;
        }
    }
    /* Empty marker-only items have a line without an inline container. Its
     * first baseline is also its last, including inside an inline-block. */
    if ((box->flags & HAS_BASELINE) && box_has_outside_marker_line(box)) {
        *baseline = box->baseline;
        return true;
    }
    return false;
}

/* Distance from the atomic inline's margin-box top to the parent baseline.
 * CSS 2.1 10.8.1: middle uses the parent's x-height; an inline-block's normal
 * baseline comes from its last line, or its bottom margin edge when clipped
 * or when it has no in-flow lines. Use the existing ex-unit metric/fallback. */
static int layout_atomic_baseline(const struct box *box, int height, uint8_t align,
    const css_computed_style *parent_style, const html_content *content)
{
    if (align == CSS_VERTICAL_ALIGN_MIDDLE) {
        int ex = FIXTOINT(css_unit_len2device_px(parent_style, &content->unit_len_ctx, INTTOFIX(1), CSS_UNIT_EX));
        return (height + ex) / 2;
    }

    /* Text buttons keep their label baseline even with the UA's clipping
     * style; applying the ordinary clipped-inline-block rule sinks nearby text. */
    bool text_button = box->gadget && (box->gadget->type == GADGET_BUTTON ||
        box->gadget->type == GADGET_SUBMIT || box->gadget->type == GADGET_RESET);
    int baseline;
    /* CSS 2.1 17.5.3: an inline table exports its first row's baseline. */
    if ((lh__box_is_flex_container(box) || lh__box_is_grid_container(box) || box->type == BOX_TABLE) &&
        (box->flags & HAS_BASELINE))
        return box->margin[TOP] + box->border[TOP].width + box->baseline;
    if (box->type == BOX_INLINE_BLOCK && !lh__box_is_object(box) &&
        (text_button || (css_computed_overflow_x(box->style) == CSS_OVERFLOW_VISIBLE &&
                            css_computed_overflow_y(box->style) == CSS_OVERFLOW_VISIBLE)) &&
        layout_last_line_baseline(box, &baseline)) {
        return box->margin[TOP] + box->border[TOP].width + baseline;
    }
    return height;
}

/**
 * Measure the baseline and line height for mixed text styles or inline objects.
 * The parent's zero-width strut contributes ascent and descent even if there
 * is no text. Top/bottom-aligned objects contribute height without extending
 * the baseline; middle alignment contributes on both sides of the baseline.
 */
static bool layout_line_alignment_metrics(struct box *first, struct box *end, bool force, const html_content *content,
    int *height, int *baseline, struct line_baselines *baselines)
{
    const css_computed_style *parent_style = first->parent->parent->style;
    bool needs_metrics = force;
    for (struct box *b = first; b != end; b = b->next) {
        if (box_needs_line_metrics(&content->unit_len_ctx, b, parent_style) && !box_is_out_of_flow(b)) {
            needs_metrics = true;
            break;
        }
    }
    if (!needs_metrics)
        return false;

    int strut_height = line_height(&content->unit_len_ctx, parent_style);
    int above = layout_text_baseline(content, parent_style, strut_height, baselines);
    int below = strut_height - above;
    int aligned_height = strut_height;

    for (struct box *b = first; b != end; b = b->next) {
        if (lh__box_is_float_box(b) || box_is_out_of_flow(b) || !b->style)
            continue;
        css_fixed_or_calc value = (css_fixed_or_calc)0;
        css_unit unit = CSS_UNIT_PX;
        uint8_t align = css_computed_vertical_align(b->style, &value, &unit);
        bool object = box_needs_inline_alignment(b);
        /* Closing markers receive their opening box's height in pass 2.
         * During pass 1 that field may still be AUTO (or from an old reflow). */
        int h = b->type == BOX_INLINE_END ? line_height(&content->unit_len_ctx, b->style) : b->height;
        if (object) {
            h += b->margin[TOP] + b->border[TOP].width + b->padding[TOP] + b->padding[BOTTOM] +
                b->border[BOTTOM].width + b->margin[BOTTOM];
        } else if (b->type != BOX_TEXT && b->type != BOX_INLINE && b->type != BOX_INLINE_END) {
            continue;
        }
        if (align == CSS_VERTICAL_ALIGN_BASELINE || align == CSS_VERTICAL_ALIGN_INHERIT ||
            (object && align == CSS_VERTICAL_ALIGN_MIDDLE)) {
            int a = object ? layout_atomic_baseline(b, h, align, parent_style, content) :
                             layout_text_baseline(content, b->style, h, baselines);
            above = max(above, a);
            below = max(below, h - a);
        } else {
            aligned_height = max(aligned_height, h);
        }
    }
    *height = max(*height, max(above + below, aligned_height));
    *baseline = above;
    return true;
}


/* A legal break, either before box or inside its text. Text prefix widths are
 * measured only if this fallback is used, not for every fitting text box. */
struct inline_line_break {
    struct box *box;
    size_t offset;
    int x;
    int left, right;
    int clear_level;
    int place_below_level;
};

static struct inline_line_break layout_inline_break(
    struct box *box, size_t offset, int x, int left, int right, struct box *cont)
{
    return (struct inline_line_break){box, offset, x, left, right, cont->clear_level, cont->cached_place_below_level};
}

/* Floats encountered after a rolled-back break belong to the next attempt.
 * Restore the placement constraints too; retaining either would make the
 * retried float constrain its own placement or enter the list twice. */
static void layout_line_rollback_floats(struct box *first, struct box *end, struct box *cont,
    const struct inline_line_break *point)
{
    for (struct box *box = first; box != end; box = box->next) {
        if (!lh__box_is_float_box(box))
            continue;
        struct box **link = &cont->float_children;
        while (*link && *link != box)
            link = &(*link)->next_float;
        if (*link) {
            *link = box->next_float;
            box->next_float = NULL;
        }
    }
    cont->clear_level = point->clear_level;
    cont->cached_place_below_level = point->place_below_level;
}

/**
 * Position a line of boxes in inline formatting context.
 *
 * \param  first   box at start of line
 * \param  width   available width on input, updated with actual width on output
 *                 (may be incorrect if the line gets split?)
 * \param  y	   coordinate of top of line, updated on exit to bottom
 * \param  cx	   coordinate of left of line relative to cont
 * \param  cy	   coordinate of top of line relative to cont
 * \param  cont	   ancestor box which defines horizontal space, for floats
 * \param  indent  apply any first-line indent
 * \param  has_text_children  at least one TEXT in the inline_container
 * \param  record_baseline  retain the last line baseline for an inline-block ancestor
 * \param  record_first_baseline  retain the block's first line for marker/flex alignment
 * \param  next_box  updated to first box for next line, or 0 at end
 * \param  content  memory pool for any new boxes
 * \return  true on success, false on memory exhaustion
 */
static bool layout_line(struct box *first, int *width, int *y, int cx, int cy, struct box *cont, bool indent,
    bool has_text_children, bool record_baseline, bool record_first_baseline,
    html_content *content, struct box **next_box)
{
    int height, used_height;
    int float_advance = 0;
    int x0 = 0;
    int x1 = *width;
    int x, h;
    int fy = cy;
    struct box *left;
    struct box *right;
    struct box *b;
    struct box *d;
    struct box *br_box = 0;
    bool move_y = false;
    bool place_below = false;
    struct box *owner = first->parent->parent;
    bool outside_marker = box_has_outside_marker(owner) && !(owner->flags & HAS_BASELINE);
    bool needs_line_metrics = outside_marker;
    bool has_content = false, after_space = false;
    int space_after = 0;
    struct inline_wrap_state wrap_state = {0};
    struct inline_line_break last_break = {0}, first_inline = {0};
    struct box *retry_left = NULL, *retry_right = NULL;
    unsigned int i;
    const struct gui_layout_table *font_func = content->font_func;
    plot_font_style_t fstyle;
    struct line_baselines baselines = {0};

    NSLOG(layout, DEBUG, "first %p, first->text '%.*s', width %i, y %i, cx %i, cy %i", first, (int)first->length,
        first->text, *width, *y, cx, cy);

    /* find sides at top of line */
    x0 += cx;
    x1 += cx;
    find_sides(cont->float_children, cy, cy, &x0, &x1, &left, &right);
    x0 -= cx;
    x1 -= cx;

    if (indent)
        x0 += layout_text_indent(&content->unit_len_ctx, first->parent->parent->style, *width);

    if (x1 < x0)
        x1 = x0;

    /* get minimum line height from containing block.
     * this is the line-height if there are text children and also in the
     * case of an initially empty text input */
    if (has_text_children || first->parent->parent->gadget)
        used_height = height = line_height(&content->unit_len_ctx, first->parent->parent->style);
    else
        /* inline containers with no text are usually for layout and
         * look better with no minimum line-height */
        used_height = height = 0;

    /* pass 1: find height of line assuming sides at top of line. Prepare the
     * entire last unbreakable run, including any overflowing closing edges.
     * Loop body executed at least once;
     * keep in sync with the loop in layout_minmax_line() */

    NSLOG(layout, DEBUG, "x0 %i, x1 %i, x1 - x0 %i", x0, x1, x1 - x0);


    for (x = 0, b = first; b != NULL; b = b->next) {
        bool boundary = layout_inline_break_before(&wrap_state, b);
        if (x > x1 - x0 && boundary)
            break;
        struct css_size min_width, min_height;
        int max_width, max_height;

        assert(lh__box_is_inline_content(b));

        NSLOG(layout, DEBUG, "pass 1: b %p, x %i", b, x);

        if (b->type == BOX_BR)
            break;

        if (lh__box_is_float_box(b)) {
            /* Floats do not contribute to line dimensions. Lay out their
             * contents in pass 2, after setting the float container, when
             * their size is needed for placement. Doing it here as well
             * repeats the entire subtree at each level of nested floats. */
            b->width = 0;
            continue;
        }
        if (lh__box_is_inline_positioned(b))
            continue;

        assert(b->style != NULL);
        needs_line_metrics |= box_needs_line_metrics(&content->unit_len_ctx, b, first->parent->parent->style);
        font_plot_style_from_css(content, b->style, &fstyle);

        x += space_after;
        space_after = layout_inline_space(b, &has_content, &after_space, font_func, content);

        if (lh__box_is_atomic_inline(b)) {
            /* minmax MUST have been calculated by parent's minmax pass */
            assert(b->max_width != UNKNOWN_MAX_WIDTH && "inline-level block reached layout_line without minmax");

            if (!layout_float(b, *width, content))
                return false;

            /* width MUST be valid after layout_float */
            assert(b->width >= 0 && "inline-level block has negative width after layout_float");
            h = b->border[TOP].width + b->padding[TOP] + b->height + b->padding[BOTTOM] + b->border[BOTTOM].width;
            if (height < h)
                height = h;
            x += b->margin[LEFT] + b->border[LEFT].width + b->padding[LEFT] + b->width + b->padding[RIGHT] +
                b->border[RIGHT].width + b->margin[RIGHT];
            continue;
        }

        if (b->type == BOX_INLINE) {
            /* calculate borders, margins, and padding */
            layout_find_dimensions(&content->unit_len_ctx, *width, -1, b,
                &(struct layout_dimension_outputs){
                    .margin = b->margin,
                    .padding = b->padding,
                    .border = b->border,
                });
            for (i = 0; i != 4; i++)
                if (b->margin[i] == AUTO)
                    b->margin[i] = 0;
            x += b->margin[LEFT] + b->border[LEFT].width + b->padding[LEFT];
            if (b->inline_end) {
                b->inline_end->margin[RIGHT] = b->margin[RIGHT];
                b->inline_end->padding[RIGHT] = b->padding[RIGHT];
                b->inline_end->border[RIGHT] = b->border[RIGHT];
            } else {
                x += b->padding[RIGHT] + b->border[RIGHT].width + b->margin[RIGHT];
            }
        } else if (b->type == BOX_INLINE_END) {
            b->width = 0;
            x += b->padding[RIGHT] + b->border[RIGHT].width + b->margin[RIGHT];
            continue;
        }

        if (lh__box_is_replace(b) == false) {
            /* inline non-replaced, 10.3.1 and 10.6.1 */
            b->height = line_height(&content->unit_len_ctx, b->style ? b->style : b->parent->parent->style);
            if (height < b->height)
                height = b->height;

            if (!b->text) {
                b->width = 0;
                continue;
            }

            if (b->width == UNKNOWN_WIDTH) {
                /** \todo handle errors */

                /* If it's a select element, we must use the
                 * width of the widest option text */
                if (b->parent->parent->gadget && b->parent->parent->gadget->type == GADGET_SELECT) {
                    int opt_maxwidth = 0;
                    struct form_option *o;

                    for (o = b->parent->parent->gadget->data.select.items; o; o = o->next) {
                        int opt_width;
                        font_func->width(&fstyle, o->text, strlen(o->text), &opt_width);

                        if (opt_maxwidth < opt_width)
                            opt_maxwidth = opt_width;
                    }
                    b->width = opt_maxwidth;
                    if (nsoption_bool(core_select_menu))
                        b->width += SCROLLBAR_WIDTH;
                } else {
                    font_func->width(&fstyle, b->text, b->length, &b->width);
                    b->flags |= MEASURED;
                }
            }

            /* If the current text has not been measured (i.e. its
             * width was estimated after splitting), and it appears to fit on
             * the line (including an exact fit), measure it properly, so next box is placed
             * correctly and to detect overflow (the estimate can be
             * wrong for variable-width fonts and Unicode text). */
            if (b->text && (x + b->width <= x1 - x0) && !(b->flags & MEASURED)) {
                font_func->width(&fstyle, b->text, b->length, &b->width);
                b->flags |= MEASURED;
            }

            x += b->width;
            continue;
        }

        /* inline replaced, 10.3.2 and 10.6.2 */
        assert(b->style);

        layout_find_dimensions(&content->unit_len_ctx, *width, -1, b,
            &(struct layout_dimension_outputs){
                .width = &b->width,
                .height = &b->height,
                .max_width = &max_width,
                .min_width = &min_width,
                .max_height = &max_height,
                .min_height = &min_height,
            });

        if (b->object && !(b->flags & REPLACE_DIM)) {
            layout_get_object_dimensions(b, &b->width, &b->height,
                &(struct layout_size_limits){
                    .min_width = min_width,
                    .max_width = max_width,
                    .min_height = min_height,
                    .max_height = max_height,
                },
                *width /* containing block / available line width */);
        } else if (b->flags & IFRAME) {
            /* TODO: should we look at the content dimensions? */
            if (b->width == AUTO)
                b->width = 400;
            if (b->height == AUTO)
                b->height = 300;

            /* We reformat the iframe browser window to new
             * dimensions in pass 2 */
        } else {
            /* form control with no object */
            if (b->width == AUTO)
                b->width = FIXTOINT(css_unit_len2device_px(b->style, &content->unit_len_ctx, INTTOFIX(1), CSS_UNIT_EM));
            if (b->height == AUTO)
                b->height = FIXTOINT(
                    css_unit_len2device_px(b->style, &content->unit_len_ctx, INTTOFIX(1), CSS_UNIT_EM));
        }

        /* Reformat object to new box size */
        if (b->object && content_can_reformat(b->object)) {
            /* content_reformat skips unchanged viewports, including height.
             * The child document's content extent is not an intrinsic size. */
            content_reformat(b->object, false, b->width, b->height);
        }

        if (height < b->height)
            height = b->height;

        x += b->width;
    }

    /* Include the strut descent when determining which floats this line meets. */
    int baseline;
    if (needs_line_metrics && layout_line_alignment_metrics(
            first, b, outside_marker && (height > 0 || has_text_children),
            content, &height, &baseline, &baselines) && outside_marker) {
        height += layout_align_outside_marker(owner, content, first->parent->y + *y, baseline, &baselines);
    }

    /* find new sides using this height */
    x0 = cx;
    x1 = cx + *width;
    find_sides(cont->float_children, cy, cy + height, &x0, &x1, &left, &right);
    x0 -= cx;
    x1 -= cx;

    if (indent)
        x0 += layout_text_indent(&content->unit_len_ctx, first->parent->parent->style, *width);

    if (x1 < x0)
        x1 = x0;

    struct box *measured_end = b;
    space_after = 0;
    has_content = after_space = false;
    wrap_state = (struct inline_wrap_state){0};

    /* pass 2: place boxes in line: loop body executed at least once */

    NSLOG(layout, DEBUG, "x0 %i, x1 %i, x1 - x0 %i", x0, x1, x1 - x0);

    for (x = 0, b = first; b; b = b->next) {
        bool boundary = layout_inline_break_before(&wrap_state, b);
        if (boundary) {
            if (x > x1 - x0)
                break; /* The first unbreakable run overflows as a unit. */
            last_break = layout_inline_break(b, 0, x, x0, x1, cont);
        }
        if (b == measured_end && b->type != BOX_BR)
            break;

        NSLOG(layout, DEBUG, "pass 2: b %p, x %i", b, x);

        if (lh__box_is_inline_positioned(b)) {
            b->x = x + space_after;

        } else if (lh__box_is_inline_flow(b)) {
            assert(b->width != UNKNOWN_WIDTH);

            if (first_inline.box == NULL) {
                first_inline = layout_inline_break(b, 0, x, x0, x1, cont);
                retry_left = left;
                retry_right = right;
            }
            x += space_after;
            b->x = x;

            if ((b->type == BOX_INLINE && !b->inline_end) || lh__box_is_atomic_inline(b)) {
                b->x += b->margin[LEFT] + b->border[LEFT].width;
                x = b->x + b->padding[LEFT] + b->width + b->padding[RIGHT] + b->border[RIGHT].width + b->margin[RIGHT];
            } else if (b->type == BOX_INLINE) {
                b->x += b->margin[LEFT] + b->border[LEFT].width;
                x = b->x + b->padding[LEFT] + b->width;
            } else if (b->type == BOX_INLINE_END) {
                b->height = b->inline_end->height;
                x += b->padding[RIGHT] + b->border[RIGHT].width + b->margin[RIGHT];
            } else {
                x += b->width;
            }
            /* Log TEXT box positioning */
            if (b->text && b->length > 0) {
                NSLOG(wisp, INFO, "TEXT_POSITION: '%.*s' x=%d y=%d width=%d (parent=%p parent->y=%d cy=%d)",
                    (int)b->length, b->text, b->x, b->parent ? b->parent->y : -1, b->width, b->parent,
                    b->parent ? b->parent->y : -1, cy);
            }

            space_after = layout_inline_space(b, &has_content, &after_space, font_func, content);
            move_y = true;

            bool text_wrap = b->text && b->length && !layout_inline_atomic(b) && layout_inline_wraps(b);
            if (x > x1 - x0) {
                size_t split = 0;
                int w = 0;
                if (text_wrap) {
                    font_plot_style_from_css(content, b->style, &fstyle);
                    font_func->split(&fstyle, b->text, b->length, x1 - x0 - b->x, &split, &w);
                    if (split == b->length) {
                        x += w - b->width;
                        b->width = w;
                        b->flags |= MEASURED;
                    }
                }
                if (split && split < b->length &&
                    (b->x + w <= x1 - x0 || (!last_break.box && !retry_left && !retry_right))) {
                    if (!layout_text_box_split(content, &fstyle, b, split, w))
                        return false;
                    x = b->x + w;
                    b = b->next;
                    break;
                }
                if (x > x1 - x0 && last_break.box) {
                    struct box *end = b->next;
                    b = last_break.box;
                    x = last_break.x;
                    if (last_break.offset) {
                        font_plot_style_from_css(content, b->style, &fstyle);
                        font_func->width(&fstyle, b->text, last_break.offset, &w);
                        if (!layout_text_box_split(content, &fstyle, b, last_break.offset, w))
                            return false;
                        x += w;
                        b = b->next;
                    }
                    layout_line_rollback_floats(b, end, cont, &last_break);
                    x0 = last_break.left;
                    x1 = last_break.right;
                    break;
                }
            }
            if (x <= x1 - x0 && text_wrap) {
                /* This whole text fits. Retain its last internal opportunity
                 * in case a joined word/closing edge overflows later. */
                size_t offset = b->length;
                while (offset > 0 && b->text[offset - 1] != ' ')
                    offset--;
                if (offset > 1)
                    last_break = layout_inline_break(b, offset - 1, b->x, x0, x1, cont);
            }
        } else if (b->type == BOX_BR) {
            b->x = x;
            b->width = 0;
            br_box = b;
            b = b->next;
            move_y = true;
            break;

        } else {
            /* float */
            NSLOG(layout, DEBUG, "float %p", b);

            d = b->children;
            d->float_children = 0;
            d->cached_place_below_level = 0;
            b->float_container = d->float_container = cont;

            if (!layout_float(d, *width, content))
                return false;

            NSLOG(layout, DEBUG, "%p : %d %d", d, d->margin[TOP], d->border[TOP].width);

            d->x = d->margin[LEFT] + d->border[LEFT].width;
            d->y = d->margin[TOP] + d->border[TOP].width;
            b->width = d->margin[LEFT] + d->border[LEFT].width + d->padding[LEFT] + d->width + d->padding[RIGHT] +
                d->border[RIGHT].width + d->margin[RIGHT];
            b->height = d->margin[TOP] + d->border[TOP].width + d->padding[TOP] + d->height + d->padding[BOTTOM] +
                d->border[BOTTOM].width + d->margin[BOTTOM];

            if (b->width > (x1 - x0) - x)
                place_below = true;
            if (d->style &&
                (css_computed_clear(d->style) == CSS_CLEAR_NONE ||
                    (css_computed_clear(d->style) == CSS_CLEAR_LEFT && left == 0) ||
                    (css_computed_clear(d->style) == CSS_CLEAR_RIGHT && right == 0) ||
                    (css_computed_clear(d->style) == CSS_CLEAR_BOTH && left == 0 && right == 0)) &&
                (!place_below || (left == 0 && right == 0 && x == 0)) &&
                (!cont->float_children || (cy >= cont->clear_level && cy >= cont->cached_place_below_level))) {
                /* + not cleared or,
                 *   cleared and there are no floats to clear
                 * + fits without needing to be placed below or,
                 *   this line is empty with no floats
                 * + current y, cy, is below the clear level
                 *
                 * Place on this line, but only a positive outer height
                 * shortens it. Keep all floats in the placement/clear list. */
                if (b->type == BOX_FLOAT_LEFT) {
                    b->x = cx + x0;
                    if (b->height > 0) {
                        if (b->width > 0)
                            x0 += b->width;
                        left = b;
                    }
                } else {
                    b->x = cx + x1 - b->width;
                    if (b->height > 0) {
                        if (b->width > 0)
                            x1 -= b->width;
                        right = b;
                    }
                }
                b->y = cy;
            } else {
                /* cleared or doesn't fit on line */
                /* place below into next available space */
                int fcy = cy;
                if (cont->float_children)
                    fcy = max(fcy, max(cont->clear_level, cont->cached_place_below_level));
                fy = (fy > fcy) ? fy : fcy;
                fy = (fy == cy) ? fy + height : fy;

                place_float_below(b, *width, cx, fy, cont);
                fy = b->y;
                if (d->style &&
                    ((css_computed_clear(d->style) == CSS_CLEAR_LEFT && left != 0) ||
                        (css_computed_clear(d->style) == CSS_CLEAR_RIGHT && right != 0) ||
                        (css_computed_clear(d->style) == CSS_CLEAR_BOTH && (left != 0 || right != 0)))) {
                    /* to be cleared below existing
                     * floats */
                    if (b->type == BOX_FLOAT_LEFT)
                        b->x = cx;
                    else
                        b->x = cx + *width - b->width;

                    /* left/right already identify a float on the cleared side. */
                    layout_clear(cont->float_children, css_computed_clear(d->style), &fcy);
                    if (fcy > cont->clear_level)
                        cont->clear_level = fcy;
                    if (b->y < fcy)
                        b->y = fcy;
                }
                if (b->type == BOX_FLOAT_LEFT)
                    left = b;
                else
                    right = b;
            }
            add_float_to_container(cont, b);
        }
    }

    if (x1 - x0 < x && first_inline.box && (retry_left || retry_right)) {
        /* Retry the entire first run at the nearest float bottom. Leading
         * floats have already been placed and stay above the retried text.
         * An interposed float cannot push its preceding unbreakable content
         * below itself: that run overflows alongside it instead. */
        if (!retry_left)
            float_advance = retry_right->y + retry_right->height - cy;
        else if (!retry_right)
            float_advance = retry_left->y + retry_left->height - cy;
        else
            float_advance = min(retry_left->y + retry_left->height, retry_right->y + retry_right->height) - cy;
        float_advance = max(0, float_advance);
        layout_line_rollback_floats(first_inline.box, b, cont, &first_inline);
        b = first_inline.box;
        x = first_inline.x;
        x0 = first_inline.left;
        x1 = first_inline.right;
        br_box = NULL;
        move_y = true;
    }

    /* Empty inline markers may have consumed a space before we discovered
     * the line break. Remove it before text alignment, and move those markers
     * back with it so their borders/backgrounds end at the trimmed position. */
    struct box *trailing_space = layout_trailing_space(first, b);
    if (trailing_space) {
        x -= trailing_space->space;
        for (d = trailing_space->next; d != b; d = d->next) {
            if (lh__box_is_inline_flow(d) && !box_is_out_of_flow(d))
                d->x -= trailing_space->space;
        }
    }

    /* CSS Text 3 section 6.1: an overflowing line is start-aligned.
     * Use the available line interval, after floats and indentation, and
     * keep the ordinary alignment when the trimmed contents fit. */
    enum css_text_align_e align = css_computed_text_align(owner->style);
    if (x > x1 - x0)
        align = CSS_TEXT_ALIGN_DEFAULT;

    /* set positions */
    switch (align) {
    case CSS_TEXT_ALIGN_RIGHT:
    case CSS_TEXT_ALIGN_LIBCSS_RIGHT:
        x0 = x1 - x;
        break;
    case CSS_TEXT_ALIGN_CENTER:
    case CSS_TEXT_ALIGN_LIBCSS_CENTER:
        x0 = (x0 + (x1 - x)) / 2;
        break;
    case CSS_TEXT_ALIGN_LEFT:
    case CSS_TEXT_ALIGN_LIBCSS_LEFT:
    case CSS_TEXT_ALIGN_JUSTIFY:
        /* leave on left */
        break;
    case CSS_TEXT_ALIGN_DEFAULT:
        /* None; consider text direction */
        switch (css_computed_direction(owner->style)) {
        case CSS_DIRECTION_LTR:
            /* leave on left */
            break;
        case CSS_DIRECTION_RTL:
            x0 = x1 - x;
            break;
        }
        break;
    }

    bool line_has_content = false;
    for (d = first; d != b; d = d->next) {
        d->flags &= ~NEW_LINE;

        if (!line_has_content && !box_is_out_of_flow(d) && !lh__box_is_float_box(d) &&
            (layout_inline_has_content(d) || d->type == BOX_TEXT || d->type == BOX_BR ||
                ((d->type == BOX_INLINE || d->type == BOX_INLINE_END) &&
                    box_inline_has_edges(&content->unit_len_ctx, d))))
            line_has_content = true;

        if (lh__box_is_inline_positioned(d)) {
            /* Finish the static anchor after the line height is known. */
            continue;
        } else if ((d->type == BOX_INLINE && lh__box_is_replace(d) == false) || d->type == BOX_BR ||
            d->type == BOX_TEXT || d->type == BOX_INLINE_END) {
            /* regular (non-replaced) inlines */
            d->x += x0;
            d->y = *y - d->padding[TOP];

            if (d->type == BOX_TEXT && d->height > used_height) {
                /* text */
                used_height = d->height;
            }
        } else if (d->type == BOX_INLINE || lh__box_is_atomic_inline(d)) {
            /* Replaced inlines and atomic inline containers. */
            d->x += x0;
            d->y = *y + d->border[TOP].width + d->margin[TOP];
            h = d->margin[TOP] + d->border[TOP].width + d->padding[TOP] + d->height + d->padding[BOTTOM] +
                d->border[BOTTOM].width + d->margin[BOTTOM];
            if (used_height < h)
                used_height = h;
        }
    }

    first->flags |= NEW_LINE;

    assert(b != first || (move_y && 0 < float_advance && (left || right)));

    /* A marker-only inline formatting context still has a strut. Do not claim
     * an empty attempt beside a float when actual text is waiting on a later
     * line, or when a following block must provide the marker's baseline. */
    if (outside_marker && !has_text_children && b == NULL && box_has_outside_marker_line(owner)) {
        line_has_content = true;
        move_y = true;
    }

    /* CSS 2.1 9.4.2: undecorated inline fragments alone do not create a
     * line. Text waiting on a later line must not give these fragments the
     * container's strut. Empty BOX_TEXT nodes still preserve explicit newlines.
     * Keep the distance to retry below a float separate from line height. */
    if (!line_has_content)
        used_height = 0;

    /* Recompute from boxes actually placed on this line after wrapping. */
    bool aligned_line = line_has_content && needs_line_metrics &&
        layout_line_alignment_metrics(first, b, outside_marker && line_has_content,
            content, &used_height, &baseline, &baselines);

    if (outside_marker && aligned_line && line_has_content && move_y) {
        int push = layout_align_outside_marker(owner, content, first->parent->y + *y, baseline, &baselines);
        *y += push;
        cy += push;
        for (d = first; push && d != b; d = d->next) {
            if (lh__box_is_float_box(d))
                layout_shift_float(d, push);
            else if (!box_is_out_of_flow(d))
                d->y += push;
        }
    }

    /* handle vertical-align by adjusting box y values */
    /** \todo  proper vertical alignment handling */
    bool preceding_content = false;
    for (d = first; d != b; d = d->next) {
        if (lh__box_is_inline_positioned(d)) {
            /* CSS 2.1 10.3.7/10.6.4: inline static positions follow the
             * insertion point; a hypothetical block starts at the block's
             * content edge, below any preceding content on this line.
             * Neither kind interrupts the actual in-flow text. */
            switch (ns_computed_display_static(d->style)) {
            case CSS_DISPLAY_INLINE:
            case CSS_DISPLAY_INLINE_BLOCK:
            case CSS_DISPLAY_INLINE_FLEX:
            case CSS_DISPLAY_INLINE_GRID:
            case CSS_DISPLAY_INLINE_TABLE:
                d->x += x0;
                d->y = *y;
                break;
            default:
                d->x = 0;
                d->y = *y + (preceding_content ? used_height : 0);
                break;
            }
            continue;
        }
        preceding_content |= layout_inline_has_content(d);
        if (aligned_line && box_needs_inline_alignment(d) && !box_is_out_of_flow(d)) {
            css_fixed_or_calc value = (css_fixed_or_calc)0;
            css_unit unit = CSS_UNIT_PX;
            h = d->margin[TOP] + d->border[TOP].width + d->padding[TOP] + d->height + d->padding[BOTTOM] +
                d->border[BOTTOM].width + d->margin[BOTTOM];
            uint8_t align = css_computed_vertical_align(d->style, &value, &unit);
            switch (align) {
            case CSS_VERTICAL_ALIGN_BASELINE:
            case CSS_VERTICAL_ALIGN_INHERIT:
            case CSS_VERTICAL_ALIGN_MIDDLE:
                d->y += baseline - layout_atomic_baseline(d, h, align, first->parent->parent->style, content);
                break;
            case CSS_VERTICAL_ALIGN_BOTTOM:
                d->y += used_height - h;
                break;
            default:
                break;
            }
            continue;
        }
        if ((d->type == BOX_INLINE && d->inline_end) || d->type == BOX_BR || d->type == BOX_TEXT ||
            d->type == BOX_INLINE_END) {
            css_fixed_or_calc value = (css_fixed_or_calc)0;
            css_unit unit = CSS_UNIT_PX;
            switch (css_computed_vertical_align(d->style, &value, &unit)) {
            case CSS_VERTICAL_ALIGN_SUPER:
            case CSS_VERTICAL_ALIGN_TOP:
            case CSS_VERTICAL_ALIGN_TEXT_TOP:
                /* already at top */
                break;
            case CSS_VERTICAL_ALIGN_SUB:
            case CSS_VERTICAL_ALIGN_BOTTOM:
            case CSS_VERTICAL_ALIGN_TEXT_BOTTOM:
                d->y += used_height - d->height;
                break;
            default:
            case CSS_VERTICAL_ALIGN_BASELINE:
                if (aligned_line)
                    d->y += baseline - layout_text_baseline(content, d->style, d->height, &baselines);
                else
                    d->y += 0.75 * (used_height - d->height);
                break;
            }
        }
    }

    if ((record_baseline || record_first_baseline) && line_has_content && move_y) {
        if (!aligned_line) {
            /* Preserve the actual baseline of the existing text-only path. */
            for (d = first; d != b; d = d->next) {
                css_fixed_or_calc value = (css_fixed_or_calc)0;
                css_unit unit = CSS_UNIT_PX;
                if (d->type == BOX_TEXT && d->length &&
                    css_computed_vertical_align(d->style, &value, &unit) == CSS_VERTICAL_ALIGN_BASELINE) {
                    baseline = d->y - *y + layout_text_baseline(content, d->style, d->height, &baselines);
                    break;
                }
            }
            if (d == b)
                baseline = layout_text_baseline(content, first->parent->parent->style, used_height, &baselines);
        }
        if (record_baseline) {
            first->parent->baseline = *y + baseline;
            first->parent->flags |= HAS_BASELINE;
        }
        if (record_first_baseline && !(owner->flags & HAS_BASELINE)) {
            owner->baseline = first->parent->y + *y + baseline;
            owner->flags |= HAS_BASELINE;
        }
    }

    /* handle clearance for br */
    if (br_box && css_computed_clear(br_box->style) != CSS_CLEAR_NONE) {
        int clear_y;
        if (layout_clear(cont->float_children, css_computed_clear(br_box->style), &clear_y) &&
            used_height < clear_y - cy)
            used_height = clear_y - cy;
    }

    if (move_y)
        *y += max(used_height, float_advance);
    *next_box = b;
    *width = x; /* return actual width */
    return true;
}


static bool layout_can_reuse_inline_layout(const struct box *box)
{
    if (!layout_can_reuse_inline_intrinsics(box))
        return false;
    /* Cached inline containers retain only their last baseline. First-baseline
     * layout must republish its block baseline after ancestor positions
     * and marker fonts have been resolved. Intrinsic widths remain reusable. */
    if (layout_needs_first_baseline(box->parent))
        return false;
    for (const struct box *child = box->children; child != NULL; child = child->next) {
        /* The final relative-position pass mutates inline coordinates. BR
         * clearance can depend on the line's position even with no floats. */
        if (child->style == NULL || css_computed_position(child->style) != CSS_POSITION_STATIC ||
            (child->type == BOX_BR && css_computed_clear(child->style) != CSS_CLEAR_NONE))
            return false;
    }
    return true;
}

/**
 * Layout lines of text or inline boxes with floats.
 *
 * \param inline_container inline container box
 * \param width horizontal space available
 * \param cont ancestor box which defines horizontal space, for floats
 * \param cx box position relative to cont
 * \param cy box position relative to cont
 * \param content  memory pool for any new boxes
 * \return true on success, false on memory exhaustion
 */
static bool layout_inline_container(
    struct box *inline_container, int width, struct box *cont, int cx, int cy, html_content *content)
{
    bool first_line = true;
    bool has_text_children;
    struct box *c, *next;
    int y = 0;
    int curwidth, maxwidth = width;

    assert(inline_container->type == BOX_INLINE_CONTAINER);
    if (inline_container->flags & REUSE_INLINE_LAYOUT) {
        if (inline_container->inline_layout_width == width && cont->float_children == NULL) {
            /* No floats or positioned descendants: the saved coordinates are
             * local to this container, independent of its new parent placement.
             * Parent/ancestor styles and baseline requirements are unchanged. */
            inline_container->flags &= ~REUSE_INLINE_LAYOUT;
            inline_container->flags |= INLINE_LAYOUT_CURRENT;
            HTML_PROFILE(if (content->restyling) content->restyle_profile.inline_layout_reuses++);
            return true;
        }
        HTML_PROFILE(if (content->restyling) {
            if (inline_container->inline_layout_width != width)
                content->restyle_profile.inline_layout_width_misses++;
            else
                content->restyle_profile.inline_layout_float_misses++;
        });
        layout_discard_inline_layout(inline_container);
    }
    HTML_PROFILE(if (content->restyling && (inline_container->flags & INLINE_LAYOUT_VALID)) {
        if (inline_container->inline_layout_width == width)
            content->restyle_profile.inline_layout_repeat_same_width++;
        else
            content->restyle_profile.inline_layout_repeat_changed_width++;
    });
    if ((inline_container->flags & INLINE_LAYOUT_CURRENT) && inline_container->inline_layout_width == width &&
        cont->float_children == NULL) {
        /* Flex/grid can revisit the same text while resolving ancestor sizes.
         * Keep their sizing passes, but reuse these independent local lines.
         * The final positioning pass expires this permission before reflow. */
        HTML_PROFILE(content->restyle_profile.inline_layout_repeat_reuses++);
        return true;
    }
    inline_container->flags &= ~(INLINE_LAYOUT_VALID | INLINE_LAYOUT_CURRENT);
    HTML_PROFILE(if (content->restyling) content->restyle_profile.inline_layout_calculations++);
    inline_container->width = width;
    inline_container->flags &= ~HAS_BASELINE;

    /* Only inline-block ancestors need this cache. Avoid extra font metrics
     * calls for ordinary document paragraphs and unrelated formatting contexts. */
    struct box *ancestor = inline_container->parent;
    while (ancestor && ancestor->type == BOX_BLOCK)
        ancestor = ancestor->parent;
    bool record_baseline = ancestor && ancestor->type == BOX_INLINE_BLOCK;
    bool record_first_baseline = layout_needs_first_baseline(inline_container->parent);

    NSLOG(layout, DEBUG, "inline_container %p, width %i, cont %p, cx %i, cy %i", inline_container, width, cont, cx, cy);


    has_text_children = false;
    for (c = inline_container->children; c; c = c->next) {
        bool is_pre = false;

        box_rejoin_text(c);

        if (c->style) {
            enum css_white_space_e whitespace;

            whitespace = css_computed_white_space(c->style);

            is_pre = (whitespace == CSS_WHITE_SPACE_PRE || whitespace == CSS_WHITE_SPACE_PRE_LINE ||
                whitespace == CSS_WHITE_SPACE_PRE_WRAP);
        }

        if ((lh__box_is_object(c) == false && c->text && (c->length || is_pre)) || c->type == BOX_BR)
            has_text_children = true;
    }

    /** \todo fix wrapping so that a box with horizontal scrollbar will
     * shrink back to 'width' if no word is wider than 'width' (Or just set
     * curwidth = width and have the multiword lines wrap to the min width)
     */
    for (c = inline_container->children; c;) {

        fflush(stderr);

        NSLOG(layout, DEBUG, "c %p", c);

        /* Use the available width for layout, not inline_container->width which
         * may contain a stale min_width value from minmax calculation */
        curwidth = (inline_container->width > width) ? width : inline_container->width;
        if (!layout_line(c, &curwidth, &y, cx, cy + y, cont, first_line, has_text_children, record_baseline,
                record_first_baseline, content, &next))
            return false;
        maxwidth = max(maxwidth, curwidth);
        c = next;
        first_line = false;
    }

    inline_container->width = maxwidth;
    inline_container->height = y;
    HTML_PROFILE(if (content->restyling && !content->restyle.no_inline_layout_reuse) {
        if (!layout_can_reuse_inline_layout(inline_container))
            content->restyle_profile.inline_layout_other_exclusions++;
        else if (cont->float_children != NULL)
            content->restyle_profile.inline_layout_float_exclusions++;
    });
    if (!content->restyle.no_inline_layout_reuse && cont->float_children == NULL &&
        layout_can_reuse_inline_layout(inline_container)) {
        inline_container->inline_layout_width = width;
        inline_container->flags |= INLINE_LAYOUT_VALID;
        if (content->restyling && !content->restyle.no_intrinsic_reuse)
            inline_container->flags |= INLINE_LAYOUT_CURRENT;
    }

    /* Log inline container final dimensions */
    NSLOG(wisp, INFO, "INLINE_CONTAINER_DONE: ic=%p parent=%p ic_y=%d ic_height=%d (parent_list_marker=%p)",
        inline_container, inline_container->parent, inline_container->y, inline_container->height,
        inline_container->parent ? inline_container->parent->list_marker : NULL);

    return true;
}


/* One depth-first traversal frame, kept on the stack. cx/cy are coordinates
 * in the formatting-context root; y is the completed child's bottom in its
 * parent (temporarily reused for float avoidance while positioning the next box).
 * Margin lookahead and ancestor unwinding share the pending collapse chain. */
struct block_layout_state {
    int cx, cy, y;
    unsigned int max_pos_margin, max_neg_margin;
    struct box *margin_collapse; /* End of the pending adjoining-margin chain. */
    struct box *margin_top;      /* Parent or sibling that receives its offset. */
    bool in_margin;
    /* Margins visited by layout, excluding future edges found by lookahead. */
    unsigned int seen_pos_margin, seen_neg_margin;
    struct box *empty_margin_box; /* Outermost empty branch awaiting its own position. */
    bool margin_applied;          /* A parent has already taken this chain's offset. */
    struct box *clearance_box;    /* Lookahead found a required clearance boundary. */
    struct box *clearance_parent; /* Cleared empty margins cannot escape this bottom. */
    struct box *adjoining_parent; /* Positioned top still adjoining the first child chain. */
    struct {
        struct box *parent;
        struct box *clearance;     /* First child awaiting preceding float geometry. */
        unsigned int positive, negative;
    } provisional_top;            /* Consumed margins awaiting the clearance decision. */
};

static void layout_block_adjoining_parent(struct block_layout_state *state, struct box *box)
{
    if (box->type != BOX_BLOCK || !box->children || box->border[TOP].width || box->padding[TOP] ||
        box_style_creates_block_context(box) || lh__box_is_replace(box)) {
        state->adjoining_parent = NULL;
    } else if (!state->adjoining_parent || !box_is_descendant_of(box, state->adjoining_parent)) {
        state->adjoining_parent = box;
    }
}

static void layout_block_record_margin(struct block_layout_state *state, int margin)
{
    if (state->margin_applied || margin == AUTO)
        return;
    if (margin > 0)
        state->seen_pos_margin = max(state->seen_pos_margin, (unsigned int)margin);
    else if (margin < 0)
        state->seen_neg_margin = max(state->seen_neg_margin, -(unsigned int)margin);
}

/* The flow cursor has already advanced using the unshifted position. Moving
 * this empty branch now cannot change sibling spacing or ancestor heights.
 * Nested self-collapsing children share their parent's top edge. */
static void layout_block_finish_empty_margin(struct box *box, struct block_layout_state *state)
{
    if (box_is_out_of_flow(box))
        return;
    if (box == state->adjoining_parent)
        state->adjoining_parent = NULL;
    if (box == state->margin_collapse) {
        state->adjoining_parent = NULL;
        state->margin_applied = false;
        state->seen_pos_margin = state->seen_neg_margin = 0;
    }
    if (box == state->empty_margin_box) {
        assert(box->height == 0);
        box->y += (int)state->seen_pos_margin - (int)state->seen_neg_margin;
        state->empty_margin_box = NULL;
    }
    layout_block_record_margin(state, box->margin[BOTTOM]);
}

static void layout_block_log_box(const struct box *box, const struct box *block)
{
    const char *tag = "";
    const char *cls = "";
    dom_string *name = NULL;
    dom_string *class_attr = NULL;
    if (box->node != NULL) {
        if (dom_node_get_node_name(box->node, &name) == DOM_NO_ERR && name != NULL) {
            tag = dom_string_data(name);
        }
        if (dom_element_get_attribute(box->node, corestring_dom_class, &class_attr) == DOM_NO_ERR &&
            class_attr != NULL) {
            cls = dom_string_data(class_attr);
        }
    }
    /* Always log processing, not just when class exists */
    NSLOG(layout, DEBUG, "processing: tag %s class %s box %p width %i type %d", tag, cls, box, box->width, box->type);

    if (box->type == BOX_INLINE_GRID || box->type == BOX_INLINE_FLEX) {
        fprintf(stderr, "BLOCK_CTX: Visiting inline-level container %p type %d (parent %p)\n", (void *)box, box->type,
            (void *)block);
        fflush(stderr);
    }

    /* Include tag/class in Box metrics for easier debugging */
    NSLOG(layout, DEBUG, "Box metrics for %p (tag %s class %s): w=%d m=%d,%d,%d,%d p=%d,%d,%d,%d b=%d,%d,%d,%d", box,
        tag, cls, box->width, box->margin[TOP], box->margin[RIGHT], box->margin[BOTTOM], box->margin[LEFT],
        box->padding[TOP], box->padding[RIGHT], box->padding[BOTTOM], box->padding[LEFT], box->border[TOP].width,
        box->border[RIGHT].width, box->border[BOTTOM].width, box->border[LEFT].width);

    if (class_attr != NULL)
        dom_string_unref(class_attr);
    if (name != NULL)
        dom_string_unref(name);
}


/* Measure before positioning: table widths depend on available float space. */
static bool layout_block_size_child(struct box *box, struct box *block, int viewport_height, html_content *content,
    struct block_layout_state *state)
{
    int lm, rm;
    /* Width measurement must use the space below a float that this box
     * clears. Applying clearance to the margin chain happens afterward. */
    int clear_y = state->cy + (int)state->max_pos_margin - (int)state->max_neg_margin;
    if (state->clearance_box == box)
        layout_clear(block->float_children, css_computed_clear(box->style), &clear_y);

    /* Blocks establishing a block formatting context get minimum
     * left and right margins to avoid any floats. */
    lm = rm = 0;

    if (box->type == BOX_FLEX || box->type == BOX_GRID || box->type == BOX_BLOCK || box->flags & IFRAME ||
        box->type == BOX_INLINE_FLEX || box->type == BOX_INLINE_GRID) {
        {
            const char *tag = "";
            const char *cls = "";
            dom_string *name = NULL;
            dom_string *class_attr = NULL;
            if (box->node != NULL) {
                if (dom_node_get_node_name(box->node, &name) == DOM_NO_ERR && name != NULL) {
                    tag = dom_string_data(name);
                }
                if (dom_element_get_attribute(box->node, corestring_dom_class, &class_attr) == DOM_NO_ERR &&
                    class_attr != NULL) {
                    cls = dom_string_data(class_attr);
                }
            }

            if (!lh__box_is_object(box) && box->style && block->float_children &&
                box_style_creates_block_context(box)) {
                int top = state->cy + (int)state->max_pos_margin - (int)state->max_neg_margin;
                state->y = layout_block_avoid_floats(
                    &content->unit_len_ctx, box, viewport_height, block->float_children, state->cx, max(top, clear_y));
                /* Float avoidance below uses padding-edge coordinates. */
                state->y += box->border[TOP].width;
            } else {
                layout_block_find_dimensions(&content->unit_len_ctx, box->parent->width, viewport_height, 0, 0, box);
            }

            /* Detect if width is still the sentinel after dimension calculation */
            if (box->width == UNKNOWN_WIDTH || box->width > 100000000) {
                NSLOG(layout, WARNING,
                    "OVERFLOW_BUG: after layout_block_find_dimensions: box %p type=%d width=%d parent_width=%d tag=%s class=%s",
                    box, box->type, box->width, box->parent ? box->parent->width : -1, tag, cls);
            }

            if (class_attr != NULL)
                dom_string_unref(class_attr);
            if (name != NULL)
                dom_string_unref(name);
        }
        if (box->type == BOX_BLOCK && !(box->flags & IFRAME)) {
            layout_block_add_scrollbar(box, RIGHT);
            layout_block_add_scrollbar(box, BOTTOM);
        }
    } else if (box->type == BOX_TABLE) {
        if (box->style != NULL) {
            enum css_width_e wtype;
            css_fixed_or_calc width = (css_fixed_or_calc)0;
            css_unit unit = CSS_UNIT_PX;

            wtype = css_computed_width(box->style, &width, &unit);

            if (wtype == CSS_WIDTH_AUTO) {
                /* max available width may be
                 * diminished due to floats. */
                int x0, x1, top;
                struct box *left, *right;
                top = state->cy + (int)state->max_pos_margin - (int)state->max_neg_margin;
                top = max(top, clear_y);
                x0 = state->cx;
                x1 = state->cx + box->parent->width - box->parent->padding[LEFT] - box->parent->padding[RIGHT];
                find_sides(block->float_children, top, top, &x0, &x1, &left, &right);
                /* calculate min required left & right
                 * margins needed to avoid floats */
                lm = x0 - state->cx;
                rm = state->cx + box->parent->width - box->parent->padding[LEFT] - box->parent->padding[RIGHT] - x1;
            }
        }
        if (!layout_table(box, box->parent->width - lm - rm, content))
            return false;
        layout_solve_width(box, box->parent->width, box->width, lm, rm, -1, -1);
    }

    return true;
}


/* Refresh the lookahead result without changing whether its margin has
 * already been consumed. Callers own that transition and deferred decisions. */
static void layout_block_scan_margins(struct box *box, struct box *block, int viewport_height,
    const html_content *content, struct block_layout_state *state)
{
    const struct margin_scan scan = {
        .unit_len_ctx = &content->unit_len_ctx, .block = block, .viewport_height = viewport_height,
        .flow_y = state->cy, .mode = MARGIN_SCAN_FLOW, .boundary = state->clearance_parent,
        .adjoining_parent = state->adjoining_parent};
    struct margin_scan_result result = {.positive = state->max_pos_margin, .negative = state->max_neg_margin};
    state->margin_collapse = layout_next_margin_block(box, &scan, &result);
    state->max_pos_margin = result.positive;
    state->max_neg_margin = result.negative;
    state->margin_top = result.top;
    state->clearance_box = result.clearance;
}

/* Resolve a parent's top past float-only containers, bounded so later siblings
 * cannot move it. Stop before a potentially cleared child's margins and defer
 * that decision until the preceding floats have their final geometry. */
static bool layout_block_resolve_parent_top(struct box *box, struct box *block, int viewport_height,
    const html_content *content, struct block_layout_state *state)
{
    if (state->margin_top != box || !state->in_margin || !state->margin_collapse ||
        state->margin_collapse->type != BOX_INLINE_CONTAINER ||
        !box_inline_container_only_floats(&content->unit_len_ctx, state->margin_collapse))
        return false;

    const struct margin_scan scan = {
        .unit_len_ctx = &content->unit_len_ctx, .block = block, .viewport_height = viewport_height,
        .flow_y = state->cy, .mode = MARGIN_SCAN_TOP, .boundary = box};
    struct margin_scan_result result = {.positive = state->max_pos_margin, .negative = state->max_neg_margin};
    struct box *end = layout_next_margin_block(box, &scan, &result);
    state->provisional_top.clearance = result.pending_clearance;
    if (result.pending_clearance) {
        state->provisional_top.parent = box;
        state->provisional_top.positive = result.positive;
        state->provisional_top.negative = result.negative;
    }
    if (!end || (end->type == BOX_INLINE_CONTAINER &&
            box_inline_container_only_floats(&content->unit_len_ctx, end)))
        return false;

    state->max_pos_margin = result.positive;
    state->max_neg_margin = result.negative;
    if (end == box && !box_prevents_collapse_through(&content->unit_len_ctx, box)) {
        /* Its own bottom is still adjoining. Keep the flow cursor pending and
         * position this empty branch when it finishes. */
        state->margin_top = NULL;
        return false;
    }
    state->margin_collapse = end;
    return true;
}

/* Consuming the pending margin is one transition, whether its receiving edge
 * is a parent found by lookahead or the current in-flow child. */
static void layout_block_consume_margin(struct box *box, struct block_layout_state *state)
{
    state->cy += (int)state->max_pos_margin - (int)state->max_neg_margin;
    box->y += (int)state->max_pos_margin - (int)state->max_neg_margin;
    state->in_margin = false;
    state->max_pos_margin = state->max_neg_margin = 0;
    state->margin_applied = true;
    state->seen_pos_margin = state->seen_neg_margin = 0;
    state->clearance_parent = NULL;
    layout_block_adjoining_parent(state, box);
}

/* Clearance separates this top from the preceding chain. An empty box keeps
 * its own top/bottom margins adjoining, bounded by the cleared parent edge. */
static void layout_block_apply_clearance(struct box *box, struct box *block, int viewport_height,
    const html_content *content, struct block_layout_state *state)
{
    int floor = state->cy;
    layout_clear(block->float_children, css_computed_clear(box->style), &floor);
    bool empty = !box_prevents_collapse_through(&content->unit_len_ctx, box);
    struct margin_scan_result top = {0};
    if (empty) {
        const struct margin_scan scan = {
            .unit_len_ctx = &content->unit_len_ctx, .block = block, .viewport_height = viewport_height,
            .flow_y = state->cy, .mode = MARGIN_SCAN_TOP, .boundary = box};
        layout_next_margin_block(box, &scan, &top);
    }
    /* Empty boxes retain an adjoining top/bottom chain. Keep the cursor
     * above that chain so its top margin is not counted again below it. */
    int position = floor + box->border[TOP].width - ((int)top.positive - (int)top.negative);
    box->y += position - state->cy;
    state->cy = position;
    state->max_pos_margin = state->max_neg_margin = 0;
    state->seen_pos_margin = state->seen_neg_margin = 0;
    state->empty_margin_box = empty ? box : NULL;
    state->margin_applied = !empty;
    state->in_margin = empty;
    state->clearance_parent = empty ? box->parent : NULL;
    state->adjoining_parent = NULL;
    state->clearance_box = NULL;
    state->margin_top = NULL;
    state->margin_collapse = box;
    if (empty) {
        layout_block_scan_margins(box, block, viewport_height, content, state);
    }
}


/* A revised parent top can also expose different horizontal space beside
 * external floats. Replay the preceding float-only containers through ordinary
 * float placement, preserving external floats and never reading future boxes'
 * geometry from an earlier layout pass. This rare retry adds no persistent state. */
static bool layout_block_reposition_floats(struct box *parent, struct box *stop, struct box *block,
    html_content *content, int shift)
{
    if (!shift)
        return true;
    struct box *floats = block->float_children;
    block->float_children = NULL;
    block->clear_level = block->cached_place_below_level = 0;
    while (floats) {
        struct box *fl = floats;
        floats = fl->next_float;
        if (box_is_descendant_of(fl, parent))
            continue;
        add_float_to_container(block, fl);
        if (css_computed_clear(fl->children->style) != CSS_CLEAR_NONE)
            block->clear_level = max(block->clear_level, fl->y);
    }

    int cx = 0, cy = shift;
    for (struct box *ancestor = parent; ancestor != block; ancestor = ancestor->parent) {
        cx += ancestor->x;
        cy += ancestor->y;
    }
    struct box *box = parent->children;
    while (box != stop) {
        assert(box);
        cx += box->x;
        cy += box->y;
        if (box->type == BOX_INLINE_CONTAINER && box_inline_container_only_floats(&content->unit_len_ctx, box)) {
            if (!layout_inline_container(box, box->parent->width, block, cx, cy, content))
                return false;
        } else if (box->type == BOX_BLOCK && box->children && !box_is_out_of_flow(box) &&
            !box_style_creates_block_context(box)) {
            box = box->children;
            continue;
        }
        while (!box->next) {
            cx -= box->x;
            cy -= box->y;
            box = box->parent;
            assert(box != parent);
        }
        cx -= box->x;
        cy -= box->y;
        box = box->next;
    }
    return true;
}

/* Resume the bounded parent-top scan with the original adjoining margins.
 * No clearance means revising that parent's provisional position, rather than
 * applying the child's margins a second time inside it. */
static bool layout_block_resolve_clearance(struct box *box, struct box *block, int viewport_height,
    html_content *content, struct block_layout_state *state)
{
    struct box *parent = state->provisional_top.parent;
    assert(parent && box_is_descendant_of(box, parent));
    assert(state->margin_applied);
    int applied = (int)state->provisional_top.positive - (int)state->provisional_top.negative;
    const struct margin_scan scan = {
        .unit_len_ctx = &content->unit_len_ctx, .block = block, .viewport_height = viewport_height,
        .flow_y = state->cy - applied, .mode = MARGIN_SCAN_RESUME_TOP, .boundary = parent,
        .adjoining_parent = parent, .applied_margin = applied};
    struct margin_scan_result result = {
        .positive = state->provisional_top.positive, .negative = state->provisional_top.negative};
    struct box *end = layout_next_margin_block(box, &scan, &result);
    state->provisional_top.clearance = NULL;
    if (result.clearance == box) {
        layout_block_scan_margins(box, block, viewport_height, content, state);
        state->in_margin = true;
        state->margin_applied = false;
        return true;
    }

    int margin = (int)result.positive - (int)result.negative;
    if (!layout_block_reposition_floats(parent, box, block, content, margin - applied))
        return false;
    state->margin_collapse = end;
    state->margin_top = NULL;
    state->clearance_box = result.clearance;
    state->provisional_top.clearance = result.pending_clearance;
    state->provisional_top.positive = result.positive;
    state->provisional_top.negative = result.negative;
    if (end == parent && !box_prevents_collapse_through(&content->unit_len_ctx, parent)) {
        /* The whole parent collapses through. Restore the pending flow edge;
         * its own position is finalized separately from its following sibling. */
        parent->y -= applied;
        state->cy -= applied;
        state->max_pos_margin = state->seen_pos_margin = result.positive;
        state->max_neg_margin = state->seen_neg_margin = result.negative;
        state->empty_margin_box = parent;
        state->margin_applied = false;
        state->in_margin = true;
        state->adjoining_parent = NULL;
        /* The bounded top excludes the parent's own bottom. Resume ordinary
         * flow lookahead to include that edge and the following siblings. */
        layout_block_scan_margins(box, block, viewport_height, content, state);
    } else {
        parent->y += margin - applied;
        state->cy += margin - applied;
    }
    return true;
}


/* Resolve the pending margin chain, then apply dimensions and clearance. */
static bool layout_block_position_child(struct box *box, struct box *block, int viewport_height, html_content *content,
    struct block_layout_state *state)
{
    if (box->type == BOX_BLOCK)
        box->flags &= ~HAS_BASELINE;
    /* If we don't know which box the current margin collapses
     * through to, find out.  Update the pos/neg margin values. */
    if (state->margin_collapse == NULL) {
        layout_block_scan_margins(box, block, viewport_height, content, state);
        /* We have a margin that has not yet been applied. */
        state->in_margin = true;
        state->margin_applied = false;
    }

    bool resolved_parent_top = layout_block_resolve_parent_top(box, block, viewport_height, content, state);

    /* CSS 2.1 §8.3.1: parent/first-child collapse moves the parent,
     * while preceding self-collapsing siblings do not consume the margin.
     * Lookahead identifies both the end of the chain and its receiving top
     * edge; these can be different boxes. */
    if (state->margin_top == box && (state->margin_collapse != box || resolved_parent_top) && state->in_margin) {
        layout_block_consume_margin(box, state);
        state->margin_top = NULL;
    }

    /* Parent lookahead may have stopped here before the preceding floats
     * were placed. Their final geometry is now available for clearance. */
    if (state->provisional_top.clearance == box) {
        if (!layout_block_resolve_clearance(box, block, viewport_height, content, state))
            return false;
    }

    /* Float avoidance may supply a minimum padding-edge position. Actual
     * CSS clearance is handled separately from this geometric constraint. */
    state->y = 0;

    if (!layout_block_size_child(box, block, viewport_height, content, state))
        return false;

    /* Position box: horizontal. */
    box->x = box->parent->padding[LEFT] + box->margin[LEFT] + box->border[LEFT].width;
    state->cx += box->x;

    /* Position box: vertical. */
    if (box->border[TOP].width) {
        box->y += box->border[TOP].width;
        state->cy += box->border[TOP].width;
    }

    if (state->clearance_box == box)
        layout_block_apply_clearance(box, block, viewport_height, content, state);

    /* Intrinsic HAS_HEIGHT is conservative for empty inline fragments. Do
     * not consume the pending chain at a block that still collapses through. */
    if (((box->type == BOX_BLOCK && (box->flags & HAS_HEIGHT) &&
             box_prevents_collapse_through(&content->unit_len_ctx, box)) ||
            box->type == BOX_FLEX || box->type == BOX_GRID ||
            box->type == BOX_TABLE || (box->type == BOX_INLINE_CONTAINER &&
                box_inline_container_has_line_boxes(&content->unit_len_ctx, box)) ||
            state->margin_collapse == box) &&
        state->in_margin == true) {
        /* Margin goes above this box. */
        NSLOG(wisp, INFO, "MARGIN_COLLAPSE: box=%p type=%d cy_before=%d max_pos_margin=%u max_neg_margin=%u", box,
            box->type, state->cy, state->max_pos_margin, state->max_neg_margin);
        layout_block_consume_margin(box, state);
        NSLOG(wisp, INFO, "MARGIN_COLLAPSE: box=%p cy_after=%d box->y=%d", box, state->cy, box->y);
    }

    layout_block_record_margin(state, box->margin[TOP]);
    if (!state->margin_applied && state->empty_margin_box == NULL && box->type == BOX_BLOCK &&
        !box_prevents_collapse_through(&content->unit_len_ctx, box))
        state->empty_margin_box = box;

    /* Apply independent formatting-context float avoidance. */
    if (box->type != BOX_INLINE_CONTAINER && (state->y > 0) && (state->cy < state->y)) {
        box->y += state->y - state->cy;
        state->cy = state->y;
        /* This independent context's top is already positioned; it cannot
         * also receive a deferred self-collapsing branch adjustment. */
        state->empty_margin_box = NULL;
        state->seen_pos_margin = state->seen_neg_margin = 0;
    }

    return true;
}


/* Lay out an independent formatting context and advance past its contents. */
static bool layout_block_child_context(struct box *box, int viewport_height, html_content *content,
    struct block_layout_state *state, enum css_overflow_e overflow_x, enum css_overflow_e overflow_y)
{
    if (box->type == BOX_FLEX || box->type == BOX_INLINE_FLEX) {
        const char *tag = "";
        const char *cls = "";
        dom_string *name = NULL;
        dom_string *class_attr = NULL;
        if (box->node != NULL) {
            if (dom_node_get_node_name(box->node, &name) == DOM_NO_ERR && name != NULL) {
                tag = dom_string_data(name);
            }
            if (dom_element_get_attribute(box->node, corestring_dom_class, &class_attr) == DOM_NO_ERR &&
                class_attr != NULL) {
                cls = dom_string_data(class_attr);
            }
        }
        NSLOG(layout, DEBUG,
            "flex pre: tag %s class %s box %p, overflow_x %d, overflow_y %d, wrap %d, parent_w %i, box_w %i", tag, cls,
            box, overflow_x, overflow_y, css_computed_flex_wrap(box->style), box->parent ? box->parent->width : 0,
            box->width);
        NSLOG(layout, DEBUG, "calling layout_flex for flex container %p width %i", box, box->width);
        if (box->type == BOX_FLEX) {
            if (!layout_flex(box, box->width, content)) {
                return false;
            }
        } else {
            /* Inline flex handled as block flex here if needed, or pass TODO */
            /* Ideally call layout_flex but check if it supports INLINE_FLEX */
            /* Assuming layout_flex handles it or we rely on block behavior for now */
            /* fprintf(stderr, "Handling INLINE_FLEX %p in block context\n", box); */
        }
        NSLOG(layout, DEBUG, "flex post: box %p, w %i, h %i", box, box->width, box->height);
        if (class_attr != NULL)
            dom_string_unref(class_attr);
        if (name != NULL)
            dom_string_unref(name);
    } else if (box->type == BOX_GRID || box->type == BOX_INLINE_GRID) {
        NSLOG(layout, DEBUG, "calling layout_grid for grid container %p width %i", box, box->width);
        if (!layout_grid(box, box->parent->width, content)) {
            return false;
        }
    } else {
        if (!layout_block_context(box, viewport_height, content))
            return false;
    }

    state->cy += box->padding[TOP];

    if (box->height == AUTO) {
        box->height = 0;
        layout_block_add_scrollbar(box, BOTTOM);
    }

    state->cx -= box->x;
    state->cy += box->height + box->padding[BOTTOM] + box->border[BOTTOM].width;
    state->y = box->y + box->padding[TOP] + box->height + box->padding[BOTTOM] + box->border[BOTTOM].width;

    return true;
}


/* A table's final size is known before it is moved below obstructing floats. */
static void layout_block_place_table(struct box *box, const struct box *block, struct block_layout_state *state)
{
    /* Move down to avoid floats if necessary. */
    int x0, x1;
    struct box *left, *right;
    state->y = state->cy;
    while (1) {
        enum css_width_e wtype;
        css_fixed_or_calc width = (css_fixed_or_calc)0;
        css_unit unit = CSS_UNIT_PX;

        wtype = css_computed_width(box->style, &width, &unit);

        x0 = state->cx;
        x1 = state->cx + box->parent->width;
        find_sides(block->float_children, state->y, state->y + box->height, &x0, &x1, &left, &right);
        if (wtype == CSS_WIDTH_AUTO)
            break;
        if (box->width <= x1 - x0)
            break;
        if (!left && !right)
            break;
        /* The bottom edge itself is outside the float's exclusion area. */
        if (!left)
            state->y = right->y + right->height;
        else if (!right)
            state->y = left->y + left->height;
        else
            state->y = min(left->y + left->height, right->y + right->height);
    }
    box->x += x0 - state->cx;
    state->cx = x0;
    box->y += state->y - state->cy;
    state->cy = state->y;
}


/* Ordinary descendants keep float coordinates in their shared formatting
 * context, not in their structural parent. Moving a completed block therefore
 * also moves these floats and restores their bottom-edge ordering. Independent
 * contexts move with the block normally and must not be adjusted twice. */
static void layout_shift_block_floats(struct box *box, int shift)
{
    if ((box->type != BOX_BLOCK && box->type != BOX_INLINE_CONTAINER) || box_is_out_of_flow(box) ||
        (box->type == BOX_BLOCK && box_style_creates_block_context(box)))
        return;
    for (struct box *child = box->children; child; child = child->next) {
        if (lh__box_is_float_box(child)) {
            layout_shift_float(child, shift);
        } else {
            layout_shift_block_floats(child, shift);
        }
    }
}

/* A block child exports its first line in pre-relative-position coordinates.
 * Align before advancing sibling flow so the push contributes to auto heights.
 * Borders are outside the box's coordinate origin but inside its baseline. */
static void layout_block_first_baseline(struct box *box, html_content *content, struct block_layout_state *state)
{
    struct box *parent = box->parent;
    if ((box->type != BOX_BLOCK && box->type != BOX_TABLE && !lh__box_is_flex_container(box) &&
            !lh__box_is_grid_container(box)) ||
        !(box->flags & HAS_BASELINE) || box_is_out_of_flow(box) || parent == NULL ||
        (parent->type != BOX_BLOCK && parent->type != BOX_TABLE_CELL) || (parent->flags & HAS_BASELINE) ||
        !layout_needs_first_baseline(parent))
        return;
    struct line_baselines baselines = {0};
    int baseline = lh__box_first_baseline(box);
    int push = layout_align_outside_marker(
        parent, content, box->y - box->border[TOP].width, box->border[TOP].width + baseline, &baselines);
    if (push)
        layout_shift_block_floats(box, push);
    box->y += push;
    state->y += push;
    state->cy += push;
    parent->baseline = box->y + baseline;
    parent->flags |= HAS_BASELINE;
}

/* Finish completed ancestors and return the next sibling, or NULL at the
 * formatting-context boundary. Heights are constrained before updating the
 * flow cursor; margins that cannot escape stay inside their parent. */
static struct box *
layout_block_advance(struct box *box, struct box *block, html_content *content, struct block_layout_state *state)
{
    layout_block_first_baseline(box, content, state);
    layout_block_finish_empty_margin(box, state);
    if (!box->next) {
        /* No more siblings:
         * up to first ancestor with a sibling. */

        do {
            if (box == state->margin_collapse) {
                /* Current margin collapsed though to
                 * this box.  Unset margin_collapse. */
                state->margin_collapse = NULL;
            }

            /* Apply bottom margin */
            if (state->margin_applied && !box_prevents_collapse_through(&content->unit_len_ctx, box)) {
                /* This empty child's bottom already joined its parent's top. */
            } else if (box->margin[BOTTOM] > 0 && (unsigned int)box->margin[BOTTOM] > state->max_pos_margin) {
                state->max_pos_margin = (unsigned int)box->margin[BOTTOM];
            } else if (box->margin[BOTTOM] != AUTO && box->margin[BOTTOM] < 0 &&
                (unsigned int)(-box->margin[BOTTOM]) > state->max_neg_margin)
                state->max_neg_margin = (unsigned int)(-box->margin[BOTTOM]);

            box = box->parent;

            if (box == block)
                break;

            /* Margin is invalidated if this is a box
             * margins can't collapse through.
             * CSS 2.1 §8.3.1: last-child bottom margin collapses
             * with parent only if parent has:
             * - height: auto, unless the whole box collapses through
             * - min-height: 0
             * - no bottom padding
             * - no bottom border
             * - no adjoining chain from a self-collapsing cleared child */
            bool has_min_height = box_has_positive_min_height(&content->unit_len_ctx, box);
            bool contains_clearance = box == state->clearance_parent;
            /* Dimensions were refreshed before descending into this parent.
             * A definite zero height also contains a nonempty child's bottom
             * margin; MAKE_HEIGHT only records positive intrinsic heights.
             * A self-collapsing zero-height box still shares its top chain. */
            bool separates_height = box->height != AUTO && box_prevents_collapse_through(&content->unit_len_ctx, box);
            if (box->type == BOX_BLOCK &&
                (separates_height || has_min_height || contains_clearance || box->padding[BOTTOM] != 0 ||
                    box->border[BOTTOM].width != 0)) {

                /* CSS 2.1 sections 8.3.1 and 10.6.3: clearance or bottom
                 * padding/border keeps the child's collapsed margin inside
                 * the auto content height. Account for it before resetting.
                 * A preferred aspect ratio can also grow for content. */
                if ((box->height == AUTO || (box->flags & ASPECT_RATIO_HEIGHT)) &&
                    (contains_clearance || box->padding[BOTTOM] != 0 || box->border[BOTTOM].width != 0)) {
                    int margin = (int)state->max_pos_margin - (int)state->max_neg_margin;
                    /* Negative margins cannot make content height negative. */
                    margin = max(margin, box->padding[TOP] - state->y);
                    state->y += margin;
                    state->cy += margin;
                }
                state->margin_collapse = NULL;
                state->in_margin = false;
                state->max_pos_margin = state->max_neg_margin = 0;
                state->margin_applied = false;
                state->seen_pos_margin = state->seen_neg_margin = 0;
                state->clearance_parent = NULL;
            }

            int marker_height = layout_marker_min_height(box, content);
            if (marker_height > 0) {
                int extra = max(0, box->padding[TOP] + marker_height - state->y);
                state->y += extra;
                state->cy += extra;
            }
            if (box->height == AUTO) {
                box->height = state->y - box->padding[TOP];

                if (box->type == BOX_BLOCK)
                    layout_block_add_scrollbar(box, BOTTOM);
            } else {
                layout_ratio_content_height(box, state->y - box->padding[TOP]);
                state->cy += box->height - (state->y - box->padding[TOP]);
            }

            /* Apply any min-height and max-height to
             * boxes in normal flow */
            int previous_height = box->height;
            if (box->style && css_computed_position(box->style) != CSS_POSITION_ABSOLUTE &&
                layout_apply_minmax_height(&content->unit_len_ctx, box, NULL)) {
                /* The unconstrained height was already accounted for above.
                 * Advance only by the constraint's change, even when the
                 * specified height differs from the children's flow height. */
                state->cy += box->height - previous_height;
            }

            state->cy += box->padding[BOTTOM] + box->border[BOTTOM].width;
            state->cx -= box->x;

            state->y = box->y + box->padding[TOP] + box->height + box->padding[BOTTOM] + box->border[BOTTOM].width;
            layout_block_first_baseline(box, content, state);
            layout_block_finish_empty_margin(box, state);

        } while (box->next == NULL);
        if (box == block)
            return NULL;

        /* CSS 2.1 §8.3.1: After exiting loop, check if the box we're at
         * (which has a sibling) itself has padding/border that prevents
         * margins from its children collapsing with the next sibling */
        if (box->type == BOX_BLOCK && (box->padding[BOTTOM] != 0 || box->border[BOTTOM].width != 0)) {

            state->in_margin = false;
            state->max_pos_margin = state->max_neg_margin = 0;
        }
    }

    /* To next sibling. */

    if (box == state->margin_collapse) {
        /* Current margin collapsed though to this box.
         * Unset margin_collapse. */
        state->margin_collapse = NULL;
    }

    /* Apply bottom margin.
     * CSS 2.1 §8.3.1: a self-collapsing block's margins were already
     * accumulated by layout_next_margin_block (which traverses through
     * parent-child chains). Do NOT accumulate them again here, or we
     * get double-application when the main loop's inline descent
     * reprocesses the same box. */
    if (!box_prevents_collapse_through(&content->unit_len_ctx, box) && !(box->flags & MAKE_HEIGHT) &&
        box->height == 0) {
        /* Self-collapsing: margins already handled by
         * layout_next_margin_block. Skip. */
    } else if (box->margin[BOTTOM] > 0 && (unsigned int)box->margin[BOTTOM] > state->max_pos_margin) {
        state->max_pos_margin = (unsigned int)box->margin[BOTTOM];
    } else if (box->margin[BOTTOM] != AUTO && box->margin[BOTTOM] < 0 &&
        (unsigned int)(-box->margin[BOTTOM]) > state->max_neg_margin)
        state->max_neg_margin = (unsigned int)(-box->margin[BOTTOM]);

    /* Mark that we have a pending margin to apply to the next sibling.
     * CSS 2.1 §8.3.1: "The bottom margin of an in-flow block-level
     * element always collapses with the top margin of its next
     * in-flow block-level sibling." */
    state->in_margin = true;

    box = box->next;
    box->y = state->y;
    return box;
}


static void layout_block_finish(struct box *block, html_content *content, struct block_layout_state *state)
{
    /* Contained margins contribute to flow height before the marker minimum. */
    state->cy += state->max_pos_margin - state->max_neg_margin;
    int marker_height = layout_marker_min_height(block, content);
    if (marker_height > 0)
        state->cy = max(state->cy, block->padding[TOP] + marker_height);

    /* Increase height to contain any floats inside (CSS 2.1 10.6.7). */
    for (struct box *box = block->float_children; box; box = box->next_float) {
        state->y = box->y + box->height + box->padding[BOTTOM] + box->border[BOTTOM].width + box->margin[BOTTOM];
        if (state->cy < state->y)
            state->cy = state->y;
    }

    if (block->height == AUTO) {
        block->height = state->cy - block->padding[TOP];
        if (block->type == BOX_BLOCK)
            layout_block_add_scrollbar(block, BOTTOM);
    }

    layout_ratio_content_height(block, state->cy - block->padding[TOP]);

    if (block->style && css_computed_position(block->style) != CSS_POSITION_ABSOLUTE) {
        /* Block is in normal flow */
        layout_apply_minmax_height(&content->unit_len_ctx, block, NULL);
    }
}


/* Documented in layout_internal.h */
bool layout_block_context(struct box *block, int viewport_height, html_content *content)
{
    struct box *box;

    struct block_layout_state state = {0};
    css_fixed gadget_size;
    css_unit gadget_unit; /* Checkbox / radio buttons */

    assert(block->type == BOX_BLOCK || block->type == BOX_INLINE_BLOCK || block->type == BOX_TABLE_CELL ||
        block->type == BOX_FLEX || block->type == BOX_GRID || block->type == BOX_INLINE_FLEX ||
        block->type == BOX_INLINE_GRID);
    assert(block->width != UNKNOWN_WIDTH);
    assert(block->width != AUTO);

    block->float_children = NULL;
    block->cached_place_below_level = 0;
    block->clear_level = 0;
    block->flags &= ~HAS_BASELINE;

    /* special case if the block contains an object */
    if (block->object) {
        int temp_width = block->width;
        /* Width is already solved; resolve the remaining auto height before
         * applying its CSS constraints below. */
        struct css_size no_min = {.type = CSS_SIZE_AUTO, .value = 0};
        layout_get_object_dimensions(block, &temp_width, &block->height,
            &(struct layout_size_limits){
                .min_width = no_min,
                .max_width = INT_MAX,
                .min_height = no_min,
                .max_height = INT_MAX,
            },
            block->width);
        /* Flex/grid callers can arrive with auto height: finish its constraints
         * before giving the child content its viewport. Width is already solved
         * by the enclosing formatting context. */
        layout_apply_minmax_height(&content->unit_len_ctx, block, NULL);
        if (block->width == UNKNOWN_WIDTH || block->width >= 100000000) {
            fprintf(stderr, "LAYOUT_ERROR: block %p has INVALID width %d after layout_block_object\n", (void *)block,
                block->width);
        }
        return layout_block_object(block);
    } else if (block->flags & REPLACE_DIM) {
        return true;
    }

    if (block->height == AUTO) {
        layout_prepare_aspect_ratio_height(block);
    }

    /* special case if the block contains an radio button or checkbox */
    if (block->gadget && (block->gadget->type == GADGET_RADIO || block->gadget->type == GADGET_CHECKBOX)) {
        /* form checkbox or radio button
         * if width or height is AUTO, set it to 1em */
        gadget_unit = CSS_UNIT_EM;
        gadget_size = INTTOFIX(1);
        if (block->height == AUTO)
            block->height = FIXTOINT(
                css_unit_len2device_px(block->style, &content->unit_len_ctx, gadget_size, gadget_unit));
    }

    box = block->children;
    /* set current coordinates to top-left of the block */
    state.cx = 0;
    state.y = state.cy = block->padding[TOP];
    if (box)
        box->y = block->padding[TOP];

    /* Walk descendants depth-first. Ordinary blocks share this traversal;
     * tables, inline containers and independent formatting contexts lay out
     * their own contents, then rejoin at ancestor/sibling advancement. */
    while (box) {
        enum css_overflow_e overflow_x = CSS_OVERFLOW_VISIBLE;
        enum css_overflow_e overflow_y = CSS_OVERFLOW_VISIBLE;

        assert(box->type == BOX_BLOCK || box->type == BOX_FLEX || box->type == BOX_GRID || box->type == BOX_TABLE ||
            box->type == BOX_INLINE_CONTAINER);

        layout_block_log_box(box, block);

        /* Tables are laid out before being positioned, because the
         * position depends on the width which is calculated in
         * table layout. Blocks and inline containers are positioned
         * before being laid out, because width is not dependent on
         * content, and the position is required during layout for
         * correct handling of floats.
         */

        if (box->style &&
            (css_computed_position(box->style) == CSS_POSITION_ABSOLUTE ||
                css_computed_position(box->style) == CSS_POSITION_FIXED)) {
            box->x = box->parent->padding[LEFT];
            /* absolute positioned; this element will establish
             * its own block context when it gets laid out later,
             * so no need to look at its children now. */
            goto advance_to_next_box;
        }

        if (box->style) {
            overflow_x = css_computed_overflow_x(box->style);
            overflow_y = css_computed_overflow_y(box->style);
        }
        /* Position floats at the current margin edge without consuming the
         * adjoining margins. Once their geometry is known, restart lookahead
         * so the following block can decide whether clearance is needed. */
        if (box->type == BOX_INLINE_CONTAINER && box_inline_container_only_floats(&content->unit_len_ctx, box) &&
            !box_has_outside_marker_line(box->parent)) {
            struct block_layout_state saved = state;
            int flow_y = box->y;
            /* A float follows preceding margins and its containing block's
             * own top, not the full chain found past that block by lookahead.
             * For a deferred empty branch, include its descendants' margins
             * but stop before its own bottom and any following siblings. */
            state.max_pos_margin = state.seen_pos_margin;
            state.max_neg_margin = state.seen_neg_margin;
            if (state.empty_margin_box) {
                const struct margin_scan scan = {
                    .unit_len_ctx = &content->unit_len_ctx, .block = block, .viewport_height = viewport_height,
                    .flow_y = state.cy, .mode = MARGIN_SCAN_TOP, .boundary = state.empty_margin_box};
                struct margin_scan_result result = {
                    .positive = state.max_pos_margin, .negative = state.max_neg_margin};
                layout_next_margin_block(state.empty_margin_box, &scan, &result);
                state.max_pos_margin = result.positive;
                state.max_neg_margin = result.negative;
            }
            state.margin_collapse = box;
            state.margin_top = NULL;
            state.in_margin = true;
            if (!layout_block_position_child(box, block, viewport_height, content, &state) ||
                !layout_inline_container(box, box->parent->width, block, state.cx, state.cy, content))
                return false;
            state = saved;
            if (!state.margin_applied || state.margin_collapse == box)
                state.margin_collapse = NULL;
            state.y = flow_y;
            goto advance_to_next_box;
        }
        if (!layout_block_position_child(box, block, viewport_height, content, &state))
            return false;

        /* Replaced contents use their own viewport below, not the formatting
         * context selected for ordinary children by display/overflow. */
        if (!box->object && (box->type == BOX_FLEX || box->type == BOX_GRID ||
            (box->type == BOX_BLOCK && box_style_creates_block_context(box)))) {

            if (!layout_block_child_context(box, viewport_height, content, &state, overflow_x, overflow_y))
                return false;

            /* Skip children, because they are done in the new
             * block context */
            goto advance_to_next_box;
        }

        NSLOG(layout, DEBUG, "box %p, cx %i, cy %i, width %i", box, state.cx, state.cy, box->width);

        /* Layout (except tables). */
        if (box->object) {
            if (!layout_block_object(box))
                return false;

        } else if (box->type == BOX_INLINE_CONTAINER) {
            /* Detect if parent width is still the sentinel */
            if (box->parent->width == UNKNOWN_WIDTH || box->parent->width > 100000000) {
                NSLOG(layout, WARNING,
                    "OVERFLOW_BUG: INLINE_CONTAINER %p inheriting huge parent_width=%d from parent %p", box,
                    box->parent->width, box->parent);
            }
            if (!layout_inline_container(box, box->parent->width, block, state.cx, state.cy, content))
                return false;

        } else if (box->type == BOX_TABLE) {
            layout_block_place_table(box, block, &state);
        }

        /* Advance to next box. */
        if (box->type == BOX_BLOCK && !box->object && !(box->iframe) && box->children) {
            /* Down into children. */

            if (box == state.margin_collapse && (state.margin_top != NULL ||
                    box->padding[TOP] || box->border[TOP].width)) {
                /* A top separator starts a new child margin chain even when
                 * an ancestor already consumed this box's outer top margin. */
                state.margin_collapse = NULL;
            }

            state.y = box->padding[TOP];
            box = box->children;
            box->y = state.y;
            state.cy += state.y;
            continue;
        }
        /* Completed children, including tables, occupy both padding edges.
         * Keep the context cursor consistent with the sibling position below. */
        state.cy += box->padding[TOP];

        if (box->type == BOX_BLOCK) {
            int marker_height = layout_marker_min_height(box, content);
            if (box->height == AUTO) {
                box->height = marker_height;
                layout_block_add_scrollbar(box, BOTTOM);
            }
        }

        /* Leaves do not take the ancestor-unwind path in layout_block_advance.
         * Constrain them before advancing the flow cursor or sizing ancestors.
         * Loaded replaced objects were constrained during size resolution,
         * before any scrollbar space was reserved inside their content area. */
        if (box->type == BOX_BLOCK && box->style != NULL && (!box->object || (box->flags & REPLACE_DIM)))
            layout_apply_minmax_height(&content->unit_len_ctx, box, NULL);

        state.cy += box->height + box->padding[BOTTOM] + box->border[BOTTOM].width;
        state.cx -= box->x;
        state.y = box->y + box->padding[TOP] + box->height + box->padding[BOTTOM] + box->border[BOTTOM].width;

    advance_to_next_box:
        box = layout_block_advance(box, block, content, &state);
    }

    layout_block_finish(block, content, &state);

    return true;
}

/**
 * Get a dom node's element tag type.
 *
 * \param[in]  node  Node to get tag type of.
 * \param[in]  type  Returns element tag type on success.
 * \return true if on success, false otherwise.
 */
static bool layout__get_element_tag(const dom_node *node, dom_html_element_type *type)
{
    dom_html_element_type element_type;
    dom_node_type node_type;
    dom_exception exc;

    exc = dom_node_get_node_type(node, &node_type);
    if (exc != DOM_NO_ERR || node_type != DOM_ELEMENT_NODE) {
        return false;
    }

    exc = dom_html_element_get_tag_type(node, &element_type);
    if (exc != DOM_NO_ERR) {
        return false;
    }

    *type = element_type;
    return true;
}


/**
 * Check a node's tag type.
 *
 * \param[in]  node  Node to check tag type of.
 * \param[in]  type  Tag type to test for.
 * \return true if if node has given type, false otherwise.
 */
static inline bool layout__check_element_type(const dom_node *node, dom_html_element_type type)
{
    dom_html_element_type element_type;

    if (!layout__get_element_tag(node, &element_type)) {
        return false;
    }

    return element_type == type;
}


/**
 * Helper to get attribute value from a LI node.
 *
 * \param[in]  li_node    DOM node for the LI element;
 * \param[out] value_out  Returns the value on success.
 * \return true if node has value, otherwise false.
 */
static bool layout__get_li_value(dom_node *li_node, dom_long *value_out)
{
    dom_exception exc;
    dom_long value;
    bool has_value;

    /** \todo
     * dom_html_li_element_get_value() is rubbish and we can't tell
     * a lack of value attribute or invalid value from a perfectly
     * valid '-1'.
     *
     * This helps for the common case of no value.  However we should
     * fix libdom to have some kind of sane interface to get numerical
     * attributes.
     */
    exc = dom_element_has_attribute(li_node, corestring_dom_value, &has_value);
    if (exc != DOM_NO_ERR || has_value == false) {
        return false;
    }

    exc = dom_html_li_element_get_value((dom_html_li_element *)li_node, &value);
    if (exc != DOM_NO_ERR) {
        return false;
    }

    *value_out = value;
    return true;
}


/**
 * Helper to get start attribute value from a OL node.
 *
 * \param[in]  ol_node    DOM node for the OL element;
 * \param[out] start_out  Returns the value on success.
 * \return true if node has value, otherwise false.
 */
static bool layout__get_ol_start(dom_node *ol_node, dom_long *start_out)
{
    dom_exception exc;
    dom_long start;
    bool has_start;

    /** \todo
     * see layout__get_li_value().
     */
    exc = dom_element_has_attribute(ol_node, corestring_dom_start, &has_start);
    if (exc != DOM_NO_ERR || has_start == false) {
        return false;
    }

    exc = dom_html_olist_element_get_start((dom_html_olist_element *)ol_node, &start);
    if (exc != DOM_NO_ERR) {
        return false;
    }

    *start_out = start;
    return true;
}


/**
 * Helper to get reversed attribute value from a OL node.
 *
 * \param[in]  ol_node    DOM node for the OL element;
 * \return true if node has reversed, otherwise false.
 */
static bool layout__get_ol_reversed(dom_node *ol_node)
{
    dom_exception exc;
    bool has_reversed;

    exc = dom_element_has_attribute(ol_node, corestring_dom_reversed, &has_reversed);
    if (exc != DOM_NO_ERR) {
        return false;
    }

    return has_reversed;
}


/**
 * Get the number of list items for a list owner.
 *
 * \param[in]  list_owner  DOM node to count list items for.
 * \param[in]  count_out   Returns list item count on success.
 * \return true on success, otherwise false.
 */
static bool layout__get_list_item_count(dom_node *list_owner, dom_long *count_out)
{
    dom_html_element_type tag_type;
    dom_exception exc;
    dom_node *child;
    int count;

    if (list_owner == NULL) {
        return false;
    }

    if (!layout__get_element_tag(list_owner, &tag_type)) {
        return false;
    }

    if (tag_type != DOM_HTML_ELEMENT_TYPE_OL && tag_type != DOM_HTML_ELEMENT_TYPE_UL) {
        return false;
    }

    exc = dom_node_get_first_child(list_owner, &child);
    if (exc != DOM_NO_ERR) {
        return false;
    }

    count = 0;
    while (child != NULL) {
        dom_node *temp_node;

        if (layout__check_element_type(child, DOM_HTML_ELEMENT_TYPE_LI)) {
            struct box *child_box;
            if (dom_node_get_user_data(child, corestring_dom___ns_key_box_node_data, &child_box) != DOM_NO_ERR) {
                dom_node_unref(child);
                return false;
            }

            if (child_box != NULL && child_box->list_marker != NULL) {
                count++;
            }
        }

        exc = dom_node_get_next_sibling(child, &temp_node);
        dom_node_unref(child);
        if (exc != DOM_NO_ERR) {
            return false;
        }

        child = temp_node;
    }

    *count_out = count;
    return true;
}


/**
 * Handle list item counting, if this is a list owner box.
 *
 * \param[in]  box  Box to do list item counting for.
 */
static void layout__ordered_list_count(struct box *box)
{
    dom_html_element_type tag_type;
    dom_exception exc;
    dom_node *child;
    int step = 1;
    dom_long next;

    if (box->node == NULL) {
        return;
    }

    if (!layout__get_element_tag(box->node, &tag_type)) {
        return;
    }

    if (tag_type != DOM_HTML_ELEMENT_TYPE_OL && tag_type != DOM_HTML_ELEMENT_TYPE_UL) {
        return;
    }

    next = 1;
    if (tag_type == DOM_HTML_ELEMENT_TYPE_OL) {
        bool have_start = layout__get_ol_start(box->node, &next);
        bool have_reversed = layout__get_ol_reversed(box->node);

        if (have_reversed) {
            step = -1;
        }

        if (!have_start && have_reversed) {
            layout__get_list_item_count(box->node, &next);
        }
    }

    exc = dom_node_get_first_child(box->node, &child);
    if (exc != DOM_NO_ERR) {
        return;
    }

    while (child != NULL) {
        dom_node *temp_node;

        if (layout__check_element_type(child, DOM_HTML_ELEMENT_TYPE_LI)) {
            struct box *child_box;

            if (dom_node_get_user_data(child, corestring_dom___ns_key_box_node_data, &child_box) != DOM_NO_ERR) {
                dom_node_unref(child);
                return;
            }

            if (child_box != NULL && child_box->list_marker != NULL) {
                dom_long value;
                struct box *marker = child_box->list_marker;
                if (layout__get_li_value(child, &value)) {
                    marker->list_value = value;
                    next = marker->list_value;
                } else {
                    marker->list_value = next;
                }
                next += step;
            }
        }

        exc = dom_node_get_next_sibling(child, &temp_node);
        dom_node_unref(child);
        if (exc != DOM_NO_ERR) {
            return;
        }

        child = temp_node;
    }
}

/**
 * Set up the marker text for a numerical list item.
 *
 * \param[in]  box      The list item's main box.
 */
static bool layout__set_numerical_marker_text(struct box *box)
{
    struct box *marker = box->list_marker;
    size_t counter_len;
    css_error css_res;
    enum {
        /**
         * initial length of a list marker buffer
         *
         * enough for 9,999,999,999,999,999,999 in decimal
         * or five characters for 4-byte UTF-8.
         */
        LIST_MARKER_SIZE = 20,
    };

    char *text = talloc_array(marker, char, LIST_MARKER_SIZE);
    if (text == NULL)
        return false;

    css_res = css_computed_format_list_style(
        box->style, marker->list_value, text, LIST_MARKER_SIZE, &counter_len);
    if (css_res == CSS_OK) {
        if (counter_len >= LIST_MARKER_SIZE) {
            /* Use computed size as marker did not fit in
             * default allocation. */
            char *resized = talloc_realloc(marker, text, char, counter_len + 1);
            if (resized == NULL) {
                talloc_free(text);
                return false;
            }
            text = resized;
            css_res = css_computed_format_list_style(
                box->style, marker->list_value, text, counter_len + 1, &counter_len);
        }
    }
    if (css_res != CSS_OK) {
        talloc_free(text);
        return false;
    }
    text[counter_len] = '\0';
    marker->text = text;
    marker->length = counter_len;
    return true;
}

/**
 * Find out if box's style represents a numerical list style type.
 *
 * \param[in]  b  Box with style to test.
 * \return true if box has numerical list style type, false otherwise.
 */
static bool layout__list_item_is_numerical(const struct box *b)
{
    enum css_list_style_type_e t = css_computed_list_style_type(b->style);

    switch (t) {
    case CSS_LIST_STYLE_TYPE_DISC: /* Fall through. */
    case CSS_LIST_STYLE_TYPE_CIRCLE: /* Fall through. */
    case CSS_LIST_STYLE_TYPE_SQUARE: /* Fall through. */
    case CSS_LIST_STYLE_TYPE_DISCLOSURE_OPEN: /* Fall through. */
    case CSS_LIST_STYLE_TYPE_DISCLOSURE_CLOSED: /* Fall through. */
    case CSS_LIST_STYLE_TYPE_NONE:
        return false;

    default:
        return true;
    }
}

/**
 * Prepare marker text before intrinsic/line layout, and position outside markers.
 */
static bool layout_lists(const html_content *content, struct box *box)
{
    struct box *child;

    layout__ordered_list_count(box);

    for (child = box->children; child; child = child->next) {
        if (child->list_marker) {
            struct box *marker = child->list_marker;

            if (layout__list_item_is_numerical(child)) {
                if (marker->text == NULL && !layout__set_numerical_marker_text(child))
                    return false;
            }
            if (!box_has_outside_marker(child)) {
                marker->type = marker->object || !marker->text ? BOX_INLINE : BOX_TEXT;
                /* The marker's separator uses the font's space, rather than the
                 * fixed outside-marker gap. Keep it out of the atomic text. */
                while (marker->length && marker->text[marker->length - 1] == ' ')
                    marker->length--;
                marker->space = marker->object || marker->length ? UNKNOWN_WIDTH : 0;
            } else if (marker->object) {
                /* Image content dimensions are in CSS pixels */
                marker->width = content_get_width(marker->object);
                marker->height = content_get_height(marker->object);
#ifdef WISP_DEVICE_PIXEL_LAYOUT
                {
                    int dpi = browser_get_dpi();
                    if (dpi > 0 && dpi != 96) {
                        marker->width = (marker->width * dpi) / 96;
                        marker->height = (marker->height * dpi) / 96;
                    }
                }
#endif
                /* Detached outside markers bypass ordinary object layout.
                 * SVG content needs its resolved viewport to parse shapes;
                 * loading alone only supplies the intrinsic dimensions. */
                if (content_can_reformat(marker->object))
                    content_reformat(marker->object, false, marker->width, marker->height);
                marker->x = -marker->width;
                marker->y = (line_height(&content->unit_len_ctx, marker->style) - marker->height) / 2;
            } else if (marker->text) {
                if (marker->width == UNKNOWN_WIDTH) {
                    plot_font_style_t fstyle;
                    font_plot_style_from_css(content, marker->style, &fstyle);
                    content->font_func->width(&fstyle, marker->text, marker->length, &marker->width);
                    marker->flags |= MEASURED;
                }
                marker->x = -marker->width;
                marker->y = 0;
                marker->height = line_height(&content->unit_len_ctx, marker->style);
            } else {
                marker->x = 0;
                marker->y = 0;
                marker->width = 0;
                marker->height = 0;
            }
            /* Gap between outside marker and content. Inside markers use the
             * normal inline layout path, including their measured separator. */
            if (box_has_outside_marker(child))
                marker->x -= 4;

            /* Log marker positioning relative to LI */
            NSLOG(wisp, INFO,
                "MARKER_LAYOUT: LI=%p marker=%p marker_text='%.*s' marker_xy=(%d,%d) LI_children=%p LI_first_child_y=%d",
                child, marker, marker->text ? (int)marker->length : 0, marker->text ? marker->text : "", marker->x,
                marker->y, child->children, child->children ? child->children->y : -999);
        }
        if (!layout_lists(content, child))
            return false;
    }
    return true;
}


/**
 * Compute box offsets for a relatively or absolutely positioned box with
 * respect to a box.
 *
 * \param  unit_len_ctx           Length conversion context
 * \param  box               box to compute offsets for
 * \param  containing_block  box to compute percentages with respect to
 * \param  top               updated to top offset, or AUTO
 * \param  right             updated to right offset, or AUTO
 * \param  bottom            updated to bottom offset, or AUTO
 * \param  left              updated to left offset, or AUTO
 *
 * See CSS 2.1 9.3.2. containing_block must have width and height.
 */
static void layout_compute_offsets(const css_unit_ctx *unit_len_ctx, struct box *box, struct box *containing_block,
    int *top, int *right, int *bottom, int *left)
{
    uint32_t type;
    css_fixed_or_calc value = (css_fixed_or_calc)0;
    css_unit unit = CSS_UNIT_PX;

    assert(containing_block->width != UNKNOWN_WIDTH);
    assert(containing_block->width != AUTO);
    assert(containing_block->height != AUTO);

    /* left */
    type = css_computed_left(box->style, &value, &unit);
    if (type == CSS_LEFT_SET) {
        if (!lh__length_to_px(box->style, unit_len_ctx, containing_block->width, value, unit, left)) {
            *left = AUTO;
        }
    } else {
        *left = AUTO;
    }

    /* right */
    type = css_computed_right(box->style, &value, &unit);
    if (type == CSS_RIGHT_SET) {
        if (!lh__length_to_px(box->style, unit_len_ctx, containing_block->width, value, unit, right)) {
            *right = AUTO;
        }
    } else {
        *right = AUTO;
    }

    /* top */
    type = css_computed_top(box->style, &value, &unit);
    if (type == CSS_TOP_SET) {
        if (!lh__length_to_px(box->style, unit_len_ctx, containing_block->height, value, unit, top)) {
            *top = AUTO;
        }
    } else {
        *top = AUTO;
    }

    /* bottom */
    type = css_computed_bottom(box->style, &value, &unit);
    if (type == CSS_BOTTOM_SET) {
        if (!lh__length_to_px(box->style, unit_len_ctx, containing_block->height, value, unit, bottom)) {
            *bottom = AUTO;
        }
    } else {
        *bottom = AUTO;
    }
}


/* CSS Flexbox §4.1: when both insets on an axis are auto, position an
 * absolute child as the sole, fixed-size item in its parent's content box.
 * This runs after absolute sizing, so neither flex factors nor stretch can
 * change the child's size or make it consume space in the flex container. */
static void layout_absolute_flex_static_position(struct box *box, int cx, int cy, bool auto_x, bool auto_y)
{
    const struct box *flex = box->parent;
    bool horizontal = lh__flex_main_is_horizontal(flex);
    int main_free = lh__box_size_main(horizontal, flex) - lh__box_size_main(horizontal, box) -
        lh__delta_outer_main(flex, box);
    int cross_free = (horizontal ? flex->height - box->height : flex->width - box->width) -
        lh__delta_outer_cross(flex, box);
    int main_offset = 0;
    int cross_offset = 0;

    switch (css_computed_justify_content(flex->style)) {
    case CSS_JUSTIFY_CONTENT_FLEX_END:
        main_offset = main_free;
        break;
    case CSS_JUSTIFY_CONTENT_CENTER:
        main_offset = main_free / 2;
        break;
    case CSS_JUSTIFY_CONTENT_SPACE_AROUND:
    case CSS_JUSTIFY_CONTENT_SPACE_EVENLY:
        main_offset = max(0, main_free / 2);
        break;
    default:
        break;
    }
    if (lh__flex_direction_reversed(flex))
        main_offset = main_free - main_offset;

    cross_offset = lh__flex_cross_offset(flex, lh__box_align_self(flex, box), cross_free);

    if (auto_x) {
        box->x = cx + flex->padding[LEFT] + (horizontal ? main_offset : cross_offset) + lh__non_auto_margin(box, LEFT) +
            box->border[LEFT].width;
    }
    if (auto_y) {
        box->y = cy + flex->padding[TOP] + (horizontal ? cross_offset : main_offset) + lh__non_auto_margin(box, TOP) +
            box->border[TOP].width;
    }
}


/* Temporary values for the CSS 2.1 absolute-positioning equations.
 * Kept on the stack; resolving one axis does not change the other. */
struct absolute_layout {
    int top, right, bottom, left;
    int width, height, max_width;
    struct css_size min_width;
    int static_left, static_top;
    int available_width;
};

static void layout_absolute_log_box(struct box *box, struct box *containing_block, const struct absolute_layout *layout)
{
    int top = layout->top, left = layout->left, right = layout->right, width = layout->width;
    {
        css_fixed_or_calc wlen = (css_fixed_or_calc)0;
        css_unit wunit = CSS_UNIT_PX;
        const char *tag = "";
        const char *cls = "";
        dom_string *name = NULL;
        dom_string *class_attr = NULL;
        if (box->node != NULL) {
            if (dom_node_get_node_name(box->node, &name) == DOM_NO_ERR && name != NULL) {
                tag = dom_string_data(name);
            }
            if (dom_element_get_attribute(box->node, corestring_dom_class, &class_attr) == DOM_NO_ERR &&
                class_attr != NULL) {
                cls = dom_string_data(class_attr);
            }
        }
        uint8_t wtype = css_computed_width(box->style, &wlen, &wunit);
        long wlen_val = (wunit == CSS_UNIT_CALC) ? 0 : (long)wlen.value;
        void *wlen_calc = (wunit == CSS_UNIT_CALC) ? (void *)wlen.calc : NULL;
        NSLOG(layout, INFO,
            "abs offsets pre: tag %s class %s top %i left %i right %i width %i wtype %u wlen %ld wunit %u wcalc %p cb.width %i cb.height %i",
            tag, cls, top, left, right, width, (unsigned)wtype, wlen_val, (unsigned)wunit, wlen_calc,
            containing_block->width, containing_block->height);
        if (class_attr != NULL)
            dom_string_unref(class_attr);
        if (name != NULL)
            dom_string_unref(name);
    }
    {
        const char *tag = "";
        const char *cls = "";
        dom_string *name = NULL;
        dom_string *class_attr = NULL;
        if (box->node != NULL) {
            if (dom_node_get_node_name(box->node, &name) == DOM_NO_ERR && name != NULL) {
                tag = dom_string_data(name);
            }
            if (dom_element_get_attribute(box->node, corestring_dom_class, &class_attr) == DOM_NO_ERR &&
                class_attr != NULL) {
                cls = dom_string_data(class_attr);
            }
        }
        NSLOG(layout, INFO, "abs pre: tag %s class %s box %p", tag, cls, box);
        if (class_attr != NULL)
            dom_string_unref(class_attr);
        if (name != NULL)
            dom_string_unref(name);
    }
}

/* CSS 2.1 §10.3.7: solve left/width/right, including shrink-to-fit and
 * horizontal auto margins. The containing block includes its padding here. */
static void
layout_absolute_resolve_horizontal(struct box *box, const struct box *containing_block, struct absolute_layout *layout)
{
    int left = layout->left, right = layout->right, width = layout->width;
    int max_width = layout->max_width;
    struct css_size min_width = layout->min_width;
    int available_width = layout->available_width;
    int static_left = layout->static_left;
    int *margin = box->margin;
    const int *padding = box->padding;
    const struct box_border *border = box->border;
    int space;

    /* 10.3.7 */
    NSLOG(layout, DEBUG, "%i + %i + %i + %i + %i + %i + %i + %i + %i = %i", left, margin[LEFT], border[LEFT].width,
        padding[LEFT], width, padding[RIGHT], border[RIGHT].width, margin[RIGHT], right, containing_block->width);


    if (left == AUTO && width == AUTO && right == AUTO) {
        if (margin[LEFT] == AUTO)
            margin[LEFT] = 0;
        if (margin[RIGHT] == AUTO)
            margin[RIGHT] = 0;
        left = static_left;

        /* Use CSS min-width (min_width parameter), not intrinsic min_width (box->min_width.value).
         * For absolute/fixed elements, box->min_width hasn't been populated by minmax calculations. */
        int min_w = (min_width.type == CSS_SIZE_SET) ? min_width.value : 0;
        width = min(max(min_w, available_width), box->max_width);
        width -= box->margin[LEFT] + box->border[LEFT].width + box->padding[LEFT] + box->padding[RIGHT] +
            box->border[RIGHT].width + box->margin[RIGHT];

        /* Adjust for {min|max}-width */
        if (max_width >= 0 && width > max_width)
            width = max_width;
        if (min_width.type == CSS_SIZE_SET && width < min_width.value)
            width = min_width.value;

        right = containing_block->width - left - margin[LEFT] - border[LEFT].width - padding[LEFT] - width -
            padding[RIGHT] - border[RIGHT].width - margin[RIGHT];
    } else if (left != AUTO && width != AUTO && right != AUTO) {

        /* Adjust for {min|max}-width */
        if (max_width >= 0 && width > max_width)
            width = max_width;
        if (min_width.type == CSS_SIZE_SET && min_width.value > 0 && width < min_width.value)
            width = min_width.value;

        if (margin[LEFT] == AUTO && margin[RIGHT] == AUTO) {
            space = containing_block->width - left - border[LEFT].width - padding[LEFT] - width - padding[RIGHT] -
                border[RIGHT].width - right;
            if (space < 0) {
                margin[LEFT] = 0;
                margin[RIGHT] = space;
            } else {
                margin[LEFT] = margin[RIGHT] = space / 2;
            }
        } else if (margin[LEFT] == AUTO) {
            margin[LEFT] = containing_block->width - left - border[LEFT].width - padding[LEFT] - width -
                padding[RIGHT] - border[RIGHT].width - margin[RIGHT] - right;
        } else if (margin[RIGHT] == AUTO) {
            margin[RIGHT] = containing_block->width - left - margin[LEFT] - border[LEFT].width - padding[LEFT] - width -
                padding[RIGHT] - border[RIGHT].width - right;
        } else {
            right = containing_block->width - left - margin[LEFT] - border[LEFT].width - padding[LEFT] - width -
                padding[RIGHT] - border[RIGHT].width - margin[RIGHT];
        }
    } else {
        if (margin[LEFT] == AUTO)
            margin[LEFT] = 0;
        if (margin[RIGHT] == AUTO)
            margin[RIGHT] = 0;

        if (left == AUTO && width == AUTO && right != AUTO) {
            available_width -= right;

            int min_w = (min_width.type == CSS_SIZE_SET) ? min_width.value : 0;
            width = min(max(min_w, available_width), box->max_width);
            width -= box->margin[LEFT] + box->border[LEFT].width + box->padding[LEFT] + box->padding[RIGHT] +
                box->border[RIGHT].width + box->margin[RIGHT];

            /* Adjust for {min|max}-width */
            if (max_width >= 0 && width > max_width)
                width = max_width;
            if (min_width.type == CSS_SIZE_SET && width < min_width.value)
                width = min_width.value;

            left = containing_block->width - margin[LEFT] - border[LEFT].width - padding[LEFT] - width -
                padding[RIGHT] - border[RIGHT].width - margin[RIGHT] - right;
        } else if (left == AUTO && width != AUTO && right == AUTO) {

            /* Adjust for {min|max}-width */
            if (max_width >= 0 && width > max_width)
                width = max_width;
            if (min_width.type == CSS_SIZE_SET && min_width.value > 0 && width < min_width.value)
                width = min_width.value;

            left = static_left;
            right = containing_block->width - left - margin[LEFT] - border[LEFT].width - padding[LEFT] - width -
                padding[RIGHT] - border[RIGHT].width - margin[RIGHT];
        } else if (left != AUTO && width == AUTO && right == AUTO) {
            available_width -= left;

            int min_w = (min_width.type == CSS_SIZE_SET) ? min_width.value : 0;
            width = min(max(min_w, available_width), box->max_width);
            width -= box->margin[LEFT] + box->border[LEFT].width + box->padding[LEFT] + box->padding[RIGHT] +
                box->border[RIGHT].width + box->margin[RIGHT];

            /* Adjust for {min|max}-width */
            if (max_width >= 0 && width > max_width)
                width = max_width;
            if (min_width.type == CSS_SIZE_SET && width < min_width.value)
                width = min_width.value;

            right = containing_block->width - left - margin[LEFT] - border[LEFT].width - padding[LEFT] - width -
                padding[RIGHT] - border[RIGHT].width - margin[RIGHT];
        } else if (left == AUTO && width != AUTO && right != AUTO) {

            /* Adjust for {min|max}-width */
            if (max_width >= 0 && width > max_width)
                width = max_width;
            if (min_width.type == CSS_SIZE_SET && width < min_width.value)
                width = min_width.value;

            left = containing_block->width - margin[LEFT] - border[LEFT].width - padding[LEFT] - width -
                padding[RIGHT] - border[RIGHT].width - margin[RIGHT] - right;
        } else if (left != AUTO && width == AUTO && right != AUTO) {
            width = containing_block->width - left - margin[LEFT] - border[LEFT].width - padding[LEFT] -
                padding[RIGHT] - border[RIGHT].width - margin[RIGHT] - right;

            /* Adjust for {min|max}-width */
            if (max_width >= 0 && width > max_width)
                width = max_width;
            if (min_width.type == CSS_SIZE_SET && width < min_width.value)
                width = min_width.value;

        } else if (left != AUTO && width != AUTO && right == AUTO) {

            /* Adjust for {min|max}-width */
            if (max_width >= 0 && width > max_width)
                width = max_width;
            if (min_width.type == CSS_SIZE_SET && width < min_width.value)
                width = min_width.value;

            right = containing_block->width - left - margin[LEFT] - border[LEFT].width - padding[LEFT] - width -
                padding[RIGHT] - border[RIGHT].width - margin[RIGHT];
        }
    }

    NSLOG(layout, DEBUG, "%i + %i + %i + %i + %i + %i + %i + %i + %i = %i", left, margin[LEFT], border[LEFT].width,
        padding[LEFT], width, padding[RIGHT], border[RIGHT].width, margin[RIGHT], right, containing_block->width);

    layout->left = left;
    layout->right = right;
    layout->width = width;
}

/* Dispatch the inner formatter after horizontal sizing has chosen the width.
 * Preserve each formatter's existing containing-block setup. */
static bool
layout_absolute_contents(struct box *box, struct box *containing_block, int containing_width, html_content *content)
{
    int width = box->width;

    if (box->type == BOX_BLOCK || box->type == BOX_INLINE_BLOCK || box->object || box->flags & IFRAME) {
        if (!layout_block_context(box, -1, content))
            return false;
    } else if (box->type == BOX_TABLE) {
        /* layout_table also expects the containing block to be
         * stored in the float_container field */
        box->float_container = containing_block;
        /* \todo  layout_table considers margins etc. again */
        if (!layout_table(box, width, content))
            return false;
        box->float_container = NULL;
        layout_solve_width(box, box->parent->width, box->width, 0, 0, -1, -1);
    } else if (box->type == BOX_FLEX || box->type == BOX_INLINE_FLEX) {
        /* layout_table also expects the containing block to be
         * stored in the float_container field */
        box->float_container = containing_block;
        NSLOG(layout, INFO, "calling layout_flex for positioned flex %p width %i", box, width);
        if (!layout_flex(box, width, content))
            return false;
        box->float_container = NULL;
    } else if (box->type == BOX_GRID || box->type == BOX_INLINE_GRID) {
        box->float_container = containing_block;
        NSLOG(layout, INFO, "calling layout_grid for positioned grid %p width %i", box, width);
        if (!layout_grid(box, containing_width, content))
            return false;
        box->float_container = NULL;
    }

    return true;
}

/* CSS 2.1 §10.6.4: solve top/height/bottom after content layout, when an
 * auto height can use the measured content height. */
static void
layout_absolute_resolve_vertical(struct box *box, const struct box *containing_block, struct absolute_layout *layout)
{
    int top = layout->top, bottom = layout->bottom, height = layout->height;
    int static_top = layout->static_top;
    int *margin = box->margin;
    const int *padding = box->padding;
    const struct box_border *border = box->border;
    int space;

    if (height == AUTO && (box->flags & ASPECT_RATIO_HEIGHT)) {
        height = box->height;
    }

    /* 10.6.4 */
    NSLOG(layout, DEBUG, "%i + %i + %i + %i + %i + %i + %i + %i + %i = %i", top, margin[TOP], border[TOP].width,
        padding[TOP], height, padding[BOTTOM], border[BOTTOM].width, margin[BOTTOM], bottom, containing_block->height);

    if (top == AUTO && height == AUTO && bottom == AUTO) {
        top = static_top;
        height = box->height;
        if (margin[TOP] == AUTO)
            margin[TOP] = 0;
        if (margin[BOTTOM] == AUTO)
            margin[BOTTOM] = 0;
        bottom = containing_block->height - top - margin[TOP] - border[TOP].width - padding[TOP] - height -
            padding[BOTTOM] - border[BOTTOM].width - margin[BOTTOM];
    } else if (top != AUTO && height != AUTO && bottom != AUTO) {
        if (margin[TOP] == AUTO && margin[BOTTOM] == AUTO) {
            space = containing_block->height - top - border[TOP].width - padding[TOP] - height - padding[BOTTOM] -
                border[BOTTOM].width - bottom;
            margin[TOP] = margin[BOTTOM] = space / 2;
        } else if (margin[TOP] == AUTO) {
            margin[TOP] = containing_block->height - top - border[TOP].width - padding[TOP] - height - padding[BOTTOM] -
                border[BOTTOM].width - margin[BOTTOM] - bottom;
        } else if (margin[BOTTOM] == AUTO) {
            margin[BOTTOM] = containing_block->height - top - margin[TOP] - border[TOP].width - padding[TOP] - height -
                padding[BOTTOM] - border[BOTTOM].width - bottom;
        } else {
            bottom = containing_block->height - top - margin[TOP] - border[TOP].width - padding[TOP] - height -
                padding[BOTTOM] - border[BOTTOM].width - margin[BOTTOM];
        }
    } else {
        if (margin[TOP] == AUTO)
            margin[TOP] = 0;
        if (margin[BOTTOM] == AUTO)
            margin[BOTTOM] = 0;
        if (top == AUTO && height == AUTO && bottom != AUTO) {
            height = box->height;
            top = containing_block->height - margin[TOP] - border[TOP].width - padding[TOP] - height - padding[BOTTOM] -
                border[BOTTOM].width - margin[BOTTOM] - bottom;
        } else if (top == AUTO && height != AUTO && bottom == AUTO) {
            top = static_top;
            bottom = containing_block->height - top - margin[TOP] - border[TOP].width - padding[TOP] - height -
                padding[BOTTOM] - border[BOTTOM].width - margin[BOTTOM];
        } else if (top != AUTO && height == AUTO && bottom == AUTO) {
            height = box->height;
            bottom = containing_block->height - top - margin[TOP] - border[TOP].width - padding[TOP] - height -
                padding[BOTTOM] - border[BOTTOM].width - margin[BOTTOM];
        } else if (top == AUTO && height != AUTO && bottom != AUTO) {
            top = containing_block->height - margin[TOP] - border[TOP].width - padding[TOP] - height - padding[BOTTOM] -
                border[BOTTOM].width - margin[BOTTOM] - bottom;
        } else if (top != AUTO && height == AUTO && bottom != AUTO) {
            height = containing_block->height - top - margin[TOP] - border[TOP].width - padding[TOP] - padding[BOTTOM] -
                border[BOTTOM].width - margin[BOTTOM] - bottom;
        } else if (top != AUTO && height != AUTO && bottom == AUTO) {
            bottom = containing_block->height - top - margin[TOP] - border[TOP].width - padding[TOP] - height -
                padding[BOTTOM] - border[BOTTOM].width - margin[BOTTOM];
        }
    }

    NSLOG(layout, DEBUG, "%i + %i + %i + %i + %i + %i + %i + %i + %i = %i", top, margin[TOP], border[TOP].width,
        padding[TOP], height, padding[BOTTOM], border[BOTTOM].width, margin[BOTTOM], bottom, containing_block->height);

    layout->top = top;
    layout->bottom = bottom;
    layout->height = height;
}


/**
 * Layout and position an absolutely positioned box.
 *
 * \param  box               absolute box to layout and position
 * \param  containing_block  containing block
 * \param  cx                position of box relative to containing_block
 * \param  cy                position of box relative to containing_block
 * \param  content           memory pool for any new boxes
 * \return  true on success, false on memory exhaustion
 */
static bool layout_absolute(struct box *box, struct box *containing_block, int cx, int cy, html_content *content)
{
    struct absolute_layout layout;
    int max_height;
    struct css_size min_height;
    bool replaced_object = box->object != NULL;
    int *margin = box->margin;
    int *padding = box->padding;
    struct box_border *border = box->border;
    bool block_containing_box = containing_block->type == BOX_BLOCK || containing_block->type == BOX_INLINE_BLOCK ||
        containing_block->type == BOX_TABLE_CELL || containing_block->type == BOX_FLEX ||
        containing_block->type == BOX_INLINE_FLEX || containing_block->type == BOX_GRID ||
        containing_block->type == BOX_INLINE_GRID;

    assert(box->type == BOX_BLOCK || box->type == BOX_TABLE || box->type == BOX_INLINE_BLOCK || box->type == BOX_FLEX ||
        box->type == BOX_GRID || box->type == BOX_INLINE_FLEX || box->type == BOX_INLINE_GRID);

    /* Normal layout supplies the fallback static position. Flex children
     * with auto insets receive their final alignment after absolute sizing. */
    layout.static_left = cx + box->x;
    layout.static_top = cy + box->y;

    if (block_containing_box) {
        /* Block level container => temporarily increase containing
         * block dimensions to include padding (we restore this
         * again at the end) */
        containing_block->width += containing_block->padding[LEFT] + containing_block->padding[RIGHT];
        containing_block->height += containing_block->padding[TOP] + containing_block->padding[BOTTOM];
    }

    /* Capture available_width AFTER padding adjustment */
    layout.available_width = containing_block->width;

    layout_compute_offsets(
        &content->unit_len_ctx, box, containing_block, &layout.top, &layout.right, &layout.bottom, &layout.left);
    bool auto_x = layout.left == AUTO && layout.right == AUTO;
    bool auto_y = layout.top == AUTO && layout.bottom == AUTO;

    /* Pass containing block into layout_find_dimensions via the float
     * containing block box member. This is unused for absolutely positioned
     * boxes because a box can't be floated and absolutely positioned. */
    box->float_container = containing_block;
    layout_find_dimensions(&content->unit_len_ctx, layout.available_width, -1, box,
        &(struct layout_dimension_outputs){
            .width = &layout.width,
            .height = &layout.height,
            .max_width = &layout.max_width,
            .min_width = &layout.min_width,
            .max_height = replaced_object ? &max_height : NULL,
            .min_height = replaced_object ? &min_height : NULL,
            .margin = margin,
            .padding = padding,
            .border = border,
        });
    box->float_container = NULL;

    if (replaced_object) {
        /* CSS 2.2 10.3.8 / 10.6.5: resolve replaced dimensions before
         * solving the inset equations. Opposing insets position an image;
         * they do not stretch its auto dimensions like a non-replaced box.
         * Resolve limits against the containing block's padding box too. */
        layout_get_object_dimensions(box, &layout.width, &layout.height,
            &(struct layout_size_limits){
                .min_width = layout.min_width,
                .max_width = layout.max_width,
                .min_height = min_height,
                .max_height = max_height,
            },
            layout.available_width);
        if (max_height >= 0)
            layout.height = min(layout.height, max_height);
        if (min_height.type == CSS_SIZE_SET)
            layout.height = max(layout.height, min_height.value);
    }

    layout_absolute_log_box(box, containing_block, &layout);
    layout_absolute_resolve_horizontal(box, containing_block, &layout);

    /* Store the containing block for use by box_coords() and other functions.
     * Per CSS 2.1 §10.1, box->x/y are relative to the containing block. */
    box->abs_containing_block = containing_block;
    box->x = layout.left + margin[LEFT] + border[LEFT].width;
    if (block_containing_box) {
        /* Block-level ancestor => reset container's width */
        containing_block->width -= containing_block->padding[LEFT] + containing_block->padding[RIGHT];
    } else {
        /** \todo inline ancestors */
    }
    box->width = layout.width;
    box->height = layout.height;

    NSLOG(layout, INFO, "abs box %p: width %i parent %p parent.width %i", box, box->width, containing_block,
        containing_block->width);

    if (!layout_absolute_contents(box, containing_block, layout.available_width, content))
        return false;
    layout_absolute_resolve_vertical(box, containing_block, &layout);

    box->y = layout.top + margin[TOP] + border[TOP].width;

    if (block_containing_box) {
        /* Block-level ancestor => reset container's height */
        containing_block->height -= containing_block->padding[TOP] + containing_block->padding[BOTTOM];
    } else {
        /** \todo Inline ancestors */
    }
    box->height = layout.height;
    if (!replaced_object)
        layout_apply_minmax_height(&content->unit_len_ctx, box, containing_block);

    if (box->parent && lh__box_is_flex_container(box->parent) && (auto_x || auto_y)) {
        layout_absolute_flex_static_position(box, cx, cy, auto_x, auto_y);
    }

    NSLOG(layout, DEBUG,
        "abs final: box=%p box->x %i box->y %i box->width %i box->height %i (top=%i left=%i cy=%i cx=%i) abs_cb=%p",
        box, box->x, box->y, box->width, box->height, layout.top, layout.left, cy, cx, box->abs_containing_block);

    return true;
}


/* A non-none transform establishes a containing block even when it is an
 * identity transform. Position:relative alone does not contain fixed boxes. */
static bool layout_has_transform(const struct box *box)
{
    uint32_t count = 0;
    const css_transform_function *functions = NULL;
    return box->style && css_computed_transform(box->style, &count, &functions) == CSS_TRANSFORM_FUNCTIONS &&
        count > 0;
}

/* Shared controls need final dimensions even when an ordinary block does not
 * establish its own formatting context. This runs once per control in the
 * existing final positioning walk, after flex/grid and absolute sizing. */
static void layout_text_control(struct box *box, html_content *content)
{
    if (box->gadget &&
        (box->gadget->type == GADGET_TEXTAREA || box->gadget->type == GADGET_PASSWORD ||
            box->gadget->type == GADGET_TEXTBOX)) {
        plot_font_style_t fstyle;
        int ta_width = box->padding[LEFT] + box->width + box->padding[RIGHT];
        int ta_height = box->padding[TOP] + box->height + box->padding[BOTTOM];
        font_plot_style_from_css(content, box->style, &fstyle);
        fstyle.background = NS_TRANSPARENT;
        textarea_set_layout(box->gadget->data.text.ta, &fstyle, ta_width, ta_height, box->padding[TOP],
            box->padding[RIGHT], box->padding[BOTTOM], box->padding[LEFT]);
        if (box->styles != NULL && box->styles->styles[CSS_PSEUDO_ELEMENT_PLACEHOLDER] != NULL) {
            font_plot_style_from_css(content, box->styles->styles[CSS_PSEUDO_ELEMENT_PLACEHOLDER], &fstyle);
            fstyle.background = NS_TRANSPARENT;
            textarea_set_placeholder_style(box->gadget->data.text.ta, &fstyle);
        }
    }
}


/**
 * Recursively layout and position absolutely positioned boxes.
 *
 * \param  box               tree of boxes to layout
 * \param  containing_block  current containing block
 * \param  cx                position of box relative to containing_block
 * \param  cy                position of box relative to containing_block
 * \param  content           memory pool for any new boxes
 * \return  true on success, false on memory exhaustion
 */
static bool
layout_position_absolute(struct box *box, struct box *containing_block, int cx, int cy, html_content *content)
{
    struct box *c;

    box->flags &= ~HAS_FIXED_DESCENDANT;
    for (c = box->children; c; c = c->next) {
        if ((c->type == BOX_BLOCK || c->type == BOX_TABLE || c->type == BOX_INLINE_BLOCK || c->type == BOX_FLEX ||
                c->type == BOX_GRID || c->type == BOX_INLINE_FLEX || c->type == BOX_INLINE_GRID) &&
            (css_computed_position(c->style) == CSS_POSITION_ABSOLUTE ||
                css_computed_position(c->style) == CSS_POSITION_FIXED)) {
            struct box *abs_cb = containing_block;
            int static_x = cx, static_y = cy;
            if (css_computed_position(c->style) == CSS_POSITION_FIXED) {
                abs_cb = c->parent;
                while (abs_cb && !layout_has_transform(abs_cb))
                    abs_cb = abs_cb->parent;
                if (abs_cb == NULL)
                    abs_cb = &content->viewport.box;
                int cb_x, cb_y;
                box_coords_at_scroll(box, &static_x, &static_y, 0, 0);
                box_coords_at_scroll(abs_cb, &cb_x, &cb_y, 0, 0);
                static_x -= cb_x;
                static_y -= cb_y;
            } else if (c->parent != NULL && (c->parent->type == BOX_FLEX || c->parent->type == BOX_INLINE_FLEX) &&
                c->parent->style != NULL && css_computed_position(c->parent->style) != CSS_POSITION_STATIC) {
                abs_cb = c->parent;
                static_x = static_y = 0;
            }
            const char *cb_tag = "";
            const char *cb_cls = "";
            const char *tag = "";
            const char *cls = "";
            dom_string *cb_name = NULL;
            dom_string *cb_class_attr = NULL;
            dom_string *name = NULL;
            dom_string *class_attr = NULL;
            if (abs_cb->node != NULL) {
                if (dom_node_get_node_name(abs_cb->node, &cb_name) == DOM_NO_ERR && cb_name != NULL) {
                    cb_tag = dom_string_data(cb_name);
                }
                if (dom_element_get_attribute(abs_cb->node, corestring_dom_class, &cb_class_attr) == DOM_NO_ERR &&
                    cb_class_attr != NULL) {
                    cb_cls = dom_string_data(cb_class_attr);
                }
            }
            if (c->node != NULL) {
                if (dom_node_get_node_name(c->node, &name) == DOM_NO_ERR && name != NULL) {
                    tag = dom_string_data(name);
                }
                if (dom_element_get_attribute(c->node, corestring_dom_class, &class_attr) == DOM_NO_ERR &&
                    class_attr != NULL) {
                    cls = dom_string_data(class_attr);
                }
            }
            NSLOG(layout, INFO, "abs call: elem tag %s class %s box %p cb tag %s class %s cb %p cb.width %i", tag, cls,
                c, cb_tag, cb_cls, abs_cb, abs_cb->width);
            if (class_attr != NULL)
                dom_string_unref(class_attr);
            if (name != NULL)
                dom_string_unref(name);
            if (cb_class_attr != NULL)
                dom_string_unref(cb_class_attr);
            if (cb_name != NULL)
                dom_string_unref(cb_name);
            if (!layout_absolute(c, abs_cb, static_x, static_y, content))
                return false;
            if (!layout_position_absolute(c, c, 0, 0, content))
                return false;
        } else if ((c->style && (css_computed_position(c->style) == CSS_POSITION_RELATIVE ||
                                   css_computed_position(c->style) == CSS_POSITION_STICKY)) ||
            layout_has_transform(c)) {
            if (!layout_position_absolute(c, c, 0, 0, content))
                return false;
        } else {
            int px, py;
            if (c->style &&
                (css_computed_float(c->style) == CSS_FLOAT_LEFT || css_computed_float(c->style) == CSS_FLOAT_RIGHT)) {
                /* Float x/y coords are relative to nearest
                 * ansestor with float_children, rather than
                 * relative to parent. Need to get x/y relative
                 * to parent */
                struct box *p;
                px = c->x;
                py = c->y;
                for (p = box->parent; p && !p->float_children; p = p->parent) {
                    px -= p->x;
                    py -= p->y;
                }
            } else {
                /* Not a float, so box x/y coords are relative
                 * to parent */
                px = c->x;
                py = c->y;
            }
            if (!layout_position_absolute(c, containing_block, cx + px, cy + py, content))
                return false;
        }
        if ((c->flags & HAS_FIXED_DESCENDANT) ||
            (c->abs_containing_block && (c->abs_containing_block->flags & IS_VIEWPORT)))
            box->flags |= HAS_FIXED_DESCENDANT;
    }

    layout_text_control(box, content);

    return true;
}


/**
 * Compute a box's relative offset as per CSS 2.1 9.4.3
 *
 * \param  unit_len_ctx  Length conversion context
 * \param  box	Box to compute relative offsets for.
 * \param  x	Receives relative offset in x.
 * \param  y	Receives relative offset in y.
 */
static void layout_compute_relative_offset(const css_unit_ctx *unit_len_ctx, struct box *box, int *x, int *y)
{
    int left, right, top, bottom;
    struct box *containing_block;

    assert(box && box->parent && box->style && css_computed_position(box->style) == CSS_POSITION_RELATIVE);

    if (box->float_container &&
        (css_computed_float(box->style) == CSS_FLOAT_LEFT || css_computed_float(box->style) == CSS_FLOAT_RIGHT)) {
        containing_block = box->float_container;
    } else {
        containing_block = box->parent;
    }

    layout_compute_offsets(unit_len_ctx, box, containing_block, &top, &right, &bottom, &left);

    if (left == AUTO && right == AUTO)
        left = right = 0;
    else if (left == AUTO)
        /* left is auto => computed = -right */
        left = -right;
    else if (right == AUTO)
        /* right is auto => computed = -left */
        right = -left;
    else {
        /* over constrained => examine direction property
         * of containing block */
        if (containing_block->style && css_computed_direction(containing_block->style) == CSS_DIRECTION_RTL) {
            /* right wins */
            left = -right;
        } else {
            /* assume LTR in all other cases */
            right = -left;
        }
    }

    assert(left == -right);

    if (top == AUTO && bottom == AUTO) {
        top = bottom = 0;
    } else if (top == AUTO) {
        top = -bottom;
    } else {
        /* bottom is AUTO, or neither are AUTO */
        bottom = -top;
    }

    NSLOG(layout, DEBUG, "left %i, right %i, top %i, bottom %i", left, right, top, bottom);

    *x = left;
    *y = top;
}


/**
 * Adjust positions of relatively positioned boxes.
 *
 * \param  unit_len_ctx  Length conversion context
 * \param  root  box to adjust the position of
 * \param  fp    box which forms the block formatting context for children of
 *		 "root" which are floats
 * \param  fx    x offset due to intervening relatively positioned boxes
 *               between current box, "root", and the block formatting context
 *               box, "fp", for float children of "root"
 * \param  fy    y offset due to intervening relatively positioned boxes
 *               between current box, "root", and the block formatting context
 *               box, "fp", for float children of "root"
 */
static void layout_position_relative(const css_unit_ctx *unit_len_ctx, struct box *root, struct box *fp, int fx, int fy)
{
    struct box *box; /* for children of "root" */
    struct box *fn; /* for block formatting context box for children of
                     * "box" */
    struct box *fc; /* for float children of the block formatting context,
                     * "fp" */
    int x, y; /* for the offsets resulting from any relative
               * positioning on the current block */
    int fnx, fny; /* for affsets which apply to flat children of "box" */

    /**\todo ensure containing box is large enough after moving boxes */

    assert(root);

    /* No further sizing uses the current-pass permission. Expire it on this
     * existing walk so ordinary reflows cannot reuse an earlier pass's lines. */
    root->flags &= ~(REUSE_INLINE_LAYOUT | INLINE_LAYOUT_CURRENT);

    /* Normal children */
    for (box = root->children; box; box = box->next) {

        if (box->type == BOX_TEXT)
            continue;

        /* If relatively positioned, get offsets */
        if (box->style && css_computed_position(box->style) == CSS_POSITION_RELATIVE)
            layout_compute_relative_offset(unit_len_ctx, box, &x, &y);
        else
            x = y = 0;

        /* Adjust float coordinates.
         * (note float x and y are relative to their block formatting
         * context box and not their parent) */
        if (box->style &&
            (css_computed_float(box->style) == CSS_FLOAT_LEFT || css_computed_float(box->style) == CSS_FLOAT_RIGHT) &&
            (fx != 0 || fy != 0)) {
            /* box is a float and there is a float offset to
             * apply */
            for (fc = fp->float_children; fc; fc = fc->next_float) {
                if (box == fc->children) {
                    /* Box is floated in the block
                     * formatting context block, fp.
                     * Apply float offsets. */
                    box->x += fx;
                    box->y += fy;
                    fx = fy = 0;
                }
            }
        }

        if (box->float_children) {
            fn = box;
            fnx = fny = 0;
        } else {
            fn = fp;
            fnx = fx + x;
            fny = fy + y;
        }

        /* recurse first */
        layout_position_relative(unit_len_ctx, box, fn, fnx, fny);

        /* Ignore things we're not interested in. */
        if (!box->style || (box->style && css_computed_position(box->style) != CSS_POSITION_RELATIVE))
            continue;

        box->x += x;
        box->y += y;

        /* Handle INLINEs - their "children" are in fact
         * the sibling boxes between the INLINE and
         * INLINE_END boxes */
        if (box->type == BOX_INLINE && box->inline_end) {
            struct box *b;
            for (b = box->next; b && b != box->inline_end; b = b->next) {
                b->x += x;
                b->y += y;
            }
        }
    }
}


/**
 * Find a box's bounding box relative to itself, i.e. the box's border edge box
 *
 * \param  unit_len_ctx  Length conversion context
 * \param  box      box find bounding box of
 * \return Border bounds, including the existing text overflow allowance.
 */
static struct rect layout_get_box_bbox(const css_unit_ctx *unit_len_ctx, struct box *box)
{
    /* Final geometry consumes pixels, never unresolved layout states. */
    assert(lh__is_resolved(box->width));
    assert(lh__is_resolved(box->height));
#ifndef NDEBUG
    for (int side = TOP; side <= LEFT; side++) {
        assert(lh__is_resolved(box->padding[side]));
        assert(lh__is_resolved(box->border[side].width));
    }
#endif
    /* Catch boxes with unresolved width - this would cause overflow */
    if (box->width == UNKNOWN_WIDTH || box->width >= 100000000) {
        int pos = (box->style) ? css_computed_position(box->style) : -1;
        fprintf(stderr, "CRITICAL: Box %p Type %d Position %d has invalid width %d in layout_get_box_bbox\n",
            (void *)box, box->type, pos, box->width);
        struct box *p = box->parent;
        int depth = 0;

        fprintf(stderr, "CRITICAL: Box %p (width %d) Ancestry:\n", (void *)box, box->width);

        while (p && depth < 20) {
            fprintf(stderr, "CRITICAL: Ancestor[%d] %p type %d width %d\n", depth, (void *)p, p->type, p->width);
            p = p->parent;
            depth++;
        }
        fflush(stderr);
        abort();
    }

    /* Check padding and border for INT_MAX values */
    if (box->padding[LEFT] >= 100000000) {
        fprintf(stderr, "CRITICAL: Box %p has huge padding[LEFT] %d\n", (void *)box, box->padding[LEFT]);
        fflush(stderr);
        abort();
    }
    if (box->padding[RIGHT] >= 100000000) {
        fprintf(stderr, "CRITICAL: Box %p has huge padding[RIGHT] %d\n", (void *)box, box->padding[RIGHT]);
        fflush(stderr);
        abort();
    }
    if (box->border[RIGHT].width >= 100000000) {
        fprintf(stderr, "CRITICAL: Box %p has huge border[RIGHT] %d\n", (void *)box, box->border[RIGHT].width);
        fflush(stderr);
        abort();
    }

    struct rect bounds = {-box->border[LEFT].width, -box->border[TOP].width,
        box->padding[LEFT] + box->width + box->padding[RIGHT] + box->border[RIGHT].width,
        box->padding[TOP] + box->height + box->padding[BOTTOM] + box->border[BOTTOM].width};

    /* Assert: result must be reasonable */
    assert(bounds.x1 < 100000000 && "desc_x1 calculation produced huge value");

    /* To stop the top of text getting clipped when css line-height is
     * reduced, we increase the top of the descendant bbox. */
    if (box->type == BOX_BLOCK && box->style != NULL && css_computed_overflow_y(box->style) == CSS_OVERFLOW_VISIBLE &&
        box->object == NULL) {
        css_fixed_or_calc font_size = (css_fixed_or_calc)0;
        css_unit font_unit = CSS_UNIT_PT;
        int text_height;

        css_computed_font_size(box->style, &font_size, &font_unit);
        if (font_unit == CSS_UNIT_CALC) {
            if (!lh__length_to_px(box->style, unit_len_ctx, -1, font_size, font_unit, &text_height)) {
                text_height = 0;
            }
        } else {
            text_height = css_unit_len2device_px(box->style, unit_len_ctx, font_size.value, font_unit);
        }
        text_height = FIXTOINT(text_height * 3 / 4);
        bounds.y0 = min(bounds.y0, -text_height);
    }
    return bounds;
}


/**
 * Apply changes to box descendant_[xy][01] values due to given child.
 *
 * \param  unit_len_ctx  Length conversion context
 * \param  box      box to update
 * \param  child    a box, which may affect box's descendant bbox
 */
static void layout_update_descendant_bbox(const css_unit_ctx *unit_len_ctx, struct box *box, struct box *child)
{
    if (child->abs_containing_block && (child->abs_containing_block->flags & IS_VIEWPORT))
        return;

    int child_x = child->x, child_y = child->y;
    struct box *coordinate_parent = child->abs_containing_block;
    if (!coordinate_parent) {
        coordinate_parent = (child->type == BOX_FLOAT_LEFT || child->type == BOX_FLOAT_RIGHT) ? child->float_container
                                                                                              : child->parent;
    }
    if (coordinate_parent != box) {
        /* Inline contents are flattened siblings; floats and positioned boxes
         * use a coordinate ancestor that can skip structural parents. Put all
         * of them in this box's unscrolled coordinate space before unioning. */
        if (box->type == BOX_INLINE && coordinate_parent == box->parent) {
            child_x -= box->x;
            child_y -= box->y;
        } else {
            int box_x, box_y;
            box_coords_unscrolled(child, &child_x, &child_y);
            box_coords_unscrolled(box, &box_x, &box_y);
            child_x -= box_x;
            child_y -= box_y;
        }
    }

    bool html_object = (child->object && content_get_type(child->object) == CONTENT_HTML);

    enum css_overflow_e overflow_x = CSS_OVERFLOW_VISIBLE;
    enum css_overflow_e overflow_y = CSS_OVERFLOW_VISIBLE;

    if (child->style != NULL) {
        overflow_x = css_computed_overflow_x(child->style);
        overflow_y = css_computed_overflow_y(child->style);
    }

    /* Assert: child's width must be valid before computing bbox */
    assert(child->width != UNKNOWN_WIDTH && "child has UNKNOWN_WIDTH in layout_update_descendant_bbox");
    assert(
        child->width >= -1 && child->width < 100000000 && "child has invalid width in layout_update_descendant_bbox");

    /* Get child's border edge */
    struct rect bounds = layout_get_box_bbox(unit_len_ctx, child);

    if (overflow_x == CSS_OVERFLOW_VISIBLE && html_object == false) {
        /* get child's descendant bbox relative to box */
        bounds.x0 = child->descendant_x0;
        bounds.x1 = child->descendant_x1;
        /* Assert: child's descendant_x1 must be reasonable */
        assert(bounds.x1 < 100000000 && "child has INT_MAX descendant_x1 - propagating to parent");
    }
    if (overflow_y == CSS_OVERFLOW_VISIBLE && html_object == false) {
        /* get child's descendant bbox relative to box */
        bounds.y0 = child->descendant_y0;
        bounds.y1 = child->descendant_y1;
    }

    bounds.x0 += child_x;
    bounds.y0 += child_y;
    bounds.x1 += child_x;
    bounds.y1 += child_y;

    /* increase box's descendant bbox to contain descendants */
    assert(bounds.x1 < 100000000 && "About to set box->descendant_x1 to INT_MAX from child");
    box->descendant_x0 = min(box->descendant_x0, bounds.x0);
    box->descendant_y0 = min(box->descendant_y0, bounds.y0);
    box->descendant_x1 = max(box->descendant_x1, bounds.x1);
    box->descendant_y1 = max(box->descendant_y1, bounds.y1);
}


/**
 * Recursively calculate the descendant_[xy][01] values for a laid-out box tree
 * and inform iframe browser windows of their size and position.
 *
 * \param  unit_len_ctx  Length conversion context
 * \param  box      tree of boxes to update
 */
static void layout_calculate_descendant_bboxes(const css_unit_ctx *unit_len_ctx, struct box *box)
{
    struct box *child;

    assert(lh__is_resolved(box->width));
    assert(lh__is_resolved(box->height));
    /* assert((box->width >= 0) && (box->height >= 0)); */

    /* Handle boxes with invalid negative width - set descendant to box size to prevent overflow */
    if (box->width < 0) {
        box->descendant_x0 = 0;
        box->descendant_y0 = 0;
        box->descendant_x1 = 0; /* Safe default */
        box->descendant_y1 = box->height > 0 ? box->height : 0;
        return; /* Skip normal processing - width is invalid */
    }

    /* Initialise box's descendant box to border edge box */
    struct rect bounds = layout_get_box_bbox(unit_len_ctx, box);
    box->descendant_x0 = bounds.x0;
    box->descendant_y0 = bounds.y0;
    box->descendant_x1 = bounds.x1;
    box->descendant_y1 = bounds.y1;
    if (box->type == BOX_TABLE && table_has_collapsed_borders(box)) {
        const struct table_border_grid *grid = box->table->borders;
        /* CSS 2.2 uses the first row to size the table's left/right outer
         * borders. Wider borders in subsequent rows overflow that box. */
        for (unsigned int row = 0; row < grid->rows; row++) {
            const struct table_border_edge *edge = &grid->vertical[(size_t)row * (grid->columns + 1)];
            box->descendant_x0 = min(box->descendant_x0, grid->x[0] - edge[0].border.width / 2);
            box->descendant_x1 = max(
                box->descendant_x1, grid->x[grid->columns] + (edge[grid->columns].border.width + 1) / 2);
        }
    }
    if (box->type == BOX_INLINE_GRID && box->descendant_x1 > 100000000) {
        fprintf(stderr, "BBOX_RESULT: BOX_INLINE_GRID %p AFTER layout_get_box_bbox descendant_x1=%d\n", (void *)box,
            box->descendant_x1);
        fflush(stderr);
        assert(0 && "layout_get_box_bbox produced INT_MAX");
    }

    /* Extend it to contain HTML contents if box is replaced */
    if (box->object && content_get_type(box->object) == CONTENT_HTML) {
        int obj_width = FIXTOINT(
            css_unit_device2css_px(INTTOFIX(content_get_width(box->object)), unit_len_ctx->device_dpi));
        int obj_height = FIXTOINT(
            css_unit_device2css_px(INTTOFIX(content_get_height(box->object)), unit_len_ctx->device_dpi));
        if (box->descendant_x1 < obj_width)
            box->descendant_x1 = obj_width;
        if (box->descendant_y1 < obj_height)
            box->descendant_y1 = obj_height;
    }

    if (box->iframe != NULL) {
        int x, y;
        box_coords_unscrolled(box, &x, &y);

        browser_window_set_position(box->iframe, x, y);
        browser_window_set_dimensions(box->iframe, box->width, box->height);
        browser_window_reformat(box->iframe, true, box->width, box->height);
    }

    if (box->type == BOX_INLINE || box->type == BOX_TEXT)
        return;

    if (box->type == BOX_INLINE_END) {
        box = box->inline_end;
        for (child = box->next; child; child = child->next) {
            if (child->type == BOX_FLOAT_LEFT || child->type == BOX_FLOAT_RIGHT)
                continue;

            layout_update_descendant_bbox(unit_len_ctx, box, child);

            if (child == box->inline_end)
                break;
        }
        return;
    }

    if (box->flags & REPLACE_DIM)
        /* Box's children aren't displayed if the box is replaced */
        return;

    /* Paint walks the structural tree (CSS 2.2 Appendix E.2). Bounds used to
     * prune that walk must include floats through every intervening ancestor,
     * even a zero-height inline container. Walking the formatting-context
     * float list separately would bypass those ancestors' bounds/clips.
     * Keep each box's content extents here and apply its overflow when merging
     * it into its parent. This does not change any normal-flow dimensions. */
    for (child = box->children; child; child = child->next) {
        layout_calculate_descendant_bboxes(unit_len_ctx, child);

        /* Catch if child has invalid descendant values after processing */
        assert(child->descendant_x1 < 100000000 &&
            "Child has huge descendant_x1 after layout_calculate_descendant_bboxes");

        layout_update_descendant_bbox(unit_len_ctx, box, child);
    }

    if (box_has_outside_marker(box)) {
        child = box->list_marker;
        layout_calculate_descendant_bboxes(unit_len_ctx, child);

        layout_update_descendant_bbox(unit_len_ctx, box, child);
    }
}

/* Reuse the final diagnostic walk for constraint preparation after every box's
 * descendant bounds have settled. This adds no separate document traversal. */
static bool layout_finalize_boxes(html_content *content, struct box *box)
{
    const css_unit_ctx *unit_len_ctx = &content->unit_len_ctx;
    struct box *child;

    if (box == NULL)
        return true;
    /* Background geometry is still trimmed before applying CSS transforms.
     * Use a full viewport pass until that coordinate-space mixing is fixed.
     * Embedded HTML objects have no browser-window query for their descendants. */
    if (layout_has_transform(box) || (box->object && content_get_type(box->object) == CONTENT_HTML))
        content->redraw_region_safe = false;
    /* Sticky movement and frames have a sparse damage query. Other viewport-dependent
     * painting still needs a full repaint; scrolling must not scan the DOM. */
    if ((box->style && css_computed_position(box->style) == CSS_POSITION_FIXED) ||
        (box->object && content_get_type(box->object) == CONTENT_HTML))
        content->scroll_reuse_safe = false;
    if (content->scroll_reuse_safe && box->style) {
        for (uint32_t i = 0; i < css_computed_background_layer_count(box->style); i++) {
            if (css_computed_background_attachment_layer(box->style, i) == CSS_BACKGROUND_ATTACHMENT_FIXED) {
                content->scroll_reuse_safe = false;
                break;
            }
        }
    }
    box->flags &= ~(HAS_STICKY_DESCENDANT | HAS_STICKY_ANCESTOR | HAS_IFRAME_DESCENDANT);
    if (box->flags & IFRAME) {
        for (struct box *ancestor = box->parent; ancestor; ancestor = ancestor->parent) {
            if (ancestor->flags & HAS_IFRAME_DESCENDANT)
                break;
            ancestor->flags |= HAS_IFRAME_DESCENDANT;
        }
    }
    if (box->parent && !(box->abs_containing_block && (box->abs_containing_block->flags & IS_VIEWPORT)) &&
        ((box->parent->flags & HAS_STICKY_ANCESTOR) || (box->parent->sticky && box->parent->sticky->valid)))
        box->flags |= HAS_STICKY_ANCESTOR;
    if (!box_sticky_prepare(content, box))
        return false;

    if (box->node != NULL) {
        const char *tag = "";
        const char *cls = "";
        dom_string *name = NULL;
        dom_string *class_attr = NULL;

        if (dom_node_get_node_name(box->node, &name) == DOM_NO_ERR && name != NULL) {
            tag = dom_string_data(name);
        }
        if (dom_element_get_attribute(box->node, corestring_dom_class, &class_attr) == DOM_NO_ERR &&
            class_attr != NULL) {
            cls = dom_string_data(class_attr);
        }

        bool is_site_nav = (cls[0] != '\0' && strstr(cls, "site-navigation") != NULL);
        bool is_site_nav_child = false;

        if (!is_site_nav && box->parent && box->parent->node) {
            dom_string *pclass_attr = NULL;
            if (dom_element_get_attribute(box->parent->node, corestring_dom_class, &pclass_attr) == DOM_NO_ERR &&
                pclass_attr != NULL) {
                const char *pcls = dom_string_data(pclass_attr);
                if (pcls != NULL && strstr(pcls, "site-navigation") != NULL) {
                    is_site_nav_child = true;
                }
            }
            if (pclass_attr != NULL) {
                dom_string_unref(pclass_attr);
            }
        }

        if (cls[0] != '\0' && (strstr(cls, "row") != NULL || is_site_nav)) {
            int total_height = box->height + box->padding[TOP] + box->padding[BOTTOM] + box->border[TOP].width +
                box->border[BOTTOM].width + lh__non_auto_margin(box, TOP) + lh__non_auto_margin(box, BOTTOM);
            NSLOG(layout, INFO, "FINAL_HEIGHT tag=%s class=%s box=%p y=%d h=%d total=%d m=(%d,%d) p=(%d,%d) b=(%d,%d)",
                tag, cls, box, box->y, box->height, total_height, box->margin[TOP], box->margin[BOTTOM],
                box->padding[TOP], box->padding[BOTTOM], box->border[TOP].width, box->border[BOTTOM].width);
        }
        if (is_site_nav && box->style != NULL) {
            css_fixed_or_calc hval = (css_fixed_or_calc)0;
            css_unit hunit = CSS_UNIT_PX;
            css_fixed_or_calc min_hval = (css_fixed_or_calc)0;
            css_unit min_hunit = CSS_UNIT_PX;
            css_fixed_or_calc max_hval = (css_fixed_or_calc)0;
            css_unit max_hunit = CSS_UNIT_PX;
            css_fixed_or_calc lhval = (css_fixed_or_calc)0;
            css_unit lhunit = CSS_UNIT_PX;
            enum css_height_e htype = css_computed_height(box->style, &hval, &hunit);
            enum css_min_height_e min_htype = ns_computed_min_height(box->style, &min_hval, &min_hunit);
            enum css_max_height_e max_htype = css_computed_max_height(box->style, &max_hval, &max_hunit);
            enum css_line_height_e lhtype = css_computed_line_height(box->style, &lhval, &lhunit);
            int hpx = -1;
            int min_hpx = -1;
            int max_hpx = -1;
            int lhpx = line_height(unit_len_ctx, box->style);

            if (htype == CSS_HEIGHT_SET && hunit != CSS_UNIT_PCT) {
                if (!lh__length_to_px(box->style, unit_len_ctx, -1, hval, hunit, &hpx)) {
                    hpx = -1;
                }
            }
            if (min_htype == CSS_MIN_HEIGHT_SET && min_hunit != CSS_UNIT_PCT) {
                if (!lh__length_to_px(box->style, unit_len_ctx, -1, min_hval, min_hunit, &min_hpx)) {
                    min_hpx = -1;
                }
            }
            if (max_htype == CSS_MAX_HEIGHT_SET && max_hunit != CSS_UNIT_PCT) {
                if (!lh__length_to_px(box->style, unit_len_ctx, -1, max_hval, max_hunit, &max_hpx)) {
                    max_hpx = -1;
                }
            }

            NSLOG(layout, INFO,
                "SITE_NAV style box=%p height_type=%d height_px=%d min_type=%d min_px=%d max_type=%d max_px=%d line_type=%d line_px=%d",
                box, htype, hpx, min_htype, min_hpx, max_htype, max_hpx, lhtype, lhpx);

            for (child = box->children; child; child = child->next) {
                const char *ctag = "";
                const char *ccls = "";
                dom_string *cname = NULL;
                dom_string *cclass_attr = NULL;
                if (child->node != NULL) {
                    if (dom_node_get_node_name(child->node, &cname) == DOM_NO_ERR && cname != NULL) {
                        ctag = dom_string_data(cname);
                    }
                    if (dom_element_get_attribute(child->node, corestring_dom_class, &cclass_attr) == DOM_NO_ERR &&
                        cclass_attr != NULL) {
                        ccls = dom_string_data(cclass_attr);
                    }
                }
                NSLOG(layout, INFO, "SITE_NAV_CHILD tag=%s class=%s type=%d y=%d h=%d total=%d", ctag, ccls,
                    child->type, child->y, child->height,
                    child->height + child->padding[TOP] + child->padding[BOTTOM] + child->border[TOP].width +
                        child->border[BOTTOM].width + lh__non_auto_margin(child, TOP) +
                        lh__non_auto_margin(child, BOTTOM));
                if (cclass_attr != NULL) {
                    dom_string_unref(cclass_attr);
                }
                if (cname != NULL) {
                    dom_string_unref(cname);
                }
            }
        }
        if (is_site_nav_child) {
            NSLOG(
                layout, INFO, "SITE_NAV_CHILDREN tag=%s class=%s box=%p y=%d h=%d", tag, cls, box, box->y, box->height);
            for (child = box->children; child; child = child->next) {
                const char *ctag = "";
                const char *ccls = "";
                dom_string *cname = NULL;
                dom_string *cclass_attr = NULL;
                if (child->node != NULL) {
                    if (dom_node_get_node_name(child->node, &cname) == DOM_NO_ERR && cname != NULL) {
                        ctag = dom_string_data(cname);
                    }
                    if (dom_element_get_attribute(child->node, corestring_dom_class, &cclass_attr) == DOM_NO_ERR &&
                        cclass_attr != NULL) {
                        ccls = dom_string_data(cclass_attr);
                    }
                }
                NSLOG(layout, INFO,
                    "SITE_NAV_GRANDCHILD tag=%s class=%s type=%d y=%d h=%d total=%d m=(%d,%d) p=(%d,%d) b=(%d,%d)",
                    ctag, ccls, child->type, child->y, child->height,
                    child->height + child->padding[TOP] + child->padding[BOTTOM] + child->border[TOP].width +
                        child->border[BOTTOM].width + lh__non_auto_margin(child, TOP) +
                        lh__non_auto_margin(child, BOTTOM),
                    child->margin[TOP], child->margin[BOTTOM], child->padding[TOP], child->padding[BOTTOM],
                    child->border[TOP].width, child->border[BOTTOM].width);
                if (cclass_attr != NULL) {
                    dom_string_unref(cclass_attr);
                }
                if (cname != NULL) {
                    dom_string_unref(cname);
                }
            }
        }

        if (class_attr != NULL) {
            dom_string_unref(class_attr);
        }
        if (name != NULL) {
            dom_string_unref(name);
        }
    }

    for (child = box->children; child; child = child->next) {
        if (child->type == BOX_FLOAT_LEFT || child->type == BOX_FLOAT_RIGHT) {
            continue;
        }
        if (!layout_finalize_boxes(content, child))
            return false;
    }

    for (child = box->float_children; child; child = child->next_float) {
        if (!layout_finalize_boxes(content, child))
            return false;
    }

    if (box_has_outside_marker(box)) {
        if (!layout_finalize_boxes(content, box->list_marker))
            return false;
    }
    return true;
}


/* Only these containers keep their intrinsic min/max fields through used
 * layout. Blocks overwrite theirs with CSS sizing constraints. Exclude atomic
 * children, floats and widgets: their measurement depends on more than the
 * retained text/styles and the document inputs already checked by restyle. */
static bool layout_can_reuse_inline_intrinsics(const struct box *box)
{
    if (box->type != BOX_INLINE_CONTAINER || box->max_width == UNKNOWN_MAX_WIDTH ||
        box->parent == NULL || box->parent->type != BOX_BLOCK || box->parent->gadget != NULL ||
        box->parent->object != NULL || box->parent->list_marker != NULL)
        return false;
    for (const struct box *child = box->children; child != NULL; child = child->next) {
        if ((child->type != BOX_TEXT && child->type != BOX_INLINE && child->type != BOX_INLINE_END &&
                child->type != BOX_BR) || child->children != NULL || child->object != NULL ||
            child->gadget != NULL || child->list_marker != NULL ||
            (child->flags & (IS_REPLACED | REPLACE_DIM | IFRAME)))
            return false;
    }
    return true;
}

static void layout_reset_box(struct box *box)
{
    /* Rejoin before intrinsic measurement, not just before line layout: old
     * wrapping must not change a retained sibling's min/max contributions. */
    box_rejoin_text(box);
    box->flags &= ~(NEW_LINE | MEASURED | HAS_HEIGHT | MAKE_HEIGHT | NEED_MIN | INLINE_LAYOUT_CURRENT |
        HEIGHT_STRETCHED | ASPECT_RATIO_HEIGHT | HAS_FIXED_DESCENDANT | HAS_STICKY_DESCENDANT | HAS_STICKY_ANCESTOR |
        HAS_IFRAME_DESCENDANT);
    if (box->sticky)
        box->sticky->valid = false;
    box->x = box->y = 0;
    if (!(box->flags & REUSE_INLINE_LAYOUT)) {
        box->flags &= ~(HAS_BASELINE | INLINE_LAYOUT_VALID);
        box->width = UNKNOWN_WIDTH;
        box->height = box->baseline = 0;
        box->inline_layout_width = 0;
    }
    if (!(box->flags & REUSE_INLINE_INTRINSICS)) {
        box->min_width = (struct css_size){.type = CSS_SIZE_AUTO, .value = 0};
        box->content_min_width = 0;
        box->max_width = UNKNOWN_MAX_WIDTH;
        box->flags &= ~(INLINE_INTRINSIC_HEIGHT | INLINE_INTRINSIC_NEED_MIN);
    }
    box->descendant_x0 = box->descendant_y0 = 0;
    box->descendant_x1 = box->descendant_y1 = 0;
    for (unsigned i = 0; i < 4; i++)
        box->margin[i] = box->padding[i] = box->border[i].width = 0;
    if (box->space != 0)
        box->space = UNKNOWN_WIDTH;
    box->byte_offset = 0;
    box->float_children = box->next_float = box->float_container = NULL;
    box->abs_containing_block = NULL;
    box->clear_level = box->cached_place_below_level = 0;

    /* Column types depend on cell styles/units; min/max and used widths are
     * also cached here. Row/column spans belong to normalization and survive. */
    talloc_free(box->table);
    box->table = NULL;
}

/* Only text and inline markers were preserved. Reset/rejoin before either
 * intrinsic measurement or line layout, without touching the parent's sizing. */
static void layout_discard_inline_layout(struct box *box)
{
    box->flags &= ~(INLINE_LAYOUT_VALID | REUSE_INLINE_LAYOUT | INLINE_LAYOUT_CURRENT | HAS_BASELINE);
    box->width = UNKNOWN_WIDTH;
    box->height = box->baseline = 0;
    box->inline_layout_width = 0;
    for (struct box *child = box->children; child != NULL; child = child->next)
        layout_reset_box(child);
}

/* exported function documented in html/layout.h */
void layout_reset_document(html_content *content)
{
    struct box *root = content->layout, *box = root;
    content->sticky_layout_valid = false;
    assert(content->base.locked && content->restyling);
    assert(content->frameset == NULL);

    /* Follow structural children only: float chains are derived, and markers
     * are owned separately. No traversal stack or allocation per box. */
    while (box != NULL) {
        box->flags &= ~(REUSE_INLINE_INTRINSICS | REUSE_INLINE_LAYOUT);
        if (!content->restyle.no_intrinsic_reuse && layout_can_reuse_inline_intrinsics(box))
            box->flags |= REUSE_INLINE_INTRINSICS;
        if (!content->restyle.no_inline_layout_reuse && (box->flags & REUSE_INLINE_INTRINSICS) &&
            (box->flags & INLINE_LAYOUT_VALID) && layout_can_reuse_inline_layout(box))
            box->flags |= REUSE_INLINE_LAYOUT;
        layout_reset_box(box);
        if (box->list_marker != NULL) {
            struct box *marker = box->list_marker;
            if (box_has_outside_marker(box))
                layout_reset_box(marker);
            marker->list_value = 1;
            if (layout__list_item_is_numerical(box)) {
                /* Numbering can change outside the replaced region. Symbolic
                 * markers use static strings; only numerical text is owned. */
                talloc_free(marker->text);
                marker->text = NULL;
                marker->length = 0;
            }
        }
        if (box->children != NULL && !(box->flags & REUSE_INLINE_LAYOUT)) {
            box = box->children;
            continue;
        }
        while (box != root && box->next == NULL)
            box = box->parent;
        box = box == root ? NULL : box->next;
    }
}

/* exported function documented in html/layout.h */
bool layout_document(html_content *content, int width, int height)
{
    struct box *doc = content->layout;
    content->sticky_layout_valid = false;
    const struct gui_layout_table *font_func = content->font_func;

    NSLOG(wisp, DEBUG, "PROFILER: START layout_document %p", content);
    HTML_PROFILE_START(document_layout_start);
    HTML_PROFILE_CPU_START(document_layout_cpu_start);
    HTML_PROFILE(content->render_profile.layout_calls++);

    NSLOG(layout, DEBUG, "Doing layout to %ix%i of %s", width, height, nsurl_access(content_get_url(&content->base)));

    content->viewport = (struct html_viewport){
        .box = {.type = BOX_BLOCK, .flags = IS_VIEWPORT, .width = width, .height = height},
        .owner = content,
    };

    if (!layout_lists(content, doc) || !layout_minmax_block(doc, font_func, content))
        goto failed;

    layout_block_find_dimensions(&content->unit_len_ctx, width, height, 0, 0, doc);
    doc->x = doc->margin[LEFT] + doc->border[LEFT].width;
    doc->y = doc->margin[TOP] + doc->border[TOP].width;
    width -= doc->margin[LEFT] + doc->border[LEFT].width + doc->padding[LEFT] + doc->padding[RIGHT] +
        doc->border[RIGHT].width + doc->margin[RIGHT];
    if (width < 0) {
        width = 0;
    }
    doc->width = width;

    if (!layout_block_context(doc, height, content))
        goto failed;

    /* make <html> and <body> fill available height */
    if (doc->y + doc->padding[TOP] + doc->height + doc->padding[BOTTOM] + doc->border[BOTTOM].width +
            doc->margin[BOTTOM] <
        height) {
        doc->height = height -
            (doc->y + doc->padding[TOP] + doc->padding[BOTTOM] + doc->border[BOTTOM].width + doc->margin[BOTTOM]);
        if (doc->children) {
            /* The first child can be an anonymous inline container when body
             * is inline. Its stretched height is no longer the line result. */
            doc->children->flags &= ~(INLINE_LAYOUT_VALID | INLINE_LAYOUT_CURRENT);
            doc->children->height = doc->height -
                (doc->children->margin[TOP] + doc->children->border[TOP].width + doc->children->padding[TOP] +
                    doc->children->padding[BOTTOM] + doc->children->border[BOTTOM].width +
                    doc->children->margin[BOTTOM]);
        }
    }

    if (!layout_position_absolute(doc, doc, 0, 0, content))
        goto failed;
    layout_position_relative(&content->unit_len_ctx, doc, doc, 0, 0);

    layout_calculate_descendant_bboxes(&content->unit_len_ctx, doc);
    content->scroll_reuse_safe = true;
    content->redraw_region_safe = true;
    if (!layout_finalize_boxes(content, doc))
        goto failed;
    content->sticky_layout_valid = true;
    HTML_PROFILE(content->render_profile.layout_ns += html_profile_now() - document_layout_start);
    HTML_PROFILE(if (document_layout_cpu_start)
                     content->render_profile.layout_cpu_ns += html_profile_cpu_now() - document_layout_cpu_start);

    NSLOG(wisp, DEBUG, "PROFILER: STOP layout_document %p", content);

    return true;

failed:
    /* Failed layout does not reach the final positioning walk. Never leave
     * pass-local permissions behind, even when only part of the tree ran. */
    for (struct box *box = doc; box != NULL;) {
        box->flags &= ~(REUSE_INLINE_LAYOUT | INLINE_LAYOUT_CURRENT);
        if (box->children != NULL) {
            box = box->children;
            continue;
        }
        while (box != doc && box->next == NULL)
            box = box->parent;
        box = box == doc ? NULL : box->next;
    }
    return false;
}

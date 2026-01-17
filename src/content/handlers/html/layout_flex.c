/*
 * Copyright 2022 Michael Drake <tlsa@netsurf-browser.org>
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
 * HTML layout implementation: display: flex.
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

#include <string.h>

#include <neosurf/utils/corestrings.h>
#include <neosurf/utils/log.h>
#include <neosurf/utils/utils.h>

#include <neosurf/content/handlers/html/box.h>
#include <neosurf/content/handlers/html/box_inspect.h>
#include <neosurf/content/handlers/html/html.h>
#include <neosurf/content/handlers/html/private.h>
#include "content/handlers/html/layout_internal.h"

/* Type for values that can be either a fixed value or a calc expression */
#ifndef css_fixed_or_calc_typedef
#define css_fixed_or_calc_typedef
typedef union {
    css_fixed value;
    lwc_string *calc;
} css_fixed_or_calc;
#endif

/**
 * Flex item data
 */
struct flex_item_data {
    enum css_flex_basis_e basis;
    css_fixed_or_calc basis_length;
    css_unit basis_unit;
    struct box *box;

    css_fixed shrink;
    css_fixed grow;

    struct css_size min_main;
    int max_main;
    struct css_size min_cross;
    int max_cross;

    int target_main_size;
    int base_size;
    int main_size;
    size_t line;

    bool freeze;
    bool min_violation;
    bool max_violation;
    bool has_pct_basis; /* True if flex-basis was a percentage (for two-pass layout) */

    int32_t order;
    size_t original_index;
};

/**
 * Flex line data
 */
struct flex_line_data {
    int main_size;
    int cross_size;

    int used_main_size;
    int main_auto_margin_count;

    int pos;

    size_t first;
    size_t count;
    size_t frozen;
};

/**
 * Flex layout context
 */
struct flex_ctx {
    html_content *content;
    const struct box *flex;
    const css_unit_ctx *unit_len_ctx;

    int main_size;
    int cross_size;

    int available_main;
    int available_cross;

    int main_gap; /**< Gap between items in main axis direction (px) */
    int cross_gap; /**< Gap between lines in cross axis direction (px) */

    bool horizontal;
    bool main_reversed;
    enum css_flex_wrap_e wrap;

    struct flex_items {
        size_t count;
        struct flex_item_data *data;
    } item;

    struct flex_lines {
        size_t count;
        size_t alloc;
        struct flex_line_data *data;
    } line;

    bool needs_two_pass; /**< True if any item has % flex-basis in column flex */
};

/**
 * Destroy a flex layout context
 *
 * \param[in] ctx  Flex layout context
 */
static void layout_flex_ctx__destroy(struct flex_ctx *ctx)
{
    if (ctx != NULL) {
        free(ctx->item.data);
        free(ctx->line.data);
        free(ctx);
    }
}

/**
 * Create a flex layout context
 *
 * \param[in] content  HTML content containing flex box
 * \param[in] flex     Box to create layout context for
 * \return flex layout context or NULL on error
 */
static struct flex_ctx *layout_flex_ctx__create(html_content *content, const struct box *flex)
{
    struct flex_ctx *ctx;

    ctx = calloc(1, sizeof(*ctx));
    if (ctx == NULL) {
        return NULL;
    }
    ctx->line.alloc = 1;

    ctx->item.count = box_count_children(flex);
    ctx->item.data = calloc(ctx->item.count, sizeof(*ctx->item.data));
    if (ctx->item.data == NULL) {
        layout_flex_ctx__destroy(ctx);
        return NULL;
    }

    ctx->line.alloc = 1;
    ctx->line.data = calloc(ctx->line.alloc, sizeof(*ctx->line.data));
    if (ctx->line.data == NULL) {
        layout_flex_ctx__destroy(ctx);
        return NULL;
    }

    ctx->flex = flex;
    ctx->content = content;
    ctx->unit_len_ctx = &content->unit_len_ctx;

    ctx->wrap = css_computed_flex_wrap(flex->style);
    ctx->horizontal = lh__flex_main_is_horizontal(flex);
    ctx->main_reversed = lh__flex_direction_reversed(flex);

    /* Read CSS gap properties per flexbox spec:
     * - For row direction (horizontal): column-gap is main, row-gap is cross
     * - For column direction (vertical): row-gap is main, column-gap is cross */
    ctx->main_gap = 0;
    ctx->cross_gap = 0;
    if (flex->style != NULL) {
        css_fixed gap_len = 0;
        css_unit gap_unit = CSS_UNIT_PX;

        /* Main gap */
        if (ctx->horizontal) {
            if (css_computed_column_gap(flex->style, &gap_len, &gap_unit) == CSS_COLUMN_GAP_SET) {
                ctx->main_gap = FIXTOINT(css_unit_len2device_px(flex->style, ctx->unit_len_ctx, gap_len, gap_unit));
            }
        } else {
            if (css_computed_row_gap(flex->style, &gap_len, &gap_unit) == CSS_ROW_GAP_SET) {
                ctx->main_gap = FIXTOINT(css_unit_len2device_px(flex->style, ctx->unit_len_ctx, gap_len, gap_unit));
            }
        }

        NSLOG(flex, INFO, "Gap parsed: flex=%p horizontal=%d main_gap=%d", flex, ctx->horizontal, ctx->main_gap);

        /* Cross gap */
        if (ctx->horizontal) {
            if (css_computed_row_gap(flex->style, &gap_len, &gap_unit) == CSS_ROW_GAP_SET) {
                ctx->cross_gap = FIXTOINT(css_unit_len2device_px(flex->style, ctx->unit_len_ctx, gap_len, gap_unit));
            }
        } else {
            if (css_computed_column_gap(flex->style, &gap_len, &gap_unit) == CSS_COLUMN_GAP_SET) {
                ctx->cross_gap = FIXTOINT(css_unit_len2device_px(flex->style, ctx->unit_len_ctx, gap_len, gap_unit));
            }
        }
        // NSLOG(
        //     flex, WARNING, "Flex gap: main=%d cross=%d horizontal=%d", ctx->main_gap, ctx->cross_gap,
        //     ctx->horizontal);
        //  fprintf(
        //      stderr, "FLEX GAP DEBUG: main=%d cross=%d horizontal=%d\n", ctx->main_gap, ctx->cross_gap,
        //      ctx->horizontal);
    }

    return ctx;
}

/**
 * Find box side representing the start of flex container in main direction.
 *
 * \param[in] ctx   Flex layout context.
 * \return the start side.
 */
static enum box_side layout_flex__main_start_side(const struct flex_ctx *ctx)
{
    if (ctx->horizontal) {
        return (ctx->main_reversed) ? RIGHT : LEFT;
    } else {
        return (ctx->main_reversed) ? BOTTOM : TOP;
    }
}

/**
 * Find box side representing the end of flex container in main direction.
 *
 * \param[in] ctx   Flex layout context.
 * \return the end side.
 */
static enum box_side layout_flex__main_end_side(const struct flex_ctx *ctx)
{
    if (ctx->horizontal) {
        return (ctx->main_reversed) ? LEFT : RIGHT;
    } else {
        return (ctx->main_reversed) ? TOP : BOTTOM;
    }
}

/**
 * Redistribute auto margin space for a column flex container.
 *
 * This is called after the container's height becomes definite (e.g., via
 * flex: 1 from parent). It shifts children with margin-top/bottom: auto
 * to distribute the extra vertical space that wasn't available during
 * initial layout.
 *
 * \param[in] flex  The column flex container box
 * \return true on success
 */
bool layout_flex_redistribute_auto_margins_vertical(struct box *flex)
{
    int container_height = flex->height;
    int content_height = 0;
    int auto_margin_count = 0;
    css_fixed grow_factor_sum = 0;
    int grow_item_count = 0;
    int child_count = 0;

    NSLOG(flex, INFO, "Flex redistribute: container_h=%d", container_height);

    /* Get CSS row-gap for column flex (this is the main axis gap) */
    int css_row_gap = 0;
    if (flex->style) {
        css_fixed gap_len = 0;
        css_unit gap_unit = CSS_UNIT_PX;
        uint8_t gap_type = css_computed_row_gap(flex->style, &gap_len, &gap_unit);
        if (gap_type == CSS_ROW_GAP_SET && gap_unit != CSS_UNIT_PCT) {
            /* Convert to pixels using a dummy unit context - we just need px conversion */
            css_row_gap = FIXTOINT(gap_len); /* Already in px if unit is px */
        }
    }

    /* First pass: count children */
    int child_count_initial = 0;
    for (struct box *child = flex->children; child; child = child->next) {
        if (child->type != BOX_FLOAT_LEFT && child->type != BOX_FLOAT_RIGHT) {
            child_count_initial++;
        }
    }

    /* Calculate gap_total from CSS row-gap, not from measured positions */
    int gap_total = (child_count_initial > 1) ? (child_count_initial - 1) * css_row_gap : 0;

    /* Second pass: calculate content height, auto margins, and flex-grow */
    struct box *prev_child = NULL;
    for (struct box *child = flex->children; child; child = child->next) {
        if (child->type == BOX_FLOAT_LEFT || child->type == BOX_FLOAT_RIGHT) {
            continue;
        }

        child_count++;

        /* Calculate this child's outer height (excluding auto margins which we'll compute) */
        int child_outer_height = child->height + child->padding[TOP] + child->padding[BOTTOM] +
            child->border[TOP].width + child->border[BOTTOM].width;

        /* Check CSS computed style for auto margins - box margin is already resolved to 0 */
        bool margin_top_auto = false;
        bool margin_bottom_auto = false;
        if (child->style) {
            css_fixed len;
            css_unit unit;
            uint8_t margin_type = css_computed_margin_top(child->style, &len, &unit);
            margin_top_auto = (margin_type == CSS_MARGIN_AUTO);
            margin_type = css_computed_margin_bottom(child->style, &len, &unit);
            margin_bottom_auto = (margin_type == CSS_MARGIN_AUTO);
        }
        /* Also check for AUTO sentinel in box margin (used when style is unavailable) */
        if (child->margin[TOP] == AUTO) {
            margin_top_auto = true;
        }
        if (child->margin[BOTTOM] == AUTO) {
            margin_bottom_auto = true;
        }

        if (!margin_top_auto) {
            child_outer_height += child->margin[TOP];
        } else {
            auto_margin_count++;
        }
        if (!margin_bottom_auto) {
            child_outer_height += child->margin[BOTTOM];
        } else {
            auto_margin_count++;
        }

        content_height += child_outer_height;

        /* gap_total is now calculated from CSS row-gap at the top - no need to measure */

        /* Get flex-grow factor if this child has a style */
        if (child->style) {
            css_fixed grow;
            css_computed_flex_grow(child->style, &grow);
            if (grow > 0) {
                grow_factor_sum += grow;
                grow_item_count++;
                NSLOG(flex, INFO, "  Child %p: height=%d, grow=%d", child, child->height, FIXTOINT(grow));
            }
        }
    }

    NSLOG(flex, INFO, "  Initial: content_h=%d, auto_margins=%d, grow_sum=%d, gap_total=%d", content_height,
        auto_margin_count, FIXTOINT(grow_factor_sum), gap_total);

    /* Calculate extra space, accounting for gaps between items */
    int extra_space = container_height - content_height - gap_total;

    if (extra_space <= 0) {
        /* No extra space to distribute */
        NSLOG(flex, INFO, "  No extra space (extra=%d)", extra_space);
        return true;
    }

    /* Step 1: Redistribute extra space to flex-grow items
     * This increases their heights proportionally */
    if (grow_factor_sum > 0 && grow_item_count > 0) {
        css_fixed remainder = 0;
        int distributed = 0;

        for (struct box *child = flex->children; child; child = child->next) {
            if (child->type == BOX_FLOAT_LEFT || child->type == BOX_FLOAT_RIGHT) {
                continue;
            }

            if (child->style) {
                css_fixed grow;
                css_computed_flex_grow(child->style, &grow);

                if (grow > 0) {
                    /* Calculate this child's share of extra space */
                    css_fixed ratio = FDIV(grow, grow_factor_sum);
                    css_fixed result = FMUL(INTTOFIX(extra_space), ratio) + remainder;
                    int growth = FIXTOINT(result);
                    remainder = FIXFRAC(result);

                    int old_height = child->height;
                    child->height += growth;
                    distributed += growth;

                    NSLOG(flex, INFO, "  GROW: child %p: %d -> %d (+%d)", child, old_height, child->height, growth);
                }
            }
        }

        /* After redistributing to flex-grow items, recalculate content_height
         * The extra space has now been absorbed by the flex-grow items */
        content_height += distributed;
        extra_space = container_height - content_height;

        NSLOG(flex, INFO, "  After grow: distributed=%d, new extra_space=%d", distributed, extra_space);
    }

    /* Step 2: Redistribute remaining space to auto margins AND reposition children.
     * IMPORTANT: Even if there are no auto margins, we must reposition children
     * after flex-grow distribution changed heights */
    int margin_each = (auto_margin_count > 0 && extra_space > 0) ? (extra_space / auto_margin_count) : 0;
    int margin_remainder = (auto_margin_count > 0 && extra_space > 0) ? (extra_space % auto_margin_count) : 0;

    if (auto_margin_count > 0 && extra_space > 0) {
        NSLOG(flex, INFO, "  Auto margin redistribute: extra=%d, margin_each=%d", extra_space, margin_each);
    }

    /* Reposition all children - this is needed after both GROW and auto margin distribution */
    int current_y = flex->padding[TOP];
    bool first_child = true;
    for (struct box *child = flex->children; child; child = child->next) {
        if (child->type == BOX_FLOAT_LEFT || child->type == BOX_FLOAT_RIGHT) {
            continue;
        }

        /* Add CSS row-gap between items (not before first item) */
        if (!first_child) {
            current_y += css_row_gap;
        }
        first_child = false;

        /* Check CSS computed style for auto margins - box margin is already resolved to 0 */
        bool margin_top_auto = false;
        bool margin_bottom_auto = false;
        if (child->style) {
            css_fixed len;
            css_unit unit;
            uint8_t margin_type = css_computed_margin_top(child->style, &len, &unit);
            margin_top_auto = (margin_type == CSS_MARGIN_AUTO);
            margin_type = css_computed_margin_bottom(child->style, &len, &unit);
            margin_bottom_auto = (margin_type == CSS_MARGIN_AUTO);
        }
        /* Also check for AUTO sentinel in box margin (used when style is unavailable) */
        if (child->margin[TOP] == AUTO) {
            margin_top_auto = true;
        }
        if (child->margin[BOTTOM] == AUTO) {
            margin_bottom_auto = true;
        }

        /* Add margin-top (resolved or auto) */
        if (margin_top_auto) {
            int this_margin = margin_each;
            if (margin_remainder > 0) {
                this_margin++;
                margin_remainder--;
            }
            current_y += this_margin;
        } else {
            current_y += child->margin[TOP];
        }

        /* Position the child */
        current_y += child->border[TOP].width;
        child->y = current_y;
        current_y += child->padding[TOP] + child->height + child->padding[BOTTOM];
        current_y += child->border[BOTTOM].width;

        NSLOG(flex, DEEPDEBUG, "  Position: child %p at y=%d", child, child->y);

        /* Add margin-bottom (resolved or auto) */
        if (margin_bottom_auto) {
            int this_margin = margin_each;
            if (margin_remainder > 0) {
                this_margin++;
                margin_remainder--;
            }
            current_y += this_margin;
        } else {
            current_y += child->margin[BOTTOM];
        }
    }

    return true;
}

/**
 * Two-pass resolve for column flex with percentage flex-basis.
 *
 * After first pass determines container height, re-resolve percentage flex-basis
 * against the now-definite height and apply flex-shrink algorithm.
 * This matches Firefox/Chrome behavior for indefinite-height column flex.
 *
 * \param[in] ctx   Flex layout context
 * \param[in] flex  Flex container box (mutable for height adjustment)
 */
static void layout_flex__two_pass_resolve(struct flex_ctx *ctx, struct box *flex)
{
    /* DISABLED: Two-pass algorithm not working correctly.
     * Regular redistribute_auto_margins_vertical handles this case. */
    (void)ctx;
    (void)flex;
    return;

    int definite_height = flex->height; /* Now known after pass 1 */
    int gap_total = (ctx->item.count > 1) ? (int)(ctx->item.count - 1) * ctx->main_gap : 0;

    /* Calculate what heights would be if % flex-basis resolved against definite height */
    int total_pct_demand = 0;
    int total_other = 0;
    css_fixed scaled_shrink_sum = 0;

    for (size_t i = 0; i < ctx->item.count; i++) {
        struct flex_item_data *item = &ctx->item.data[i];
        struct box *b = item->box;

        if (item->has_pct_basis) {
            /* Re-resolve percentage against definite height */
            int pct = FIXTOINT(item->basis_length.value);
            int resolved = (definite_height * pct) / 100;
            total_pct_demand += resolved;
            scaled_shrink_sum += FMUL(item->shrink, INTTOFIX(resolved));

            NSLOG(flex, INFO, "PASS2: item %p pct=%d%% resolved=%dpx against h=%d", b, pct, resolved, definite_height);
        } else {
            /* Non-percentage items: calculate outer height */
            total_other += b->height + b->padding[TOP] + b->padding[BOTTOM] + b->border[TOP].width +
                b->border[BOTTOM].width;
            if (b->margin[TOP] != AUTO)
                total_other += b->margin[TOP];
            if (b->margin[BOTTOM] != AUTO)
                total_other += b->margin[BOTTOM];
        }
    }

    /* Check if shrinking is needed */
    int available = definite_height - gap_total;
    int total_demand = total_pct_demand + total_other;

    NSLOG(flex, INFO, "PASS2: available=%d total_demand=%d (pct=%d other=%d gap=%d)", available, total_demand,
        total_pct_demand, total_other, gap_total);

    if (total_demand > available && total_pct_demand > 0 && scaled_shrink_sum > 0) {
        /* Need to shrink percentage items.
         * Algorithm: Items where content_min > resolved% are frozen at content_min.
         * Remaining space is distributed to shrinkable items. */

        bool frozen[64] = {false};
        int new_heights[64] = {0};
        int frozen_total = 0;
        int shrinkable_total = 0;
        css_fixed shrinkable_factor_sum = 0;

        /* First pass: identify frozen items (content_min > resolved%) and calculate totals */
        for (size_t i = 0; i < ctx->item.count && i < 64; i++) {
            struct flex_item_data *item = &ctx->item.data[i];
            struct box *b = item->box;

            if (item->has_pct_basis) {
                int pct = FIXTOINT(item->basis_length.value);
                int resolved = (definite_height * pct) / 100;
                int content_min = b->height;

                if (content_min >= resolved) {
                    /* This item can't shrink - it needs its content_min */
                    new_heights[i] = content_min;
                    frozen[i] = true;
                    frozen_total += content_min;
                    NSLOG(flex, INFO, "PASS2: item %p FROZEN: pct=%d%% resolved=%d < content=%d", b, pct, resolved,
                        content_min);
                } else {
                    /* This item can shrink */
                    new_heights[i] = resolved;
                    shrinkable_total += resolved;
                    shrinkable_factor_sum += FMUL(item->shrink, INTTOFIX(resolved));
                    NSLOG(flex, INFO, "PASS2: item %p SHRINKABLE: pct=%d%% resolved=%d content=%d", b, pct, resolved,
                        content_min);
                }
            }
        }

        /* Calculate remaining space for shrinkable items */
        int remaining_for_shrinkable = available - frozen_total;

        NSLOG(flex, INFO, "PASS2: available=%d frozen_total=%d remaining=%d shrinkable_total=%d", available,
            frozen_total, remaining_for_shrinkable, shrinkable_total);

        if (remaining_for_shrinkable > 0 && shrinkable_total > remaining_for_shrinkable) {
            /* Need to shrink the shrinkable items */
            int overflow = shrinkable_total - remaining_for_shrinkable;

            NSLOG(flex, INFO, "PASS2: shrinkable overflow=%d", overflow);

            /* Distribute shrink proportionally to shrinkable items */
            for (size_t i = 0; i < ctx->item.count && i < 64; i++) {
                struct flex_item_data *item = &ctx->item.data[i];
                struct box *b = item->box;

                if (item->has_pct_basis && !frozen[i] && item->shrink > 0) {
                    css_fixed scaled_factor = FMUL(item->shrink, INTTOFIX(new_heights[i]));
                    css_fixed ratio = FDIV(scaled_factor, shrinkable_factor_sum);
                    int shrink_amount = FIXTOINT(FMUL(INTTOFIX(overflow), ratio));

                    int target = new_heights[i] - shrink_amount;
                    int content_min = b->height;

                    /* Don't go below content min */
                    if (target < content_min) {
                        target = content_min;
                    }

                    NSLOG(flex, INFO, "PASS2: item %p shrink: %d - %d = %d (min=%d)", b, new_heights[i], shrink_amount,
                        target, content_min);
                    new_heights[i] = target;
                }
            }
        } else if (remaining_for_shrinkable <= 0) {
            /* Not enough space even for frozen items - cap shrinkable at content_min */
            for (size_t i = 0; i < ctx->item.count && i < 64; i++) {
                struct flex_item_data *item = &ctx->item.data[i];
                struct box *b = item->box;
                if (item->has_pct_basis && !frozen[i]) {
                    new_heights[i] = b->height;
                }
            }
        }

        /* Apply final heights */
        for (size_t i = 0; i < ctx->item.count && i < 64; i++) {
            struct flex_item_data *item = &ctx->item.data[i];
            struct box *b = item->box;

            if (item->has_pct_basis) {
                NSLOG(flex, INFO, "PASS2: item %p: final height=%d (was %d)", b, new_heights[i], b->height);
                b->height = new_heights[i];
            }
        }
    }

    /* After adjusting heights, re-run redistribute on nested flex containers */
    for (size_t i = 0; i < ctx->item.count; i++) {
        struct flex_item_data *item = &ctx->item.data[i];
        struct box *b = item->box;

        if (b->type == BOX_FLEX && b->style) {
            uint8_t child_flex_dir = css_computed_flex_direction(b->style);
            bool child_is_column = (child_flex_dir == CSS_FLEX_DIRECTION_COLUMN ||
                child_flex_dir == CSS_FLEX_DIRECTION_COLUMN_REVERSE);
            if (child_is_column) {
                NSLOG(flex, INFO, "PASS2: re-running redistribute on nested flex %p (h=%d)", b, b->height);
                layout_flex_redistribute_auto_margins_vertical(b);
            }
        }
    }
}

/**
 * Perform layout on a flex item
 *
 * \param[in] ctx              Flex layout context
 * \param[in] item             Item to lay out
 * \param[in] available_width  Available width for item in pixels
 * \return true on success false on failure
 */
static bool layout_flex_item(const struct flex_ctx *ctx, const struct flex_item_data *item, int available_width)
{
    bool success;
    struct box *b = item->box;

    switch (b->type) {
    case BOX_BLOCK:
        success = layout_block_context(b, -1, ctx->content);
        break;
    case BOX_TABLE:
        b->float_container = b->parent;
        success = layout_table(b, available_width, ctx->content);
        b->float_container = NULL;
        break;
    case BOX_FLEX:
        b->float_container = b->parent;
        success = layout_flex(b, available_width, ctx->content);
        b->float_container = NULL;
        break;
    case BOX_GRID:
        b->float_container = b->parent;
        success = layout_grid(b, available_width, ctx->content);
        b->float_container = NULL;
        break;
    default:
        assert(0 && "Bad flex item back type");
        success = false;
        break;
    }

    if (!success) {
        NSLOG(flex, ERROR, "box %p: layout failed", b);
    }

    return success;
}

/**
 * Calculate an item's base and target main sizes.
 *
 * \param[in] ctx                    Flex layout context
 * \param[in] item                   Item to get sizes of
 * \param[in] available_main_size    Available size in main axis (px), for resolving percentage flex-basis
 * \param[in] available_cross_size   Available size in cross axis (px)
 * \return true on success false on failure
 */
static inline bool layout_flex__base_and_main_sizes(
    struct flex_ctx *ctx, struct flex_item_data *item, int available_main_size, int available_cross_size)
{
    struct box *b = item->box;
    int content_min_width = b->min_width.value;
    int content_max_width = b->max_width;
    int delta_outer_main = lh__delta_outer_main(ctx->flex, b);

    /* Per CSS Flexbox spec §4.5, for flex items:
     * - min-width/height: auto computes to min-content IF overflow is visible
     * - If overflow is NOT visible (hidden/auto/scroll), min-width:auto = 0
     * This allows flex items to shrink below their content size when overflow
     * is set to hide/clip the overflowing content. */
    if (ctx->horizontal && b->style != NULL) {
        enum css_overflow_e overflow_x = css_computed_overflow_x(b->style);
        if (overflow_x != CSS_OVERFLOW_VISIBLE) {
            content_min_width = 0;
            NSLOG(flex, DEEPDEBUG, "box %p: overflow_x=%d != visible, setting content_min_width=0", b, overflow_x);
        }
    }

    NSLOG(flex, DEEPDEBUG, "box %p: delta_outer_main: %i", b, delta_outer_main);
    NSLOG(flex, DEBUG, "box %p: item->basis=%d (SET=1, AUTO=2, CONTENT=3)", b, item->basis);

    if (item->basis == CSS_FLEX_BASIS_SET) {
        /* Use css_computed_flex_basis_px to properly evaluate calc() expressions.
         * For row flex: percentages resolve against available width.
         * For column flex: percentages resolve against available height (main axis).
         * If the reference size is indefinite (AUTO), percentage resolves to 'auto' per spec. */
        int basis_px = 0;
        uint8_t basis_type = css_computed_flex_basis_px(b->style, ctx->unit_len_ctx, available_main_size, &basis_px);

        /* Track if this item has percentage flex-basis for two-pass layout.
         * In column flex with indefinite height, we'll need to re-resolve this
         * after the first pass determines the container's height. */
        item->has_pct_basis = (item->basis_unit == CSS_UNIT_PCT && !ctx->horizontal);
        if (item->has_pct_basis) {
            ctx->needs_two_pass = true;
        }

        if (basis_type == CSS_FLEX_BASIS_SET) {
            /* Handle box-sizing: border-box by converting to content-box.
             * The delta_outer_main will be added later at line 314, so we
             * need to subtract padding+border now if box-sizing is border-box. */
            layout_handle_box_sizing(ctx->unit_len_ctx, b, available_cross_size, ctx->horizontal, &basis_px);

            /* For column flex with flex-basis: 0 (e.g., from 'flex: 1'), the item
             * should still grow to fit its content. The 0 means "start from content
             * size" not "use zero height". Defer to content-based sizing. */
            if (ctx->horizontal == false && basis_px == 0) {
                item->base_size = AUTO;
                NSLOG(flex, DEBUG, "box %p: flex-basis:0 in column flex, deferred to content sizing", b);
            } else {
                item->base_size = basis_px;
            }
        } else {
            /* Fallback to content-based sizing if calc() couldn't be evaluated.
             * For column flex with percentage flex-basis and indefinite container,
             * this is the first pass - will re-resolve in second pass. */
            NSLOG(flex, DEEPDEBUG, "flex-basis: resolved to auto (calc/pct with indefinite container)");
            item->base_size = AUTO;
        }
    } else if (item->basis == CSS_FLEX_BASIS_AUTO) {
        /* For horizontal flex, width is known before layout.
         * For vertical (column) flex, height must be calculated first by
         * layout_flex_item() below, so defer to line 365-367. */
        if (ctx->horizontal) {
            item->base_size = b->width;
            NSLOG(flex, DEBUG, "box %p: flex-basis:auto horizontal, base_size=%d from width", b, item->base_size);
        } else {
            item->base_size = AUTO; /* Will be set after layout below */
            NSLOG(flex, DEBUG, "box %p: flex-basis:auto column, deferred base_size to AUTO", b);
        }
    } else {
        item->base_size = AUTO;
        NSLOG(flex, DEBUG, "box %p: flex-basis not SET or AUTO (basis=%d), base_size=AUTO", b, item->basis);
    }


    if (ctx->horizontal == false) {
        if (b->width == AUTO) {
            /* Check if CSS width is a content-sizing keyword (fit-content, min-content, max-content).
             * These use intrinsic content width, not stretch behavior. */
            css_fixed css_width_val;
            css_unit css_width_unit;
            uint8_t wtype = css_computed_width(b->style, &css_width_val, &css_width_unit);

            bool is_content_sizing = (wtype == CSS_WIDTH_SET &&
                (css_width_unit == CSS_UNIT_FIT_CONTENT || css_width_unit == CSS_UNIT_MIN_CONTENT ||
                    css_width_unit == CSS_UNIT_MAX_CONTENT));

            if (is_content_sizing) {
                /* Content-sizing: use intrinsic content width, clamped by available */
                b->width = content_max_width;
                if (b->width > available_cross_size) {
                    b->width = available_cross_size;
                }
            } else {
                /* Auto: stretch to fill available width (standard flex cross-axis behavior) */
                b->width = available_cross_size;
                /* But don't exceed content max if it's smaller (shrink-wrap) */
                if (b->width > content_max_width) {
                    b->width = content_max_width;
                }
            }
            b->width -= lh__delta_outer_width(b);
        }

        if (!layout_flex_item(ctx, item, b->width)) {
            return false;
        }

        /* CSS Flexbox §4.5: automatic minimum size for flex items.
         * When min-height: auto (not explicitly set), the minimum size should be
         * the content's minimum size, not 0. This prevents flex items from
         * shrinking below their content (e.g., images with CSS height).
         *
         * For column flex: update min_main from actual content height.
         * This handles both CSS-specified heights and intrinsic image sizes. */
        if (!ctx->horizontal && item->min_main.type == CSS_SIZE_AUTO) {
            int content_min_height = b->height + lh__delta_outer_height(b);
            if (content_min_height > 0) {
                NSLOG(flex, DEEPDEBUG, "AUTO MIN-HEIGHT: box %p min_main 0 -> %d (content height)", b,
                    content_min_height);
                item->min_main.value = content_min_height;
                item->min_main.type = CSS_SIZE_SET; /* Now it's set to content min */
            }
        }
    }

    if (item->base_size == AUTO) {
        if (ctx->horizontal == false) {
            NSLOG(flex, DEBUG, "box %p: setting base_size from b->height=%d", b, b->height);
            item->base_size = b->height;
        } else {
            item->base_size = content_max_width - delta_outer_main;
        }
    }

    item->base_size += delta_outer_main;

    /* Per CSS Flexbox spec §4.5: automatic minimum size is capped by
     * the item's specified size (width/height) when it's definite.
     * automatic_min = min(content_min, specified_size) */
    if (ctx->horizontal && b->width != AUTO) {
        int specified_main = b->width + delta_outer_main;
        if (content_min_width > specified_main) {
            NSLOG(flex, WARNING, "AUTO-MIN CAP: content_min_width %d -> %d (specified width=%d)", content_min_width,
                specified_main, b->width);
            content_min_width = specified_main;
        }
    }

    if (ctx->horizontal) {
        int before_clamp = item->base_size;
        item->base_size = min(item->base_size, available_main_size);
        item->base_size = max(item->base_size, content_min_width);
        if (item->base_size != before_clamp) {
            NSLOG(flex, WARNING,
                "CLAMP: base_size changed from %d to %d (content_min_width=%d, available_main_size=%d)", before_clamp,
                item->base_size, content_min_width, available_main_size);
        }
    }

    item->target_main_size = item->base_size;
    item->main_size = item->base_size;

    if (item->max_main > 0 && item->main_size > item->max_main + delta_outer_main) {
        item->main_size = item->max_main + delta_outer_main;
    }

    /* Only apply min_main if it was set (explicit or from content calculation) */
    if (item->min_main.type == CSS_SIZE_SET && item->main_size < item->min_main.value + delta_outer_main) {
        item->main_size = item->min_main.value + delta_outer_main;
    }

    NSLOG(flex, DEEPDEBUG, "flex-item box: %p: base_size: %i, main_size %i", b, item->base_size, item->main_size);

    return true;
}

/**
 * Fill out all item's data in a flex container.
 *
 * \param[in] ctx              Flex layout context
 * \param[in] flex             Flex box
 * \param[in] available_width  Available width in pixels
 */
static void layout_flex_ctx__populate_item_data(struct flex_ctx *ctx, const struct box *flex, int available_width)
{
    size_t i = 0;
    bool horizontal = ctx->horizontal;

    /* DIAG: Log flex container info */
    {
        const char *flex_cls = "";
        dom_string *flex_class_attr = NULL;
        if (flex->node != NULL) {
            if (dom_element_get_attribute(flex->node, corestring_dom_class, &flex_class_attr) == DOM_NO_ERR &&
                flex_class_attr != NULL) {
                flex_cls = dom_string_data(flex_class_attr);
            }
        }
        NSLOG(flex, DEEPDEBUG, "DIAG: flex container class='%s' box=%p available_width=%i", flex_cls, flex,
            available_width);
        if (flex_class_attr != NULL)
            dom_string_unref(flex_class_attr);
    }

    for (struct box *b = flex->children; b != NULL; b = b->next) {
        struct flex_item_data *item = &ctx->item.data[i++];

        /* Skip boxes without styles - they shouldn't exist after
         * normalization, but add defensive check to prevent crash */
        if (b->style == NULL) {
            NSLOG(flex, ERROR, "DIAG: flex-item box=%p has NULL style! type=%d, skipping", b, b->type);
            i--; /* Don't increment item index for skipped boxes */
            continue;
        }

        b->float_container = b->parent;
        layout_find_dimensions(ctx->unit_len_ctx, available_width, -1, b, b->style, &b->width, &b->height,
            horizontal ? &item->max_main : &item->max_cross, horizontal ? &item->min_main : &item->min_cross,
            horizontal ? &item->max_cross : &item->max_main, horizontal ? &item->min_cross : &item->min_main, b->margin,
            b->padding, b->border);
        b->float_container = NULL;

        /* Determine min type from CSS computed values.
         * Per CSS Flexbox spec §4.5, flex items have min-width/min-height initial value of 'auto',
         * which triggers automatic minimum size based on content. However, libcss computes the
         * general initial value of 0 (for non-flex items) because it doesn't know layout context.
         *
         * Solution: For flex items, treat min-size: 0 as if it were 'auto', enabling automatic
         * minimum size. Only explicitly set non-zero values should prevent shrinking below content.
         * This matches browser behavior where flex items don't shrink below content by default. */
        {
            css_fixed value;
            css_unit unit;
            enum css_min_height_e min_h_type = ns_computed_min_height(b->style, &value, &unit);
            enum css_min_width_e min_w_type = ns_computed_min_width(b->style, &value, &unit);

            if (horizontal) {
                /* Horizontal flex: min-width affects main axis.
                 * Treat 0 as AUTO to enable automatic minimum size per spec. */
                item->min_main.type = (min_w_type == CSS_MIN_WIDTH_SET && item->min_main.value != 0) ? CSS_SIZE_SET
                                                                                                     : CSS_SIZE_AUTO;
                item->min_cross.type = (min_h_type == CSS_MIN_HEIGHT_SET && item->min_cross.value != 0) ? CSS_SIZE_SET
                                                                                                        : CSS_SIZE_AUTO;
            } else {
                /* Column flex: min-height affects main axis.
                 * Treat 0 as AUTO to enable automatic minimum size per spec. */
                item->min_main.type = (min_h_type == CSS_MIN_HEIGHT_SET && item->min_main.value != 0) ? CSS_SIZE_SET
                                                                                                      : CSS_SIZE_AUTO;
                item->min_cross.type = (min_w_type == CSS_MIN_WIDTH_SET && item->min_cross.value != 0) ? CSS_SIZE_SET
                                                                                                       : CSS_SIZE_AUTO;
            }
            NSLOG(flex, DEEPDEBUG, "box %p: min_main.type=%d value=%d (AUTO=0, SET=1) min_h_type=%d horizontal=%d", b,
                item->min_main.type, item->min_main.value, min_h_type, horizontal);
        }

        /* DIAG: Log flex item info with class name */
        {
            const char *item_cls = "";
            dom_string *item_class_attr = NULL;
            if (b->node != NULL) {
                if (dom_element_get_attribute(b->node, corestring_dom_class, &item_class_attr) == DOM_NO_ERR &&
                    item_class_attr != NULL) {
                    item_cls = dom_string_data(item_class_attr);
                }
            }
            NSLOG(flex, DEEPDEBUG, "DIAG: flex-item class='%s' box=%p width=%i (AUTO=%i)", item_cls, b, b->width,
                (b->width == AUTO));
            if (item_class_attr != NULL)
                dom_string_unref(item_class_attr);
        }

        NSLOG(flex, DEEPDEBUG, "flex-item box: %p: width: %i", b, b->width);

        item->box = b;
        item->basis = css_computed_flex_basis(b->style, &item->basis_length, &item->basis_unit);

        /* Fetch 'order' property for storing */
        css_computed_order(b->style, &item->order);
        item->original_index = i; /* Store original DOM index (1-based as i was incremented) */

        css_computed_flex_shrink(b->style, &item->shrink);
        css_computed_flex_grow(b->style, &item->grow);

        NSLOG(flex, DEEPDEBUG, "flex-item box %p: flex-grow=%d flex-shrink=%d", b, FIXTOINT(item->grow),
            FIXTOINT(item->shrink));

        /* Pass correct reference size for percentage flex-basis resolution:
         * - Row flex: main=width, cross=height
         * - Column flex: main=height, cross=width */
        int ref_main = horizontal ? available_width : ctx->available_main;
        int ref_cross = horizontal ? ctx->available_main : available_width;
        layout_flex__base_and_main_sizes(ctx, item, ref_main, ref_cross);
    }
}

/**
 * Ensure context's lines array has a free space
 *
 * \param[in] ctx  Flex layout context
 * \return true on success false on out of memory
 */
static bool layout_flex_ctx__ensure_line(struct flex_ctx *ctx)
{
    struct flex_line_data *temp;
    size_t line_alloc = ctx->line.alloc * 2;

    if (ctx->line.alloc > ctx->line.count) {
        return true;
    }

    temp = realloc(ctx->line.data, sizeof(*ctx->line.data) * line_alloc);
    if (temp == NULL) {
        return false;
    }
    ctx->line.data = temp;

    memset(ctx->line.data + ctx->line.alloc, 0, sizeof(*ctx->line.data) * (line_alloc - ctx->line.alloc));
    ctx->line.alloc = line_alloc;

    return true;
}

/**
 * Assigns flex items to the line and returns the line
 *
 * \param[in] ctx         Flex layout context
 * \param[in] item_index  Index to first item to assign to this line
 * \return Pointer to the new line, or NULL on error.
 */
static struct flex_line_data *layout_flex__build_line(struct flex_ctx *ctx, size_t item_index)
{
    enum box_side start_side = layout_flex__main_start_side(ctx);
    enum box_side end_side = layout_flex__main_end_side(ctx);
    struct flex_line_data *line;
    int used_main = 0;

    if (!layout_flex_ctx__ensure_line(ctx)) {
        return NULL;
    }

    line = &ctx->line.data[ctx->line.count];
    line->first = item_index;

    NSLOG(flex, DEEPDEBUG, "flex container %p: available main: %i", ctx->flex, ctx->available_main);

    while (item_index < ctx->item.count) {
        struct flex_item_data *item = &ctx->item.data[item_index];
        struct box *b = item->box;
        int pos_main;
        int gap_for_item;

        pos_main = ctx->horizontal ? item->main_size : b->height + lh__delta_outer_main(ctx->flex, b);

        /* Account for gap: if this item is added, we need line->count gaps total
         * (one gap between each pair of items) */
        gap_for_item = (line->count > 0) ? ctx->main_gap : 0;

        if (ctx->wrap == CSS_FLEX_WRAP_NOWRAP || pos_main + used_main + gap_for_item <= ctx->available_main ||
            lh__box_is_absolute(item->box) || ctx->available_main == AUTO || line->count == 0 || pos_main == 0) {
            if (lh__box_is_absolute(item->box) == false) {
                /* Use pos_main which reflects actual b->height for column flex,
                 * not pre-computed item->main_size which may be stale */
                line->main_size += pos_main;
                used_main += pos_main + gap_for_item;

                NSLOG(flex, DEEPDEBUG,
                    "MARGIN_CHECK: box %p margin[LEFT]=%d margin[RIGHT]=%d (AUTO=%d) start=%d end=%d", b,
                    b->margin[LEFT], b->margin[RIGHT], AUTO, start_side, end_side);
                if (b->margin[start_side] == AUTO) {
                    line->main_auto_margin_count++;
                }
                if (b->margin[end_side] == AUTO) {
                    line->main_auto_margin_count++;
                }
            }
            item->line = ctx->line.count;
            line->count++;
            item_index++;
        } else {
            break;
        }
    }

    if (line->count > 0) {
        /* Add gap_total to main_size - gaps are part of the line's main axis space */
        if (line->count > 1 && ctx->main_gap > 0) {
            int gap_total = (int)(line->count - 1) * ctx->main_gap;
            line->main_size += gap_total;
            NSLOG(flex, INFO, "LINE gap added: count=%zu main_gap=%d gap_total=%d new main_size=%d", line->count,
                ctx->main_gap, gap_total, line->main_size);
        }
        ctx->line.count++;
    } else {
        NSLOG(layout, ERROR, "Failed to fit any flex items");
    }

    return line;
}

/**
 * Freeze an item on a line
 *
 * \param[in] line  Line to containing item
 * \param[in] item  Item to freeze
 */
static inline void layout_flex__item_freeze(struct flex_line_data *line, struct flex_item_data *item)
{
    item->freeze = true;
    line->frozen++;

    if (!lh__box_is_absolute(item->box)) {
        line->used_main_size += item->target_main_size;
    }

    NSLOG(flex, DEEPDEBUG,
        "flex-item box: %p: "
        "Frozen at target_main_size: %i",
        item->box, item->target_main_size);
}

/**
 * Calculate remaining free space and unfrozen item factor sum
 *
 * \param[in]  ctx                  Flex layout context
 * \param[in]  line                 Line to calculate free space on
 * \param[out] unfrozen_factor_sum  Returns sum of unfrozen item's flex factors
 * \param[in]  initial_free_main    Initial free space in main direction
 * \param[in]  available_main       Available space in main direction
 * \param[in]  grow                 Whether to grow or shrink
 * return remaining free space on line
 */
static inline int layout_flex__remaining_free_main(struct flex_ctx *ctx, struct flex_line_data *line,
    css_fixed *unfrozen_factor_sum, int initial_free_main, int available_main, bool grow)
{
    int remaining_free_main = available_main;
    size_t item_count = line->first + line->count;

    *unfrozen_factor_sum = 0;

    for (size_t i = line->first; i < item_count; i++) {
        struct flex_item_data *item = &ctx->item.data[i];

        if (item->freeze) {
            remaining_free_main -= item->target_main_size;
        } else {
            remaining_free_main -= item->base_size;

            *unfrozen_factor_sum += grow ? item->grow : item->shrink;
        }
    }

    if (*unfrozen_factor_sum < F_1) {
        int free_space = FIXTOINT(FMUL(INTTOFIX(initial_free_main), *unfrozen_factor_sum));

        if (free_space < remaining_free_main) {
            remaining_free_main = free_space;
        }
    }

    NSLOG(flex, DEEPDEBUG, "Remaining free space: %i", remaining_free_main);

    return remaining_free_main;
}

/**
 * Clamp flex item target main size and get min/max violations
 *
 * \param[in] ctx   Flex layout context
 * \param[in] line  Line to align items on
 * return total violation in pixels
 */
static inline int layout_flex__get_min_max_violations(struct flex_ctx *ctx, struct flex_line_data *line)
{

    int total_violation = 0;
    size_t item_count = line->first + line->count;

    for (size_t i = line->first; i < item_count; i++) {
        struct flex_item_data *item = &ctx->item.data[i];
        int target_main_size = item->target_main_size;

        NSLOG(flex, DEEPDEBUG, "item %p: target_main_size: %i", item->box, target_main_size);

        if (item->freeze) {
            continue;
        }

        if (item->max_main > 0 && target_main_size > item->max_main) {
            target_main_size = item->max_main;
            item->max_violation = true;
            NSLOG(flex, DEEPDEBUG, "Violation: max_main: %i", item->max_main);
        }

        /* Only apply min_main constraint when it's been explicitly set or computed from content */
        if (item->min_main.type == CSS_SIZE_SET && target_main_size < item->min_main.value) {
            target_main_size = item->min_main.value;
            item->min_violation = true;
            NSLOG(flex, DEEPDEBUG, "Violation: min_main: %i", item->min_main.value);
        }

        /* Only apply box->min_width for horizontal flex (where width is main axis).
         * For column flex, min_width is cross-axis, not main-axis.
         * (struct box has no min_height field to use for column flex) */
        if (ctx->horizontal && target_main_size < item->box->min_width.value) {
            target_main_size = item->box->min_width.value;
            item->min_violation = true;
            NSLOG(flex, DEEPDEBUG, "Violation: box min_width: %i", item->box->min_width.value);
        }

        if (target_main_size < 0) {
            target_main_size = 0;
            item->min_violation = true;
            NSLOG(flex, DEEPDEBUG, "Violation: less than 0");
        }

        /* DIAG: Log violations */
        if (target_main_size != item->target_main_size) {
            NSLOG(flex, DEEPDEBUG, "DIAG: violation box=%p orig=%d new=%d min_main=%d box_min_width=%d", item->box,
                item->target_main_size, target_main_size, item->min_main, item->box->min_width);
        }

        total_violation += target_main_size - item->target_main_size;
        item->target_main_size = target_main_size;
    }

    NSLOG(flex, DEEPDEBUG, "Total violation: %i", total_violation);

    return total_violation;
}

/**
 * Distribute remaining free space proportional to the flex factors.
 *
 * Remaining free space may be negative.
 *
 * \param[in] ctx                  Flex layout context
 * \param[in] line                 Line to distribute free space on
 * \param[in] unfrozen_factor_sum  Sum of unfrozen item's flex factors
 * \param[in] remaining_free_main  Remaining free space in main direction
 * \param[in] grow                 Whether to grow or shrink
 */
static inline void layout_flex__distribute_free_main(struct flex_ctx *ctx, struct flex_line_data *line,
    css_fixed unfrozen_factor_sum, int remaining_free_main, bool grow)
{
    size_t item_count = line->first + line->count;

    if (grow) {
        css_fixed remainder = 0;
        for (size_t i = line->first; i < item_count; i++) {
            struct flex_item_data *item = &ctx->item.data[i];
            css_fixed result;
            css_fixed ratio;

            if (item->freeze) {
                continue;
            }

            ratio = FDIV(item->grow, unfrozen_factor_sum);
            result = FMUL(INTTOFIX(remaining_free_main), ratio) + remainder;

            item->target_main_size = item->base_size + FIXTOINT(result);
            remainder = FIXFRAC(result);
        }
    } else {
        css_fixed scaled_shrink_factor_sum = 0;
        css_fixed remainder = 0;

        for (size_t i = line->first; i < item_count; i++) {
            struct flex_item_data *item = &ctx->item.data[i];
            css_fixed scaled_shrink_factor;

            if (item->freeze) {
                continue;
            }

            scaled_shrink_factor = FMUL(item->shrink, INTTOFIX(item->base_size));
            scaled_shrink_factor_sum += scaled_shrink_factor;
        }

        for (size_t i = line->first; i < item_count; i++) {
            struct flex_item_data *item = &ctx->item.data[i];
            css_fixed scaled_shrink_factor;
            css_fixed result;
            css_fixed ratio;

            if (item->freeze) {
                continue;
            } else if (scaled_shrink_factor_sum == 0) {
                item->target_main_size = item->main_size;
                layout_flex__item_freeze(line, item);
                continue;
            }

            scaled_shrink_factor = FMUL(item->shrink, INTTOFIX(item->base_size));
            ratio = FDIV(scaled_shrink_factor, scaled_shrink_factor_sum);
            result = FMUL(INTTOFIX(abs(remaining_free_main)), ratio) + remainder;

            item->target_main_size = item->base_size - FIXTOINT(result);
            remainder = FIXFRAC(result);
        }
    }
}

/**
 * Resolve flexible item lengths along a line.
 *
 * See 9.7 of Tests CSS Flexible Box Layout Module Level 1.
 *
 * \param[in] ctx   Flex layout context
 * \param[in] line  Line to resolve
 * \return true on success, false on failure.
 */
static bool layout_flex__resolve_line(struct flex_ctx *ctx, struct flex_line_data *line)
{
    size_t item_count = line->first + line->count;
    int available_main = ctx->available_main;
    int initial_free_main;
    bool grow;

    if (available_main == AUTO) {
        available_main = line->main_size;
    }

    /* Subtract gap space from available main - gaps are fixed gutters per CSS spec */
    if (line->count > 1 && ctx->main_gap > 0) {
        int gap_total = (int)(line->count - 1) * ctx->main_gap;
        NSLOG(flex, WARNING, "GAP DEDUCT: available_main_before=%d gap_total=%d line->count=%zu", available_main,
            gap_total, line->count);
        available_main -= gap_total;
        NSLOG(flex, WARNING, "GAP DEDUCT: available_main_after=%d", available_main);
    }

    grow = (line->main_size < available_main);
    initial_free_main = available_main;

    NSLOG(flex, DEEPDEBUG, "box %p: line %zu: first: %zu, count: %zu", ctx->flex, line - ctx->line.data, line->first,
        line->count);
    NSLOG(flex, DEEPDEBUG, "Line main_size: %i, available_main: %i", line->main_size, available_main);

    for (size_t i = line->first; i < item_count; i++) {
        struct flex_item_data *item = &ctx->item.data[i];

        /* 3. Size inflexible items */
        if (grow) {
            if (item->grow == 0 || item->base_size > item->main_size) {
                item->target_main_size = item->main_size;
                layout_flex__item_freeze(line, item);
            }
        } else {
            if (item->shrink == 0 || item->base_size < item->main_size) {
                item->target_main_size = item->main_size;
                layout_flex__item_freeze(line, item);
            }
        }

        /* 4. Calculate initial free space */
        if (item->freeze) {
            initial_free_main -= item->target_main_size;
        } else {
            initial_free_main -= item->base_size;
        }
    }

    /* 5. Loop */
    while (line->frozen < line->count) {
        css_fixed unfrozen_factor_sum;
        int remaining_free_main;
        int total_violation;

        NSLOG(flex, DEEPDEBUG, "flex-container: %p: Resolver pass", ctx->flex);

        /* b */
        remaining_free_main = layout_flex__remaining_free_main(
            ctx, line, &unfrozen_factor_sum, initial_free_main, available_main, grow);

        /* c */
        if (remaining_free_main != 0) {
            layout_flex__distribute_free_main(ctx, line, unfrozen_factor_sum, remaining_free_main, grow);
        }

        /* d */
        total_violation = layout_flex__get_min_max_violations(ctx, line);

        /* e */
        for (size_t i = line->first; i < item_count; i++) {
            struct flex_item_data *item = &ctx->item.data[i];

            if (item->freeze) {
                continue;
            }

            if (total_violation == 0 || (total_violation > 0 && item->min_violation) ||
                (total_violation < 0 && item->max_violation)) {
                layout_flex__item_freeze(line, item);
            }
        }
    }

    return true;
}

/**
 * Position items along a line
 *
 * \param[in] ctx   Flex layout context
 * \param[in] line  Line to resolve
 * \return true on success, false on failure.
 */
static bool layout_flex__place_line_items_main(struct flex_ctx *ctx, struct flex_line_data *line)
{
    int main_pos = ctx->flex->padding[layout_flex__main_start_side(ctx)];
    int post_multiplier = ctx->main_reversed ? 0 : 1;
    int pre_multiplier = ctx->main_reversed ? -1 : 0;
    size_t item_count = line->first + line->count;
    int extra_remainder = 0;
    int extra = 0;
    int jc_gap_pre = 0;
    int jc_gap_between = 0;
    int jc_gap_between_rem = 0;
    int jc_gap_pre_extra = 0;

    if (ctx->main_reversed) {
        main_pos = lh__box_size_main(ctx->horizontal, ctx->flex) - main_pos;
    }

    if (ctx->available_main != AUTO && ctx->available_main != UNKNOWN_WIDTH &&
        ctx->available_main > line->used_main_size) {
        NSLOG(flex, DEEPDEBUG, "PLACE_MAIN: auto_margin_count=%d available=%d used=%d free=%d",
            line->main_auto_margin_count, ctx->available_main, line->used_main_size,
            ctx->available_main - line->used_main_size);
        if (line->main_auto_margin_count > 0) {
            extra = ctx->available_main - line->used_main_size;
            NSLOG(flex, DEEPDEBUG, "PLACE_MAIN: distributing extra=%d to %d auto margins", extra,
                line->main_auto_margin_count);
            extra_remainder = extra % line->main_auto_margin_count;
            extra /= line->main_auto_margin_count;
        } else {
            int free_main = ctx->available_main - line->used_main_size;
            uint8_t jc = css_computed_justify_content(ctx->flex->style);
            switch (jc) {
            default:
                break;
            case CSS_JUSTIFY_CONTENT_FLEX_END:
                jc_gap_pre = free_main;
                break;
            case CSS_JUSTIFY_CONTENT_CENTER:
                jc_gap_pre = free_main / 2;
                jc_gap_pre_extra = free_main - (jc_gap_pre * 2);
                break;
            case CSS_JUSTIFY_CONTENT_SPACE_BETWEEN:
                if (line->count > 1) {
                    int gaps = (int)(line->count - 1);
                    jc_gap_between = free_main / gaps;
                    jc_gap_between_rem = free_main % gaps;
                }
                break;
            case CSS_JUSTIFY_CONTENT_SPACE_AROUND:
                if (line->count > 0) {
                    int denom = (int)line->count;
                    int base_between = free_main / denom;
                    int remainder = free_main % denom;
                    jc_gap_between = base_between;
                    jc_gap_pre = base_between / 2;
                    jc_gap_pre_extra = base_between % 2;
                    jc_gap_between_rem = remainder;
                }
                break;
            case CSS_JUSTIFY_CONTENT_SPACE_EVENLY: {
                int gaps = (int)(line->count + 1);
                jc_gap_between = free_main / gaps;
                jc_gap_pre = jc_gap_between;
                jc_gap_between_rem = free_main % gaps;
                if (jc_gap_between_rem > 0) {
                    jc_gap_pre_extra = 1;
                    jc_gap_between_rem--;
                }
            } break;
            }
        }
    }

    if (!ctx->main_reversed) {
        main_pos += jc_gap_pre + jc_gap_pre_extra;
    } else {
        main_pos -= jc_gap_pre + jc_gap_pre_extra;
    }

    for (size_t i = line->first; i < item_count; i++) {
        enum box_side main_end = ctx->horizontal ? RIGHT : BOTTOM;
        enum box_side main_start = ctx->horizontal ? LEFT : TOP;
        struct flex_item_data *item = &ctx->item.data[i];
        struct box *b = item->box;
        int extra_total = 0;
        int extra_post = 0;
        int extra_pre = 0;
        int box_size_main;
        int *box_pos_main;

        if (ctx->horizontal) {
            b->width = item->target_main_size - lh__delta_outer_width(b);
            NSLOG(flex, DEEPDEBUG, "ITEM[%zu]: width=%d target_main=%d delta_outer=%d", i, b->width,
                item->target_main_size, lh__delta_outer_width(b));

            if (!layout_flex_item(ctx, item, b->width)) {
                return false;
            }

            /* Per CSS Flexbox spec §9.4.7: "Determine the hypothetical cross size
             * of each item by performing layout with the used main size and the
             * available space, treating auto as fit-content."
             * If height is still AUTO after layout, compute fit-content height. */
            if (b->height == AUTO) {
                if (b->children == NULL) {
                    /* Empty box: content height is 0 */
                    b->height = 0;
                    NSLOG(flex, DEEPDEBUG, "ITEM[%zu]: empty box, height resolved to 0 per CSS spec §9.4.7", i);
                } else {
                    /* Non-empty box: compute height from children (fit-content).
                     * This is the bottom edge of the last child plus padding/border. */
                    int content_bottom = 0;
                    for (struct box *child = b->children; child != NULL; child = child->next) {
                        if (child->type == BOX_FLOAT_LEFT || child->type == BOX_FLOAT_RIGHT) {
                            continue;
                        }
                        int child_bottom = child->y + child->height;
                        if (child->margin[BOTTOM] != AUTO) {
                            child_bottom += child->margin[BOTTOM];
                        }
                        if (child_bottom > content_bottom) {
                            content_bottom = child_bottom;
                        }
                    }
                    b->height = content_bottom;
                    NSLOG(
                        flex, WARNING, "ITEM[%zu]: computed height %d from children per CSS spec §9.4.7", i, b->height);
                }
            }
        }

        box_size_main = lh__box_size_main(ctx->horizontal, b);
        box_pos_main = ctx->horizontal ? &b->x : &b->y;


        if (!lh__box_is_absolute(b)) {
            if (b->margin[main_start] == AUTO) {
                extra_pre = extra + extra_remainder;
            }
            if (b->margin[main_end] == AUTO) {
                extra_post = extra + extra_remainder;
            }
            extra_total = extra_pre + extra_post;

            main_pos += pre_multiplier * (extra_total + box_size_main + lh__delta_outer_main(ctx->flex, b));
            NSLOG(flex, DEEPDEBUG, "ITEM[%zu]: after pre_mult main_pos=%d (pre_mult=%d)", i, main_pos, pre_multiplier);
        }

        *box_pos_main = main_pos + lh__non_auto_margin(b, main_start) + extra_pre + b->border[main_start].width;
        NSLOG(flex, DEEPDEBUG, "ITEM[%zu]: POSITIONED at %d (main_pos=%d)", i, *box_pos_main, main_pos);

        if (!lh__box_is_absolute(b)) {
            int cross_size;
            int box_size_cross = lh__box_size_cross(ctx->horizontal, b);

            main_pos += post_multiplier * (extra_total + box_size_main + lh__delta_outer_main(ctx->flex, b));
            NSLOG(
                flex, DEEPDEBUG, "ITEM[%zu]: after post_mult main_pos=%d (post_mult=%d)", i, main_pos, post_multiplier);

            /* DIAG: Log detailed child contribution for column flex debugging */
            if (!ctx->horizontal) {
                NSLOG(flex, WARNING,
                    "COLUMN_CHILD[%zu]: box %p type=%d h=%d m_top=%d m_bot=%d delta_outer=%d total_contrib=%d running_main=%d",
                    i, b, b->type, box_size_main, b->margin[TOP], b->margin[BOTTOM], lh__delta_outer_main(ctx->flex, b),
                    box_size_main + lh__delta_outer_main(ctx->flex, b), main_pos);
            }

            if (jc_gap_between > 0 || jc_gap_between_rem > 0) {
                int extra_between_for_item = 0;
                if (jc_gap_between_rem > 0) {
                    int gap_idx = (int)(i - line->first);
                    int gaps = (int)(line->count - 1);
                    if (gaps > 0) {
                        if (!ctx->main_reversed) {
                            if (gap_idx < jc_gap_between_rem && gap_idx < gaps) {
                                extra_between_for_item = 1;
                            }
                        } else {
                            int rev_idx = gaps - 1 - gap_idx;
                            if (rev_idx < jc_gap_between_rem && gap_idx < gaps) {
                                extra_between_for_item = 1;
                            }
                        }
                    }
                }

                main_pos += (!ctx->main_reversed ? 1 : -1) * (jc_gap_between + extra_between_for_item);
                NSLOG(flex, DEEPDEBUG, "ITEM[%zu]: after jc_gap main_pos=%d (jc_gap=%d)", i, main_pos, jc_gap_between);
            }

            /* Add CSS gap property spacing between items (not after the last item) */
            if (i < item_count - 1 && ctx->main_gap > 0) {
                NSLOG(
                    flex, DEEPDEBUG, "ITEM[%zu]: ADDING CSS GAP main_pos_before=%d gap=%d", i, main_pos, ctx->main_gap);
                main_pos += (!ctx->main_reversed ? 1 : -1) * ctx->main_gap;
                NSLOG(flex, DEEPDEBUG, "ITEM[%zu]: ADDING CSS GAP main_pos_after=%d", i, main_pos);
            }

            cross_size = box_size_cross + lh__delta_outer_cross(ctx->flex, b);
            if (line->cross_size < cross_size) {
                NSLOG(flex, WARNING, "LINE CROSS_SIZE update: box %p type=%d height=%d -> line->cross_size=%d", b,
                    b->type, box_size_cross, cross_size);
                line->cross_size = cross_size;
            }
        }
    }

    return true;
}

/**
 * Collect items onto lines and place items along the lines
 *
 * \param[in] ctx   Flex layout context
 * \return true on success, false on failure.
 */
static bool layout_flex__collect_items_into_lines(struct flex_ctx *ctx)
{
    size_t pos = 0;

    while (pos < ctx->item.count) {
        struct flex_line_data *line;

        line = layout_flex__build_line(ctx, pos);
        if (line == NULL) {
            return false;
        }

        pos += line->count;

        NSLOG(flex, DEEPDEBUG,
            "flex-container: %p: "
            "fitted: %zu (total: %zu/%zu)",
            ctx->flex, line->count, pos, ctx->item.count);

        if (!layout_flex__resolve_line(ctx, line)) {
            return false;
        }

        if (!layout_flex__place_line_items_main(ctx, line)) {
            return false;
        }

        /* DIAG: Log line finalization */
        NSLOG(flex, WARNING, "LINE FINAL: container %p line_idx=%zu items=%zu cross_size=%d main_size=%d", ctx->flex,
            ctx->line.count - 1, line->count, line->cross_size, line->main_size);

        ctx->cross_size += line->cross_size;
        if (ctx->main_size < line->main_size) {
            ctx->main_size = line->main_size;
        }
    }

    /* Add total cross_gap to container's cross_size (one gap between each pair of lines) */
    if (ctx->line.count > 1 && ctx->cross_gap > 0) {
        ctx->cross_size += (ctx->line.count - 1) * ctx->cross_gap;
    }

    return true;
}

/**
 * Align items on a line.
 *
 * \param[in] ctx    Flex layout context
 * \param[in] line   Line to align items on
 * \param[in] extra  Extra line width in pixels
 */
static void layout_flex__place_line_items_cross(struct flex_ctx *ctx, struct flex_line_data *line, int extra)
{
    enum box_side cross_start = ctx->horizontal ? TOP : LEFT;
    size_t item_count = line->first + line->count;

    for (size_t i = line->first; i < item_count; i++) {
        struct flex_item_data *item = &ctx->item.data[i];
        struct box *b = item->box;
        int cross_free_space;
        int *box_size_cross;
        int *box_pos_cross;

        box_pos_cross = ctx->horizontal ? &b->y : &b->x;
        box_size_cross = lh__box_size_cross_ptr(ctx->horizontal, b);

        cross_free_space = line->cross_size + extra - *box_size_cross - lh__delta_outer_cross(ctx->flex, b);

        /* DIAG: Log cross placement for each item */
        NSLOG(flex, WARNING, "CROSS_PLACE[%zu]: box %p type=%d line_cross=%d item_cross=%d free_space=%d", i, b,
            b->type, line->cross_size, *box_size_cross, cross_free_space);

        switch (lh__box_align_self(ctx->flex, b)) {
        default:
            /* Fall through. */
        case CSS_ALIGN_SELF_STRETCH:
            if (lh__box_size_cross_is_auto(ctx->horizontal, b)) {
                int old_cross_size = *box_size_cross;
                int target_cross_size = old_cross_size + cross_free_space;

                /* Set stretched size and mark with flag so layout_flex preserves it */
                *box_size_cross = target_cross_size;
                if (ctx->horizontal && cross_free_space > 0) {
                    b->flags |= HEIGHT_STRETCHED;
                }

                /* Relayout children for stretch */
                if (!layout_flex_item(ctx, item, b->width)) {
                    return;
                }

                /* If this stretched item is a column flex container and its height increased,
                 * redistribute auto margins to push items with margin-top/bottom: auto. */
                if (ctx->horizontal && cross_free_space > 0 && b->type == BOX_FLEX && b->style) {
                    uint8_t child_dir = css_computed_flex_direction(b->style);
                    if (child_dir == CSS_FLEX_DIRECTION_COLUMN || child_dir == CSS_FLEX_DIRECTION_COLUMN_REVERSE) {
                        NSLOG(flex, INFO, "Stretched column flex %p: height %d -> %d, redistributing", b,
                            old_cross_size, target_cross_size);
                        layout_flex_redistribute_auto_margins_vertical(b);
                    }
                }
            }
            /* Fall through. */
        case CSS_ALIGN_SELF_FLEX_START:
            *box_pos_cross = ctx->flex->padding[cross_start] + line->pos + lh__non_auto_margin(b, cross_start) +
                b->border[cross_start].width;
            break;

        case CSS_ALIGN_SELF_FLEX_END:
            *box_pos_cross = ctx->flex->padding[cross_start] + line->pos + cross_free_space +
                lh__non_auto_margin(b, cross_start) + b->border[cross_start].width;
            break;

        case CSS_ALIGN_SELF_BASELINE:
            /* Fall through. */
        case CSS_ALIGN_SELF_CENTER:
            *box_pos_cross = ctx->flex->padding[cross_start] + line->pos + cross_free_space / 2 +
                lh__non_auto_margin(b, cross_start) + b->border[cross_start].width;
            break;
        }
    }
}

/**
 * Place the lines and align the items on the line.
 *
 * \param[in] ctx  Flex layout context
 */
static void layout_flex__place_lines(struct flex_ctx *ctx)
{
    bool reversed = ctx->wrap == CSS_FLEX_WRAP_WRAP_REVERSE;
    int line_pos = reversed ? ctx->cross_size : 0;
    int post_multiplier = reversed ? 0 : 1;
    int pre_multiplier = reversed ? -1 : 0;
    int extra_remainder = 0;
    int extra = 0;

    if (ctx->available_cross != AUTO && ctx->available_cross > ctx->cross_size && ctx->line.count > 0) {
        extra = ctx->available_cross - ctx->cross_size;

        extra_remainder = extra % ctx->line.count;
        extra /= ctx->line.count;
    }

    for (size_t i = 0; i < ctx->line.count; i++) {
        struct flex_line_data *line = &ctx->line.data[i];

        line_pos += pre_multiplier * line->cross_size;
        line->pos = line_pos;
        line_pos += post_multiplier * line->cross_size + extra + extra_remainder;

        /* Add cross_gap between lines (not after the last line) */
        if (i < ctx->line.count - 1 && ctx->cross_gap > 0) {
            line_pos += (!reversed ? 1 : -1) * ctx->cross_gap;
        }

        layout_flex__place_line_items_cross(ctx, line, extra + extra_remainder);

        if (extra_remainder > 0) {

            extra_remainder--;
        }
    }
}

/**
 * Layout a flex container.
 *
 * \param[in] flex             table to layout
 * \param[in] available_width  width of containing block
 * \param[in] content          memory pool for any new boxes
 * \return  true on success, false on memory exhaustion
 */
/**
 * Sort flex items by 'order' property, then by original index (stable sort)
 */
static int flex_item_cmp(const void *a, const void *b)
{
    const struct flex_item_data *fa = (const struct flex_item_data *)a;
    const struct flex_item_data *fb = (const struct flex_item_data *)b;

    if (fa->order != fb->order) {
        return fa->order - fb->order;
    }

    return (int)(fa->original_index - fb->original_index);
}

bool layout_flex(struct box *flex, int available_width, html_content *content)
{
    int max_height;
    struct css_size min_height;
    int max_width = -1;
    struct css_size min_width;
    struct flex_ctx *ctx;
    bool success = false;

    ctx = layout_flex_ctx__create(content, flex);
    if (ctx == NULL) {
        return false;
    }

    NSLOG(flex, DEEPDEBUG, "box %p: %s, available_width %i, width: %i", flex,
        ctx->horizontal ? "horizontal" : "vertical", available_width, flex->width);

    /* Per CSS Flexbox §9.8: "Once the cross size of a flex line has been determined,
     * the cross sizes of items in auto-sized flex containers are also considered
     * definite for the purpose of layout."
     *
     * If this flex container was stretched by its parent (HEIGHT_STRETCHED flag),
     * preserve the stretched height by not letting layout_find_dimensions overwrite it.
     * Clear the flag after checking since we've now handled the stretch.
     */
    bool height_was_stretched = (flex->flags & HEIGHT_STRETCHED) != 0;
    flex->flags &= ~HEIGHT_STRETCHED; /* Clear flag after reading */
    int *height_ptr = height_was_stretched ? NULL : &flex->height;

    layout_find_dimensions(ctx->unit_len_ctx, available_width, -1, flex, flex->style, NULL, /* width - already set */
        height_ptr, /* height - NULL if already definite from stretch */
        &max_width, &min_width, &max_height, &min_height, flex->margin, flex->padding, flex->border);

    if (height_was_stretched) {
        NSLOG(flex, DEBUG, "box %p: preserved stretched height %d", flex, flex->height);
    }

    available_width = min(available_width, flex->width);

    int resolved_height;
    if (flex->width == AUTO || flex->width == UNKNOWN_WIDTH) {
        flex->width = ctx->horizontal ? ctx->main_size : ctx->cross_size;
        if (max_width >= 0 && flex->width > max_width) {
            flex->width = max_width;
        }
        if (min_width.type == CSS_SIZE_SET && min_width.value > 0 && flex->width < min_width.value) {
            flex->width = min_width.value;
        }
    }

    if (flex->height != AUTO) {
        resolved_height = flex->height;
    } else if (min_height.type == CSS_SIZE_SET && min_height.value > 0) {
        resolved_height = min_height.value;
    } else {
        resolved_height = AUTO;
    }

    if (ctx->horizontal) {
        ctx->available_main = available_width;
        ctx->available_cross = resolved_height;
    } else {
        ctx->available_main = resolved_height;
        ctx->available_cross = available_width;
    }

    NSLOG(flex, DEEPDEBUG, "box %p: available_main: %i", flex, ctx->available_main);
    NSLOG(flex, DEEPDEBUG, "box %p: available_cross: %i", flex, ctx->available_cross);
    NSLOG(flex, INFO, "box %p: available_main: %i", flex, ctx->available_main);
    NSLOG(flex, INFO, "box %p: available_cross: %i", flex, ctx->available_cross);

    layout_flex_ctx__populate_item_data(ctx, flex, available_width);

    /* Sort flex items by 'order' property then by original index */
    if (ctx->item.count > 1) {
        qsort(ctx->item.data, ctx->item.count, sizeof(struct flex_item_data), flex_item_cmp);
    }

    if (ctx->item.count == 0) {
        layout_flex_ctx__destroy(ctx);
        return true;
    }

    /* Place items onto lines. */
    success = layout_flex__collect_items_into_lines(ctx);
    if (!success) {
        goto cleanup;
    }

    layout_flex__place_lines(ctx);

    if (flex->height == AUTO) {
        flex->height = ctx->horizontal ? ctx->cross_size : ctx->main_size;
    }

    /* TWO-PASS LAYOUT: Re-resolve percentage flex-basis against definite height
     * and apply flex-shrink algorithm. Only runs for column flex with % flex-basis. */
    layout_flex__two_pass_resolve(ctx, flex);

    if (flex->height != AUTO) {
        if (max_height >= 0 && flex->height > max_height) {
            flex->height = max_height;
        }
        if (min_height.type == CSS_SIZE_SET && min_height.value > 0 && flex->height < min_height.value) {
            flex->height = min_height.value;
        }
    }

    /* For column flex containers, redistribute auto margins now that height is definite.
     * This allows margin-top: auto and margin-bottom: auto to work correctly by
     * distributing extra vertical space after the container's final height is known.
     */
    if (!ctx->horizontal) {
        NSLOG(flex, INFO, "Column flex %p: final height=%d, calling redistribute", flex, flex->height);
        layout_flex_redistribute_auto_margins_vertical(flex);

        /* Also handle nested column flex containers recursively */
        for (struct box *child = flex->children; child; child = child->next) {
            if (child->type == BOX_FLEX && child->style) {
                uint8_t child_flex_dir = css_computed_flex_direction(child->style);
                bool child_is_column = (child_flex_dir == CSS_FLEX_DIRECTION_COLUMN ||
                    child_flex_dir == CSS_FLEX_DIRECTION_COLUMN_REVERSE);
                if (child_is_column) {
                    NSLOG(flex, INFO, "Nested column flex %p: redistributing", child);
                    layout_flex_redistribute_auto_margins_vertical(child);
                }
            }
        }
    }

    success = true;

cleanup:
    /* DIAG: Final flex container summary (before destroying ctx) */
    NSLOG(flex, WARNING, "FLEX DONE: box %p %s w=%d h=%d %s", flex, ctx->horizontal ? "ROW" : "COL", flex->width,
        flex->height, success ? "OK" : "FAIL");

    layout_flex_ctx__destroy(ctx);

    return success;
}

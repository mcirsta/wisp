/* SPDX-License-Identifier: GPL-2.0-or-later
 * CSSOM reads share renderer selection, including elements without boxes. */
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <wisp/content/handlers/html/private.h>
#include <wisp/content/handlers/html/box.h>
#include "content/handlers/css/select.h"
#include "content/handlers/html/box_construct.h"
#include "content/handlers/html/css.h"
#include "content/handlers/html/layout_internal.h"
#include "content/handlers/html/box_sticky.h"

static bool flex_or_grid(const css_computed_style *style)
{
    if (style == NULL)
        return false;
    uint8_t display = css_computed_display(style, false);
    return display == CSS_DISPLAY_FLEX || display == CSS_DISPLAY_INLINE_FLEX || display == CSS_DISPLAY_GRID ||
        display == CSS_DISPLAY_INLINE_GRID;
}

/* Search only the originating node's anonymous/inline wrappers. Styles can
 * be shared, so use retained pseudo provenance, not style pointer identity. */
static struct box *pseudo_box(struct box *owner, css_pseudo_element pseudo, dom_node *node)
{
    for (struct box *box = owner->children; box != NULL; box = box->next) {
        unsigned flag = pseudo == CSS_PSEUDO_ELEMENT_BEFORE ? TEXT_BEFORE : TEXT_AFTER;
        if (box->type != BOX_TEXT && box->type != BOX_INLINE_END && box->text_node == node && (box->flags & flag))
            return box;
        if (box->node == NULL || box->node == node) {
            struct box *found = pseudo_box(box, pseudo, node);
            if (found != NULL)
                return found;
        }
    }
    return NULL;
}

void html_computed_style_fini(struct html_computed_style *result)
{
    if (result->owned != NULL)
        css_select_results_destroy(result->owned);
    if (result->initial != NULL)
        css_computed_style_destroy(result->initial);
    *result = (struct html_computed_style){0};
}

nserror html_computed_style_get(
    html_content *html, dom_node *node, css_pseudo_element pseudo, struct html_computed_style *result)
{
    *result = (struct html_computed_style){0};
    dom_node **chain = NULL, *current = dom_node_ref(node);
    size_t count = 0, capacity = 0;
    const css_computed_style *parent = NULL, *root = html->unit_len_ctx.root_style;
    const css_select_results *selected = NULL;
    css_select_results *root_owned = NULL;
    nserror error = NSERROR_OK;
    /* Find the closest already-rendered ancestor. The common visible-element
     * path borrows its selected styles and allocates nothing. */
    while (current != NULL) {
        struct box *box = box_for_node(current);
        if (box != NULL && box->style != NULL) {
            parent = box->style;
            if (count == 0) {
                selected = box->styles;
                result->box = box;
                result->blockify = box == html->layout || (box->parent != NULL && flex_or_grid(box->parent->style));
            }
            break;
        }
        dom_node_type type;
        if (dom_node_get_node_type(current, &type) != DOM_NO_ERR) {
            error = NSERROR_DOM;
            goto out;
        }
        if (type != DOM_ELEMENT_NODE)
            break;
        if (count == capacity) {
            size_t next = capacity ? capacity * 2 : 16;
            if (next < capacity || next > SIZE_MAX / sizeof(*chain)) {
                error = NSERROR_NOMEM;
                goto out;
            }
            dom_node **grown = realloc(chain, next * sizeof(*chain));
            if (grown == NULL) {
                error = NSERROR_NOMEM;
                goto out;
            }
            chain = grown;
            capacity = next;
        }
        chain[count++] = current;
        current = NULL;
        if (dom_node_get_parent_node(chain[count - 1], &current) != DOM_NO_ERR) {
            error = NSERROR_DOM;
            goto out;
        }
    }
    for (size_t i = count; i > 0; i--) {
        /* A display:none node can have a libcss cache without a retained box
         * style. This path deliberately reselects it; retire the old selection
         * data first, just as the renderer does before rebuilding styles. */
        error = nscss_invalidate_node(chain[i - 1], NULL);
        if (error != NSERROR_OK)
            goto out;
        if (i == 1)
            result->blockify = parent == NULL || flex_or_grid(parent);
        css_select_results *next = box_get_style(html, parent, root, chain[i - 1]);
        if (next == NULL) {
            error = NSERROR_NOMEM;
            goto out;
        }
        if (result->owned != NULL && result->owned != root_owned)
            css_select_results_destroy(result->owned);
        result->owned = next;
        selected = next;
        parent = next->styles[CSS_PSEUDO_ELEMENT_NONE];
        if (root == NULL) {
            root = parent;
            root_owned = next;
        }
    }
    result->style = pseudo == CSS_PSEUDO_ELEMENT_NONE ? parent : selected ? selected->styles[pseudo] : NULL;
    if (result->style == NULL && parent != NULL && pseudo != CSS_PSEUDO_ELEMENT_NONE) {
        /* ::before/::after exist for style queries even without generated
         * content. Their unspecified values inherit from the originating node. */
        nscss_select_ctx ctx = {.ctx = html->select_ctx,
            .quirks = html->base.quirks,
            .base_url = html->base_url,
            .universal = html->universal,
            .root_style = root,
            .parent_style = parent};
        css_unit_ctx units = html->unit_len_ctx;
        units.root_style = root;
        result->initial = nscss_get_blank_style(&ctx, &units, parent);
        result->style = result->initial;
        if (result->style == NULL)
            error = NSERROR_NOMEM;
    }
    if (pseudo != CSS_PSEUDO_ELEMENT_NONE) {
        result->blockify = flex_or_grid(parent);
        struct box *owner = result->box;
        if (owner != NULL && owner->type == BOX_INLINE && owner->parent != NULL)
            owner = owner->parent->parent;
        result->box = owner != NULL && result->style != NULL &&
                (pseudo == CSS_PSEUDO_ELEMENT_BEFORE || pseudo == CSS_PSEUDO_ELEMENT_AFTER)
            ? pseudo_box(owner, pseudo, node)
            : NULL;
    }
out:
    dom_node_unref(current);
    for (size_t i = 0; i < count; i++)
        dom_node_unref(chain[i]);
    free(chain);
    if (root_owned != NULL && root_owned != result->owned)
        css_select_results_destroy(root_owned);
    if (error != NSERROR_OK)
        html_computed_style_fini(result);
    return error;
}

struct resolved_value_ctx {
    const struct html_computed_style *selected;
    const css_unit_ctx *units;
};

/* The content-box reference used for ordinary percentage edges; absolute
 * positioning uses its established containing block's padding box instead. */
static int edge_reference_width(const struct box *box)
{
    const struct box *cb = box->abs_containing_block;
    bool absolute = cb != NULL;
    if (cb == NULL) {
        cb = box->parent;
        while (cb != NULL &&
            (cb->type == BOX_INLINE_CONTAINER || cb->type == BOX_INLINE || cb->type == BOX_FLOAT_LEFT ||
                cb->type == BOX_FLOAT_RIGHT))
            cb = cb->parent;
    }
    if (cb == NULL || !lh__is_resolved(cb->width))
        return -1;
    int64_t width = cb->width;
    if (absolute)
        width += (int64_t)cb->padding[LEFT] + cb->padding[RIGHT];
    return width >= 0 && width < INT_MAX ? (int)width : -1;
}

static css_error padding_pixels(const struct resolved_value_ctx *ctx, unsigned side, int *pixels)
{
    css_fixed_or_calc length;
    css_unit unit;
    if (padding_funcs[side](ctx->selected->style, &length, &unit) != CSS_PADDING_SET)
        return CSS_INVALID;
    return css_computed_length_to_px(
        ctx->selected->style, ctx->units, edge_reference_width(ctx->selected->box), length, unit, pixels);
}

static css_error inset_pixels(const struct resolved_value_ctx *ctx, unsigned side, int64_t *pixels)
{
    struct box *box = ctx->selected->box;
    const css_computed_style *style = ctx->selected->style;
    uint8_t position = css_computed_position(style);
    if (position == CSS_POSITION_STATIC)
        return CSS_PROPERTY_NOT_SET;
    const unsigned opposite[] = {BOTTOM, LEFT, TOP, RIGHT};
    uint8_t (*const getters[])(const css_computed_style *, css_fixed_or_calc *, css_unit *) = {
        css_computed_top, css_computed_right, css_computed_bottom, css_computed_left};
    css_fixed_or_calc length;
    css_unit unit;
    bool horizontal = side == LEFT || side == RIGHT;
    bool both_set = getters[side](style, &length, &unit) == CSS_LEFT_SET &&
        getters[opposite[side]](style, &length, &unit) == CSS_LEFT_SET;
    if (position == CSS_POSITION_RELATIVE) {
        const struct box *cb = box->float_container != NULL && css_computed_float(style) != CSS_FLOAT_NONE
            ? box->float_container
            : box->parent;
        if (cb == NULL)
            return CSS_INVALID;
        bool rtl = cb->style != NULL && css_computed_direction(cb->style) == CSS_DIRECTION_RTL;
        /* CSSOM §9 keeps the ignored over-constrained inset computed. */
        if (both_set && side == (horizontal ? (rtl ? LEFT : RIGHT) : BOTTOM))
            return CSS_PROPERTY_NOT_SET;
        int x, y;
        layout_compute_relative_offset(ctx->units, box, &x, &y);
        *pixels = side == LEFT ? x : side == TOP ? y : side == RIGHT ? -(int64_t)x : -(int64_t)y;
        return CSS_OK;
    }
    if (position == CSS_POSITION_STICKY) {
        const struct box_sticky *sticky = box_sticky_get(box);
        if (sticky == NULL)
            return CSS_INVALID;
        const struct box_sticky_axis *axis = horizontal ? &sticky->x : &sticky->y;
        bool start = side == TOP || side == LEFT;
        if (start ? axis->start_auto : axis->end_auto)
            return CSS_PROPERTY_NOT_SET;
        *pixels = start ? axis->inset_start : axis->inset_end;
        return CSS_OK;
    }
    const struct box *cb = box->abs_containing_block;
    if (cb == NULL || cb->type == BOX_INLINE)
        return CSS_INVALID;
    unsigned start = horizontal ? LEFT : TOP, end = horizontal ? RIGHT : BOTTOM;
    bool definite = (horizontal ? css_computed_width(style, &length, &unit)
                                : css_computed_height(style, &length, &unit)) == CSS_WIDTH_SET ||
        (box->flags & IS_REPLACED);
    bool margins_set = margin_funcs[start](style, &length, &unit) != CSS_MARGIN_AUTO &&
        margin_funcs[end](style, &length, &unit) != CSS_MARGIN_AUTO;
    bool rtl = cb->style != NULL && css_computed_direction(cb->style) == CSS_DIRECTION_RTL;
    if (both_set && definite && margins_set && side == (horizontal ? (rtl ? LEFT : RIGHT) : BOTTOM))
        return CSS_PROPERTY_NOT_SET;
    if (side == start) {
        *pixels = (int64_t)(horizontal ? box->x : box->y) - box->border[start].width - box->margin[start];
    } else {
        *pixels = (int64_t)(horizontal ? cb->width : cb->height) + cb->padding[start] + cb->padding[end] -
            (horizontal ? box->x : box->y) - (horizontal ? box->width : box->height) - box->padding[start] -
            box->padding[end] - box->border[end].width - box->margin[end];
    }
    return CSS_OK;
}

static css_error used_length(void *pw, const char *name, css_fixed *value)
{
    const struct resolved_value_ctx *ctx = pw;
    const struct html_computed_style *selected = ctx->selected;
    const struct box *box = selected->box;
    if (box == NULL)
        return CSS_PROPERTY_NOT_SET;
    uint8_t display = css_computed_display(selected->style, selected->blockify);
    bool inline_text = display == CSS_DISPLAY_INLINE && !(box->flags & IS_REPLACED);
    bool row = display == CSS_DISPLAY_TABLE_ROW || display == CSS_DISPLAY_TABLE_ROW_GROUP ||
        display == CSS_DISPLAY_TABLE_HEADER_GROUP || display == CSS_DISPLAY_TABLE_FOOTER_GROUP;
    bool column = display == CSS_DISPLAY_TABLE_COLUMN || display == CSS_DISPLAY_TABLE_COLUMN_GROUP;
    int64_t pixels;
    if (strcmp(name, "width") == 0 || strcmp(name, "height") == 0) {
        bool width = name[0] == 'w';
        if (inline_text || (width ? row : column))
            return CSS_PROPERTY_NOT_SET;
        pixels = width ? box->width : box->height;
        if (pixels == INT_MIN || pixels == INT_MAX)
            return CSS_INVALID;
        unsigned start = width ? LEFT : TOP, end = width ? RIGHT : BOTTOM;
        pixels += (int64_t)box->padding[start] + box->padding[end];
        if (css_computed_box_sizing(selected->style) == CSS_BOX_SIZING_BORDER_BOX) {
            pixels += (int64_t)box->border[start].width + box->border[end].width;
        } else {
            /* Layout also stores scrollbar and table-cell alignment space in
             * padding. Derive the CSS sizing box using the actual CSS padding. */
            int first, last;
            css_error error = padding_pixels(ctx, start, &first);
            if (error == CSS_OK)
                error = padding_pixels(ctx, end, &last);
            if (error != CSS_OK)
                return error;
            pixels -= (int64_t)first + last;
        }
    } else if (strcmp(name, "top") == 0 || strcmp(name, "right") == 0 || strcmp(name, "bottom") == 0 ||
        strcmp(name, "left") == 0) {
        unsigned side = name[0] == 't' ? TOP : name[0] == 'r' ? RIGHT : name[0] == 'b' ? BOTTOM : LEFT;
        css_error error = inset_pixels(ctx, side, &pixels);
        if (error != CSS_OK)
            return error;
    } else {
        bool margin = strncmp(name, "margin-", 7) == 0;
        bool padding = strncmp(name, "padding-", 8) == 0;
        if (!margin && !padding)
            return CSS_PROPERTY_NOT_SET;
        /* CSS2 applicability: margins exclude internal table boxes; padding
         * excludes rows, row groups, columns and column groups. */
        if (row || column || (margin && display == CSS_DISPLAY_TABLE_CELL))
            return CSS_PROPERTY_NOT_SET;
        const char *side = name + (margin ? 7 : 8);
        unsigned edge = strcmp(side, "top") == 0 ? TOP
            : strcmp(side, "right") == 0         ? RIGHT
            : strcmp(side, "bottom") == 0        ? BOTTOM
                                                 : LEFT;
        const struct box *fragment = box;
        if (edge == RIGHT && box->type == BOX_INLINE && box->inline_end != NULL)
            fragment = box->inline_end;
        if (margin) {
            pixels = fragment->margin[edge];
        } else {
            int padding_px;
            css_error error = padding_pixels(ctx, edge, &padding_px);
            if (error != CSS_OK)
                return error;
            pixels = padding_px;
        }
        if (pixels == INT_MIN || pixels == INT_MAX)
            return CSS_INVALID;
    }
    /* Avoid INTTOFIX overflow before converting device pixels back to CSS px. */
    if (ctx->units->device_dpi <= 0)
        return CSS_INVALID;
    int64_t fixed = pixels * F_1 * F_96 / ctx->units->device_dpi;
    if (fixed < INT32_MIN || fixed > INT32_MAX)
        return CSS_INVALID;
    *value = (css_fixed)fixed;
    return CSS_OK;
}

css_error html_computed_style_value(
    html_content *html, const struct html_computed_style *style, const char *name, size_t length, lwc_string **out)
{
    int width = style->box != NULL ? edge_reference_width(style->box) : -1;
    int height = style->box != NULL ? lh__available_height_for_box(style->box, -1) : -1;
    css_unit_ctx units = lh__with_container_dimensions(&html->unit_len_ctx, width, height);
    struct resolved_value_ctx resolved = {.selected = style, .units = &units};
    struct css_computed_value_ctx ctx = {
        .units = &units, .blockify = style->blockify, .used_length = used_length, .pw = &resolved};
    return css_computed_property_value(style->style, &ctx, name, length, out);
}

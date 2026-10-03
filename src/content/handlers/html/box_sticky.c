/* CSS Positioned Layout, section 3.4: sticky positioning. */
#include <assert.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>

#include <wisp/browser_window.h>
#include <wisp/content/handlers/html/box.h>
#include <wisp/content/handlers/html/box_inspect.h>
#include <wisp/content/handlers/html/private.h>
#include <wisp/utils/corestrings.h>
#include "content/handlers/html/box_sticky.h"
#include "content/handlers/html/layout_internal.h"
#include "content/handlers/html/restyle_profile.h"
#include "desktop/scrollbar.h"
#include "utils/talloc.h"

static int64_t lesser(int64_t a, int64_t b)
{
    return a < b ? a : b;
}

static int64_t greater(int64_t a, int64_t b)
{
    return a > b ? a : b;
}

static int64_t axis_offset(const struct box_sticky_axis *axis, int64_t box_shift, int64_t cb_shift, int64_t port_shift)
{
    assert(axis != NULL);
    assert(axis->border_size >= 0 && axis->containing_size >= 0 && axis->scrollport_size >= 0);
    if (axis->start_auto && axis->end_auto)
        return 0;

    int64_t border_start = axis->border_start + box_shift;
    int64_t border_end = border_start + axis->border_size;
    int64_t view_start = axis->scrollport_start + port_shift + (axis->start_auto ? 0 : axis->inset_start);
    int64_t view_end = axis->scrollport_start + port_shift + axis->scrollport_size -
        (axis->end_auto ? 0 : axis->inset_end);

    /* Only competing specified insets need logical-end reduction. An auto
     * opposite edge is unconstrained, so preserve a sole specified inset even
     * for an oversized box. This follows Chromium/Firefox; see the compatibility
     * decision in docs/sticky-positioning.md. */
    if (!axis->start_auto && !axis->end_auto) {
        int64_t deficit = axis->border_size - (view_end - view_start);
        if (deficit > 0) {
            if (axis->reverse)
                view_start -= deficit;
            else
                view_end += deficit;
        }
    }

    int64_t offset = 0;
    if (!axis->start_auto)
        offset = greater(offset, view_start - border_start);
    if (!axis->end_auto)
        offset = lesser(offset, view_end - border_end);

    /* Margins constrain the available travel. A box already against/outside
     * its containing edge has no travel in that direction: sticky must not
     * pull it back just to repair its normal-flow overflow. Negative margins
     * extend the travel; do not discard their sign. */
    int64_t minimum = lesser(0, axis->containing_start + cb_shift + axis->margin_start - border_start);
    int64_t maximum = greater(
        0, axis->containing_start + cb_shift + axis->containing_size - axis->margin_end - border_end);
    return lesser(greater(offset, minimum), maximum);
}

int64_t box_sticky_axis_offset(const struct box_sticky_axis *axis)
{
    return axis_offset(axis, 0, 0, 0);
}

/* Preparation is separate from the scalar solver and never runs on scroll. */
static bool block_container(const struct box *box)
{
    switch (box->type) {
    case BOX_BLOCK:
    case BOX_INLINE_BLOCK:
    case BOX_FLEX:
    case BOX_INLINE_FLEX:
    case BOX_GRID:
    case BOX_INLINE_GRID:
    case BOX_TABLE_CELL:
        return true;
    default:
        return false;
    }
}

const char *box_sticky_unsupported(const struct box *box)
{
    if (!box->node || !box->style)
        return "non_principal_box";
    if (!block_container(box) || box->type == BOX_TABLE_CELL)
        return "box_type";
    if (box->parent == NULL)
        return "root_box";
    if (css_computed_float(box->style) != CSS_FLOAT_NONE &&
        !(box->parent->type == BOX_FLEX || box->parent->type == BOX_INLINE_FLEX || box->parent->type == BOX_GRID ||
            box->parent->type == BOX_INLINE_GRID))
        return "floated_sticky";
    for (const struct box *ancestor = box; ancestor; ancestor = ancestor->parent) {
        if (ancestor != box && (ancestor->flags & REPLACE_DIM))
            return "replaced_ancestor";
        if (ancestor->style == NULL)
            continue;
        if (css_computed_writing_mode(ancestor->style) != CSS_WRITING_MODE_HORIZONTAL_TB)
            return "writing_mode";
        uint32_t count = 0;
        const css_transform_function *functions = NULL;
        if (css_computed_transform(ancestor->style, &count, &functions) == CSS_TRANSFORM_FUNCTIONS && count)
            return "transform";
    }
    return NULL;
}

/* The root's overflow, or the body's when the root is visible in both axes,
 * belongs to the viewport. Ordinary inline boxes cannot establish scrollports. */
static bool scroll_container(const struct html_content *html, const struct box *box)
{
    if (box == html->layout || !block_container(box) || !box->style)
        return false;
    uint8_t x = css_computed_overflow_x(box->style), y = css_computed_overflow_y(box->style);
    if (x == CSS_OVERFLOW_VISIBLE && y == CSS_OVERFLOW_VISIBLE)
        return false;
    if (box->parent == html->layout && box->node && html->layout->style &&
        css_computed_overflow_x(html->layout->style) == CSS_OVERFLOW_VISIBLE &&
        css_computed_overflow_y(html->layout->style) == CSS_OVERFLOW_VISIBLE) {
        dom_string *name = NULL;
        if (dom_node_get_node_name(box->node, &name) == DOM_NO_ERR && name) {
            bool body = dom_string_caseless_lwc_isequal(name, corestring_lwc_body);
            dom_string_unref(name);
            if (body)
                return false;
        }
    }
    /* visible in one axis computes to auto beside a non-visible other axis.
     * Either way this ancestor supplies both axes under current overflow rules. */
    return x == CSS_OVERFLOW_HIDDEN || x == CSS_OVERFLOW_AUTO || x == CSS_OVERFLOW_SCROLL || y == CSS_OVERFLOW_HIDDEN ||
        y == CSS_OVERFLOW_AUTO || y == CSS_OVERFLOW_SCROLL;
}

/* Same unscrolled coordinate ancestry as box_coords_unscrolled(), with wide
 * sums so unsupported extreme geometry is rejected before narrowing to int. */
static void layout_origin(const struct box *box, int64_t *x, int64_t *y)
{
    *x = *y = 0;
    for (; box;) {
        *x += box->x;
        *y += box->y;
        if (box->abs_containing_block)
            box = box->abs_containing_block;
        else if (box->type == BOX_FLOAT_LEFT || box->type == BOX_FLOAT_RIGHT)
            box = box->float_container;
        else
            box = box->parent;
    }
}

static bool pixel_value(int64_t value, int *out)
{
    /* Keep layout's AUTO and UNKNOWN_WIDTH values out of prepared pixels. */
    if (value <= INT_MIN || value >= INT_MAX)
        return false;
    *out = value;
    return true;
}

static bool sticky_margin(
    const struct html_content *html, const struct box *box, const struct box *cb, enum box_side side, int *margin)
{
    css_fixed_or_calc value;
    css_unit unit;
    uint8_t type;
    switch (side) {
    case LEFT:
        type = css_computed_margin_left(box->style, &value, &unit);
        break;
    case RIGHT:
        type = css_computed_margin_right(box->style, &value, &unit);
        break;
    case TOP:
        type = css_computed_margin_top(box->style, &value, &unit);
        break;
    default:
        type = css_computed_margin_bottom(box->style, &value, &unit);
        break;
    }
    /* Block width solving stores leftover line space in the end margin even
     * when CSS specified zero. That alignment bookkeeping must not prohibit
     * sticky travel. Auto margins, however, need their actual resolved share. */
    if (type == CSS_MARGIN_SET) {
        if (!lh__length_to_px(box->style, &html->unit_len_ctx, cb->width, value, unit, margin))
            return false;
    } else {
        *margin = lh__non_auto_margin(box, side);
    }
    return pixel_value(*margin, margin);
}

static int64_t scrollport_size(const struct box *box, bool horizontal)
{
    int start = horizontal ? LEFT : TOP, end = horizontal ? RIGHT : BOTTOM;
    int64_t size = (int64_t)(horizontal ? box->width : box->height) + box->padding[start] + box->padding[end];
    if (!(box->flags & IS_VIEWPORT)) {
        uint8_t overflow = horizontal ? css_computed_overflow_y(box->style) : css_computed_overflow_x(box->style);
        bool bar = overflow == CSS_OVERFLOW_SCROLL ||
            (overflow == CSS_OVERFLOW_AUTO && (horizontal ? box_vscrollbar_present(box) : box_hscrollbar_present(box)));
        if (bar)
            size -= SCROLLBAR_WIDTH;
    }
    return size > 0 ? size : 0;
}

static bool
prepare_axis(const struct html_content *html, const struct box *box, struct box_sticky *sticky, bool horizontal)
{
    struct box_sticky_axis *axis = horizontal ? &sticky->x : &sticky->y;
    struct box *cb = sticky->containing_block, *sp = sticky->scrolling_ancestor;
    int start = horizontal ? LEFT : TOP, end = horizontal ? RIGHT : BOTTOM;
    int64_t bx, by, cx, cy, sx, sy;
    layout_origin(box, &bx, &by);
    layout_origin(cb, &cx, &cy);
    layout_origin(sp, &sx, &sy);
    int64_t port_size = scrollport_size(sp, horizontal);
    if (!pixel_value((horizontal ? bx : by) - box->border[start].width, &axis->border_start) ||
        !pixel_value((int64_t)(horizontal ? box->width : box->height) + box->padding[start] + box->padding[end] +
                box->border[start].width + box->border[end].width,
            &axis->border_size) ||
        !pixel_value((horizontal ? cx : cy) + cb->padding[start], &axis->containing_start) ||
        !pixel_value(horizontal ? cb->width : cb->height, &axis->containing_size) ||
        !pixel_value(horizontal ? sx : sy, &axis->scrollport_start) ||
        !pixel_value(port_size > 0 ? port_size : 0, &axis->scrollport_size) || axis->border_size < 0 ||
        axis->containing_size < 0)
        return false;
    if (!sticky_margin(html, box, cb, start, &axis->margin_start) ||
        !sticky_margin(html, box, cb, end, &axis->margin_end))
        return false;
    axis->reverse = horizontal && cb->style && css_computed_direction(cb->style) == CSS_DIRECTION_RTL;
    css_fixed_or_calc value;
    css_unit unit;
    uint8_t type = horizontal ? css_computed_left(box->style, &value, &unit)
                              : css_computed_top(box->style, &value, &unit);
    axis->start_auto = type != (horizontal ? CSS_LEFT_SET : CSS_TOP_SET);
    if (!axis->start_auto &&
        (!lh__length_to_px(box->style, &html->unit_len_ctx, axis->scrollport_size, value, unit, &axis->inset_start) ||
            !pixel_value(axis->inset_start, &axis->inset_start)))
        return false;
    type = horizontal ? css_computed_right(box->style, &value, &unit) : css_computed_bottom(box->style, &value, &unit);
    axis->end_auto = type != (horizontal ? CSS_RIGHT_SET : CSS_BOTTOM_SET);
    if (!axis->end_auto &&
        (!lh__length_to_px(box->style, &html->unit_len_ctx, axis->scrollport_size, value, unit, &axis->inset_end) ||
            !pixel_value(axis->inset_end, &axis->inset_end)))
        return false;
    return true;
}

bool box_sticky_prepare(struct html_content *html, struct box *box)
{
    if (!box->style || css_computed_position(box->style) != CSS_POSITION_STICKY || box_sticky_unsupported(box)) {
        if (box->sticky)
            talloc_free(box->sticky);
        box->sticky = NULL;
        return true;
    }
    struct box_sticky prepared = {.owner = html, .scrolling_ancestor = &html->viewport.box};
    for (struct box *ancestor = box->parent; ancestor; ancestor = ancestor->parent) {
        if (!prepared.containing_block && block_container(ancestor))
            prepared.containing_block = ancestor;
        if (scroll_container(html, ancestor)) {
            prepared.scrolling_ancestor = ancestor;
            break;
        }
    }
    if (!prepared.containing_block || !prepare_axis(html, box, &prepared, true) ||
        !prepare_axis(html, box, &prepared, false)) {
        if (box->sticky)
            box->sticky->valid = false;
        return true;
    }
    if (prepared.x.start_auto && prepared.x.end_auto && prepared.y.start_auto && prepared.y.end_auto) {
        if (box->sticky)
            talloc_free(box->sticky);
        box->sticky = NULL;
        return true;
    }
    if (!box->sticky) {
        box->sticky = talloc(box, struct box_sticky);
        if (!box->sticky)
            return false;
    }
    prepared.valid = true;
    *box->sticky = prepared;
    for (struct box *ancestor = box->parent; ancestor; ancestor = ancestor->parent)
        ancestor->flags |= HAS_STICKY_DESCENDANT;
    return true;
}

const struct box_sticky *box_sticky_get(const struct box *box)
{
    const struct box_sticky *sticky = box->sticky;
    return sticky && sticky->valid && sticky->owner->sticky_layout_valid ? sticky : NULL;
}

/* Stack-local ancestry, evaluated once from root to leaf. Constraint owners are
 * ancestors: nested sticky boxes reuse their already resolved shifts instead of
 * recursively resolving those ancestors again for each constraint. */
struct sticky_frame {
    const struct box *box;
    struct sticky_frame *parent, *child;
    int64_t scroll_x, scroll_y, shift_x, shift_y;
};

struct sticky_query {
    int scroll[2];
    bool have_scroll, local;
    int64_t x, y;
#ifdef WISP_PROFILE_RESTYLE
    struct html_content *owner;
    uint64_t ancestor_steps;
#endif
};

static struct sticky_frame *ancestor_frame(struct sticky_frame *frame, const struct box *box)
{
    for (; frame; frame = frame->parent)
        if (frame->box == box)
            return frame;
    return NULL;
}

static void resolve_frames(struct sticky_frame *frame, struct sticky_query *query)
{
    for (; frame; frame = frame->child) {
        const struct box *box = frame->box;
        const struct box *coordinate_parent = box->abs_containing_block     ? box->abs_containing_block
            : (box->type == BOX_FLOAT_LEFT || box->type == BOX_FLOAT_RIGHT) ? box->float_container
                                                                            : box->parent;
        struct sticky_frame *cp = ancestor_frame(frame->parent, coordinate_parent);
        bool fixed = coordinate_parent && (coordinate_parent->flags & IS_VIEWPORT);
        frame->scroll_x = fixed ? query->scroll[0] : cp ? cp->scroll_x - scrollbar_get_offset(cp->box->scroll_x) : 0;
        frame->scroll_y = fixed ? query->scroll[1] : cp ? cp->scroll_y - scrollbar_get_offset(cp->box->scroll_y) : 0;
        frame->shift_x = !fixed && frame->parent ? frame->parent->shift_x : 0;
        frame->shift_y = !fixed && frame->parent ? frame->parent->shift_y : 0;
        const struct box_sticky *sticky = box_sticky_get(box);
        if (sticky) {
            const struct sticky_frame *cb = ancestor_frame(frame->parent, sticky->containing_block);
            const struct sticky_frame *sp = ancestor_frame(frame->parent, sticky->scrolling_ancestor);
            assert(cb && (sp || (sticky->scrolling_ancestor->flags & IS_VIEWPORT)));
            frame->shift_x += axis_offset(&sticky->x, frame->scroll_x + frame->shift_x,
                cb->scroll_x + cb->shift_x - scrollbar_get_offset(cb->box->scroll_x),
                sp ? sp->scroll_x + sp->shift_x : query->scroll[0]);
            frame->shift_y += axis_offset(&sticky->y, frame->scroll_y + frame->shift_y,
                cb->scroll_y + cb->shift_y - scrollbar_get_offset(cb->box->scroll_y),
                sp ? sp->scroll_y + sp->shift_y : query->scroll[1]);
        }
        query->x = frame->shift_x - (query->local && cp ? cp->shift_x : 0);
        query->y = frame->shift_y - (query->local && cp ? cp->shift_y : 0);
    }
}

static void resolve_ancestry(const struct box *box, struct sticky_frame *child, struct sticky_query *query)
{
    HTML_PROFILE(query->ancestor_steps++);
    struct sticky_frame frame = {.box = box, .child = child};
    if (child)
        child->parent = &frame;
    const struct box_sticky *sticky = box_sticky_get(box);
    HTML_PROFILE(if (sticky) query->owner = sticky->owner);
    if (sticky && !query->have_scroll) {
        browser_window_get_scroll(sticky->owner->bw, &query->scroll[0], &query->scroll[1]);
        query->have_scroll = true;
    }
    if (box->parent)
        resolve_ancestry(box->parent, &frame, query);
    else
        resolve_frames(&frame, query);
}

static void resolve_offset(const struct box *box, const int scroll[2], bool local, int64_t *x, int64_t *y)
{
    *x = *y = 0;
    if (!box->sticky && !(box->flags & HAS_STICKY_ANCESTOR))
        return;
    if (local && !box->sticky && box->type != BOX_FLOAT_LEFT && box->type != BOX_FLOAT_RIGHT)
        return;
    struct sticky_query query = {.local = local, .have_scroll = scroll != NULL};
    if (scroll) {
        query.scroll[0] = scroll[0];
        query.scroll[1] = scroll[1];
    }
    resolve_ancestry(box, NULL, &query);
    HTML_PROFILE(if (query.owner) {
        query.owner->render_profile.sticky_queries++;
        query.owner->render_profile.sticky_ancestor_steps += query.ancestor_steps;
    });
    *x = query.x;
    *y = query.y;
}

void box_sticky_total_offset(const struct box *box, const int scroll[2], int64_t *x, int64_t *y)
{
    resolve_offset(box, scroll, false, x, y);
}

void box_sticky_local_offset(const struct box *box, const int scroll[2], int64_t *x, int64_t *y)
{
    resolve_offset(box, scroll, true, x, y);
}

bool box_sticky_clip(const struct box *box, const int scroll[2], struct rect *clip)
{
    const struct box_sticky *sticky = NULL;
    for (const struct box *p = box; p && !sticky; p = p->parent)
        sticky = box_sticky_get(p);
    if (!sticky)
        return true;
    return box_clip_to_ancestors(sticky->owner, box, scroll, clip);
}

bool box_clip_to_ancestors(const struct html_content *html, const struct box *box,
    const int scroll[2], struct rect *clip)
{
    const struct box *overflow_from = NULL;
    for (const struct box *p = box; p && p->parent; p = p->parent) {
        if (p->abs_containing_block && (p->abs_containing_block->flags & IS_VIEWPORT))
            break;
        /* An absolute descendant escapes overflow between itself and its
         * containing block (CSS 2.2 11.1.1), but not that block's own clip. */
        if (!overflow_from && p->abs_containing_block)
            overflow_from = p->abs_containing_block;
        struct box *ancestor = p->parent;
        if (ancestor == overflow_from)
            overflow_from = NULL;
        bool overflow = !overflow_from && scroll_container(html, ancestor);
        css_computed_clip_rect css_clip;
        bool explicit_clip = ancestor->style && css_computed_position(ancestor->style) == CSS_POSITION_ABSOLUTE &&
            css_computed_clip(ancestor->style, &css_clip) == CSS_CLIP_RECT;
        if (!overflow && !explicit_clip)
            continue;
        int x, y;
        if (scroll)
            box_coords_at_scroll(ancestor, &x, &y, scroll[0], scroll[1]);
        else
            box_coords(ancestor, &x, &y);
        struct rect r = {.x0 = x,
            .y0 = y,
            .x1 = box_sticky_coordinate((int64_t)x + scrollport_size(ancestor, true)),
            .y1 = box_sticky_coordinate((int64_t)y + scrollport_size(ancestor, false))};
        if (overflow) {
            clip->x0 = greater(clip->x0, r.x0);
            clip->y0 = greater(clip->y0, r.y0);
            clip->x1 = lesser(clip->x1, r.x1);
            clip->y1 = lesser(clip->y1, r.y1);
        }
        if (explicit_clip) {
            r = (struct rect){x - ancestor->border[LEFT].width, y - ancestor->border[TOP].width,
                x + ancestor->padding[LEFT] + ancestor->width + ancestor->padding[RIGHT] +
                    ancestor->border[RIGHT].width,
                y + ancestor->padding[TOP] + ancestor->height + ancestor->padding[BOTTOM] +
                    ancestor->border[BOTTOM].width};
            const css_unit_ctx *units = &html->unit_len_ctx;
            if (!css_clip.left_auto)
                r.x0 = x - ancestor->border[LEFT].width +
                    FIXTOINT(css_unit_len2device_px(ancestor->style, units, css_clip.left, css_clip.lunit));
            if (!css_clip.right_auto)
                r.x1 = x - ancestor->border[LEFT].width +
                    FIXTOINT(css_unit_len2device_px(ancestor->style, units, css_clip.right, css_clip.runit));
            if (!css_clip.top_auto)
                r.y0 = y - ancestor->border[TOP].width +
                    FIXTOINT(css_unit_len2device_px(ancestor->style, units, css_clip.top, css_clip.tunit));
            if (!css_clip.bottom_auto)
                r.y1 = y - ancestor->border[TOP].width +
                    FIXTOINT(css_unit_len2device_px(ancestor->style, units, css_clip.bottom, css_clip.bunit));
            clip->x0 = greater(clip->x0, r.x0);
            clip->y0 = greater(clip->y0, r.y0);
            clip->x1 = lesser(clip->x1, r.x1);
            clip->y1 = lesser(clip->y1, r.y1);
        }
        if (clip->x0 >= clip->x1 || clip->y0 >= clip->y1)
            return false;
    }
    return true;
}

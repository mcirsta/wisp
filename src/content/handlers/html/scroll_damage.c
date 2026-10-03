/* SPDX-License-Identifier: GPL-2.0-only */
#include <wisp/content/handlers/html/box.h>
#include <wisp/content/handlers/html/box_inspect.h>
#include <wisp/content/handlers/html/private.h>
#include "content/handlers/html/box_sticky.h"
#include "content/handlers/html/redraw_helpers.h"
#include "content/handlers/html/restyle_profile.h"
#include "content/handlers/html/scroll_damage.h"

static bool paint_bounds(struct box *box, const struct rect *viewport, struct rect *bounds, int origin[2])
{
    box_coords_at_scroll(box, &origin[0], &origin[1], viewport->x0, viewport->y0);
    int x = origin[0], y = origin[1];
    /* Match the visible-overflow coverage used by HTML redraw, including its
     * exclusive far edge. Descendant extents contain borders and text overflow;
     * nested sticky movement is added separately by the sparse walk below. */
    *bounds = (struct rect){box_sticky_coordinate((int64_t)x + box->descendant_x0),
        box_sticky_coordinate((int64_t)y + box->descendant_y0),
        box_sticky_coordinate((int64_t)x + box->descendant_x1 + 1),
        box_sticky_coordinate((int64_t)y + box->descendant_y1 + 1)};
    const int scroll[] = {viewport->x0, viewport->y0};
    return box_sticky_clip(box, scroll, bounds);
}

static bool emit_damage(struct rect bounds, const struct rect *visible_viewport, const struct rect *new_viewport,
    browser_window_scroll_damage_cb add, void *pw)
{
    /* First keep only pixels present in the source viewport. Their document
     * coordinates do not change when copied; intersect the destination next. */
    if (html_clip_intersect_box(&bounds, visible_viewport, false) == CLIP_RESULT_EMPTY ||
        html_clip_intersect_box(&bounds, new_viewport, false) == CLIP_RESULT_EMPTY)
        return true;
    bounds.x0 -= new_viewport->x0;
    bounds.x1 -= new_viewport->x0;
    bounds.y0 -= new_viewport->y0;
    bounds.y1 -= new_viewport->y0;
    return add(&bounds, pw);
}

static bool scroll_damage(struct html_content *html, struct box *box, const struct rect *old_viewport,
    const struct rect *new_viewport, browser_window_scroll_damage_cb add, void *pw)
{
    HTML_PROFILE(html->render_profile.scroll_damage_boxes++);
    if (box_sticky_get(box) || (box->flags & IFRAME)) {
        struct rect old_bounds, new_bounds;
        int old_origin[2], new_origin[2];
        bool old_visible = paint_bounds(box, old_viewport, &old_bounds, old_origin);
        bool new_visible = paint_bounds(box, new_viewport, &new_bounds, new_origin);
        /* Only unchanged document position AND coverage translate with the page.
         * Equal clipped rectangles can still contain differently positioned ink.
         * Still visit nested sticky descendants: they can move independently.
         * Repaint embedded viewports even when their outer box only translates.
         * Their fixed/sticky content has its own viewport; repainting the entire
         * frame covers nested frames without traversing any child box trees. */
        if ((box->flags & IFRAME) || old_origin[0] != new_origin[0] || old_origin[1] != new_origin[1] ||
            old_visible != new_visible || old_bounds.x0 != new_bounds.x0 || old_bounds.y0 != new_bounds.y0 ||
            old_bounds.x1 != new_bounds.x1 || old_bounds.y1 != new_bounds.y1) {
            if (old_visible && !emit_damage(old_bounds, old_viewport, new_viewport, add, pw))
                return false;
            if (new_visible && !emit_damage(new_bounds, new_viewport, new_viewport, add, pw))
                return false;
        }
    }
    if (!(box->flags & (HAS_STICKY_DESCENDANT | HAS_IFRAME_DESCENDANT)))
        return true;
    for (struct box *child = box->children; child; child = child->next) {
        if (child->type != BOX_FLOAT_LEFT && child->type != BOX_FLOAT_RIGHT &&
            !scroll_damage(html, child, old_viewport, new_viewport, add, pw))
            return false;
    }
    for (struct box *child = box->float_children; child; child = child->next_float) {
        if (!scroll_damage(html, child, old_viewport, new_viewport, add, pw))
            return false;
    }
    return !box_has_outside_marker(box) || scroll_damage(html, box->list_marker, old_viewport, new_viewport, add, pw);
}

bool html_compute_scroll_damage(struct html_content *html, const struct rect *old_viewport,
    const struct rect *new_viewport, browser_window_scroll_damage_cb add, void *pw)
{
    if (!html->sticky_layout_valid || !html->scroll_reuse_safe || !html->redraw_region_safe || html->restyling ||
        html->visible_select_menu || !html->layout)
        return false;
    /* Scrollports are nonnegative document rectangles. Validate before doing
     * any subtraction, so even requests near integer limits stay well-defined. */
    if (old_viewport->x0 < 0 || old_viewport->y0 < 0 || new_viewport->x0 < 0 || new_viewport->y0 < 0 ||
        old_viewport->x1 <= old_viewport->x0 || old_viewport->y1 <= old_viewport->y0 ||
        new_viewport->x1 <= new_viewport->x0 || new_viewport->y1 <= new_viewport->y0 ||
        old_viewport->x1 - old_viewport->x0 != new_viewport->x1 - new_viewport->x0 ||
        old_viewport->y1 - old_viewport->y0 != new_viewport->y1 - new_viewport->y0)
        return false;
    HTML_PROFILE_START(start);
    HTML_PROFILE(html->render_profile.scroll_damage_queries++);
    bool result = scroll_damage(html, html->layout, old_viewport, new_viewport, add, pw);
    HTML_PROFILE(html->render_profile.scroll_damage_ns += html_profile_now() - start);
    return result;
}

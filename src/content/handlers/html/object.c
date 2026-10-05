/*
 * Copyright 2013 Vincent Sanders <vince@netsurf-browser.org>
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
 * Processing for html content object operations.
 */

#include <nsutils/time.h>
#include <assert.h>
#include <ctype.h>
#include <stdint.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <wisp/browser.h>
#include <wisp/content.h>
#include <wisp/content/handlers/css/utils.h>
#include <wisp/content/hlcache.h>
#include <wisp/desktop/gui_internal.h>
#include <wisp/misc.h>
#include <wisp/utils/config.h>
#include <wisp/utils/corestrings.h>
#include <wisp/utils/log.h>
#include <wisp/utils/nsoption.h>
#include "desktop/scrollbar.h"

#include <wisp/content/handlers/html/box.h>
#include <wisp/content/handlers/html/box_inspect.h>
#include <wisp/content/handlers/html/html.h>
#include <wisp/content/handlers/html/interaction.h>
#include <wisp/content/handlers/html/private.h>
#include "content/handlers/html/object.h"
#include "content/handlers/html/box_construct.h"
#include "utils/talloc.h"

/* Performance tracing - enable via CMake: -DNEOSURF_ENABLE_PERF_TRACE=ON */
#include <wisp/utils/perf.h>

/* break reference loop */
static void html_object_refresh(void *p);

/**
 * Retrieve objects used by HTML document
 *
 * \param h  Content to retrieve objects from
 * \param n  Pointer to location to receive number of objects
 * \return Pointer to list of objects
 */
struct content_html_object *html_get_objects(hlcache_handle *h, unsigned int *n)
{
    html_content *c = (html_content *)hlcache_handle_get_content(h);

    assert(c != NULL);
    assert(n != NULL);

    *n = c->num_objects;

    return c->object_list;
}

static bool html_object_available(const struct content_html_object *object)
{
    if (object->content == NULL)
        return false;
    content_status status = content_get_status(object->content);
    return status == CONTENT_STATUS_READY || status == CONTENT_STATUS_DONE;
}

enum html_object_state html_object_get_state(const html_content *html, dom_node *node, const nsurl *url)
{
    if (node == NULL || url == NULL)
        return HTML_OBJECT_UNKNOWN;
    for (const struct content_html_object *o = html->object_list; o != NULL; o = o->next) {
        if (o->layout_resource && !o->background && o->permitted_types == CONTENT_ANY && o->node == node &&
            o->url != NULL && nsurl_compare(url, o->url, NSURL_COMPLETE)) {
            if (o->failed)
                return HTML_OBJECT_FAILED;
            return html_object_available(o) ? HTML_OBJECT_AVAILABLE : HTML_OBJECT_PENDING;
        }
    }
    return HTML_OBJECT_UNKNOWN;
}

static bool html_object_has_fallback(const struct content_html_object *object)
{
    if (!object->layout_resource || object->background || object->permitted_types != CONTENT_ANY ||
        object->node == NULL)
        return false;
    dom_string *name = NULL;
    bool fallback = dom_node_get_node_name(object->node, &name) == DOM_NO_ERR &&
        dom_string_caseless_lwc_isequal(name, corestring_lwc_object);
    dom_string_unref(name);
    return fallback;
}

static uint32_t html_object_fetch_flags(const struct content_html_object *object)
{
    /* HTML's object algorithm falls back on HTTP errors. The body remains
     * available to navigations and other consumers sharing the cache entry. */
    return HLCACHE_RETRIEVE_SNIFF_TYPE | (html_object_has_fallback(object) ? HLCACHE_RETRIEVE_REQUIRE_OK : 0);
}

static bool html_object_can_open(const struct content_html_object *object)
{
    return object->content != NULL && object->box != NULL && !object->opened &&
        content_get_type(object->content) != CONTENT_NONE &&
        (!html_object_has_fallback(object) ||
            ((object->box->flags & IS_REPLACED) && html_object_available(object)));
}

/* HTML's object algorithm represents the children when the requested resource
 * cannot be loaded/rendered. Keep that decision with the request, not its box:
 * the box can be absent while hidden or replaced during sliced reconstruction. */
static void html_object_failed(struct content_html_object *object)
{
    html_content *html = (html_content *)object->parent;
    struct box *box = object->box;
    if (box != NULL) {
        if (!object->background)
            box->object = NULL;
        else if (object->background_layer == 0)
            box->background = NULL;
        else
            box->background_layers[object->background_layer - 1] = NULL;
    }
    /* A refresh callback may still be queued when an embedded document fails. */
    if (object->permitted_types == CONTENT_ANY)
        guit->misc->schedule(-1, html_object_refresh, object);
    object->refresh_pending = false;
    if (object->content != NULL)
        hlcache_handle_release(object->content);
    object->content = NULL;
    object->opened = false;
    object->failed = true;

    /* Images, CSS backgrounds and embed have no object fallback children.
     * A settled fallback tree also needs no reconstruction: pending -> failed
     * keeps the same representation. Rebuilding it would discard active find
     * highlights even though their text has not changed. During construction
     * retain the retry, since not every box has been published yet. */
    if (!html->aborted && html_object_has_fallback(object) &&
        (box == NULL || (box->flags & IS_REPLACED) || html->restyling))
        html_request_restyle(html);
}

/**
 * Update a box whose content has completed rendering.
 */

static void html_object_done(struct box *box, hlcache_handle *object, bool background, uint32_t layer)
{
    struct box *b;

    if (background) {
        if (layer == 0)
            box->background = object;
        else
            box->background_layers[layer - 1] = object;
        return;
    }

    box->object = object;

    /* Normalise the box type, now it has been replaced. */
    switch (box->type) {
    case BOX_TABLE:
        box->type = BOX_BLOCK;
        break;
    default:
        /* TODO: Any other box types need mapping? */
        break;
    }

    if (!(box->flags & REPLACE_DIM)) {
        /* invalidate parent min, max widths */
        for (b = box; b; b = b->parent)
            b->max_width = UNKNOWN_MAX_WIDTH;

        /* delete any clones of this box */
        while (box->next && (box->next->flags & CLONE)) {
            /* box_free_box(box->next); */
            box->next = box->next->next;
        }
    }
}


/**
 * Callback for hlcache_handle_retrieve() for objects with no box.
 */
static nserror html_object_nobox_callback(hlcache_handle *object, const hlcache_event *event, void *pw)
{
    struct content_html_object *chobject = pw;

    switch (event->type) {
    case CONTENT_MSG_ERROR:
        hlcache_handle_release(object);

        chobject->content = NULL;
        break;

    default:
        break;
    }

    return NSERROR_OK;
}

void html_deferred_reformat(void *p)
{
    html_content *c = p;
    PERF("html_deferred_reformat called (active=%d)", c->base.active);
    c->pending_reformat = false;
    if (c->restyling || c->base.status == CONTENT_STATUS_ERROR)
        return;

    /* We cannot use content__reformat() here because it has an optimization
     * that returns early if the width/height haven't changed. For
     * incremental reflows, the width/height of the viewport hasn't changed,
     * but the content inside has.
     */
    if (c->had_initial_layout && c->base.handler->reformat) {
        c->base.locked = true;
        c->base.handler->reformat(&c->base, c->base.available_width, c->base.available_height);
        c->base.locked = false;
        if (c->base.status == CONTENT_STATUS_ERROR)
            return;

        union content_msg_data data = {.background = false};
        content_broadcast(&c->base, CONTENT_MSG_REFORMAT, &data);
    }

    /* After reformat completes, try to transition to DONE state.
     * This replaces the content_set_done() from the original synchronous code.
     * html_proceed_to_done() will only call content_set_done() if:
     * - status is READY (not LOADING or already DONE)
     * - active == scripts_active (all non-script objects have finished)
     */
    html_proceed_to_done(c);
}


/**
 * Callback for hlcache_handle_retrieve() for objects with a box.
 */
static nserror html_object_callback(hlcache_handle *object, const hlcache_event *event, void *pw)
{
    struct content_html_object *o = pw;
    html_content *c = (html_content *)o->parent;
    int x, y;
    struct box *box;

    box = o->box;

    /* These document-level queries remain valid while its box is absent. */
    switch (event->type) {
    case CONTENT_MSG_GETTHREAD:
        *(event->data.jsthread) = NULL;
        return NSERROR_OK;
    case CONTENT_MSG_GETDIMS:
        *(event->data.getdims.viewport_width) = content__get_width(&c->base);
        *(event->data.getdims.viewport_height) = content__get_height(&c->base);
        return NSERROR_OK;
    case CONTENT_MSG_REFRESH:
        if (content_get_type(object) == CONTENT_HTML)
            guit->misc->schedule(event->data.delay * 1000, html_object_refresh, o);
        return NSERROR_OK;
    default:
        break;
    }

    /* HTML's object algorithm uses fallback until the resource is available.
     * Reconstruct through the normal path: attaching content to a fallback box
     * would leave its children and generated content in the live tree. The
     * request survives hiding and sliced construction, so neither loses this
     * transition. READY is sufficient for an embedded document to be laid out. */
    if (html_object_has_fallback(o) && (box == NULL || !(box->flags & IS_REPLACED)) &&
        (event->type == CONTENT_MSG_READY || event->type == CONTENT_MSG_DONE)) {
        if (event->type == CONTENT_MSG_DONE)
            c->base.active--;
        if (!c->aborted)
            html_request_restyle(c);
        return NSERROR_OK;
    }

    /* A hidden object keeps its cache handle, but owns no layout geometry.
     * Construction may also yield while callbacks arrive for the new tree. */
    if (box == NULL || c->restyling) {
        if (event->type == CONTENT_MSG_LOADING && c->bw != NULL && html_object_can_open(o)) {
            content_open(object, c->bw, &c->base, box->object_params);
            o->opened = true;
        } else if (event->type == CONTENT_MSG_DONE) {
            c->base.active--;
            if (box != NULL)
                html_object_done(box, object, o->background, o->background_layer);
        } else if (event->type == CONTENT_MSG_ERROR) {
            c->base.active--;
            html_object_failed(o);
        }
        if (!c->restyling && c->base.active == c->scripts_active &&
            (event->type == CONTENT_MSG_DONE || event->type == CONTENT_MSG_ERROR)) {
            c->pending_reformat = true;
            guit->misc->schedule(0, html_deferred_reformat, c);
        }
        return NSERROR_OK;
    }

    switch (event->type) {
    case CONTENT_MSG_LOADING:
        if (c->base.status != CONTENT_STATUS_LOADING && c->bw != NULL && html_object_can_open(o)) {
            content_open(object, c->bw, &c->base, box->object_params);
            o->opened = true;
        }
        break;

    case CONTENT_MSG_READY:
        NSLOG(wisp, WARNING,
            "SVGDIAG obj_cb READY: content=%p type=%d box=%p "
            "box.w=%d box.h=%d box.max_w=%d box.flags=0x%x "
            "can_reformat=%d will_reformat_w=%d will_reformat_h=%d",
            object, content_get_type(object), box, box->width, box->height, box->max_width, box->flags,
            content_can_reformat(object), box->max_width != UNKNOWN_MAX_WIDTH ? box->width : 0,
            box->max_width != UNKNOWN_MAX_WIDTH ? box->height : 0);
        if (content_can_reformat(object)) {
            /* TODO: avoid knowledge of box internals here */
            content_reformat(object, false, box->max_width != UNKNOWN_MAX_WIDTH ? box->width : 0,
                box->max_width != UNKNOWN_MAX_WIDTH ? box->height : 0);

            /* Adjust parent content for new object size */
            html_object_done(box, object, o->background, o->background_layer);

            /* Incremental reflow on READY - disabled by default for performance.
             * Enable via CMake: -DNEOSURF_ENABLE_INCREMENTAL_REFLOW=ON
             */
#ifdef WISP_ENABLE_INCREMENTAL_REFLOW
            if (c->base.status == CONTENT_STATUS_READY || c->base.status == CONTENT_STATUS_DONE) {
                uint64_t ms_now;
                nsu_getmonotonic_ms(&ms_now);

                if (ms_now > c->base.reformat_time) {
                    content__reformat(&c->base, false, c->base.available_width, c->base.available_height);
                } else if (c->pending_reformat == false) {
                    uint64_t delay = c->base.reformat_time - ms_now;
                    c->pending_reformat = true;
                    guit->misc->schedule(delay, html_deferred_reformat, c);
                }
            }
#endif
        }
        break;

    case CONTENT_MSG_DONE:
        PERF("Object DONE (remaining=%d)", c->base.active - 1);
        NSLOG(wisp, WARNING,
            "SVGDIAG obj_cb DONE: content=%p type=%d box=%p "
            "box.w=%d box.h=%d box.object=%p box.flags=0x%x "
            "content.w=%d content.h=%d",
            object, content_get_type(object), box, box->width, box->height, box->object, box->flags,
            content_get_width(object), content_get_height(object));
        if (c->base.active == 0) {
            NSLOG(wisp, CRITICAL,
                "ACTIVE UNDERFLOW! object_cb DONE decrement when 0 "
                "[content=%p object=%p]",
                c, object);
        }
        c->base.active--;
        NSLOG(wisp, INFO, "%d fetches active (scripts_active=%d)", c->base.active, c->scripts_active);

        html_object_done(box, object, o->background, o->background_layer);

        /*
         * Broadcast a redraw for the box area.
         *
         * For REPLACE_DIM boxes (fixed dimensions from HTML/CSS),
         * we can redraw immediately if the parent is ready.
         *
         * For dynamic-size boxes (no REPLACE_DIM), the box dimensions
         * may not be correct yet if we haven't had initial layout.
         * In that case, the reformat triggered below will handle it.
         * If we HAVE had initial layout, we need to trigger a reformat
         * so layout picks up the new intrinsic dimensions, and then
         * the CONTENT_MSG_REFORMAT broadcast will cause a full redraw.
         */
        if (c->base.status != CONTENT_STATUS_LOADING && c->had_initial_layout) {
            union content_msg_data data;

            if (!box_visible(box))
                break;


            /* Dynamic-size images need dimension updates via reformat, not just redraw */
            if (!(box->flags & REPLACE_DIM)) {
#ifdef WISP_ENABLE_INCREMENTAL_REFLOW
                /*
                 * Incremental reflow: trigger immediate reformat
                 * to pick up the new intrinsic dimensions.
                 */
                if (c->pending_reformat == false) {
                    c->pending_reformat = true;
                    guit->misc->schedule(0, html_deferred_reformat, c);
                }
#endif
                /* Skip fixed-size redraw code - wait for next reformat */
                break;
            }

            /* Fixed-size image - just redraw the box area.
             * Skip if box hasn't been laid out yet (width is still
             * the UNKNOWN_WIDTH sentinel).  This can happen when
             * content resolves synchronously (data: URIs, cache hits)
             * before layout assigns real dimensions.  Layout will
             * trigger the proper redraw later. */
            if (box->width == UNKNOWN_WIDTH)
                break;

            box_coords(box, &x, &y);

            data.redraw.x = x + box->padding[LEFT];
            data.redraw.y = y + box->padding[TOP];
            data.redraw.width = box->width;
            data.redraw.height = box->height;

            content_broadcast(&c->base, CONTENT_MSG_REDRAW, &data);
        }
        break;

    case CONTENT_MSG_ERROR:
        if (c->base.active == 0) {
            NSLOG(wisp, CRITICAL,
                "ACTIVE UNDERFLOW! object_cb ERROR decrement when 0 "
                "[content=%p object=%p]",
                c, object);
        }
        c->base.active--;
        NSLOG(wisp, INFO, "%d fetches active", c->base.active);

        html_object_failed(o);

        break;

    case CONTENT_MSG_REDRAW:
        if (c->base.status != CONTENT_STATUS_LOADING) {
            union content_msg_data data = event->data;

            if (c->had_initial_layout == false) {
                break;
            }

            if (!box_visible(box))
                break;

            box_coords(box, &x, &y);

            if (o->background) {
                /* Redraw request is for background */
                css_fixed_or_calc hpos = (css_fixed_or_calc)0, vpos = (css_fixed_or_calc)0;
                css_unit hunit = CSS_UNIT_PX;
                css_unit vunit = CSS_UNIT_PX;
                int width = box->padding[LEFT] + box->width + box->padding[RIGHT];
                int height = box->padding[TOP] + box->height + box->padding[BOTTOM];
                int t, h, l, w;

                /* Need to know background-position */
                css_computed_background_position_layer(box->style, o->background_layer, &hpos, &hunit, &vpos, &vunit);

                w = content_get_width(object);
                h = content_get_height(object);
#ifdef WISP_DEVICE_PIXEL_LAYOUT
                {
                    int dpi = browser_get_dpi();
                    if (dpi > 0 && dpi != 96) {
                        w = (w * dpi) / 96;
                        h = (h * dpi) / 96;
                    }
                }
#endif
                if (hunit == CSS_UNIT_PCT) {
                    l = (width - w) * hpos.value / INTTOFIX(100);
                } else if (hunit == CSS_UNIT_CALC) {
                    int px = 0;
                    if (css_computed_length_to_px(box->style, &c->unit_len_ctx, width - w, hpos, hunit, &px) ==
                        CSS_OK) {
                        l = px;
                    } else {
                        l = 0;
                    }
                } else {
                    l = FIXTOINT(css_unit_len2device_px(box->style, &c->unit_len_ctx, hpos.value, hunit));
                }

                if (vunit == CSS_UNIT_PCT) {
                    t = (height - h) * vpos.value / INTTOFIX(100);
                } else if (vunit == CSS_UNIT_CALC) {
                    int px = 0;
                    if (css_computed_length_to_px(box->style, &c->unit_len_ctx, height - h, vpos, vunit, &px) ==
                        CSS_OK) {
                        t = px;
                    } else {
                        t = 0;
                    }
                } else {
                    t = FIXTOINT(css_unit_len2device_px(box->style, &c->unit_len_ctx, vpos.value, vunit));
                }

                /* Redraw area depends on background-repeat */
                switch (css_computed_background_repeat_layer(box->style, o->background_layer)) {
                case CSS_BACKGROUND_REPEAT_REPEAT:
                    data.redraw.x = 0;
                    data.redraw.y = 0;
                    data.redraw.width = box->width;
                    data.redraw.height = box->height;
                    break;

                case CSS_BACKGROUND_REPEAT_REPEAT_X:
                    data.redraw.x = 0;
                    data.redraw.y += t;
                    data.redraw.width = box->width;
                    break;

                case CSS_BACKGROUND_REPEAT_REPEAT_Y:
                    data.redraw.x += l;
                    data.redraw.y = 0;
                    data.redraw.height = box->height;
                    break;

                case CSS_BACKGROUND_REPEAT_NO_REPEAT:
                    data.redraw.x += l;
                    data.redraw.y += t;
                    break;

                default:
                    break;
                }

                /* Add offset to box */
                data.redraw.x += x;
                data.redraw.y += y;

            } else {
                /* Non-background case */
                int w = content_get_width(object);
                int h = content_get_height(object);
#ifdef WISP_DEVICE_PIXEL_LAYOUT
                {
                    int dpi = browser_get_dpi();
                    if (dpi > 0 && dpi != 96) {
                        w = (w * dpi) / 96;
                        h = (h * dpi) / 96;
                    }
                }
#endif

                if (w != 0 && box->width != w) {
                    /* Not showing image at intrinsic
                     * width; need to scale the redraw
                     * request area. */
                    data.redraw.x = data.redraw.x * box->width / w;
                    data.redraw.width = data.redraw.width * box->width / w;
                }

                if (h != 0 && box->height != w) {
                    /* Not showing image at intrinsic
                     * height; need to scale the redraw
                     * request area. */
                    data.redraw.y = data.redraw.y * box->height / h;
                    data.redraw.height = data.redraw.height * box->height / h;
                }

                data.redraw.x += x + box->padding[LEFT];
                data.redraw.y += y + box->padding[TOP];
            }

            content_broadcast(&c->base, CONTENT_MSG_REDRAW, &data);
        }
        break;

    case CONTENT_MSG_LINK:
        /* Don't care about favicons that aren't on top level content */
        break;

    case CONTENT_MSG_SCROLL:
        if (box->scroll_x != NULL)
            scrollbar_set(box->scroll_x, event->data.scroll.x0, false);
        if (box->scroll_y != NULL)
            scrollbar_set(box->scroll_y, event->data.scroll.y0, false);
        break;

    case CONTENT_MSG_DRAGSAVE: {
        union content_msg_data msg_data;
        if (event->data.dragsave.content == NULL)
            msg_data.dragsave.content = object;
        else
            msg_data.dragsave.content = event->data.dragsave.content;

        content_broadcast(&c->base, CONTENT_MSG_DRAGSAVE, &msg_data);
    } break;

    case CONTENT_MSG_SAVELINK:
    case CONTENT_MSG_POINTER:
    case CONTENT_MSG_SELECTMENU:
    case CONTENT_MSG_GADGETCLICK:
        /* These messages are for browser window layer.
         * we're not interested, so pass them on. */
        content_broadcast(&c->base, event->type, &event->data);
        break;

    case CONTENT_MSG_CARET: {
        union html_focus_owner focus_owner;
        focus_owner.content = box;

        switch (event->data.caret.type) {
        case CONTENT_CARET_REMOVE:
        case CONTENT_CARET_HIDE:
            html_set_focus(c, HTML_FOCUS_CONTENT, focus_owner, true, 0, 0, 0, NULL);
            break;
        case CONTENT_CARET_SET_POS:
            html_set_focus(c, HTML_FOCUS_CONTENT, focus_owner, false, event->data.caret.pos.x, event->data.caret.pos.y,
                event->data.caret.pos.height, event->data.caret.pos.clip);
            break;
        }
    } break;

    case CONTENT_MSG_DRAG: {
        html_drag_type drag_type = HTML_DRAG_NONE;
        union html_drag_owner drag_owner;
        drag_owner.content = box;

        switch (event->data.drag.type) {
        case CONTENT_DRAG_NONE:
            drag_type = HTML_DRAG_NONE;
            drag_owner.no_owner = true;
            break;
        case CONTENT_DRAG_SCROLL:
            drag_type = HTML_DRAG_CONTENT_SCROLL;
            break;
        case CONTENT_DRAG_SELECTION:
            drag_type = HTML_DRAG_CONTENT_SELECTION;
            break;
        }
        html_set_drag_type(c, drag_type, drag_owner, event->data.drag.rect);
    } break;

    case CONTENT_MSG_SELECTION: {
        html_selection_type sel_type;
        union html_selection_owner sel_owner;

        if (event->data.selection.selection) {
            sel_type = HTML_SELECTION_CONTENT;
            sel_owner.content = box;
        } else {
            sel_type = HTML_SELECTION_NONE;
            sel_owner.none = true;
        }
        html_set_selection(c, sel_type, sel_owner, event->data.selection.read_only);
    } break;

    default:
        break;
    }

    if ((c->base.status == CONTENT_STATUS_READY || c->base.status == CONTENT_STATUS_DONE) &&
        c->base.active == c->scripts_active &&
        (event->type == CONTENT_MSG_LOADING || event->type == CONTENT_MSG_DONE || event->type == CONTENT_MSG_ERROR)) {
        /* all objects have arrived (only scripts may still be active) */
        PERF("ALL OBJECTS COMPLETE - scheduling final reformat (active=%d, scripts_active=%d)", c->base.active,
            c->scripts_active);

        /* Cancel any previous pending reformat */
        if (c->pending_reformat) {
            guit->misc->schedule(-1, html_deferred_reformat, c);
        }

        /* Schedule final reformat. This runs after we return from the current
         * callback, ensuring all object processing for this event is complete.
         * The event sequence guarantees all images are decoded before we get here
         * (CONTENT_MSG_DONE is only sent after image decode completes).
         */
        c->pending_reformat = true;
        guit->misc->schedule(0, html_deferred_reformat, c);
    }

    /* Incremental reflow during downloads - disabled by default for performance.
     * When enabled, reformats are triggered as individual images load.
     * When disabled, only one final reformat occurs after all downloads complete.
     * Enable via CMake: -DNEOSURF_ENABLE_INCREMENTAL_REFLOW=ON
     */
#ifdef WISP_ENABLE_INCREMENTAL_REFLOW
    else if (event->type == CONTENT_MSG_DONE && box != NULL && !(box->flags & REPLACE_DIM)) {

        /* 1) an object is newly fetched & converted,
         * 2) the box's dimensions need to change due to being replaced
         * 3) the object's parent HTML is ready for reformat
         */
        uint64_t ms_now;
        nsu_getmonotonic_ms(&ms_now);

        /* Always defer the reformat to allow coalescing of multiple
         * image loads. If pending_reformat is true, we do nothing
         * (the scheduled callback will handle it).
         */
        if (c->pending_reformat == false) {
            uint64_t delay = 0;
            if (ms_now < c->base.reformat_time) {
                delay = c->base.reformat_time - ms_now;
            }
            c->pending_reformat = true;
            guit->misc->schedule(0, html_deferred_reformat, c);
        }
    }
#endif

    return NSERROR_OK;
}


/**
 * Start a fetch for an object required by a page, replacing an existing
 * object.
 *
 * \param  object          Object to replace
 * \param  url             URL of object to fetch (copied)
 * \return  true on success, false on memory exhaustion
 */
static bool html_replace_object(struct content_html_object *object, nsurl *url)
{
    html_content *c;
    hlcache_child_context child;
    html_content *page;
    nserror error;

    assert(object != NULL);
    assert(object->box != NULL);

    c = (html_content *)object->parent;

    child.charset = c->encoding;
    child.quirks = c->base.quirks;

    if (object->content != NULL) {
        /* remove existing object */
        if (content_get_status(object->content) != CONTENT_STATUS_DONE) {
            c->base.active--;
            NSLOG(wisp, INFO, "%d fetches active", c->base.active);
        }

        hlcache_handle_release(object->content);
        object->content = NULL;
        object->opened = false;

        object->box->object = NULL;
    }

    /* Pre-increment active for all pages in the chain BEFORE retrieve.
     * Callbacks can fire synchronously for cached content,
     * so we must have the counter incremented before the callback
     * tries to decrement it.
     */
    for (page = c; page != NULL; page = page->page) {
        page->base.active++;
        NSLOG(wisp, INFO, "%d fetches active (pre-retrieve)", page->base.active);
        page->base.status = CONTENT_STATUS_READY;
    }

    /* initialise fetch */
    error = hlcache_handle_retrieve(url, html_object_fetch_flags(object), content_get_url(&c->base), NULL,
        html_object_callback, object, &child, object->permitted_types, &object->content);

    if (error != NSERROR_OK) {
        /* Decrement the counters we just incremented */
        for (page = c; page != NULL; page = page->page) {
            page->base.active--;
            NSLOG(wisp, INFO, "%d fetches active (retrieve failed)", page->base.active);
        }
        if (error != NSERROR_NOMEM)
            html_object_failed(object);
        return false;
    }

    return true;
}

/**
 * schedule callback for object refresh
 */
static void html_object_refresh(void *p)
{
    struct content_html_object *object = p;
    nsurl *refresh_url;

    if (object->box == NULL) {
        object->refresh_pending = true;
        return;
    }
    object->refresh_pending = false;

    assert(content_get_type(object->content) == CONTENT_HTML);

    refresh_url = content_get_refresh_url(object->content);

    /* Ignore if refresh URL has gone
     * (may happen if fetch errored) */
    if (refresh_url == NULL)
        return;

    content_invalidate_reuse_data(object->content);

    if (!html_replace_object(object, refresh_url)) {
        /** \todo handle memory exhaustion */
    }
}


/* exported interface documented in html/object.h */
nserror html_object_open_objects(html_content *html, struct browser_window *bw)
{
    struct content_html_object *object, *next;

    for (object = html->object_list; object != NULL; object = next) {
        next = object->next;

        if (!html_object_can_open(object))
            continue;

        content_open(object->content, bw, &html->base, object->box->object_params);
        object->opened = true;
    }
    return NSERROR_OK;
}


/* exported interface documented in html/object.h */
nserror html_object_abort_objects(html_content *htmlc)
{
    struct content_html_object *object;

    for (object = htmlc->object_list; object != NULL; object = object->next) {
        if (object->content == NULL)
            continue;

        switch (content_get_status(object->content)) {
        case CONTENT_STATUS_DONE:
            /* already loaded: do nothing */
            break;

        case CONTENT_STATUS_READY:
            hlcache_handle_abort(object->content);
            /* Active count will be updated when
             * html_object_callback receives
             * CONTENT_MSG_DONE from this object
             */
            break;

        default:
            hlcache_handle_abort(object->content);
            hlcache_handle_release(object->content);
            object->content = NULL;
            if (object->layout_resource) {
                htmlc->base.active--;
                NSLOG(wisp, INFO, "%d fetches active", htmlc->base.active);
            }
            break;
        }
    }

    return NSERROR_OK;
}


/* exported interface documented in html/object.h */
nserror html_object_close_objects(html_content *html)
{
    struct content_html_object *object, *next;

    for (object = html->object_list; object != NULL; object = next) {
        next = object->next;

        if (object->content == NULL || !object->opened)
            continue;

        if (content_get_type(object->content) == CONTENT_NONE)
            continue;

        if (content_get_type(object->content) == CONTENT_HTML) {
            guit->misc->schedule(-1, html_object_refresh, object);
        }

        content_close(object->content);
        object->opened = false;
    }
    return NSERROR_OK;
}


/* exported interface documented in html/object.h */
nserror html_object_free_objects(html_content *html)
{
    while (html->object_list != NULL) {
        struct content_html_object *victim = html->object_list;

        if (victim->content != NULL) {
            NSLOG(wisp, INFO, "object %p", victim->content);

            if (content_get_type(victim->content) == CONTENT_HTML) {
                guit->misc->schedule(-1, html_object_refresh, victim);
            }
            hlcache_handle_release(victim->content);
        }

        dom_node_unref(victim->node);
        if (victim->url != NULL)
            nsurl_unref(victim->url);
        html->object_list = victim->next;
        free(victim);
    }
    return NSERROR_OK;
}


static bool html_object_matches(const struct content_html_object *object, const struct box *box, nsurl *url,
    content_type permitted_types, bool background, uint32_t layer)
{
    return object->layout_resource && object->node == box->node && object->background == background &&
        object->background_layer == layer && object->permitted_types == permitted_types &&
        ((url == NULL && object->url == NULL) ||
            (url != NULL && object->url != NULL && nsurl_compare(url, object->url, NSURL_COMPLETE)));
}

static bool html_object_buffer_matches(const struct content_html_object *object, const uint8_t *buffer,
    size_t buffer_length, const char *mime_type)
{
    /* DOM identity alone is insufficient: a changed currentColor or SVG child
     * must not reuse an older serialization. Compare the actual cached bytes. */
    if (object->content == NULL)
        return false;
    size_t length = 0;
    const uint8_t *source = content_get_source_data(object->content, &length);
    lwc_string *mime = content_get_mime_type(object->content);
    bool equal = source != NULL && length == buffer_length && mime != NULL &&
        lwc_string_length(mime) == strlen(mime_type) &&
        memcmp(lwc_string_data(mime), mime_type, lwc_string_length(mime)) == 0 && memcmp(source, buffer, length) == 0;
    if (mime != NULL)
        lwc_string_unref(mime);
    return equal;
}

/* Rebind the same DOM resource rather than refetching it on every tab click.
 * Unbound entries remain available while a panel is display:none. */
static bool html_reuse_object(html_content *c, struct box *box, nsurl *url, content_type permitted_types,
    bool background, uint32_t layer, const uint8_t *buffer, size_t length, const char *mime_type)
{
    if (box == NULL || (box->node == NULL && url == NULL))
        return false;
    for (struct content_html_object *o = c->object_list; o != NULL; o = o->next) {
        if (o->box != NULL || !html_object_matches(o, box, url, permitted_types, background, layer) ||
            (buffer != NULL && o->content != NULL && !html_object_buffer_matches(o, buffer, length, mime_type)))
            continue;
        /* Keep the existing failed-resource policy when no cached source
         * survives. Do not accumulate a new failed SVG record on every click. */
        o->box = box;
        /* A fetch can become usable while its panel is hidden. Attach it when
         * shown; already-open embedded documents keep their existing state. */
        if (c->bw != NULL && html_object_can_open(o)) {
            content_open(o->content, c->bw, &c->base, box->object_params);
            o->opened = true;
        }
        if (o->refresh_pending)
            guit->misc->schedule(0, html_object_refresh, o);
        if (o->content != NULL && (content_get_status(o->content) == CONTENT_STATUS_DONE ||
                (html_object_has_fallback(o) && html_object_available(o))))
            html_object_done(box, o->content, background, layer);
        return true;
    }
    return false;
}

struct html_object_binding {
    struct content_html_object *object;
    struct box *old_box;
    struct hlcache_handle *content;
    const struct box_construct_object *request; /* NULL: becomes hidden. */
};

struct html_object_binding_plan {
    html_content *html;
    struct content_html_object *head;
    struct browser_window *bw;
    unsigned num_objects;
    size_t count;
    struct html_object_binding *bindings;
    bool applied;
};

static bool html_object_in_region(const struct box *box, const struct box *root)
{
    for (; box != NULL; box = box->parent)
        if (box == root)
            return true;
    return false;
}

/* The first publication path requires no fetch/open/refresh calls at commit.
 * Failed URL requests retain their existing failed state, as full reuse does. */
static bool html_object_can_rebind(const html_content *html, const struct content_html_object *object)
{
    if (!object->layout_resource || object->permitted_types != CONTENT_IMAGE || object->refresh_pending)
        return false;
    return object->content == NULL ||
        (content_get_status(object->content) == CONTENT_STATUS_DONE &&
            content_get_type(object->content) == CONTENT_IMAGE && (html->bw == NULL || object->opened));
}

static bool html_object_matches_request(const struct content_html_object *object,
    const struct box_construct_object *request)
{
    if (!html_object_matches(object, request->box, request->url, request->permitted_types, request->background,
            request->background_layer))
        return false;
    if (request->buffer == NULL)
        return request->url != NULL;
    return html_object_buffer_matches(object, request->buffer, request->buffer_length, request->mime_type);
}

nserror html_object_prepare_bindings(html_content *html, struct box *old_root,
    const struct box_construct_region *candidate, void *owner, struct html_object_binding_plan **result)
{
    *result = NULL;
    size_t capacity = 0;
    for (struct content_html_object *o = html->object_list; o != NULL; o = o->next)
        if (html_object_in_region(o->box, old_root))
            capacity++;
    for (const struct box_construct_object *r = candidate->objects; r != NULL; r = r->next)
        capacity++;
    if (capacity > UINT_MAX)
        return NSERROR_NOMEM;
    struct html_object_binding_plan *plan = talloc_zero(owner, struct html_object_binding_plan);
    if (plan == NULL)
        return NSERROR_NOMEM;
    if (capacity > 0) {
        plan->bindings = talloc_array(plan, struct html_object_binding, capacity);
        if (plan->bindings == NULL) {
            talloc_free(plan);
            return NSERROR_NOMEM;
        }
    }
    plan->html = html;
    plan->head = html->object_list;
    plan->num_objects = html->num_objects;
    plan->bw = html->bw;
    for (struct content_html_object *o = html->object_list; o != NULL; o = o->next) {
        if (!html_object_in_region(o->box, old_root))
            continue;
        if (!html_object_can_rebind(html, o))
            goto unsupported;
        plan->bindings[plan->count++] = (struct html_object_binding){o, o->box, o->content, NULL};
    }
    for (const struct box_construct_object *r = candidate->objects; r != NULL; r = r->next) {
        bool found = false;
        for (struct content_html_object *o = html->object_list; o != NULL; o = o->next) {
            /* Never steal a handle from an unaffected box, including generated
             * content without a DOM node. Each record serves one destination. */
            if ((o->box != NULL && !html_object_in_region(o->box, old_root)) || !html_object_can_rebind(html, o) ||
                !html_object_matches_request(o, r))
                continue;
            size_t i = 0;
            while (i < plan->count && plan->bindings[i].object != o)
                i++;
            if (i < plan->count && plan->bindings[i].request != NULL)
                continue;
            if (i == plan->count)
                plan->bindings[plan->count++] = (struct html_object_binding){o, o->box, o->content, NULL};
            plan->bindings[i].request = r;
            found = true;
            break;
        }
        if (!found)
            goto unsupported;
    }
    *result = plan;
    return NSERROR_OK;

unsupported:
    talloc_free(plan);
    return NSERROR_NOT_IMPLEMENTED;
}

nserror html_object_check_bindings(const struct html_object_binding_plan *plan)
{
    html_content *html = plan->html;
    if (plan->applied || html->object_list != plan->head ||
        html->num_objects != plan->num_objects || html->bw != plan->bw)
        return NSERROR_INVALID;
    /* Check every record before writing any. Callbacks may have completed,
     * failed or rebound a resource while the candidate was being prepared. */
    for (size_t i = 0; i < plan->count; i++) {
        const struct html_object_binding *b = &plan->bindings[i];
        struct content_html_object *o = html->object_list;
        while (o != NULL && o != b->object)
            o = o->next;
        if (o == NULL || o->box != b->old_box || o->content != b->content || !html_object_can_rebind(html, o) ||
            (b->request != NULL && !html_object_matches_request(o, b->request)))
            return NSERROR_INVALID;
    }
    return NSERROR_OK;
}

nserror html_object_apply_bindings(struct html_object_binding_plan *plan)
{
    if (!plan->html->base.locked || !plan->html->restyling || html_object_check_bindings(plan) != NSERROR_OK)
        return NSERROR_INVALID;
    html_object_commit_bindings(plan);
    return NSERROR_OK;
}

void html_object_commit_bindings(struct html_object_binding_plan *plan)
{
    assert(plan->html->base.locked && plan->html->restyling && !plan->applied);
    for (size_t i = 0; i < plan->count; i++) {
        const struct html_object_binding *b = &plan->bindings[i];
        struct content_html_object *o = b->object;
        if (o->box != NULL) {
            if (!o->background)
                o->box->object = NULL;
            else if (o->background_layer == 0)
                o->box->background = NULL;
            else
                o->box->background_layers[o->background_layer - 1] = NULL;
        }
        o->box = b->request == NULL ? NULL : b->request->box;
        if (o->box != NULL && o->content != NULL)
            html_object_done(o->box, o->content, o->background, o->background_layer);
    }
    plan->applied = true;
}

void html_object_discard_bindings(struct html_object_binding_plan *plan)
{
    talloc_free(plan);
}


/* exported interface documented in html/object.h */
static bool html_fetch_object_layer(
    html_content *c, nsurl *url, struct box *box, content_type permitted_types, bool background, uint32_t layer)
{
    struct content_html_object *object;
    hlcache_handle_callback object_callback;
    hlcache_child_context child;
    nserror error;

    if (html_reuse_object(c, box, url, permitted_types, background, layer, NULL, 0, NULL))
        return true;

    PERF("OBJECT DISCOVER '%s' (bg=%d)", nsurl_access(url), background);

    /* If we've already been aborted, don't bother attempting the
     * fetch */
    if (c->aborted)
        return true;

    child.charset = c->encoding;
    child.quirks = c->base.quirks;

    object = calloc(1, sizeof(struct content_html_object));
    if (object == NULL) {
        return false;
    }

    if (box == NULL) {
        object_callback = html_object_nobox_callback;
    } else {
        object_callback = html_object_callback;
    }

    object->parent = (struct content *)c;
    object->next = NULL;
    object->content = NULL;
    object->box = box;
    object->permitted_types = permitted_types;
    object->background = background;
    object->background_layer = layer;
    object->layout_resource = box != NULL;
    object->node = box != NULL && box->node != NULL ? dom_node_ref(box->node) : NULL;
    object->url = nsurl_ref(url);

    /* Increment active BEFORE hlcache_handle_retrieve.
     * Callbacks can fire synchronously for cached content,
     * so we must have the counter incremented before the callback
     * tries to decrement it.
     */
    if (box != NULL) {
        c->base.active++;
        NSLOG(wisp, INFO, "%d fetches active (pre-retrieve)", c->base.active);
    }

    error = hlcache_handle_retrieve(url, html_object_fetch_flags(object), content_get_url(&c->base), NULL,
        object_callback, object, &child, object->permitted_types, &object->content);
    if (error != NSERROR_OK) {
        if (box != NULL) {
            c->base.active--;
            NSLOG(wisp, INFO, "%d fetches active (retrieve failed)", c->base.active);
        }
        if (error == NSERROR_NOMEM || box == NULL) {
            dom_node_unref(object->node);
            nsurl_unref(object->url);
            free(object);
            return error != NSERROR_NOMEM;
        }
        /* A synchronous retrieval error needs the same retained decision as
         * an asynchronous load/decode error. Otherwise every rebuild retries. */
        html_object_failed(object);
    }

    /* add to content object list */
    object->next = c->object_list;
    c->object_list = object;

    c->num_objects++;

    return true;
}

/* exported interface documented in html/object.h */
bool html_fetch_object_buffer(html_content *c, const uint8_t *data, size_t len, const char *mime_type, struct box *box,
    content_type permitted_types)
{
    struct content_html_object *object;
    hlcache_child_context child;
    nserror error;

    if (html_reuse_object(c, box, NULL, permitted_types, false, 0, data, len, mime_type))
        return true;

    if (c->aborted)
        return true;

    child.charset = c->encoding;
    child.quirks = c->base.quirks;

    object = calloc(1, sizeof(struct content_html_object));
    if (object == NULL)
        return false;

    object->parent = (struct content *)c;
    object->next = NULL;
    object->content = NULL;
    object->box = box;
    object->permitted_types = permitted_types;
    object->background = false;
    object->layout_resource = box != NULL;
    object->node = box != NULL && box->node != NULL ? dom_node_ref(box->node) : NULL;

    if (box != NULL) {
        c->base.active++;
        NSLOG(wisp, INFO, "%d fetches active (pre-buffer-retrieve)", c->base.active);
    }

    error = hlcache_handle_retrieve_buffer(
        data, len, mime_type, html_object_callback, object, &child, permitted_types, &object->content);

    if (error != NSERROR_OK) {
        if (box != NULL) {
            c->base.active--;
            NSLOG(wisp, INFO, "%d fetches active (buffer retrieve failed)", c->base.active);
        }
        dom_node_unref(object->node);
        if (object->url != NULL)
            nsurl_unref(object->url);
        free(object);
        return error != NSERROR_NOMEM;
    }

    object->next = c->object_list;
    c->object_list = object;
    c->num_objects++;

    return true;
}

bool html_fetch_object(html_content *c, nsurl *url, struct box *box, content_type permitted_types, bool background)
{
    return html_fetch_object_layer(c, url, box, permitted_types, background, 0);
}

bool html_fetch_background(html_content *c, nsurl *url, struct box *box, content_type permitted_types, uint32_t layer)
{
    return html_fetch_object_layer(c, url, box, permitted_types, true, layer);
}

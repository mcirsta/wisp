/* SPDX-License-Identifier: GPL-2.0-or-later
 * CSSOM View: document scrolls bubble; element scrolls and Window resizes do not.
 * Keep DOM identities, never transient boxes, across the scheduled boundary. */
#include <stdlib.h>
#include <wisp/content.h>
#include <wisp/content/hlcache.h>
#include <wisp/content/handlers/html/private.h>
#include <wisp/desktop/gui_internal.h>
#include <wisp/misc.h>
#include <wisp/utils/corestrings.h>
#include "content/handlers/javascript/js.h"
#include "content/handlers/html/view_events.h"

struct html_scroll_event {
    dom_node *target;
    struct html_scroll_event *next;
};

static void view_events_task(void *pw);

void html_view_events_cancel(html_content *html)
{
    guit->misc->schedule(-1, view_events_task, html);
    while (html->scroll_events != NULL) {
        struct html_scroll_event *event = html->scroll_events;
        html->scroll_events = event->next;
        dom_node_unref(event->target);
        free(event);
    }
    html->scroll_events_tail = NULL;
    html->resize_event_pending = html->scroll_end_pending = html->view_events_pending = false;
}

static bool schedule_events(html_content *html)
{
    if (html->view_events_pending)
        return true;
    if (guit->misc->schedule(0, view_events_task, html) != NSERROR_OK)
        return false;
    html->view_events_pending = true;
    return true;
}

void html_queue_scroll_end(html_content *html)
{
    if (!html->aborted && html->jsthread != NULL && schedule_events(html))
        html->scroll_end_pending = true;
}

void html_queue_scroll_event(html_content *html, dom_node *target)
{
    if (html->aborted || html->jsthread == NULL || target == NULL)
        return;
    for (struct html_scroll_event *p = html->scroll_events; p != NULL; p = p->next)
        if (p->target == target)
            return;
    struct html_scroll_event *event = malloc(sizeof(*event));
    if (event == NULL)
        return;
    if (!schedule_events(html)) {
        free(event);
        return;
    }
    event->target = dom_node_ref(target);
    event->next = NULL;
    if (html->scroll_events_tail != NULL)
        html->scroll_events_tail->next = event;
    else
        html->scroll_events = event;
    html->scroll_events_tail = event;
}

static void view_events_task(void *pw)
{
    html_content *html = pw;
    html->view_events_pending = false;
    if (html->aborted || html->jsthread == NULL) {
        html_view_events_cancel(html);
        return;
    }
    uint64_t generation = html->input_generation;
    if (!content_add_user(&html->base, NULL, &generation)) {
        html_view_events_cancel(html);
        return;
    }
    /* Snapshot this turn's work. A listener can queue another scroll, replace
     * layout, or retire the document; none may invalidate this list's nodes. */
    struct html_scroll_event *events = html->scroll_events;
    html->scroll_events = html->scroll_events_tail = NULL;
    bool resize = html->resize_event_pending;
    bool scroll_end = html->scroll_end_pending;
    html->resize_event_pending = false;
    html->scroll_end_pending = false;
    if (resize)
        js_fire_window_event(html->jsthread, corestring_dom_resize);
    while (events != NULL) {
        struct html_scroll_event *event = events;
        events = event->next;
        if (!html->aborted && generation == html->input_generation && html->jsthread != NULL)
            js_fire_element_event(html->jsthread, event->target, corestring_dom_scroll);
        dom_node_unref(event->target);
        free(event);
    }
    if (scroll_end && !html->aborted && generation == html->input_generation && html->jsthread != NULL)
        js_fire_element_event(html->jsthread, (dom_node *)html->document, corestring_dom_scrollend);
    content_remove_user(&html->base, NULL, &generation);
}

/* Public frontend/core bridge, documented in html.h. */
void html_viewport_scroll(hlcache_handle *handle)
{
    if (handle == NULL || content_get_type(handle) != CONTENT_HTML)
        return;
    html_content *html = (html_content *)hlcache_handle_get_content(handle);
    html_queue_scroll_event(html, (dom_node *)html->document);
}

void html_check_viewport_resize(html_content *html)
{
    unsigned width = 0, height = 0;
    union content_msg_data msg = {.getdims = {.viewport_width = &width, .viewport_height = &height,
        .include_scrollbars = true}};
    content_broadcast(&html->base, CONTENT_MSG_GETDIMS, &msg);
    bool changed = html->view_dimensions_valid && (width != html->view_width || height != html->view_height);
    html->view_dimensions_valid = true;
    html->view_width = width;
    html->view_height = height;
    if (changed && !html->aborted && html->jsthread != NULL && schedule_events(html))
        html->resize_event_pending = true;
}

/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef WISP_HTML_VIEW_EVENTS_H
#define WISP_HTML_VIEW_EVENTS_H
#include <dom/core/node.h>
struct html_content;
/* Coalesced notifications, never JS entry from layout or a frontend callback. */
void html_queue_scroll_event(struct html_content *html, dom_node *target);
/* Record the initial viewport and queue resize after subsequent changes. */
void html_check_viewport_resize(struct html_content *html);
void html_view_events_cancel(struct html_content *html);
#endif

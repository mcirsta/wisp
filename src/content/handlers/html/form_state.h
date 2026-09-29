/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef WISP_HTML_FORM_STATE_H
#define WISP_HTML_FORM_STATE_H

#include <dom/dom.h>

/* Resolve a click on an input or its label. Returns an owned input reference,
 * including display:none inputs. Links and other interactive descendants do
 * not activate their containing label. */
dom_node *html_checkable_target(dom_node *clicked);

/* Called for each successful live-state change, including unchecked radio peers.
 * The node is borrowed; callbacks must not mutate the DOM or run script. */
typedef void (*html_checkable_changed_cb)(dom_node *node, void *pw);

/* Native activation using live DOM checkedness. Returns whether any state changed,
 * even if a later operation fails. The callback may be NULL. */
bool html_checkable_activate(dom_node *input, html_checkable_changed_cb changed, void *pw);

#endif

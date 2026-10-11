/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef WISP_HTML_FORM_STATE_H
#define WISP_HTML_FORM_STATE_H

#include <dom/dom.h>

/* Resolve a click on an input or its label. Returns an owned input reference,
 * including display:none inputs. Links and other interactive descendants do
 * not activate their containing label. */
dom_node *html_checkable_target(dom_node *clicked);

/** Shared HTML disabledness, including fieldset/first-legend ancestry. */
bool html_form_control_disabled(dom_node *control);

/** Current submit-button state, independent of rendered gadget type. */
bool html_form_is_submit_button(dom_node *node);

/** Retain the original input/button activation element before dispatch. */
dom_node *html_form_submit_target(dom_node *clicked);

/* Called for each successful live-state change, including unchecked radio peers.
 * The node is borrowed; callbacks must not mutate the DOM or run script. */
typedef void (*html_checkable_changed_cb)(dom_node *node, void *pw);

struct html_form_click_result {
    bool accepted; /* The original click was not canceled. */
    bool handled;  /* Its activation element was a label or checkable input. */
};

/* Dispatch a native click and perform checkable input/label activation. Retains
 * the clicked node and activation element across callbacks. Label association is
 * read after dispatch; input pre-activation and cancellation are one transaction. */
struct html_form_click_context {
    html_checkable_changed_cb changed;
    /* Delivers click, input and change through the host's native event adapter.
     * NULL uses libdom alone, including when scripting is unavailable. */
    bool (*dispatch)(dom_node *target, dom_string *type, void *pw);
    void *pw;
};
struct html_form_click_result html_form_click(dom_node *clicked, const struct html_form_click_context *context);

#endif

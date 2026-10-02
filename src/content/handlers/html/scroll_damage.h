/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef WISP_HTML_SCROLL_DAMAGE_H
#define WISP_HTML_SCROLL_DAMAGE_H

#include <wisp/browser_window.h>

struct html_content;

/** Settled HTML implementation; same contract as browser_window_get_scroll_damage. */
bool html_compute_scroll_damage(struct html_content *html, const struct rect *old_viewport,
    const struct rect *new_viewport, browser_window_scroll_damage_cb add, void *pw);

#endif

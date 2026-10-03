/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef WISP_HTML_CONTENT_VISIBILITY_H
#define WISP_HTML_CONTENT_VISIBILITY_H

#include <wisp/content/handlers/html/private.h>

/* CSS Containment: non-atomic inlines and internal table boxes (except cells)
 * cannot establish layout containment and do not skip their contents. */
bool html_content_visibility_applies(const css_computed_style *style, bool replaced);
bool html_until_found(dom_node *node);
nserror html_reveal_ancestors(html_content *html, dom_node *target, bool *changed);

/* Owned selection snapshot, reusing published box styles where possible. */
css_select_results *html_visibility_style(
    html_content *html, dom_node *node, const css_computed_style *parent, const css_computed_style *root);

#endif

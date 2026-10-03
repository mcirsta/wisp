/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef WISP_HTML_DETAILS_H
#define WISP_HTML_DETAILS_H

#include <wisp/content/handlers/html/private.h>

bool html_is_details(dom_node *node);
bool html_details_open(dom_node *node);
nserror html_details_set_open(dom_node *node, bool open);
/* First direct summary child, owning a reference; NULL when absent. */
nserror html_details_summary(dom_node *node, dom_node **summary);
/* Whether the direct child belongs to the collapsible contents. */
bool html_details_hides_child(dom_node *details, dom_node *child);
/* Native activation of the first summary (or the generated default legend). */
nserror html_details_activate(html_content *html, dom_node *clicked);

#endif

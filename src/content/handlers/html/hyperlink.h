/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef WISP_HTML_HYPERLINK_H
#define WISP_HTML_HYPERLINK_H

#include <dom/dom.h>
#include <wisp/utils/errors.h>

struct html_content;
struct nsurl;

/** Resolve a DOM URL string using the shared URL parser. The result is owned. */
nserror html_link_url(const dom_string *value, struct nsurl *base, struct nsurl **result);

/** Retain the original hyperlink activation element before dispatch. */
dom_node *html_hyperlink_target(dom_node *clicked);

/** HTML target resolution shared by links/forms. A non-NULL override includes
 * the empty string; the returned target is owned. */
nserror html_element_target(dom_document *document, dom_node *element, dom_string *override, char **target);

/** Read action data after callbacks. NULL URL means no navigation. Successful
 * results own the URL and malloc'd target. Never returns box-owned storage.
 * Caller must separately guard content/window retirement across callbacks. */
nserror html_hyperlink_resolve(struct html_content *html, dom_node *link, struct nsurl **url, char **target);

#endif

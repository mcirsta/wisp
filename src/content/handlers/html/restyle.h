/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef WISP_HTML_RESTYLE_H
#define WISP_HTML_RESTYLE_H

#include <wisp/content/handlers/html/private.h>

/* Tracking only: scheduling and tree lifetime remain in html.c. */
void html_restyle_record_checked(html_content *html, dom_node *node);
void html_restyle_record_full(html_content *html, const char *reason);
void html_restyle_note_dom_change(html_content *html);
void html_restyle_discard_pending(html_content *html);
void html_restyle_begin(html_content *html);
void html_restyle_capture_inputs(html_content *html);
void html_restyle_end(html_content *html, bool success);

#endif

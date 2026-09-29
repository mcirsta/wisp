/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef WISP_HTML_FORM_STATE_H
#define WISP_HTML_FORM_STATE_H

#include <dom/dom.h>

/* Resolve a click on an input or its label. Returns an owned input reference,
 * including display:none inputs. Links and other interactive descendants do
 * not activate their containing label. */
dom_node *html_checkable_target(dom_node *clicked);

/* Native activation using live DOM checkedness. Returns whether state changed.
 * Radio groups include hidden controls and controls outside forms. */
bool html_checkable_activate(dom_node *input);

#endif

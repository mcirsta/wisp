/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef WISP_HTML_DIRECTION_H
#define WISP_HTML_DIRECTION_H

#include <dom/dom.h>

/* HTML semantic directionality, independent of computed CSS direction.
 * Reads the current light DOM and form value; does not cache or mutate nodes.
 * Shadow trees and inline Unicode bidi layout are not implemented here. */
dom_exception html_element_direction(dom_node *element, bool *rtl);

#endif

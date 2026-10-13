/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef WISP_CSS_INLINE_H
#define WISP_CSS_INLINE_H
#include <dom/dom.h>
#include <libcss/declarations.h>

struct nscss_inline_style;
void nscss_inline_params(css_stylesheet_params *params, const char *url, bool quirks);
/* Lazy DOM-owned state. Borrowed while the element lives, refreshed after every
 * external attribute assignment, including assignments of identical text. */
dom_exception
nscss_inline_style_get(dom_node *element, const css_stylesheet_params *params, struct nscss_inline_style **out);
const css_declarations *nscss_inline_style_declarations(const struct nscss_inline_style *style);
/* Prepare a complete candidate before calling. Publication transfers *candidate
 * and sets it NULL; on failure without a DOM write it remains caller-owned.
 * changed also reports writes followed by a legacy DOM event failure. */
dom_exception nscss_inline_style_commit(
    dom_node *element, struct nscss_inline_style *style, css_declarations **candidate, bool *changed);
/* Rendering: use retained declarations if present, otherwise ordinary parsing.
 * Does not allocate an editable store for static elements. Result is owned. */
css_stylesheet *nscss_inline_style_for_element(dom_node *element, const char *url, bool quirks, bool *present);
#endif

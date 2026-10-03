/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef WISP_HTML_SEARCH_H
#define WISP_HTML_SEARCH_H

#include "content/textsearch.h"

nserror html_textsearch_dom(
    struct content *c, struct textsearch_context *context, const char *pattern, int length, bool sensitive);
nserror html_textsearch_resolve(struct content *c, void *match, bool activate, struct textsearch_range *range);
void html_textsearch_free_match(void *match);

#endif

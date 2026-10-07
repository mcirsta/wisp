/*
 * This file is part of libdom.
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 * Copyright 2022 Daniel Silverstone <dsilvers@netsurf-browser.org>
 */

#ifndef dom_core_tokenlist_h_
#define dom_core_tokenlist_h_

#include <stdbool.h>
#include <stdint.h>
#include <dom/core/exceptions.h>

struct dom_element;
struct dom_string;

typedef struct dom_tokenlist dom_tokenlist;

void dom_tokenlist_ref(struct dom_tokenlist *list);
void dom_tokenlist_unref(struct dom_tokenlist *list);

dom_exception dom_tokenlist_create(struct dom_element *ele, struct dom_string *attr, dom_tokenlist **list);

dom_exception dom_tokenlist_get_length(struct dom_tokenlist *list, uint32_t *length);
dom_exception _dom_tokenlist_item(struct dom_tokenlist *list, uint32_t index, struct dom_string **value);

#define dom_tokenlist_item(l, i, n) _dom_tokenlist_item((dom_tokenlist *)(l), (uint32_t)(i), (struct dom_string **)(n))

dom_exception dom_tokenlist_get_value(struct dom_tokenlist *list, struct dom_string **value);

dom_exception dom_tokenlist_set_value(struct dom_tokenlist *list, struct dom_string *value);

dom_exception dom_tokenlist_contains(struct dom_tokenlist *list, struct dom_string *value, bool *contains);

dom_exception dom_tokenlist_add(struct dom_tokenlist *list, struct dom_string *value);

dom_exception dom_tokenlist_remove(struct dom_tokenlist *list, struct dom_string *value);

/* Validate all tokens before changing the set and update the attribute once.
 * updated is true if the attribute setter was reached, even if it then failed.
 * Callers can use it for conservative renderer invalidation. */
dom_exception
dom_tokenlist_add_tokens(dom_tokenlist *list, struct dom_string *const *tokens, uint32_t count, bool *updated);
dom_exception
dom_tokenlist_remove_tokens(dom_tokenlist *list, struct dom_string *const *tokens, uint32_t count, bool *updated);
/* force: -1 toggles; 0 removes; 1 adds. result is the final membership. */
dom_exception
dom_tokenlist_toggle(dom_tokenlist *list, struct dom_string *token, int force, bool *result, bool *updated);
dom_exception dom_tokenlist_replace(
    dom_tokenlist *list, struct dom_string *token, struct dom_string *replacement, bool *result, bool *updated);

#endif

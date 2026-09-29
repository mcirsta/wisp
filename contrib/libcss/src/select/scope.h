/* SPDX-License-Identifier: MIT */
#ifndef css_select_scope_h_
#define css_select_scope_h_

#include "stylesheet.h"

bool css__selector_checked_parent_scope(const css_stylesheet *sheet, const css_selector *selector);
bool css__sheet_checked_parent_scope(const css_stylesheet *sheet, uint64_t *revision);

#endif

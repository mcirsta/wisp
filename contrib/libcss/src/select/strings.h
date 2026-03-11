/*
 * This file is part of LibCSS
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 * Copyright 2009 John-Mark Bell <jmb@netsurf-browser.org>
 */

#ifndef css_select_strings_h_
#define css_select_strings_h_

#include <libcss/errors.h>

/** Useful interned strings */
typedef struct {
    lwc_string *universal;
    lwc_string *first_child;
    lwc_string *link;
    lwc_string *visited;
    lwc_string *hover;
    lwc_string *active;
    lwc_string *focus;
    lwc_string *nth_child;
    lwc_string *nth_last_child;
    lwc_string *nth_of_type;
    lwc_string *nth_last_of_type;
    lwc_string *last_child;
    lwc_string *first_of_type;
    lwc_string *last_of_type;
    lwc_string *only_child;
    lwc_string *only_of_type;
    lwc_string *root;
    lwc_string *empty;
    lwc_string *target;
    lwc_string *lang;
    lwc_string *enabled;
    lwc_string *disabled;
    lwc_string *checked;
    lwc_string *first_line;
    lwc_string *first_letter;
    lwc_string *before;
    lwc_string *after;

    lwc_string *backdrop;
    lwc_string *file_selector_button;
    lwc_string *placeholder;
    lwc_string *marker;
    lwc_string *webkit_search_decoration;
    lwc_string *webkit_date_and_time_value;
    lwc_string *webkit_datetime_edit;
    lwc_string *webkit_datetime_edit_fields_wrapper;
    lwc_string *webkit_datetime_edit_year_field;
    lwc_string *webkit_datetime_edit_month_field;
    lwc_string *webkit_datetime_edit_day_field;
    lwc_string *webkit_datetime_edit_hour_field;
    lwc_string *webkit_datetime_edit_minute_field;
    lwc_string *webkit_datetime_edit_second_field;
    lwc_string *webkit_datetime_edit_millisecond_field;
    lwc_string *webkit_datetime_edit_meridiem_field;
    lwc_string *webkit_calendar_picker_indicator;
    lwc_string *webkit_inner_spin_button;
    lwc_string *webkit_outer_spin_button;
    lwc_string *webkit_scrollbar;

    lwc_string *width;
    lwc_string *height;
    lwc_string *prefers_color_scheme;
} css_select_strings;

css_error css_select_strings_intern(css_select_strings *str);
void css_select_strings_unref(css_select_strings *str);

#endif

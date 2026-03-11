/*
 * This file is part of LibCSS
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 * Copyright 2009 John-Mark Bell <jmb@netsurf-browser.org>
 */

#include <libwapcaplet/libwapcaplet.h>

#include "utils/css_utils.h"
#include "select/strings.h"

css_error css_select_strings_intern(css_select_strings *str)
{
    lwc_error error;

    /* Universal selector */
    error = lwc_intern_string("*", SLEN("*"), &str->universal);
    if (error != lwc_error_ok)
        return css_error_from_lwc_error(error);

    /* Pseudo classes */
    error = lwc_intern_string("first-child", SLEN("first-child"), &str->first_child);
    if (error != lwc_error_ok)
        return css_error_from_lwc_error(error);

    error = lwc_intern_string("link", SLEN("link"), &str->link);
    if (error != lwc_error_ok)
        return css_error_from_lwc_error(error);

    error = lwc_intern_string("visited", SLEN("visited"), &str->visited);
    if (error != lwc_error_ok)
        return css_error_from_lwc_error(error);

    error = lwc_intern_string("hover", SLEN("hover"), &str->hover);
    if (error != lwc_error_ok)
        return css_error_from_lwc_error(error);

    error = lwc_intern_string("active", SLEN("active"), &str->active);
    if (error != lwc_error_ok)
        return css_error_from_lwc_error(error);

    error = lwc_intern_string("focus", SLEN("focus"), &str->focus);
    if (error != lwc_error_ok)
        return css_error_from_lwc_error(error);

    error = lwc_intern_string("nth-child", SLEN("nth-child"), &str->nth_child);
    if (error != lwc_error_ok)
        return css_error_from_lwc_error(error);

    error = lwc_intern_string("nth-last-child", SLEN("nth-last-child"), &str->nth_last_child);
    if (error != lwc_error_ok)
        return css_error_from_lwc_error(error);

    error = lwc_intern_string("nth-of-type", SLEN("nth-of-type"), &str->nth_of_type);
    if (error != lwc_error_ok)
        return css_error_from_lwc_error(error);

    error = lwc_intern_string("nth-last-of-type", SLEN("nth-last-of-type"), &str->nth_last_of_type);
    if (error != lwc_error_ok)
        return css_error_from_lwc_error(error);

    error = lwc_intern_string("last-child", SLEN("last-child"), &str->last_child);
    if (error != lwc_error_ok)
        return css_error_from_lwc_error(error);

    error = lwc_intern_string("first-of-type", SLEN("first-of-type"), &str->first_of_type);
    if (error != lwc_error_ok)
        return css_error_from_lwc_error(error);

    error = lwc_intern_string("last-of-type", SLEN("last-of-type"), &str->last_of_type);
    if (error != lwc_error_ok)
        return css_error_from_lwc_error(error);

    error = lwc_intern_string("only-child", SLEN("only-child"), &str->only_child);
    if (error != lwc_error_ok)
        return css_error_from_lwc_error(error);

    error = lwc_intern_string("only-of-type", SLEN("only-of-type"), &str->only_of_type);
    if (error != lwc_error_ok)
        return css_error_from_lwc_error(error);

    error = lwc_intern_string("root", SLEN("root"), &str->root);
    if (error != lwc_error_ok)
        return css_error_from_lwc_error(error);

    error = lwc_intern_string("empty", SLEN("empty"), &str->empty);
    if (error != lwc_error_ok)
        return css_error_from_lwc_error(error);

    error = lwc_intern_string("target", SLEN("target"), &str->target);
    if (error != lwc_error_ok)
        return css_error_from_lwc_error(error);

    error = lwc_intern_string("lang", SLEN("lang"), &str->lang);
    if (error != lwc_error_ok)
        return css_error_from_lwc_error(error);

    error = lwc_intern_string("enabled", SLEN("enabled"), &str->enabled);
    if (error != lwc_error_ok)
        return css_error_from_lwc_error(error);

    error = lwc_intern_string("disabled", SLEN("disabled"), &str->disabled);
    if (error != lwc_error_ok)
        return css_error_from_lwc_error(error);

    error = lwc_intern_string("checked", SLEN("checked"), &str->checked);
    if (error != lwc_error_ok)
        return css_error_from_lwc_error(error);

    /* Pseudo elements */
    error = lwc_intern_string("first-line", SLEN("first-line"), &str->first_line);
    if (error != lwc_error_ok)
        return css_error_from_lwc_error(error);

    error = lwc_intern_string("first_letter", SLEN("first-letter"), &str->first_letter);
    if (error != lwc_error_ok)
        return css_error_from_lwc_error(error);

    error = lwc_intern_string("before", SLEN("before"), &str->before);
    if (error != lwc_error_ok)
        return css_error_from_lwc_error(error);

    error = lwc_intern_string("after", SLEN("after"), &str->after);
    if (error != lwc_error_ok)
        return css_error_from_lwc_error(error);

    error = lwc_intern_string("backdrop", SLEN("backdrop"), &str->backdrop);
    if (error != lwc_error_ok) return css_error_from_lwc_error(error);

    error = lwc_intern_string("file-selector-button", SLEN("file-selector-button"), &str->file_selector_button);
    if (error != lwc_error_ok) return css_error_from_lwc_error(error);

    error = lwc_intern_string("placeholder", SLEN("placeholder"), &str->placeholder);
    if (error != lwc_error_ok) return css_error_from_lwc_error(error);

    error = lwc_intern_string("marker", SLEN("marker"), &str->marker);
    if (error != lwc_error_ok) return css_error_from_lwc_error(error);

    error = lwc_intern_string("-webkit-search-decoration", SLEN("-webkit-search-decoration"), &str->webkit_search_decoration);
    if (error != lwc_error_ok) return css_error_from_lwc_error(error);

    error = lwc_intern_string("-webkit-date-and-time-value", SLEN("-webkit-date-and-time-value"), &str->webkit_date_and_time_value);
    if (error != lwc_error_ok) return css_error_from_lwc_error(error);

    error = lwc_intern_string("-webkit-datetime-edit", SLEN("-webkit-datetime-edit"), &str->webkit_datetime_edit);
    if (error != lwc_error_ok) return css_error_from_lwc_error(error);

    error = lwc_intern_string("-webkit-datetime-edit-fields-wrapper", SLEN("-webkit-datetime-edit-fields-wrapper"), &str->webkit_datetime_edit_fields_wrapper);
    if (error != lwc_error_ok) return css_error_from_lwc_error(error);

    error = lwc_intern_string("-webkit-datetime-edit-year-field", SLEN("-webkit-datetime-edit-year-field"), &str->webkit_datetime_edit_year_field);
    if (error != lwc_error_ok) return css_error_from_lwc_error(error);

    error = lwc_intern_string("-webkit-datetime-edit-month-field", SLEN("-webkit-datetime-edit-month-field"), &str->webkit_datetime_edit_month_field);
    if (error != lwc_error_ok) return css_error_from_lwc_error(error);

    error = lwc_intern_string("-webkit-datetime-edit-day-field", SLEN("-webkit-datetime-edit-day-field"), &str->webkit_datetime_edit_day_field);
    if (error != lwc_error_ok) return css_error_from_lwc_error(error);

    error = lwc_intern_string("-webkit-datetime-edit-hour-field", SLEN("-webkit-datetime-edit-hour-field"), &str->webkit_datetime_edit_hour_field);
    if (error != lwc_error_ok) return css_error_from_lwc_error(error);

    error = lwc_intern_string("-webkit-datetime-edit-minute-field", SLEN("-webkit-datetime-edit-minute-field"), &str->webkit_datetime_edit_minute_field);
    if (error != lwc_error_ok) return css_error_from_lwc_error(error);

    error = lwc_intern_string("-webkit-datetime-edit-second-field", SLEN("-webkit-datetime-edit-second-field"), &str->webkit_datetime_edit_second_field);
    if (error != lwc_error_ok) return css_error_from_lwc_error(error);

    error = lwc_intern_string("-webkit-datetime-edit-millisecond-field", SLEN("-webkit-datetime-edit-millisecond-field"), &str->webkit_datetime_edit_millisecond_field);
    if (error != lwc_error_ok) return css_error_from_lwc_error(error);

    error = lwc_intern_string("-webkit-datetime-edit-meridiem-field", SLEN("-webkit-datetime-edit-meridiem-field"), &str->webkit_datetime_edit_meridiem_field);
    if (error != lwc_error_ok) return css_error_from_lwc_error(error);

    error = lwc_intern_string("-webkit-calendar-picker-indicator", SLEN("-webkit-calendar-picker-indicator"), &str->webkit_calendar_picker_indicator);
    if (error != lwc_error_ok) return css_error_from_lwc_error(error);

    error = lwc_intern_string("-webkit-inner-spin-button", SLEN("-webkit-inner-spin-button"), &str->webkit_inner_spin_button);
    if (error != lwc_error_ok) return css_error_from_lwc_error(error);

    error = lwc_intern_string("-webkit-outer-spin-button", SLEN("-webkit-outer-spin-button"), &str->webkit_outer_spin_button);
    if (error != lwc_error_ok) return css_error_from_lwc_error(error);

    error = lwc_intern_string("-webkit-scrollbar", SLEN("-webkit-scrollbar"), &str->webkit_scrollbar);
    if (error != lwc_error_ok) return css_error_from_lwc_error(error);

    error = lwc_intern_string("width", SLEN("width"), &str->width);
    if (error != lwc_error_ok)
        return css_error_from_lwc_error(error);

    error = lwc_intern_string("height", SLEN("height"), &str->height);
    if (error != lwc_error_ok)
        return css_error_from_lwc_error(error);

    error = lwc_intern_string("prefers-color-scheme", SLEN("prefers-color-scheme"), &str->prefers_color_scheme);
    if (error != lwc_error_ok)
        return css_error_from_lwc_error(error);

    return CSS_OK;
}

void css_select_strings_unref(css_select_strings *str)
{
    lwc_string_unref(str->universal);
    lwc_string_unref(str->first_child);
    lwc_string_unref(str->link);
    lwc_string_unref(str->visited);
    lwc_string_unref(str->hover);
    lwc_string_unref(str->active);
    lwc_string_unref(str->focus);
    lwc_string_unref(str->nth_child);
    lwc_string_unref(str->nth_last_child);
    lwc_string_unref(str->nth_of_type);
    lwc_string_unref(str->nth_last_of_type);
    lwc_string_unref(str->last_child);
    lwc_string_unref(str->first_of_type);
    lwc_string_unref(str->last_of_type);
    lwc_string_unref(str->only_child);
    lwc_string_unref(str->only_of_type);
    lwc_string_unref(str->root);
    lwc_string_unref(str->empty);
    lwc_string_unref(str->target);
    lwc_string_unref(str->lang);
    lwc_string_unref(str->enabled);
    lwc_string_unref(str->disabled);
    lwc_string_unref(str->checked);
    lwc_string_unref(str->first_line);
    lwc_string_unref(str->first_letter);
    lwc_string_unref(str->before);
    lwc_string_unref(str->after);

    lwc_string_unref(str->backdrop);
    lwc_string_unref(str->file_selector_button);
    lwc_string_unref(str->placeholder);
    lwc_string_unref(str->marker);
    lwc_string_unref(str->webkit_search_decoration);
    lwc_string_unref(str->webkit_date_and_time_value);
    lwc_string_unref(str->webkit_datetime_edit);
    lwc_string_unref(str->webkit_datetime_edit_fields_wrapper);
    lwc_string_unref(str->webkit_datetime_edit_year_field);
    lwc_string_unref(str->webkit_datetime_edit_month_field);
    lwc_string_unref(str->webkit_datetime_edit_day_field);
    lwc_string_unref(str->webkit_datetime_edit_hour_field);
    lwc_string_unref(str->webkit_datetime_edit_minute_field);
    lwc_string_unref(str->webkit_datetime_edit_second_field);
    lwc_string_unref(str->webkit_datetime_edit_millisecond_field);
    lwc_string_unref(str->webkit_datetime_edit_meridiem_field);
    lwc_string_unref(str->webkit_calendar_picker_indicator);
    lwc_string_unref(str->webkit_inner_spin_button);
    lwc_string_unref(str->webkit_outer_spin_button);
    lwc_string_unref(str->webkit_scrollbar);

    lwc_string_unref(str->width);
    lwc_string_unref(str->height);
    lwc_string_unref(str->prefers_color_scheme);
}

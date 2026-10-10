/*
 * Copyright 2004 James Bursa <bursa@users.sourceforge.net>
 * Copyright 2003 Phil Mellor <monkeyson@users.sourceforge.net>
 * Copyright 2004 John Tytgat <joty@netsurf-browser.org>
 * Copyright 2005-9 John-Mark Bell <jmb@netsurf-browser.org>
 * Copyright 2009 Paul Blokus <paul_pl@users.sourceforge.net>
 * Copyright 2010 Michael Drake <tlsa@netsurf-browser.org>
 *
 * This file is part of NetSurf, http://www.netsurf-browser.org/
 *
 * NetSurf is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * NetSurf is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */


#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <wisp/utils/corestrings.h>
#include <wisp/utils/log.h>
#include <wisp/content/handlers/html/form_internal.h>
#include "content/handlers/html/form_state.h"
#include "content/handlers/html/form_data.h"

void html_form_data_fini(struct html_form_data *data)
{
    for (size_t i = 0; i < data->length; i++) {
        dom_string_unref(data->entries[i].name);
        dom_string_unref(data->entries[i].value);
        free(data->entries[i].file_path);
    }
    free(data->entries);
    *data = (struct html_form_data){0};
}

/* libdom stores UTF-8; QuickJS preserves lone UTF-16 surrogates as WTF-8 in
 * DOMString values. Entry names/values are scalar strings, so replace those
 * code points here without modifying the DOM or copying already-scalar text. */
static dom_exception scalar_string(dom_string *source, dom_string **result)
{
    if (source == NULL)
        return dom_string_create((const uint8_t *)"", 0, result);
    const uint8_t *bytes = (const uint8_t *)dom_string_data(source);
    size_t length = dom_string_byte_length(source);
    uint8_t *copy = NULL;
    for (size_t i = 0; i + 2 < length; i++) {
        if (bytes[i] != 0xed || bytes[i + 1] < 0xa0 || bytes[i + 1] > 0xbf || bytes[i + 2] < 0x80 ||
            bytes[i + 2] > 0xbf)
            continue;
        if (copy == NULL) {
            copy = malloc(length);
            if (copy == NULL)
                return DOM_NO_MEM_ERR;
            memcpy(copy, bytes, length);
        }
        memcpy(copy + i, "\xef\xbf\xbd", 3);
        i += 2;
    }
    if (copy == NULL) {
        *result = dom_string_ref(source);
        return DOM_NO_ERR;
    }
    dom_exception err = dom_string_create(copy, length, result);
    free(copy);
    return err;
}

static nserror append_entry(struct html_form_data *data, dom_string *name, dom_string *value, const char *path)
{
    struct html_form_entry entry = {0};
    if (scalar_string(name, &entry.name) != DOM_NO_ERR || scalar_string(value, &entry.value) != DOM_NO_ERR)
        goto fail;
    if (path != NULL) {
        entry.file_path = strdup(path);
        if (entry.file_path == NULL)
            goto fail;
    }
    if (data->length == data->capacity) {
        if (data->capacity > SIZE_MAX / sizeof(*data->entries) / 2)
            goto fail;
        size_t capacity = data->capacity == 0 ? 16 : data->capacity * 2;
        struct html_form_entry *entries = realloc(data->entries, capacity * sizeof(*entries));
        if (entries == NULL)
            goto fail;
        data->entries = entries;
        data->capacity = capacity;
    }
    data->entries[data->length++] = entry;
    return NSERROR_OK;
fail:
    dom_string_unref(entry.name);
    dom_string_unref(entry.value);
    free(entry.file_path);
    return NSERROR_NOMEM;
}

nserror html_form_data_append(struct html_form_data *data, dom_string *name, dom_string *value)
{
    return append_entry(data, name, value, NULL);
}

nserror
html_form_data_append_file(struct html_form_data *data, dom_string *name, dom_string *filename, const char *path)
{
    return append_entry(data, name, filename, path == NULL ? "" : path);
}

static nserror append_coordinate(struct html_form_data *data, dom_string *name, char axis, int coordinate)
{
    size_t length = name == NULL ? 0 : dom_string_byte_length(name);
    if (length > SIZE_MAX - 2)
        return NSERROR_NOMEM;
    char *bytes = malloc(length + 2);
    if (bytes == NULL)
        return NSERROR_NOMEM;
    if (length != 0) {
        memcpy(bytes, dom_string_data(name), length);
        bytes[length++] = '.';
    }
    bytes[length++] = axis;
    dom_string *key = NULL, *value = NULL;
    char number[32];
    int digits = snprintf(number, sizeof(number), "%d", coordinate);
    dom_exception err = dom_string_create((const uint8_t *)bytes, length, &key);
    free(bytes);
    if (err == DOM_NO_ERR)
        err = dom_string_create((const uint8_t *)number, digits, &value);
    nserror result = err == DOM_NO_ERR ? html_form_data_append(data, key, value) : NSERROR_NOMEM;
    dom_string_unref(key);
    dom_string_unref(value);
    return result;
}

static nserror form_dom_to_data_textarea(dom_html_text_area_element *text_area_element, struct html_form_data *data)
{
    dom_exception exp; /* the result from DOM operations */
    dom_string *inputname;
    dom_string *inputvalue;
    nserror res;

    /* obtain name property */
    exp = dom_html_text_area_element_get_name(text_area_element, &inputname);
    if (exp != DOM_NO_ERR) {
        NSLOG(wisp, INFO, "Could not get text area name property. exp %d", exp);
        return NSERROR_DOM;
    }

    if (inputname == NULL || dom_string_byte_length(inputname) == 0) {
        dom_string_unref(inputname);
        /* allow enumeration to continue after element with no name */
        return NSERROR_OK;
    }

    /* obtain text area value */
    exp = dom_html_text_area_element_get_value(text_area_element, &inputvalue);
    if (exp != DOM_NO_ERR) {
        NSLOG(wisp, INFO, "Could not get text area content. exp %d", exp);
        dom_string_unref(inputname);
        return NSERROR_DOM;
    }

    /* add key/value pair to entry list */
    res = html_form_data_append(data, inputname, inputvalue);

    dom_string_unref(inputvalue);
    dom_string_unref(inputname);

    return res;
}


static nserror form_dom_to_data_select_option(
    dom_html_option_element *option_element, dom_string *keyname, struct html_form_data *data)
{
    nserror res;
    dom_exception exp; /* the result from DOM operations */
    dom_string *value;
    bool selected;

    exp = dom_html_option_element_get_selected(option_element, &selected);
    if (exp != DOM_NO_ERR) {
        NSLOG(wisp, INFO, "Could not get option selected property");
        return NSERROR_DOM;
    }

    if (!selected) {
        /* unselected options do not add entries */
        return NSERROR_OK;
    }

    bool disabled;
    exp = dom_html_option_element_is_disabled(option_element, &disabled);
    if (exp != DOM_NO_ERR)
        return NSERROR_DOM;
    if (disabled)
        return NSERROR_OK;

    exp = dom_html_option_element_get_value(option_element, &value);
    if (exp != DOM_NO_ERR) {
        NSLOG(wisp, INFO, "Could not get option value");
        return NSERROR_DOM;
    }

    /* add key/value pair to entry list */
    res = html_form_data_append(data, keyname, value);

    dom_string_unref(value);

    return res;
}


static nserror form_dom_to_data_select(dom_html_select_element *select_element, struct html_form_data *data)
{
    nserror res = NSERROR_OK;
    dom_exception exp; /* the result from DOM operations */
    dom_string *inputname;
    dom_html_options_collection *options = NULL;
    uint32_t options_count;
    uint32_t option_index;
    dom_node *option_element = NULL;

    /* obtain name property */
    exp = dom_html_select_element_get_name(select_element, &inputname);
    if (exp != DOM_NO_ERR) {
        NSLOG(wisp, INFO, "Could not get select name property. exp %d", exp);
        return NSERROR_DOM;
    }

    if (inputname == NULL || dom_string_byte_length(inputname) == 0) {
        dom_string_unref(inputname);
        /* allow enumeration to continue after element with no name */
        return NSERROR_OK;
    }

    /* get options collection */
    exp = dom_html_select_element_get_options(select_element, &options);
    if (exp != DOM_NO_ERR) {
        NSLOG(wisp, INFO, "Could not get select options collection");
        dom_string_unref(inputname);
        return NSERROR_DOM;
    }

    /* get options collection length */
    exp = dom_html_options_collection_get_length(options, &options_count);
    if (exp != DOM_NO_ERR) {
        NSLOG(wisp, INFO, "Could not get select options collection length");
        dom_html_options_collection_unref(options);
        dom_string_unref(inputname);
        return NSERROR_DOM;
    }

    /* iterate over options collection */
    for (option_index = 0; option_index < options_count; ++option_index) {
        exp = dom_html_options_collection_item(options, option_index, &option_element);
        if (exp != DOM_NO_ERR) {
            NSLOG(wisp, INFO, "Could not get options item %d", option_index);
            res = NSERROR_DOM;
        } else {
            res = form_dom_to_data_select_option((dom_html_option_element *)option_element, inputname, data);

            dom_node_unref(option_element);
        }

        if (res != NSERROR_OK) {
            break;
        }
    }

    dom_html_options_collection_unref(options);
    dom_string_unref(inputname);

    return res;
}


static nserror form_dom_to_data_input_submit(
    dom_html_input_element *input_element, dom_string *inputname, dom_node *submit_button, struct html_form_data *data)
{
    dom_exception exp; /* the result from DOM operations */
    dom_string *inputvalue;
    nserror res;

    if (submit_button != (dom_node *)input_element) {
        return NSERROR_OK;
    }

    /* matched button used to submit form */
    exp = dom_html_input_element_get_value(input_element, &inputvalue);
    if (exp != DOM_NO_ERR) {
        NSLOG(wisp, INFO, "Could not get submit button value");
        return NSERROR_DOM;
    }

    /* add key/value pair to entry list */
    res = html_form_data_append(data, inputname, inputvalue);

    dom_string_unref(inputvalue);

    return res;
}


static nserror form_dom_to_data_input_image(
    dom_html_input_element *input_element, dom_string *inputname, dom_node *submit_button, struct html_form_data *data)
{
    nserror res;
    dom_exception exp; /* the result from DOM operations */
    struct image_input_coords *coords;

    /* Only use an image input if it was the thing which activated us */
    if (submit_button != (dom_node *)input_element) {
        return NSERROR_OK;
    }

    exp = dom_node_get_user_data((dom_node *)input_element, corestring_dom___ns_key_image_coords_node_data, &coords);
    if (exp != DOM_NO_ERR) {
        NSLOG(wisp, INFO, "Could not get image XY data");
        return NSERROR_DOM;
    }

    /* Keyboard activation has no pointing-device coordinate. */
    struct image_input_coords origin = {0};
    if (coords == NULL)
        coords = &origin;

    res = append_coordinate(data, inputname, 'x', coords->x);
    if (res == NSERROR_OK)
        res = append_coordinate(data, inputname, 'y', coords->y);
    return res;
}


static nserror form_dom_to_data_input_checkbox(
    dom_html_input_element *input_element, dom_string *inputname, struct html_form_data *data)
{
    nserror res;
    dom_exception exp; /* the result from DOM operations */
    bool checked;
    dom_string *inputvalue;

    exp = dom_html_input_element_get_checked(input_element, &checked);
    if (exp != DOM_NO_ERR) {
        NSLOG(wisp, INFO, "Could not get input element checked");
        return NSERROR_DOM;
    }

    if (!checked) {
        /* unchecked items do not generate a data entry */
        return NSERROR_OK;
    }

    exp = dom_element_get_attribute(input_element, corestring_dom_value, &inputvalue);
    if (exp != DOM_NO_ERR) {
        NSLOG(wisp, INFO, "Could not get input element value");
        return NSERROR_DOM;
    }

    /* ensure a default value */
    if (inputvalue == NULL) {
        inputvalue = dom_string_ref(corestring_dom_on);
    }

    /* add key/value pair to entry list */
    res = html_form_data_append(data, inputname, inputvalue);

    dom_string_unref(inputvalue);

    return res;
}


static nserror
form_dom_to_data_input_file(dom_html_input_element *input_element, dom_string *inputname, struct html_form_data *data)
{
    nserror res;
    dom_exception exp; /* the result from DOM operations */
    dom_string *inputvalue;
    const char *rawfile = NULL;

    exp = dom_html_input_element_get_value(input_element, &inputvalue);
    if (exp != DOM_NO_ERR) {
        NSLOG(wisp, INFO, "Could not get file value");
        return NSERROR_DOM;
    }

    exp = dom_node_get_user_data((dom_node *)input_element, corestring_dom___ns_key_file_name_node_data, &rawfile);
    if (exp != DOM_NO_ERR) {
        NSLOG(wisp, INFO, "Could not get file rawname");
        dom_string_unref(inputvalue);
        return NSERROR_DOM;
    }

    /* Only a native file selection supplies a path. The value attribute alone
     * must not create a selected file or expose a local pathname in GET data. */
    dom_string *filename = NULL;
    if (rawfile != NULL && rawfile[0] != '\0' && inputvalue != NULL) {
        const char *bytes = dom_string_data(inputvalue);
        size_t length = dom_string_byte_length(inputvalue), start = 0;
        for (size_t i = 0; i < length; i++)
            if (bytes[i] == '/' || bytes[i] == '\\')
                start = i + 1;
        exp = dom_string_create((const uint8_t *)bytes + start, length - start, &filename);
    }
    res = exp == DOM_NO_ERR ? html_form_data_append_file(data, inputname, filename, rawfile) : NSERROR_NOMEM;
    dom_string_unref(filename);

    dom_string_unref(inputvalue);

    return res;
}


static nserror
form_dom_to_data_input_text(dom_html_input_element *input_element, dom_string *inputname, struct html_form_data *data)
{
    nserror res;
    dom_exception exp; /* the result from DOM operations */
    dom_string *inputvalue;

    exp = dom_html_input_element_get_value(input_element, &inputvalue);
    if (exp != DOM_NO_ERR) {
        NSLOG(wisp, INFO, "Could not get input value");
        return NSERROR_DOM;
    }

    /* add key/value pair to entry list */
    res = html_form_data_append(data, inputname, inputvalue);

    dom_string_unref(inputvalue);

    return res;
}


static nserror
form_dom_to_data_input(dom_html_input_element *input_element, dom_node *submit_button, struct html_form_data *data)
{
    dom_exception exp; /* the result from DOM operations */
    dom_string *inputname;
    dom_string *inputtype;
    nserror res;

    /* obtain name property */
    exp = dom_html_input_element_get_name(input_element, &inputname);
    if (exp != DOM_NO_ERR) {
        NSLOG(wisp, INFO, "Could not get input name property. exp %d", exp);
        return NSERROR_DOM;
    }

    /* get input type */
    exp = dom_html_input_element_get_type(input_element, &inputtype);
    if (exp != DOM_NO_ERR) {
        NSLOG(wisp, INFO, "Could not get input element type");
        dom_string_unref(inputname);
        return NSERROR_DOM;
    }

    /* process according to input element type */
    if (dom_string_caseless_isequal(inputtype, corestring_dom_image)) {

        res = form_dom_to_data_input_image(input_element, inputname, submit_button, data);

    } else if (inputname == NULL || dom_string_byte_length(inputname) == 0) {
        res = NSERROR_OK;

    } else if (dom_string_caseless_isequal(inputtype, corestring_dom_submit)) {
        res = form_dom_to_data_input_submit(input_element, inputname, submit_button, data);

    } else if (dom_string_caseless_isequal(inputtype, corestring_dom_radio) ||
        dom_string_caseless_isequal(inputtype, corestring_dom_checkbox)) {

        res = form_dom_to_data_input_checkbox(input_element, inputname, data);

    } else if (dom_string_caseless_isequal(inputtype, corestring_dom_file)) {

        res = form_dom_to_data_input_file(input_element, inputname, data);

    } else if (dom_string_caseless_isequal(inputtype, corestring_dom_reset) ||
        dom_string_caseless_isequal(inputtype, corestring_dom_button)) {
        /* Skip these */
        NSLOG(wisp, INFO, "Skipping RESET and BUTTON");
        res = NSERROR_OK;

    } else {
        /* Everything else is treated as text values */
        res = form_dom_to_data_input_text(input_element, inputname, data);
    }

    dom_string_unref(inputtype);
    dom_string_unref(inputname);

    return res;
}


static nserror
form_dom_to_data_button(dom_html_button_element *button_element, dom_node *submit_button, struct html_form_data *data)
{
    dom_exception exp; /* the result from DOM operations */
    dom_string *inputname;
    dom_string *inputvalue;
    nserror res = NSERROR_OK;

    /* Only the original submitter contributes a button entry. */
    if (submit_button != (dom_node *)button_element || !html_form_is_submit_button(submit_button))
        return NSERROR_OK;

    /* obtain name property */
    exp = dom_html_button_element_get_name(button_element, &inputname);
    if (exp != DOM_NO_ERR) {
        NSLOG(wisp, INFO, "Could not get button name property. exp %d", exp);
        return NSERROR_DOM;
    }

    if (inputname == NULL || dom_string_byte_length(inputname) == 0) {
        dom_string_unref(inputname);
        /* allow enumeration to continue after element with no name */
        return NSERROR_OK;
    }

    /* get button value and add to entry list */
    exp = dom_html_button_element_get_value(button_element, &inputvalue);
    if (exp != DOM_NO_ERR) {
        NSLOG(wisp, INFO, "Could not get submit button value");
        res = NSERROR_DOM;
    } else {
        res = html_form_data_append(data, inputname, inputvalue);

        dom_string_unref(inputvalue);
    }

    dom_string_unref(inputname);

    return res;
}


nserror html_form_data_construct(dom_html_form_element *form, dom_node *submitter, struct html_form_data *data)
{
    *data = (struct html_form_data){0};
    dom_html_form_control_list controls = {0};
    dom_exception err = dom_html_form_element_get_submittable_elements(form, &controls);
    nserror result = err == DOM_NO_ERR ? NSERROR_OK : err == DOM_NO_MEM_ERR ? NSERROR_NOMEM : NSERROR_DOM;
    for (size_t i = 0; result == NSERROR_OK && i < controls.length; i++) {
        dom_node *node = controls.items[i];
        if (html_form_control_disabled(node) || dom_html_form_control_has_datalist_ancestor(node))
            continue;
        dom_html_element_type tag;
        if (dom_html_element_get_tag_type(node, &tag) != DOM_NO_ERR) {
            result = NSERROR_DOM;
            break;
        }
        switch (tag) {
        case DOM_HTML_ELEMENT_TYPE_INPUT:
            result = form_dom_to_data_input((dom_html_input_element *)node, submitter, data);
            break;
        case DOM_HTML_ELEMENT_TYPE_BUTTON:
            result = form_dom_to_data_button((dom_html_button_element *)node, submitter, data);
            break;
        case DOM_HTML_ELEMENT_TYPE_SELECT:
            result = form_dom_to_data_select((dom_html_select_element *)node, data);
            break;
        case DOM_HTML_ELEMENT_TYPE_TEXTAREA:
            result = form_dom_to_data_textarea((dom_html_text_area_element *)node, data);
            break;
        default:
            result = NSERROR_DOM;
            break;
        }
    }
    dom_html_form_control_list_fini(&controls);
    if (result != NSERROR_OK)
        html_form_data_fini(data);
    return result;
}

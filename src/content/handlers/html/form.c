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

/**
 * \file
 * Form handling functions (implementation).
 */

#include <dom/dom.h>
#include <assert.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <wisp/browser_window.h>
#include <wisp/content/fetch.h>
#include <wisp/content/handlers/css/utils.h>
#include <wisp/content/hlcache.h>
#include <wisp/desktop/textarea.h>
#include <wisp/misc.h>
#include <wisp/mouse.h>
#include <wisp/plotters.h>
#include <wisp/utils/ascii.h>
#include <wisp/utils/corestrings.h>
#include <wisp/utils/log.h>
#include <wisp/utils/messages.h>
#include "utils/talloc.h"
#include "desktop/knockout.h"
#include "desktop/scrollbar.h"

#include <wisp/content/handlers/html/box.h>
#include <wisp/content/handlers/html/box_inspect.h>
#include <wisp/content/handlers/html/form_internal.h>
#include <wisp/content/handlers/html/html.h>
#include <wisp/content/handlers/html/private.h>
#include "utils/hashmap.h"
#include "content/handlers/html/font.h"
#include "content/handlers/html/layout.h"
#include "content/handlers/html/restyle.h"
#include "content/handlers/html/form_state.h"
#include "content/handlers/html/form_data.h"
#include "content/handlers/html/hyperlink.h"

#define MAX_SELECT_HEIGHT 210
#define SELECT_LINE_SPACING 0.2
#define SELECT_BORDER_WIDTH 1
#define SELECT_SELECTED_COLOUR 0xDB9370

struct form_select_menu {
    int line_height;
    int width, height;
    struct scrollbar *scrollbar;
    int f_size;
    bool scroll_capture;
    select_menu_redraw_callback callback;
    void *client_data;
    struct content *c;
};

static plot_style_t plot_style_fill_selected = {
    .fill_type = PLOT_OP_TYPE_SOLID,
    .fill_colour = SELECT_SELECTED_COLOUR,
};

static plot_font_style_t plot_fstyle_entry = {
    .family = PLOT_FONT_FAMILY_SANS_SERIF,
    .weight = 400,
    .flags = FONTF_NONE,
    .background = 0xffffff,
    .foreground = 0x000000,
};


/**
 * Find an acceptable character set encoding with which to submit the form
 *
 * \param accept_charsets Current accept-charset attribute, or NULL
 * \param document_charset Document encoding, or NULL
 * \return Pointer to charset name (on heap, caller should free) or NULL
 */
static char *form_acceptable_charset(const char *accept_charsets, const char *document_charset)
{
    char *temp, *c;
    const char *end;

    if (!accept_charsets) {
        /* no accept-charsets attribute for this form */
        if (document_charset) {
            /* document charset present, so use it */
            return strdup(document_charset);
        } else {
            /* no document charset, so default to UTF-8 */
            return strdup("UTF-8");
        }
    }

    /* HTML splits accept-charset on ASCII whitespace. An explicitly empty
     * candidate list selects UTF-8, independently of the document encoding. */
    accept_charsets += strspn(accept_charsets, " \t\n\f\r");
    if (*accept_charsets == '\0')
        return strdup("UTF-8");

    /* make temporary copy of accept-charsets attribute */
    temp = strdup(accept_charsets);
    if (!temp)
        return NULL;

    /* make it upper case */
    for (c = temp; *c; c++) {
        *c = ascii_to_upper(*c);
    }

    /* is UTF-8 specified? */
    c = strstr(temp, "UTF-8");
    if (c) {
        free(temp);
        return strdup("UTF-8");
    }

    /* dispense with temporary copy */
    free(temp);

    /* according to RFC2070, the accept-charsets attribute of the
     * form element contains a space and/or comma separated list */
    end = accept_charsets;

    /** \todo an improvement would be to choose an encoding
     * acceptable to the server which covers as much of the input
     * values as possible. Additionally, we need to handle the
     * case where none of the acceptable encodings cover all the
     * textual input values.  For now, we just extract the first
     * element of the charset list
     */
    while (*end && !ascii_is_space(*end)) {
        if (*end == ',')
            break;
        end++;
    }

    return strndup(accept_charsets, end - accept_charsets);
}


/**
 * Callback for the select menus scroll
 */
static void form_select_menu_scroll_callback(void *client_data, struct scrollbar_msg_data *scrollbar_data)
{
    struct form_control *control = client_data;
    struct form_select_menu *menu = control->data.select.menu;
    html_content *html = (html_content *)menu->c;

    switch (scrollbar_data->msg) {
    case SCROLLBAR_MSG_MOVED:
        menu->callback(menu->client_data, 0, 0, menu->width, menu->height);
        break;
    case SCROLLBAR_MSG_SCROLL_START: {
        struct rect rect = {
            .x0 = scrollbar_data->x0, .y0 = scrollbar_data->y0, .x1 = scrollbar_data->x1, .y1 = scrollbar_data->y1};

        browser_window_set_drag_type(html->bw, DRAGGING_CONTENT_SCROLLBAR, &rect);

        menu->scroll_capture = true;
    } break;
    case SCROLLBAR_MSG_SCROLL_FINISHED:
        menu->scroll_capture = false;

        browser_window_set_drag_type(html->bw, DRAGGING_NONE, NULL);
        break;
    default:
        break;
    }
}


/**
 * Process a selection from a form select menu.
 *
 * \param  html The html content handle for the form
 * \param  control  form control with menu
 * \param  item	    index of item selected from the menu
 * \return NSERROR_OK or appropriate error code.
 */
static nserror form__select_process_selection(html_content *html, struct form_control *control, int item)
{
    struct box *inline_box;
    struct form_option *o;
    int count;
    nserror ret = NSERROR_OK;

    assert(control != NULL);
    assert(html != NULL);

    /**
     * \todo Even though the form code is effectively part of the html
     *        content handler, poking around inside contents is not good
     */

    inline_box = control->box->children->children;
    /* Option selectedness also participates in :checked selectors. */
    html_restyle_note_dom_change(html);

    for (count = 0, o = control->data.select.items; o != NULL; count++, o = o->next) {
        if (!control->data.select.multiple && o->selected) {
            o->selected = false;
            dom_html_option_element_set_selected(o->node, false);
        }

        if (count == item) {
            if (control->data.select.multiple) {
                if (o->selected) {
                    o->selected = false;
                    dom_html_option_element_set_selected(o->node, false);
                    control->data.select.num_selected--;
                } else {
                    o->selected = true;
                    dom_html_option_element_set_selected(o->node, true);
                    control->data.select.num_selected++;
                }
            } else {
                dom_html_option_element_set_selected(o->node, true);
                o->selected = true;
            }
        }

        if (o->selected) {
            control->data.select.current = o;
        }
    }

    talloc_free(inline_box->text);
    inline_box->text = 0;

    if (control->data.select.num_selected == 0) {
        inline_box->text = talloc_strdup(inline_box, messages_get("Form_None"));
    } else if (control->data.select.num_selected == 1) {
        inline_box->text = talloc_strdup(inline_box, control->data.select.current->text);
    } else {
        inline_box->text = talloc_strdup(inline_box, messages_get("Form_Many"));
    }

    if (!inline_box->text) {
        ret = NSERROR_NOMEM;
        inline_box->length = 0;
    } else {
        inline_box->length = strlen(inline_box->text);
    }
    inline_box->width = control->box->width;

    html__redraw_a_box(html, control->box);

    return ret;
}


/**
 * Handle a click on the area of the currently opened select menu.
 *
 * \param control the select menu which received the click
 * \param x X coordinate of click
 * \param y Y coordinate of click
 */
static void form_select_menu_clicked(struct form_control *control, int x, int y)
{
    struct form_select_menu *menu = control->data.select.menu;
    struct form_option *option;
    html_content *html = (html_content *)menu->c;
    int line_height, line_height_with_spacing;
    int item_bottom_y;
    int scroll, i;

    scroll = scrollbar_get_offset(menu->scrollbar);

    line_height = menu->line_height;
    line_height_with_spacing = line_height + line_height * SELECT_LINE_SPACING;

    option = control->data.select.items;
    item_bottom_y = line_height_with_spacing;
    i = 0;
    while (option && item_bottom_y < scroll + y) {
        item_bottom_y += line_height_with_spacing;
        option = option->next;
        i++;
    }

    if (option != NULL) {
        form__select_process_selection(html, control, i);
    }

    menu->callback(menu->client_data, 0, 0, menu->width, menu->height);
}


/* exported interface documented in html/form_internal.h */
void form_add_control(struct form *form, struct form_control *control)
{
    if (form == NULL) {
        return;
    }

    control->form = form;

    if (form->control_index != NULL) {
        void **slot = hashmap_insert(form->control_index, control->node);
        if (slot != NULL) {
            *(struct form_control **)slot = control;
        }
    }

    if (form->controls != NULL) {
        assert(form->last_control);

        form->last_control->next = control;
        control->prev = form->last_control;
        control->next = NULL;
        form->last_control = control;
    } else {
        form->controls = form->last_control = control;
    }
}


/* exported interface documented in html/form_internal.h */
void form_free_control(struct form_control *control)
{
    struct form_control *c, *prev = NULL;
    assert(control != NULL);

    NSLOG(wisp, INFO, "Control:%p name:%p value:%p initial:%p", control, control->name, control->value,
        control->initial_value);
    free(control->name);
    free(control->value);
    free(control->initial_value);
    if (control->last_synced_value != NULL) {
        free(control->last_synced_value);
    }

    if (control->type == GADGET_SELECT) {
        struct form_option *option, *next;

        for (option = control->data.select.items; option; option = next) {
            next = option->next;
            NSLOG(wisp, INFO, "select option:%p text:%p value:%p", option, option->text, option->value);
            free(option->text);
            free(option->value);
            free(option);
        }
        if (control->data.select.menu != NULL) {
            form_free_select_menu(control);
        }
    }

    if (control->type == GADGET_TEXTAREA || control->type == GADGET_TEXTBOX || control->type == GADGET_PASSWORD) {

        if (control->data.text.initial != NULL) {
            dom_string_unref(control->data.text.initial);
        }

        if (control->data.text.ta != NULL) {
            textarea_destroy(control->data.text.ta);
        }
    }

    /* unlink the control from the form */
    if (control->form != NULL) {
        for (c = control->form->controls; c != NULL && c != control; c = c->next)
            prev = c;
        if (c != NULL) {
            if (prev != NULL)
                prev->next = control->next;
            else
                control->form->controls = control->next;
            if (control->next != NULL)
                control->next->prev = prev;
            if (control->form->last_control == control)
                control->form->last_control = prev;
        }

        /* Retirement must neither allocate nor clear a replacement's mapping. */
        if (control->form->control_index != NULL) {
            void **slot = hashmap_lookup(control->form->control_index, control->node);
            if (slot != NULL && *slot == control)
                hashmap_remove(control->form->control_index, control->node);
        }
    }

    if (control->node_value != NULL) {
        dom_string_unref(control->node_value);
    }

    if (control->box != NULL) {
        control->box->gadget = NULL;
    }

    dom_node_unref(control->node);
    free(control);
}


/* exported interface documented in html/form_internal.h */
bool form_add_option(struct form_control *control, char *value, char *text, bool selected, void *node)
{
    struct form_option *option;

    assert(control);
    assert(control->type == GADGET_SELECT);

    option = calloc(1, sizeof *option);
    if (!option)
        return false;

    option->value = value;
    option->text = text;

    /* add to linked list */
    if (control->data.select.items == 0)
        control->data.select.items = option;
    else
        control->data.select.last_item->next = option;
    control->data.select.last_item = option;

    /* set selected */
    if (selected && (control->data.select.num_selected == 0 || control->data.select.multiple)) {
        option->selected = option->initial_selected = true;
        control->data.select.num_selected++;
        control->data.select.current = option;
    }

    control->data.select.num_items++;

    option->node = node;

    return true;
}


/* exported interface documented in html/form_internal.h */
nserror form_open_select_menu(
    void *client_data, struct form_control *control, select_menu_redraw_callback callback, struct content *c)
{
    int line_height_with_spacing;
    struct box *box;
    plot_font_style_t fstyle;
    int total_height;
    struct form_select_menu *menu;
    html_content *html = (html_content *)c;
    nserror res;

    /* if the menu is opened for the first time */
    if (control->data.select.menu == NULL) {

        menu = calloc(1, sizeof(struct form_select_menu));
        if (menu == NULL) {
            return NSERROR_NOMEM;
        }

        control->data.select.menu = menu;

        box = control->box;

        menu->width = box->width + box->border[RIGHT].width + box->padding[RIGHT] + box->border[LEFT].width +
            box->padding[LEFT];

        font_plot_style_from_css(html, control->box->style, &fstyle);
        menu->f_size = fstyle.size;

        menu->line_height = FIXTOINT(
            FDIV((FMUL(FLTTOFIX(1.2), FMUL(html->unit_len_ctx.device_dpi, INTTOFIX(fstyle.size / PLOT_STYLE_SCALE)))),
                F_72));

        line_height_with_spacing = menu->line_height + menu->line_height * SELECT_LINE_SPACING;

        total_height = control->data.select.num_items * line_height_with_spacing;
        menu->height = total_height;

        if (menu->height > MAX_SELECT_HEIGHT) {
            menu->height = MAX_SELECT_HEIGHT;
        }

        menu->client_data = client_data;
        menu->callback = callback;
        res = scrollbar_create(false, menu->height, total_height, menu->height, control,
            form_select_menu_scroll_callback, &(menu->scrollbar));
        if (res != NSERROR_OK) {
            control->data.select.menu = NULL;
            free(menu);
            return res;
        }
        menu->c = c;
    } else {
        menu = control->data.select.menu;
    }

    menu->callback(client_data, 0, 0, menu->width, menu->height);

    return NSERROR_OK;
}


/* exported interface documented in html/form_internal.h */
void form_free_select_menu(struct form_control *control)
{
    if (control->data.select.menu->scrollbar != NULL)
        scrollbar_destroy(control->data.select.menu->scrollbar);
    free(control->data.select.menu);
    control->data.select.menu = NULL;
}


/* exported interface documented in html/form_internal.h */
bool form_redraw_select_menu(
    struct form_control *control, int x, int y, float scale, const struct rect *clip, const struct redraw_context *ctx)
{
    struct box *box;
    struct form_select_menu *menu = control->data.select.menu;
    struct form_option *option;
    int line_height, line_height_with_spacing;
    int width, height;
    int x0, y0, x1, scrollbar_x, y1, y2, y3;
    int item_y;
    int text_pos_offset, text_x;
    int scrollbar_width = SCROLLBAR_WIDTH;
    int i;
    int scroll;
    int x_cp, y_cp;
    struct rect r;
    struct rect rect;
    nserror res;

    box = control->box;

    x_cp = x;
    y_cp = y;
    width = menu->width;
    height = menu->height;
    line_height = menu->line_height;

    line_height_with_spacing = line_height + line_height * SELECT_LINE_SPACING;
    scroll = scrollbar_get_offset(menu->scrollbar);

    if (scale != 1.0) {
        x *= scale;
        y *= scale;
        width *= scale;
        height *= scale;
        scrollbar_width *= scale;

        i = scroll / line_height_with_spacing;
        scroll -= i * line_height_with_spacing;
        line_height *= scale;
        line_height_with_spacing *= scale;
        scroll *= scale;
        scroll += i * line_height_with_spacing;
    }


    x0 = x;
    y0 = y;
    x1 = x + width - 1;
    y1 = y + height - 1;
    scrollbar_x = x1 - scrollbar_width;

    r.x0 = x0;
    r.y0 = y0;
    r.x1 = x1 + 1;
    r.y1 = y1 + 1;
    res = ctx->plot->clip(ctx, &r);
    if (res != NSERROR_OK) {
        return false;
    }

    rect.x0 = x0;
    rect.y0 = y0;
    rect.x1 = x1;
    rect.y1 = y1;
    res = ctx->plot->rectangle(ctx, plot_style_stroke_darkwbasec, &rect);
    if (res != NSERROR_OK) {
        return false;
    }

    x0 = x0 + SELECT_BORDER_WIDTH;
    y0 = y0 + SELECT_BORDER_WIDTH;
    x1 = x1 - SELECT_BORDER_WIDTH;
    y1 = y1 - SELECT_BORDER_WIDTH;
    height = height - 2 * SELECT_BORDER_WIDTH;

    r.x0 = x0;
    r.y0 = y0;
    r.x1 = x1 + 1;
    r.y1 = y1 + 1;
    res = ctx->plot->clip(ctx, &r);
    if (res != NSERROR_OK) {
        return false;
    }

    res = ctx->plot->rectangle(ctx, plot_style_fill_lightwbasec, &r);
    if (res != NSERROR_OK) {
        return false;
    }

    option = control->data.select.items;
    item_y = line_height_with_spacing;

    while (item_y < scroll) {
        option = option->next;
        item_y += line_height_with_spacing;
    }
    item_y -= line_height_with_spacing;
    text_pos_offset = y - scroll + (int)(line_height * (0.75 + SELECT_LINE_SPACING));
    text_x = x + (box->border[LEFT].width + box->padding[LEFT]) * scale;

    plot_fstyle_entry.size = menu->f_size;

    while (option && item_y - scroll < height) {

        if (option->selected) {
            y2 = y + item_y - scroll;
            y3 = y + item_y + line_height_with_spacing - scroll;

            rect.x0 = x0;
            rect.y0 = y0 > y2 ? y0 : y2;
            rect.x1 = scrollbar_x + 1;
            rect.y1 = y3 < y1 + 1 ? y3 : y1 + 1;
            res = ctx->plot->rectangle(ctx, &plot_style_fill_selected, &rect);
            if (res != NSERROR_OK) {
                return false;
            }
        }

        y2 = text_pos_offset + item_y;
        res = ctx->plot->text(ctx, &plot_fstyle_entry, text_x, y2, option->text, strlen(option->text));
        if (res != NSERROR_OK) {
            return false;
        }

        item_y += line_height_with_spacing;
        option = option->next;
    }

    res = scrollbar_redraw(menu->scrollbar, x_cp + menu->width - SCROLLBAR_WIDTH, y_cp, clip, scale, ctx);
    if (res != NSERROR_OK) {
        return false;
    }

    return true;
}


/* private interface described in html/form_internal.h */
bool form_clip_inside_select_menu(struct form_control *control, float scale, const struct rect *clip)
{
    struct form_select_menu *menu = control->data.select.menu;
    int width, height;


    width = menu->width;
    height = menu->height;

    if (scale != 1.0) {
        width *= scale;
        height *= scale;
    }

    if (clip->x0 >= 0 && clip->x1 <= width && clip->y0 >= 0 && clip->y1 <= height)
        return true;

    return false;
}


/* exported interface documented in netsurf/form.h */
nserror form_select_process_selection(struct form_control *control, int item)
{
    assert(control != NULL);

    return form__select_process_selection(control->html, control, item);
}


/* exported interface documented in netsurf/form.h */
struct form_option *form_select_get_option(struct form_control *control, int item)
{
    struct form_option *opt;

    opt = control->data.select.items;
    while ((opt != NULL) && (item > 0)) {
        opt = opt->next;
        item--;
    }
    return opt;
}


/* exported interface documented in netsurf/form.h */
char *form_control_get_name(struct form_control *control)
{
    return control->name;
}


/* exported interface documented in netsurf/form.h */
nserror form_control_bounding_rect(struct form_control *control, struct rect *r)
{
    box_bounds(control->box, r);
    return NSERROR_OK;
}


/* private interface described in html/form_internal.h */
const char *form_select_mouse_action(struct form_control *control, browser_mouse_state mouse, int x, int y)
{
    struct form_select_menu *menu = control->data.select.menu;
    int x0, y0, x1, y1, scrollbar_x;
    const char *status = NULL;
    bool multiple = control->data.select.multiple;

    x0 = 0;
    y0 = 0;
    x1 = menu->width;
    y1 = menu->height;
    scrollbar_x = x1 - SCROLLBAR_WIDTH;

    if (menu->scroll_capture || (x > scrollbar_x && x < x1 && y > y0 && y < y1)) {
        /* The scroll is currently capturing all events or the mouse
         * event is taking place on the scrollbar widget area
         */
        x -= scrollbar_x;
        return scrollbar_mouse_status_to_message(scrollbar_mouse_action(menu->scrollbar, mouse, x, y));
    }


    if (x > x0 && x < scrollbar_x && y > y0 && y < y1) {
        /* over option area */

        if (mouse & (BROWSER_MOUSE_CLICK_1 | BROWSER_MOUSE_CLICK_2))
            /* button 1 or 2 click */
            form_select_menu_clicked(control, x, y);

        if (!(mouse & BROWSER_MOUSE_CLICK_1 && !multiple))
            /* anything but a button 1 click over a single select
               menu */
            status = messages_get(control->data.select.multiple ? "SelectMClick" : "SelectClick");

    } else if (!(mouse & (BROWSER_MOUSE_CLICK_1 | BROWSER_MOUSE_CLICK_2)))
        /* if not a button 1 or 2 click*/
        status = messages_get("SelectClose");

    return status;
}


/* private interface described in html/form_internal.h */
void form_select_mouse_drag_end(struct form_control *control, browser_mouse_state mouse, int x, int y)
{
    int x0, y0, x1, y1;
    int box_x, box_y;
    struct box *box;
    struct form_select_menu *menu = control->data.select.menu;

    box = control->box;

    /* Get global coords of scrollbar */
    box_coords(box, &box_x, &box_y);
    box_x -= box->border[LEFT].width;
    box_y += box->height + box->border[BOTTOM].width + box->padding[BOTTOM] + box->padding[TOP];

    /* Get drag end coords relative to scrollbar */
    x = x - box_x;
    y = y - box_y;

    if (menu->scroll_capture) {
        x -= menu->width - SCROLLBAR_WIDTH;
        scrollbar_mouse_drag_end(menu->scrollbar, mouse, x, y);
        return;
    }

    x0 = 0;
    y0 = 0;
    x1 = menu->width;
    y1 = menu->height;


    if (x > x0 && x < x1 - SCROLLBAR_WIDTH && y > y0 && y < y1) {
        /* handle drag end above the option area like a regular click */
        form_select_menu_clicked(control, x, y);
    }
}


/* private interface described in html/form_internal.h */
void form_select_get_dimensions(struct form_control *control, int *width, int *height)
{
    *width = control->data.select.menu->width;
    *height = control->data.select.menu->height;
}


/* private interface described in html/form_internal.h */
void form_select_menu_callback(void *client_data, int x, int y, int width, int height)
{
    html_content *html = client_data;
    int menu_x, menu_y;
    struct box *box;

    box = html->visible_select_menu->box;
    box_coords(box, &menu_x, &menu_y);

    menu_x -= box->border[LEFT].width;
    menu_y += box->height + box->border[BOTTOM].width + box->padding[BOTTOM] + box->padding[TOP];
    content__request_redraw((struct content *)html, menu_x + x, menu_y + y, width, height);
}


/* A request owns all data used after choosing a target, which may call the
 * frontend and retire the source page. No rendered form/control is consulted. */
struct form_submission {
    nsurl *url;
    nsurl *referrer;
    char *target;
    char *encoded;
    struct fetch_multipart_data *entries;
    form_method method;
};

static void form_submission_fini(struct form_submission *request)
{
    if (request->url != NULL)
        nsurl_unref(request->url);
    if (request->referrer != NULL)
        nsurl_unref(request->referrer);
    free(request->target);
    free(request->encoded);
    fetch_multipart_data_destroy(request->entries);
}

static bool form_string_is(dom_string *value, const char *keyword)
{
    return value != NULL && dom_string_byte_length(value) == strlen(keyword) &&
        ascii_strings_equal_caseless(dom_string_data(value), keyword);
}

static dom_exception form_submission_attribute(dom_node *form, dom_node *submitter,
    dom_string *attribute, dom_string *override, dom_string **value)
{
    *value = NULL;
    dom_exception err = submitter == NULL ? DOM_NO_ERR : dom_element_get_attribute(submitter, override, value);
    if (err == DOM_NO_ERR && *value == NULL)
        err = dom_element_get_attribute(form, attribute, value);
    return err;
}

static nserror form_action_url(html_content *html, dom_node *form, dom_node *submitter, nsurl **url)
{
    dom_string *action = NULL;
    *url = NULL;
    if (form_submission_attribute(form, submitter,
            corestring_dom_action, corestring_dom_formaction, &action) != DOM_NO_ERR)
        return NSERROR_DOM;
    nserror error = NSERROR_OK;
    if (action == NULL || dom_string_byte_length(action) == 0)
        *url = nsurl_ref(content_get_url(&html->base));
    else
        error = html_link_url(action, html->base_url, url);
    dom_string_unref(action);
    return error == NSERROR_NOMEM ? error : NSERROR_OK; /* Invalid URL: no navigation. */
}

static dom_exception form_submitter_owner(dom_node *submitter, dom_html_form_element **form)
{
    dom_html_element_type tag;
    *form = NULL;
    dom_exception err = dom_html_element_get_tag_type(submitter, &tag);
    if (err != DOM_NO_ERR)
        return err;
    if (tag == DOM_HTML_ELEMENT_TYPE_INPUT)
        return dom_html_input_element_get_form((dom_html_input_element *)submitter, form);
    if (tag == DOM_HTML_ELEMENT_TYPE_BUTTON)
        return dom_html_button_element_get_form((dom_html_button_element *)submitter, form);
    return DOM_NO_ERR;
}

nserror form_submit_url(html_content *html, dom_node *submitter, nsurl **url)
{
    *url = NULL;
    dom_html_form_element *form = NULL;
    if (form_submitter_owner(submitter, &form) != DOM_NO_ERR)
        return NSERROR_DOM;
    nserror error = form == NULL ? NSERROR_OK : form_action_url(html, (dom_node *)form, submitter, url);
    dom_node_unref(form);
    return error;
}

static nserror form_submission_prepare(html_content *html, dom_node *form, dom_node *submitter,
    struct form_submission *request)
{
    dom_string *method = NULL, *enctype = NULL, *target = NULL;
    nserror error = NSERROR_DOM;
    if (form_submission_attribute(form, submitter,
            corestring_dom_method, corestring_dom_formmethod, &method) != DOM_NO_ERR ||
        form_submission_attribute(form, submitter,
            corestring_dom_enctype, corestring_dom_formenctype, &enctype) != DOM_NO_ERR ||
        (submitter != NULL && dom_element_get_attribute(submitter, corestring_dom_formtarget, &target) != DOM_NO_ERR))
        goto out;
    /* Dialog closing and text/plain transport need their own implementations;
     * neither may silently turn into a GET or a differently encoded POST. */
    if (form_string_is(method, "dialog")) {
        error = NSERROR_OK;
        goto out;
    }
    request->method = method_GET;
    if (form_string_is(method, "post")) {
        if (form_string_is(enctype, "text/plain")) {
            error = NSERROR_NOT_IMPLEMENTED;
            goto out;
        }
        request->method = form_string_is(enctype, "multipart/form-data") ? method_POST_MULTIPART : method_POST_URLENC;
    }
    request->referrer = nsurl_ref(content_get_url(&html->base));
    error = form_action_url(html, form, submitter, &request->url);
    if (error != NSERROR_OK || request->url == NULL)
        goto out;
    error = html_element_target(html->document, form, target, &request->target);
    if (error != NSERROR_OK)
        goto out;
    dom_string *accept = NULL;
    if (dom_html_form_element_get_accept_charset((dom_html_form_element *)form, &accept) != DOM_NO_ERR) {
        error = NSERROR_DOM;
        goto out;
    }
    /* Legacy encoding-label selection remains isolated from Unicode entries. */
    char *charset = form_acceptable_charset(accept == NULL ? NULL : dom_string_data(accept), html->encoding);
    dom_string_unref(accept);
    if (charset == NULL) {
        error = NSERROR_NOMEM;
        goto out;
    }
    struct html_form_data data = {0};
    error = html_form_data_construct((dom_html_form_element *)form, submitter, &data);
    if (error == NSERROR_OK) {
        error = request->method == method_POST_MULTIPART ?
            html_form_data_multipart(&data, charset, &request->entries) :
            html_form_data_urlencode(&data, charset, &request->encoded);
    }
    html_form_data_fini(&data);
    free(charset);
    if (error == NSERROR_OK && request->method == method_GET) {
        nsurl *url = NULL;
        error = nsurl_replace_query(request->url, request->encoded, &url);
        if (error == NSERROR_OK) {
            nsurl_unref(request->url);
            request->url = url;
        }
    }
out:
    dom_string_unref(method);
    dom_string_unref(enctype);
    dom_string_unref(target);
    return error;
}

static bool form_in_document(dom_node *form, dom_document *document)
{
    dom_node *root = dom_node_ref(form), *parent = NULL;
    while (dom_node_get_parent_node(root, &parent) == DOM_NO_ERR && parent != NULL) {
        dom_node_unref(root);
        root = parent;
    }
    bool connected = root == (dom_node *)document;
    dom_node_unref(root);
    return connected;
}

/* The caller pins the content and retains DOM identity across click dispatch.
 * This stage has no page callbacks until target selection/navigation. Validation
 * and SubmitEvent/FormDataEvent sequencing will be added before JS input delivery. */
static nserror form_submit_dom(html_content *html, dom_node *form, dom_node *submitter, browser_mouse_state mouse)
{
    struct browser_window *bw = html->bw;
    uint64_t generation = html->input_generation;
    if (bw == NULL || !form_in_document(form, html->document))
        return NSERROR_OK;
    struct form_submission request = {0};
    nserror error = form_submission_prepare(html, form, submitter, &request);
    if (error == NSERROR_OK && request.url != NULL) {
        struct browser_window *target = browser_window_find_target(bw, request.target, mouse);
        if (target != NULL && html->bw == bw && html->input_generation == generation) {
            error = browser_window_navigate(target, request.url, request.referrer, BW_NAVIGATE_HISTORY,
                request.method == method_POST_URLENC ? request.encoded : NULL,
                request.method == method_POST_MULTIPART ? request.entries : NULL, NULL);
        }
    }
    form_submission_fini(&request);
    return error;
}

/* private interface described in html/form_internal.h */
nserror form_submit(html_content *html, dom_node *submitter, browser_mouse_state mouse)
{
    if (!html_form_is_submit_button(submitter) || html_form_control_disabled(submitter))
        return NSERROR_OK;
    dom_html_form_element *form = NULL;
    dom_exception err = form_submitter_owner(submitter, &form);
    nserror result = err == DOM_NO_ERR ? NSERROR_OK : NSERROR_DOM;
    if (err == DOM_NO_ERR && form != NULL)
        result = form_submit_dom(html, (dom_node *)form, submitter, mouse);
    dom_node_unref(form);
    return result;
}

/* HTML implicit submission chooses the first submit button before dispatch.
 * A disabled first button still blocks later buttons. Without a submit button,
 * only forms with at most one blocking text-like input may submit. */
static nserror form_implicit_button(dom_html_form_element *form, dom_node **button, unsigned *blockers)
{
    dom_html_form_control_list controls = {0};
    *button = NULL;
    *blockers = 0;
    dom_exception err = dom_html_form_element_get_submittable_elements(form, &controls);
    for (size_t i = 0; err == DOM_NO_ERR && i < controls.length; i++) {
        dom_node *element = controls.items[i];
        if (html_form_is_submit_button(element)) {
            *button = dom_node_ref(element);
            break;
        }
        dom_html_element_type tag;
        err = dom_html_element_get_tag_type(element, &tag);
        if (err != DOM_NO_ERR)
            break;
        if (tag == DOM_HTML_ELEMENT_TYPE_INPUT) {
            dom_string *type = NULL;
            err = dom_html_input_element_get_type((dom_html_input_element *)element, &type);
            /* Unknown and missing input types are Text. These are the states
             * that do NOT block; every other implemented/unknown state does. */
            const char *nonblocking[] = {"hidden", "checkbox", "radio", "file", "submit", "image", "reset",
                "button", "range", "color"};
            bool blocks = true;
            for (size_t j = 0; j < sizeof(nonblocking) / sizeof(nonblocking[0]); j++)
                if (form_string_is(type, nonblocking[j]))
                    blocks = false;
            *blockers += blocks;
            dom_string_unref(type);
        }
    }
    dom_html_form_control_list_fini(&controls);
    return err == DOM_NO_ERR ? NSERROR_OK : err == DOM_NO_MEM_ERR ? NSERROR_NOMEM : NSERROR_DOM;
}

nserror form_submit_implicit(html_content *html, dom_node *input)
{
    struct browser_window *bw = html->bw;
    uint64_t generation = html->input_generation;
    if (bw == NULL || html_form_control_disabled(input))
        return NSERROR_OK;
    dom_html_form_element *form = NULL;
    dom_node *button = NULL;
    unsigned blockers = 0;
    nserror result = NSERROR_OK;
    if (dom_html_input_element_get_form((dom_html_input_element *)input, &form) != DOM_NO_ERR)
        return NSERROR_DOM;
    if (form == NULL)
        return NSERROR_OK;
    if (!content_add_user(&html->base, NULL, &generation)) {
        dom_node_unref(form);
        return NSERROR_NOMEM;
    }
    result = form_implicit_button(form, &button, &blockers);
    if (result == NSERROR_OK && button != NULL && !html_form_control_disabled(button)) {
        struct image_input_coords *coords = NULL;
        dom_node_get_user_data(button, corestring_dom___ns_key_image_coords_node_data, &coords);
        if (coords != NULL)
            *coords = (struct image_input_coords){0};
        struct html_form_click_result click = html_form_click(button, NULL, NULL);
        if (click.accepted && html->bw == bw && html->input_generation == generation)
            result = form_submit(html, button, BROWSER_MOUSE_CLICK_1);
    } else if (result == NSERROR_OK && button == NULL && blockers <= 1) {
        result = form_submit_dom(html, (dom_node *)form, NULL, BROWSER_MOUSE_CLICK_1);
    }
    dom_node_unref(button);
    dom_node_unref(form);
    content_remove_user(&html->base, NULL, &generation);
    return result;
}


/* Both live edits and later gadget synchronization write selector-visible DOM
 * state. Notice edits now: a subsequent restyle may have nothing left to sync. */
static dom_exception form_set_dom_value(struct form_control *control, dom_string *value)
{
    dom_string *old = NULL;
    dom_exception err = control->type == GADGET_TEXTAREA ? dom_node_get_text_content(control->node, &old)
                                                         : dom_html_input_element_get_value(control->node, &old);
    bool changed = err != DOM_NO_ERR || !dom_string_isequal(old, value);
    if (!changed && control->type == GADGET_TEXTAREA) {
        dom_node *child = NULL, *next = NULL;
        dom_node_type type;
        /* String replace all leaves no children for empty text and exactly one
         * Text node otherwise. Match that structure when checking for changes. */
        changed = dom_node_get_first_child(control->node, &child) != DOM_NO_ERR;
        if (!changed && (value == NULL || dom_string_byte_length(value) == 0))
            changed = child != NULL;
        else if (!changed)
            changed = child == NULL || dom_node_get_node_type(child, &type) != DOM_NO_ERR || type != DOM_TEXT_NODE ||
                dom_node_get_next_sibling(child, &next) != DOM_NO_ERR || next != NULL;
        dom_node_unref(next);
        dom_node_unref(child);
    }
    /* Mark before writing: a failing setter may have partly changed the DOM. */
    if (changed)
        html_restyle_note_dom_change(control->html);
    dom_string_unref(old);
    if (control->type == GADGET_TEXTAREA)
        return dom_html_text_area_element_set_value(control->node, value);
    return dom_html_input_element_set_value(control->node, value);
}

/* exported interface documented in html/form_internal.h */
void form_gadget_update_value(struct form_control *control, char *value)
{
    switch (control->type) {
    case GADGET_HIDDEN:
    case GADGET_TEXTBOX:
    case GADGET_TEXTAREA:
    case GADGET_PASSWORD:
    case GADGET_FILE:
        if (control->value != NULL) {
            free(control->value);
        }
        control->value = value;
        if (control->node != NULL) {
            dom_exception err;
            dom_string *str;
            err = dom_string_create((uint8_t *)value, strlen(value), &str);
            if (err == DOM_NO_ERR) {
                err = form_set_dom_value(control, str);
                dom_string_unref(str);
            }
        }
        break;
    default:
        /* Do nothing */
        break;
    }

    /* Finally, sync this with the DOM */
    form_gadget_sync_with_dom(control);
}


/* Exported API, see html/form_internal.h */
void form_gadget_sync_with_dom(struct form_control *control)
{
    dom_exception exc;
    dom_string *value = NULL;
    bool changed_dom = false;

    if (control->syncing ||
        (control->type != GADGET_TEXTBOX && control->type != GADGET_PASSWORD && control->type != GADGET_HIDDEN &&
            control->type != GADGET_TEXTAREA)) {
        /* Not a control we support, or the control is already
         * mid-sync so we don't want to disrupt that
         */
        return;
    }

    control->syncing = true;

    /* If we've changed value, sync that toward the DOM */
    if ((control->last_synced_value == NULL && control->value != NULL && control->value[0] != '\0') ||
        (control->last_synced_value != NULL && control->value != NULL &&
            strcmp(control->value, control->last_synced_value) != 0)) {
        char *dup = strdup(control->value);
        if (dup == NULL) {
            goto out;
        }
        if (control->last_synced_value != NULL) {
            free(control->last_synced_value);
        }
        control->last_synced_value = dup;
        exc = dom_string_create((uint8_t *)(control->value), strlen(control->value), &value);
        if (exc != DOM_NO_ERR) {
            goto out;
        }
        if (control->node_value != NULL) {
            dom_string_unref(control->node_value);
        }
        control->node_value = value;
        value = NULL;
        exc = form_set_dom_value(control, control->node_value);
        if (exc != DOM_NO_ERR) {
            goto out;
        }
        changed_dom = true;
    }

    /* Now check if the DOM has changed since our last go */
    if (control->type == GADGET_TEXTAREA) {
        exc = dom_html_text_area_element_get_value(control->node, &value);
    } else {
        exc = dom_html_input_element_get_value(control->node, &value);
    }

    if (exc != DOM_NO_ERR) {
        /* Nothing much we can do here */
        goto out;
    }

    if (!dom_string_isequal(control->node_value, value)) {
        /* The DOM has changed */
        if (!changed_dom) {
            /* And it wasn't us */
            char *value_s = value == NULL ? strdup("") : strndup(dom_string_data(value), dom_string_byte_length(value));
            char *dup = NULL;
            if (value_s == NULL) {
                goto out;
            }
            dup = strdup(value_s);
            if (dup == NULL) {
                free(value_s);
                goto out;
            }
            free(control->value);
            control->value = value_s;
            free(control->last_synced_value);
            control->last_synced_value = dup;
            if (control->type != GADGET_HIDDEN && control->data.text.ta != NULL) {
                textarea_set_text(control->data.text.ta, value_s);
            }
        }
        dom_string_unref(control->node_value);
        control->node_value = value;
        value = NULL;
    }

out:
    if (value != NULL)
        dom_string_unref(value);
    control->syncing = false;
}


static void *map_key_clone(void *key)
{
    /* The control retains its node; the index borrows that stable identity.
     * Keep stored and lookup keys in the same representation. */
    return key;
}
static void map_key_destroy(void *key)
{
    (void)key;
}
static uint32_t map_key_hash(void *key)
{
    uintptr_t v = (uintptr_t)key;
    return (uint32_t)((v >> 4) ^
#if UINTPTR_MAX > 0xffffffff
        (v >> 32) ^
#endif
        v);
}
static bool map_key_eq(void *a, void *b)
{
    return a == b;
}
static void *map_value_alloc(void *key)
{
    return malloc(sizeof(void *));
}
static void map_value_destroy(void *value)
{
    free(value);
}
static hashmap_parameters_t form_node_map_params = {
    map_key_clone, map_key_hash, map_key_eq, map_key_destroy, map_value_alloc, map_value_destroy};

/* exported interface documented in html/form_internal.h */
hashmap_t *form_control_index_create(void)
{
    return hashmap_create(&form_node_map_params);
}

/* exported interface documented in html/form_internal.h */
struct form *form_new(void *node)
{
    struct form *form = calloc(1, sizeof(*form));
    if (form == NULL)
        return NULL;
    form->node = dom_node_ref(node);
    form->control_index = form_control_index_create();
    return form;
}


/* exported interface documented in html/form_internal.h */
void form_free(struct form *form)
{
    struct form_control *c, *d;

    for (c = form->controls; c != NULL; c = d) {
        d = c->next;

        form_free_control(c);
    }

    dom_node_unref(form->node);

    if (form->control_index != NULL) {
        hashmap_destroy(form->control_index);
        form->control_index = NULL;
    }

    free(form);
}


/* exported interface documented in html/form_internal.h */
struct form_control *form_new_control(void *node, form_control_type type)
{
    struct form_control *control;

    control = calloc(1, sizeof *control);
    if (control == NULL)
        return NULL;

    control->node = dom_node_ref(node);
    control->type = type;

    return control;
}

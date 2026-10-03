/* SPDX-License-Identifier: GPL-2.0-only */
#include <stdlib.h>
#include <string.h>

#include <wisp/content/handlers/html/box.h>
#include <wisp/utils/string.h>
#include "content/handlers/html/box_construct.h"
#include "content/handlers/html/content_visibility.h"
#include "content/handlers/html/search.h"

/* Source identity and normalized character range survive destruction of every
 * layout box. This allocation exists only while the find bar holds matches. */
struct html_search_match {
    dom_node *node;
    char *text;
    size_t start, end;
};

struct html_search_walk {
    html_content *html;
    struct textsearch_context *context;
    const char *pattern;
    int length;
    bool sensitive;
};

void html_textsearch_free_match(void *data)
{
    struct html_search_match *match = data;
    dom_node_unref(match->node);
    free(match->text);
    free(match);
}

static char *search_text(dom_node *node, const css_computed_style *style)
{
    dom_string *data = NULL;
    if (dom_characterdata_get_data(node, &data) != DOM_NO_ERR || data == NULL)
        return NULL;
    uint8_t space = css_computed_white_space(style);
    char *text = space == CSS_WHITE_SPACE_NORMAL || space == CSS_WHITE_SPACE_NOWRAP
        ? squash_whitespace(dom_string_data(data))
        : strdup(dom_string_data(data));
    dom_string_unref(data);
    if (text != NULL) {
        for (char *p = text; *p != 0; p++) {
            if (*p == '\t')
                *p = ' ';
        }
        box_text_transform(text, strlen(text), css_computed_text_transform(style));
    }
    return text;
}

static nserror search_node(struct html_search_walk *walk, dom_node *node, const css_computed_style *parent)
{
    dom_node_type type;
    if (dom_node_get_node_type(node, &type) != DOM_NO_ERR)
        return NSERROR_DOM;
    if (type == DOM_TEXT_NODE) {
        if (parent == NULL || css_computed_visibility(parent) != CSS_VISIBILITY_VISIBLE)
            return NSERROR_OK;
        char *text = search_text(node, parent);
        if (text == NULL)
            return NSERROR_NOMEM;
        size_t offset = 0, length = strlen(text);
        while (offset < length) {
            unsigned matched;
            const char *found = content_textsearch_find_pattern(
                text + offset, length - offset, walk->pattern, walk->length, walk->sensitive, &matched);
            if (found == NULL)
                break;
            struct html_search_match *match = calloc(1, sizeof(*match));
            if (match == NULL) {
                free(text);
                return NSERROR_NOMEM;
            }
            match->node = dom_node_ref(node);
            match->text = strdup(text);
            match->start = found - text;
            match->end = match->start + matched;
            nserror error = match->text == NULL ? NSERROR_NOMEM
                                                : content_textsearch_add_deferred_match(walk->context, match);
            if (error != NSERROR_OK) {
                html_textsearch_free_match(match);
                free(text);
                return error;
            }
            offset = match->end;
        }
        free(text);
        return NSERROR_OK;
    }
    if (type != DOM_ELEMENT_NODE)
        return NSERROR_OK;

    struct box *box = box_for_node(node);
    css_select_results *selected = NULL;
    const css_computed_style *style;
    if (box != NULL) {
        style = box->style;
    } else {
        selected = html_visibility_style(walk->html, node, parent, walk->html->layout->style);
        if (selected == NULL)
            return NSERROR_NOMEM;
        style = selected->styles[CSS_PSEUDO_ELEMENT_NONE];
    }
    bool skipped = css_computed_content_visibility(style) == CSS_CONTENT_VISIBILITY_HIDDEN &&
        html_content_visibility_applies(style, box != NULL && (box->flags & IS_REPLACED));
    nserror error = NSERROR_OK;
    dom_node *child = NULL;
    if (css_computed_display(style, false) == CSS_DISPLAY_NONE || (skipped && !html_until_found(node)))
        goto done;
    if (dom_node_get_first_child(node, &child) != DOM_NO_ERR) {
        error = NSERROR_DOM;
        goto done;
    }
    while (child != NULL) {
        dom_node *next = NULL;
        error = search_node(walk, child, style);
        if (error == NSERROR_OK && dom_node_get_next_sibling(child, &next) != DOM_NO_ERR)
            error = NSERROR_DOM;
        dom_node_unref(child);
        child = next;
        if (error != NSERROR_OK)
            break;
    }
done:
    dom_node_unref(child);
    if (selected != NULL)
        css_select_results_destroy(selected);
    return error;
}

nserror html_textsearch_dom(
    struct content *c, struct textsearch_context *context, const char *pattern, int length, bool sensitive)
{
    html_content *html = (html_content *)c;
    if (html->restyling || html->restyle_pending || html->layout == NULL)
        return NSERROR_NEED_DATA;
    dom_node *root = NULL;
    if (dom_document_get_document_element(html->document, &root) != DOM_NO_ERR || root == NULL)
        return NSERROR_DOM;
    struct html_search_walk walk = {html, context, pattern, length, sensitive};
    nserror error = search_node(&walk, root, NULL);
    dom_node_unref(root);
    return error;
}

/* Map normalized source offsets to the current line fragments. Layout may
 * have consumed leading/trailing spaces or split a match across several boxes.
 * The source node is shared by all fragments, so no original box is retained. */
static void
search_range(struct box *box, const struct html_search_match *match, size_t *cursor, struct textsearch_range *range)
{
    if (box->text_node == match->node && box->text != NULL && box->length != 0) {
        size_t length = strlen(match->text), offset = *cursor;
        while (offset + box->length <= length && memcmp(match->text + offset, box->text, box->length) != 0)
            offset++;
        if (offset + box->length <= length) {
            size_t end = offset + box->length;
            if (match->start < end && match->end > offset) {
                if (range->start_box == NULL) {
                    range->start_box = box;
                    range->start_idx = box->byte_offset + (match->start > offset ? match->start - offset : 0);
                }
                range->end_box = box;
                range->end_idx = box->byte_offset + (match->end < end ? match->end - offset : box->length);
                range->available = match->end <= end;
            }
            *cursor = end;
        }
    }
    for (struct box *child = box->children; child != NULL; child = child->next)
        search_range(child, match, cursor, range);
}

nserror html_textsearch_resolve(struct content *c, void *data, bool activate, struct textsearch_range *range)
{
    html_content *html = (html_content *)c;
    struct html_search_match *match = data;
    if (html->restyling || html->restyle_pending || html->layout == NULL)
        return NSERROR_NEED_DATA;
    if (activate) {
        bool changed;
        nserror error = html_reveal_ancestors(html, match->node, &changed);
        if (error != NSERROR_OK)
            return error;
        if (changed)
            return NSERROR_NEED_DATA;
    }
    size_t cursor = 0;
    search_range(html->layout, match, &cursor, range);
    return NSERROR_OK;
}

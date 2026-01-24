/*
 * Copyright 2025 Marius
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

/* Block utils.h inclusion to mock inline functions */
#define NETSURF_CSS_UTILS_H_

#include <check.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <libcss/libcss.h>
#include "neosurf/css.h"
#include "neosurf/types.h"
#include "neosurf/utils/config.h"
#include "neosurf/utils/errors.h"
#include "neosurf/utils/log.h"
/* We must provide definitions that utils.h would have provided if used */
css_fixed nscss_screen_dpi = 0;

#include <dom/core/implementation.h>
#include <dom/core/string.h>
#include <libcss/fpmath.h> /* Fixed css_fixed error */
#include "utils/libdom.h"
#include "content/handlers/css/select.h"
#include "content/handlers/html/box_normalise.h"
#include "neosurf/content/handlers/html/box.h"
#include "neosurf/content/handlers/html/private.h"
#include "neosurf/desktop/gui_internal.h"
#include "neosurf/misc.h"
#include "neosurf/utf8.h"
#include "neosurf/utils/nsoption.h"

/* Mock corestrings (Must be visible before mocks uses them) */
#include "neosurf/utils/corestrings.h"
struct dom_string *corestring_dom_id;
struct dom_string *corestring_dom_title;
struct dom_string *corestring_dom_style;
struct dom_string *corestring_dom_colspan;
struct dom_string *corestring_dom_rowspan;
struct dom_string *corestring_dom___ns_key_box_node_data;

/* Mock nsoption */
struct nsoption_s nsoptions_storage[1000];
struct nsoption_s *nsoptions = nsoptions_storage;

/* Helper to log errors */
static void test_log(const char *fmt, va_list args)
{
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n");
}

/* -------------------------------------------------------------------------- */
/* MOCKS for box_construct.c */

/* We will identify styles by pointer equality using storage addresses */
/* Large buffers to mimic opaque css_computed_style */
static uint8_t mock_style_block_storage[4096];
static uint8_t mock_style_grid_storage[4096];

static css_computed_style *MOCK_STYLE_BLOCK = (css_computed_style *)mock_style_block_storage;
static css_computed_style *MOCK_STYLE_GRID = (css_computed_style *)mock_style_grid_storage;
static uint8_t mock_style_inline_storage[4096];
static css_computed_style *MOCK_STYLE_INLINE = (css_computed_style *)mock_style_inline_storage;

/* Mock nscss_get_style */
css_select_results *nscss_get_style(nscss_select_ctx *ctx, dom_node *node, const css_media *media,
    const css_unit_ctx *unit_len_ctx, const css_stylesheet *inline_style)
{
    /* Determine style based on node name or attribute?
       Parsing from file gives real nodes.
       Let's use "id" attribute.
    */
    dom_string *id_attr = NULL;
    dom_element_get_attribute(node, corestring_dom_id, &id_attr);

    css_computed_style *style_to_return = MOCK_STYLE_BLOCK;

    if (id_attr) {
        const char *id_val = dom_string_data(id_attr);
        if (id_val && strcmp(id_val, "grid") == 0) {
            style_to_return = MOCK_STYLE_GRID;
        }
        dom_string_unref(id_attr);
    }
    fprintf(stderr, "DEBUG: nscss_get_style called\n");
    css_select_results *res = calloc(1, sizeof(css_select_results));
    res->styles[CSS_PSEUDO_ELEMENT_NONE] = style_to_return;
    return res;
}

/* Mock css getters matching utils.h signatures and direct calls */

uint8_t ns_computed_display(const css_computed_style *style, bool root)
{
    if (style == MOCK_STYLE_GRID)
        return CSS_DISPLAY_GRID;
    if (style == MOCK_STYLE_INLINE)
        return CSS_DISPLAY_INLINE;
    return CSS_DISPLAY_BLOCK;
}

uint8_t ns_computed_display_static(const css_computed_style *style)
{
    if (style == MOCK_STYLE_GRID)
        return CSS_DISPLAY_GRID;
    return CSS_DISPLAY_BLOCK;
}

uint8_t css_computed_position(const css_computed_style *style)
{
    return CSS_POSITION_STATIC;
}
uint8_t css_computed_float(const css_computed_style *style)
{
    return CSS_FLOAT_NONE;
}
uint8_t css_computed_list_style_type(const css_computed_style *style)
{
    return CSS_LIST_STYLE_TYPE_NONE;
}
uint8_t css_computed_list_style_image(const css_computed_style *style, lwc_string **url)
{
    *url = NULL;
    return CSS_LIST_STYLE_IMAGE_NONE;
}
uint8_t css_computed_background_image(const css_computed_style *style, lwc_string **url)
{
    *url = NULL;
    return CSS_BACKGROUND_IMAGE_NONE;
}

css_computed_style *
nscss_get_blank_style(nscss_select_ctx *ctx, const css_unit_ctx *unit_len_ctx, const css_computed_style *parent_style)
{
    return MOCK_STYLE_BLOCK;
}

css_error css_select_results_destroy(css_select_results *results)
{
    free(results);
    return CSS_OK;
}

/* Extended Mocks for Linker Satisfaction */

/* Typedefs */
typedef struct nsurl nsurl;

/* Box Functions */
struct box *box_create(css_select_results *styles, css_computed_style *style, bool style_owned, struct nsurl *href,
    const char *target, const char *title, lwc_string *id, void *context)
{
    struct box *b = calloc(1, sizeof(struct box));
    fprintf(stderr, "DEBUG: box_create called returning %p\n", b);
    b->style = style;
    b->styles = styles;
    /* Initialize basic fields */
    return b;
}

void box_free(struct box *box)
{
    if (!box)
        return;
    /* Free styles if we own them */
    if (box->styles) {
        css_select_results_destroy(box->styles);
    }
    /* Free text if any */
    if (box->text) {
        free(box->text);
    }
    free(box);
}

/* Recursively free box tree */
static void box_free_tree(struct box *box)
{
    if (!box)
        return;
    struct box *child = box->children;
    while (child) {
        struct box *next = child->next;
        box_free_tree(child);
        child = next;
    }
    box_free(box);
}

void box_add_child(struct box *parent, struct box *child)
{
    fprintf(stderr, "DEBUG: box_add_child parent=%p child=%p\n", parent, child);
    if (!parent || !child)
        return;
    child->parent = parent;
    if (parent->last) {
        parent->last->next = child;
        child->prev = parent->last;
        parent->last = child;
    } else {
        parent->children = child;
        parent->last = child;
    }
}

/* NSURL Mocks */
nserror nsurl_create(const char *const url, nsurl **msg)
{
    *msg = (nsurl *)strdup(url);
    return NSERROR_OK;
}
nserror nsurl_join(const nsurl *base, const char *rel, nsurl **result)
{
    *result = (nsurl *)strdup(rel);
    return NSERROR_OK;
}
void nsurl_unref(nsurl *url)
{
    if (url)
        free((void *)url);
}

/* HTML Objects */
bool html_fetch_object(struct html_content *c, struct nsurl *url, struct box *box, content_type type, bool params)
{
    return false;
}

/* Strings/Talloc */
char *squash_whitespace(const char *s)
{
    return strdup(s);
}

void *_talloc_zero(const void *ctx, size_t size, const char *name)
{
    return calloc(1, size);
}

/* Minimal strndup implementation for Windows */
char *strndup(const char *s, size_t n)
{
    size_t len;
    char *s2;

    for (len = 0; len != n && s[len]; len++)
        continue;

    s2 = malloc(len + 1);
    if (!s2)
        return NULL;

    memcpy(s2, s, len);
    s2[len] = 0;
    return s2;
}

char *talloc_strdup(const void *ctx, const char *p)
{
    return strdup(p);
}
char *talloc_strndup(const void *ctx, const char *p, size_t n)
{
    return strndup(p, n);
}

/* convert_special_elements stub */
bool convert_special_elements(dom_node *node, struct html_content *c, struct box *box, bool *convert_children)
{
    *convert_children = true;
    return true;
}

/* Linker Stubs for box_get_style */
const char *nsurl_access(const nsurl *url)
{
    return "http://test";
}

css_stylesheet *
nscss_create_inline_style(const uint8_t *data, size_t len, const char *encoding, const char *base_url, bool quirks)
{
    return NULL;
}

/* -------------------------------------------------------------------------- */
/* Helper Mocks */


/* corestrings moved to top */

/* Mock misc scheduler */
static nserror mock_schedule(int t, void (*callback)(void *p), void *p)
{
    return NSERROR_OK;
}
static nserror mock_utf8_to_enc(const char *utf8, size_t len, char **result)
{
    *result = strdup(utf8);
    return NSERROR_OK;
}
static struct gui_utf8_table mock_utf8_table = {
    .utf8_to_local = mock_utf8_to_enc,
    .local_to_utf8 = mock_utf8_to_enc,
};
/* Define mock_misc matching struct gui_misc_table defined in misc.h */
static struct gui_misc_table mock_misc = {.schedule = mock_schedule};
/* Define mock_gui matching struct neosurf_table (guit) */
static struct neosurf_table mock_gui = {.misc = &mock_misc, .utf8 = &mock_utf8_table};
struct neosurf_table *guit = &mock_gui;

/* Helper stubs */
#undef NSLOG
#define NSLOG(cat, level, fmt, ...) fprintf(stderr, "NSLOG: " fmt "\n", ##__VA_ARGS__)

/* Mock UTF-8 functions for box_text_transform Unicode support */
uint32_t utf8_to_ucs4(const char *s, size_t l)
{
    /* Simple mock: just return the first byte for ASCII, or a placeholder for multi-byte */
    if ((unsigned char)s[0] < 0x80) {
        return (uint32_t)(unsigned char)s[0];
    }
    /* For multi-byte, return a placeholder (not used in test) */
    return 0xFFFD;
}

size_t utf8_from_ucs4(uint32_t c, char *s)
{
    /* Simple mock: only handle ASCII */
    if (c < 0x80) {
        s[0] = (char)c;
        return 1;
    }
    /* For non-ASCII, return replacement char */
    s[0] = '?';
    return 1;
}

size_t utf8_next(const char *s, size_t l, size_t o)
{
    /* Simple mock: advance by 1 for ASCII, or by multi-byte sequence length */
    if (o >= l)
        return l;
    unsigned char c = (unsigned char)s[o];
    if (c < 0x80)
        return o + 1;
    if ((c & 0xE0) == 0xC0)
        return o + 2;
    if ((c & 0xF0) == 0xE0)
        return o + 3;
    if ((c & 0xF8) == 0xF0)
        return o + 4;
    return o + 1;
}

/* Now Include */
#include "content/handlers/html/box_construct.c"

static void box_complete_cb(struct html_content *c, bool status)
{
    fprintf(stderr, "DEBUG: box_complete_cb called with status=%d\n", status);
    c->conversion_begun = false; /* Hack to signal completion */
    if (!status)
        c->aborted = true; /* Reuse aborted flag for error */
}

START_TEST(test_grid_construction)
{
    /* Setup DOM Tree via File Parsing */
    FILE *fp = fopen("/tmp/ns_test_grid.html", "w");
    ck_assert_ptr_nonnull(fp);
    /* <div id="grid"> is the Grid container. <div id="child"> is the child.
     */
    fprintf(fp, "<html><body><div id=\"grid\"><div id=\"child\">Child</div></div></body></html>");
    fclose(fp);

    dom_document *doc = NULL;
    nserror err = libdom_parse_file("/tmp/ns_test_grid.html", "UTF-8", &doc);
    ck_assert_int_eq(err, NSERROR_OK);
    ck_assert_ptr_nonnull(doc);

    /* Navigate to GRID element */
    /* Root -> HTML -> BODY -> DIV#grid */
    dom_element *root_el = NULL;
    dom_document_get_document_element(doc, &root_el);
    ck_assert_ptr_nonnull(root_el);

    /* Finding GRID node manually since we don't have nice selectors in test
     * env easily */
    /* Assume structure is flat enough or traverse */
    dom_node *curr = (dom_node *)root_el;
    dom_node *next = NULL;

    /* Traverse depth first to find id="grid" */
    /* Simple recursive finder or loop?
       Using box_construct logic dependencies implies we can use node
       iteration. Let's just use GetElementById? */
    dom_element *grid_el = NULL;
    /* LibDOM 0.0.1+ supports getElementById? */
    /* It takes dom_string. */
    dom_string *id_str;
    dom_string_create((const uint8_t *)"grid", 4, &id_str);

    /* dom_document_get_element_by_id returns dom_element */
    /* Prototype might be implicit but let's try. core/document.h */
    dom_document_get_element_by_id(doc, id_str, &grid_el);
    dom_string_unref(id_str);

    ck_assert_ptr_nonnull(grid_el);

    /* Setup Helper Strings */
#define INIT_STR(x, v) dom_string_create((const uint8_t *)(v), strlen(v), &x)
    INIT_STR(corestring_dom_id, "id");
    INIT_STR(corestring_dom_title, "title");
    INIT_STR(corestring_dom_style, "style");
    INIT_STR(corestring_dom_colspan, "colspan");
    INIT_STR(corestring_dom_rowspan, "rowspan");
    INIT_STR(corestring_dom___ns_key_box_node_data, "__ns_key_box_node_data");

    /* Context Setup - Heap Alloc */
    struct html_content htmlc = {0};
    htmlc.bctx = NULL;
    htmlc.conversion_begun = true; /* Mark as running */

    struct box_construct_ctx *ctx = calloc(1, sizeof(*ctx));
    ctx->content = &htmlc;
    ctx->n = (dom_node *)root_el; /* Start construction at Root element (HTML) */
    ctx->root_box = NULL;
    ctx->cb = box_complete_cb;
    ctx->bctx = NULL;

    /* RUN 1: Process GRID (and its children recursively via
     * convert_xml_to_box logic) */
    /* Wait, convert_xml_to_box processes ONE slice (starts at ctx.n).
       It calls box_construct_element(ctx.n).
       Then it iterates children of ctx.n in a loop.
       So calling it ONCE should construct the box for GRID and its
       Immediate Children? Or does it recurse? box_construct_element calls
       box_add_child. It calls convert_special_elements -> convert_children
       (recursive). Wait, convert_xml_to_box implementation: do {
         box_construct_element(ctx, ...);
         find next;
         ...
         if (type == ELEMENT) break; (to schedule next slice?)
         ...
         if (next == NULL) { normalize... }
       }
       It processes siblings?
       If I pass GRID element as ctx.n.
       It constructs box for GRID.
       Then it calls next_node.
       Since I am starting at GRID (which might be inside Body), next_node
       might go to siblings or parent? If I want to construct JUST the grid
       and its children. box_construct_element attaches to
       `props.containing_block`. Grid has no constructed parent box in
       `ctx`. So `props.containing_block` will be NULL. So GRID box will be
       created (root of this context). Then `next_node` will find children
       of GRID? `next_node` usage in `box_construct.c` traverses depth
       first. It should visit children.

       HOWEVER, `convert_xml_to_box` is designed to be sliced.
       It returns/yields.
       My `mock_schedule` executes immediately? No, it does nothing.
       So `convert_xml_to_box` runs until it yields.
       It yields after processing ONE element?
       `if (type == DOM_ELEMENT_NODE) break;` inside the loop.
       So it processes ONE element and then breaks loop, then re-schedules
       itself with `ctx->n = next`. So I must Loop `convert_xml_to_box`
       until `ctx->n` is NULL? In `convert_xml_to_box`: It calls `schedule`
       at the end (lines 1586, 1621...). If I loop manually: while (ctx.n !=
       NULL) { convert_xml_to_box(&ctx); } ? But `convert_xml_to_box` calls
       `schedule`, which I returns OK. But `convert_xml_to_box` returns
       `void`. It updates `ctx->n`. So I can loop.
    */

    /* Loop until finished */
    /* We need to be careful about termination.
       When finished, `ctx.n` will be what?
       The function checks `if (next == NULL) ... ctx->n = next`.
       So when done `ctx.n` becomes NULL.
    */
    /* Loop until finished */
    while (htmlc.conversion_begun) {
        fprintf(stderr, "DEBUG: Calling convert for ctx->n=%p\n", ctx->n);
        convert_xml_to_box(ctx);
        fprintf(stderr, "DEBUG: Returned from convert\n");
    }
    ck_assert_msg(!htmlc.aborted, "Box construction failed");

    /* VERIFY - access htmlc.layout, as ctx is freed */
    struct box *root = htmlc.layout;
    ck_assert_ptr_nonnull(root);
    /* Root is HTML (Block) */
    ck_assert_int_eq(root->type, BOX_BLOCK);

    struct box *head = root->children;
    ck_assert_ptr_nonnull(head);

    struct box *body = head->next;
    ck_assert_ptr_nonnull(body);
    /* Body is Block */
    ck_assert_int_eq(body->type, BOX_BLOCK);

    struct box *grid = body->children;
    ck_assert_ptr_nonnull(grid);
    /* Grid should be BOX_GRID */
    ck_assert_int_eq(grid->type, BOX_GRID);

    struct box *child = grid->children;
    /* If attachment failed, child is NULL */
    ck_assert_ptr_nonnull(child);

    /* Check child type. Should be BLOCK (div) or TEXT (if text node).
       I put <div>Child</div>.
       So child should be the div.
    */
    /* Note: whitespace might create text nodes?
       <div ...><div ...>
       If no whitespace, strict adjacency.
       But I used "><".
    */
    ck_assert_int_eq(child->type, BOX_BLOCK);
    ck_assert_ptr_eq(child->parent, grid);

    /* Cleanup */
    box_free_tree(root);
    dom_node_unref(grid_el);
    dom_node_unref(root_el);
    dom_node_unref(doc);
    unlink("/tmp/ns_test_grid.html");
}
END_TEST

Suite *grid_construct_suite(void)
{
    Suite *s = suite_create("grid_construct");
    TCase *tc = tcase_create("full");
    tcase_add_test(tc, test_grid_construction);
    suite_add_tcase(s, tc);
    return s;
}

int main(void)
{
    Suite *s = grid_construct_suite();
    SRunner *sr = srunner_create(s);
    srunner_run_all(sr, CK_ENV);
    int failed = srunner_ntests_failed(sr);
    srunner_free(sr);
    return (failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}

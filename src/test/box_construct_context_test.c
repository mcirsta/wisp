/* Construction ancestry must survive slices without consulting published boxes. */
#include <check.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <nsutils/time.h>
#include <libdom/bindings/hubbub/parser.h>
#include <wisp/desktop/gui_internal.h>
#include <wisp/misc.h>
#include <wisp/layout.h>
#include <wisp/utils/corestrings.h>
#include <wisp/utils/nsoption.h>
#include <wisp/utils/nsurl.h>
#include <wisp/content/handlers/html/private.h>
#include <wisp/content/handlers/html/html.h>
#include <wisp/content/handlers/html/form_internal.h>
#include "content/handlers/css/dump.h"
#include "content/handlers/css/hints.h"
#include "content/handlers/css/internal.h"
#include "content/handlers/css/select.h"
#include "content/handlers/html/box_construct.h"
#include "content/handlers/html/object.h"
#include "content/handlers/html/html_svg.h"
#include "content/handlers/html/form_bindings.h"
#include "content/handlers/html/box_mappings.h"
#include "content/handlers/html/box_boundary.h"
#include "content/handlers/html/layout.h"
#include "content/handlers/html/restyle.h"
#include "utils/talloc.h"
#include "utils/libdom.h"
#ifdef WISP_TEST_ALLOC_FAILURE
#include "test/malloc_fig.h"
#endif

static html_content html;
static css_stylesheet *sheet;
static void (*scheduled)(void *);
static void *scheduled_pw;
static unsigned completions, slices;
static bool reject_schedule;
static bool expect_failure;

/* Intercept only the fetch boundary. Ordinary construction records live
 * bindings/counts; a detached build must never reach these functions. */
static bool record_fetches;
static uint8_t *fetched_buffer;
static size_t fetched_buffer_length;

static bool
record_object(html_content *content, nsurl *url, struct box *box, content_type types, bool background, uint32_t layer)
{
    ck_assert_msg(record_fetches, "Detached construction started a live fetch: %s", nsurl_access(url));
    ck_assert_ptr_eq(content, &html);
    struct content_html_object *object = calloc(1, sizeof(*object));
    ck_assert_ptr_nonnull(object);
    object->parent = &html.base;
    object->box = box;
    object->node = box->node == NULL ? NULL : dom_node_ref(box->node);
    object->url = nsurl_ref(url);
    object->permitted_types = types;
    object->background = background;
    object->background_layer = layer;
    object->layout_resource = true;
    object->next = html.object_list;
    html.object_list = object;
    html.num_objects++;
    html.base.active++;
    return true;
}

bool html_fetch_object(html_content *content, nsurl *url, struct box *box, content_type types, bool background)
{
    return record_object(content, url, box, types, background, 0);
}

bool html_fetch_background(html_content *content, nsurl *url, struct box *box, content_type types, uint32_t layer)
{
    return record_object(content, url, box, types, true, layer);
}

bool html_fetch_object_buffer(html_content *content, const uint8_t *buffer, size_t length, const char *mime_type,
    struct box *box, content_type types)
{
    ck_assert_msg(record_fetches, "Detached SVG construction started a live fetch");
    ck_assert_str_eq(mime_type, "image/svg+xml");
    ck_assert_int_eq(types, CONTENT_IMAGE);
    ck_assert_ptr_null(fetched_buffer);
    fetched_buffer = malloc(length);
    ck_assert_ptr_nonnull(fetched_buffer);
    memcpy(fetched_buffer, buffer, length);
    fetched_buffer_length = length;
    nsurl *url;
    ck_assert_int_eq(nsurl_create("file:///serialized.svg", &url), NSERROR_OK);
    bool ok = record_object(content, url, box, types, false, 0);
    nsurl_unref(url);
    return ok;
}

/* DOM, CSS and box construction remain real. Force a scheduler yield at each
 * 64-element checkpoint, in all build modes. */
nsuerror nsu_getmonotonic_ms(uint64_t *now)
{
    static uint64_t time;
    *now = time += 100;
    return NSUERROR_OK;
}

static nserror schedule(int delay, void (*callback)(void *), void *pw)
{
    if (delay < 0) {
        ck_assert_ptr_eq(scheduled_pw, pw);
        scheduled = NULL;
        scheduled_pw = NULL;
        return NSERROR_OK;
    }
    if (reject_schedule)
        return NSERROR_NOMEM;
    ck_assert_ptr_null(scheduled);
    scheduled = callback;
    scheduled_pw = pw;
    return NSERROR_OK;
}

static void run_slice(void)
{
    ck_assert_ptr_nonnull(scheduled);
    void (*callback)(void *) = scheduled;
    void *pw = scheduled_pw;
    scheduled = NULL;
    scheduled_pw = NULL;
    slices++;
    callback(pw);
}

static void complete(html_content *content, bool success)
{
    ck_assert_msg(success == !expect_failure, "Unexpected construction result at slice %u", slices);
    ck_assert_ptr_eq(content, &html);
    content->box_conversion_context = NULL;
    completions++;
}

static dom_node *by_id(const char *id)
{
    dom_string *key;
    dom_node *node;
    ck_assert_int_eq(dom_string_create((const uint8_t *)id, strlen(id), &key), DOM_NO_ERR);
    ck_assert_int_eq(dom_document_get_element_by_id(html.document, key, &node), DOM_NO_ERR);
    dom_string_unref(key);
    ck_assert_ptr_nonnull(node);
    return node;
}

static void attribute(dom_node *node, const char *name, const char *value)
{
    dom_string *key, *text;
    ck_assert_int_eq(dom_string_create((const uint8_t *)name, strlen(name), &key), DOM_NO_ERR);
    ck_assert_int_eq(dom_string_create((const uint8_t *)value, strlen(value), &text), DOM_NO_ERR);
    ck_assert_int_eq(dom_element_set_attribute(node, key, text), DOM_NO_ERR);
    dom_string_unref(key);
    dom_string_unref(text);
}

/* Return a borrowed node: the parent owns its lifetime. */
static dom_node *element(dom_node *parent, const char *tag, const char *id, const char *class_name)
{
    dom_node *node, *inserted;
    dom_string *name;
    ck_assert_int_eq(dom_string_create((const uint8_t *)tag, strlen(tag), &name), DOM_NO_ERR);
    ck_assert_int_eq(dom_document_create_element(html.document, name, (dom_element **)&node), DOM_NO_ERR);
    dom_string_unref(name);
    ck_assert_int_eq(dom_node_append_child(parent, node, &inserted), DOM_NO_ERR);
    dom_node_unref(inserted);
    if (id != NULL)
        attribute(node, "id", id);
    if (class_name != NULL)
        attribute(node, "class", class_name);
    dom_node_unref(node);
    return node;
}

static void text(dom_node *parent, const char *value)
{
    dom_string *string;
    dom_node *node, *inserted;
    ck_assert_int_eq(dom_string_create((const uint8_t *)value, strlen(value), &string), DOM_NO_ERR);
    ck_assert_int_eq(dom_document_create_text_node(html.document, string, (dom_text **)&node), DOM_NO_ERR);
    dom_string_unref(string);
    ck_assert_int_eq(dom_node_append_child(parent, node, &inserted), DOM_NO_ERR);
    dom_node_unref(inserted);
    dom_node_unref(node);
}

static void setup(void)
{
    memset(&html, 0, sizeof(html));
    scheduled = NULL;
    scheduled_pw = NULL;
    completions = slices = 0;
    reject_schedule = false;
    record_fetches = false;
    expect_failure = false;
    ck_assert_int_eq(
        dom_implementation_create_document(DOM_IMPLEMENTATION_HTML, NULL, NULL, NULL, NULL, NULL, &html.document),
        DOM_NO_ERR);
    dom_node *root = element((dom_node *)html.document, "html", "root", NULL);
    dom_node *body = element(root, "body", "body", NULL);
    dom_node *outer = element(body, "div", "outer", NULL);
    attribute(outer, "title", "outer title");
    dom_node *link = element(outer, "a", "link", NULL);
    attribute(link, "href", "child.html");
    attribute(link, "target", "reader");
    dom_node *deep = element(link, "span", "deep", NULL);
    for (unsigned i = 0; i < 96; i++)
        deep = element(deep, "span", NULL, "nest");
    text(deep, "inside");
    text(element(deep, "div", "split", NULL), "split block");
    text(deep, "tail");
    text(outer, "after link");
    element(outer, "br", NULL, NULL);
    text(element(outer, "span", "hidden", NULL), "hidden child");
    dom_node *floating = element(outer, "div", NULL, "float");
    text(floating, "float");
    text(element(floating, "span", NULL, NULL), "inside float");
    dom_node *flex = element(outer, "div", NULL, "flex");
    text(flex, "anonymous");
    text(element(flex, "span", NULL, NULL), "flex item");
    text(element(flex, "div", NULL, NULL), "block item");
    dom_node *grid = element(outer, "div", NULL, "grid");
    text(grid, "anonymous");
    text(element(grid, "span", NULL, NULL), "grid item");
    text(element(outer, "div", NULL, "cell"), "orphan cell");
    dom_node *pre = element(outer, "pre", NULL, NULL);
    text(pre, "\nfirst\n");
    text(element(pre, "span", NULL, NULL), "pre inline");
    text(pre, "last");
    dom_node *list = element(outer, "ol", NULL, NULL);
    text(element(list, "li", NULL, NULL), "one");
    text(element(list, "li", NULL, NULL), "two");
    text(element(body, "div", "outside", NULL), "outside");

    const char *css = "html,body,div,pre,ol,li {display:block} html {font:17px serif}"
                      "#outer {color:#123456} #link {font-size:21px} #hidden {display:none}"
                      "#link::before {content:'before'} #link::after {content:'after'}"
                      ".owned-boundary::before {content:'scope before'} .owned-boundary::after {content:'scope after'}"
                      ".nest::after {content:'!'} #split::before {content:'block';display:block}"
                      ".float {float:left} .flex {display:flex} .grid {display:grid} .cell {display:table-cell}"
                      "pre {white-space:pre} li {display:list-item;list-style-type:decimal}";
    css_stylesheet_params sheet_params = {.params_version = CSS_STYLESHEET_PARAMS_VERSION_1,
        .level = CSS_LEVEL_DEFAULT,
        .charset = "UTF-8",
        .url = "file:///context.html",
        .resolve = nscss_resolve_url};
    ck_assert_int_eq(css_stylesheet_create(&sheet_params, &sheet), CSS_OK);
    css_error error = css_stylesheet_append_data(sheet, (const uint8_t *)css, strlen(css));
    ck_assert_msg(error == CSS_OK || error == CSS_NEEDDATA, "CSS parse error %d", error);
    ck_assert_int_eq(css_stylesheet_data_done(sheet), CSS_OK);
    ck_assert_int_eq(css_select_ctx_create(&html.select_ctx), CSS_OK);
    ck_assert_int_eq(css_select_ctx_append_sheet(html.select_ctx, sheet, CSS_ORIGIN_AUTHOR, NULL), CSS_OK);
    ck_assert_int_eq(nsurl_create("file:///context.html", &html.base_url), NSERROR_OK);
    ck_assert_int_eq(lwc_intern_string("*", 1, &html.universal), lwc_error_ok);
    html.encoding = "UTF-8";
    html.media.type = CSS_MEDIA_SCREEN;
    html.unit_len_ctx.font_size_default = INTTOFIX(16);
    html.unit_len_ctx.device_dpi = INTTOFIX(96);
    html.unit_len_ctx.viewport_width = INTTOFIX(800);
    html.unit_len_ctx.viewport_height = INTTOFIX(600);
}

static void teardown(void)
{
    ck_assert_ptr_null(scheduled);
    ck_assert_ptr_null(html.box_conversion_context);
    html_restyle_reset(&html);
    html_object_free_objects(&html);
    talloc_free(html.bctx);
    while (html.forms != NULL) {
        struct form *prev = html.forms->prev;
        form_free(html.forms);
        html.forms = prev;
    }
    html_free_inline_svgs(&html);
    free(fetched_buffer);
    fetched_buffer = NULL;
    dom_node_unref(html.document);
    css_select_ctx_destroy(html.select_ctx);
    css_stylesheet_destroy(sheet);
    lwc_string_unref(html.universal);
    nsurl_unref(html.base_url);
}

static nserror begin(void)
{
    dom_node *root = by_id("root");
    nserror error = dom_to_box(root, &html, complete, &html.box_conversion_context);
    dom_node_unref(root);
    return error;
}

/* Include computed styles and flattened/generated/anonymous structure without
 * pointer addresses. Outside nodes must not inherit an earlier inline's state. */
static void describe(FILE *stream, const struct box *box)
{
    fprintf(stream, "(%d flags=%x columns=%u rows=%u", box->type, box->flags & ~OWNS_REGION, box->columns, box->rows);
    if (box->text != NULL)
        fprintf(stream, " text=%.*s", (int)box->length, box->text);
    if (box->href != NULL)
        fprintf(stream, " href=%s", nsurl_access(box->href));
    if (box->title != NULL)
        fprintf(stream, " title=%s", box->title);
    if (box->target != NULL)
        fprintf(stream, " target=%s", box->target);
    if (box->usemap != NULL)
        fprintf(stream, " usemap=%s", box->usemap);
    if (box->gadget != NULL) {
        const struct form_control *g = box->gadget;
        fprintf(stream, " gadget=%d name=%s value=%s selected=%d", g->type, g->name == NULL ? "" : g->name,
            g->value == NULL ? "" : g->value, g->selected);
    }
    if (box->object_params != NULL) {
        const struct object_params *p = box->object_params;
        fprintf(stream, " object data=%s classid=%s codebase=%s type=%s codetype=%s",
            p->data == NULL ? "" : nsurl_access(p->data), p->classid == NULL ? "" : nsurl_access(p->classid),
            p->codebase == NULL ? "" : nsurl_access(p->codebase), p->type == NULL ? "" : p->type,
            p->codetype == NULL ? "" : p->codetype);
        for (const struct object_param *param = p->params; param != NULL; param = param->next)
            fprintf(stream, " param=%s value=%s type=%s valuetype=%s", param->name == NULL ? "" : param->name,
                param->value == NULL ? "" : param->value, param->type == NULL ? "" : param->type,
                param->valuetype == NULL ? "" : param->valuetype);
    }
    if (box->style != NULL)
        nscss_dump_computed_style(stream, box->style);
    if (box->type == BOX_INLINE && box->inline_end != NULL) {
        ck_assert_int_eq(box->inline_end->type, BOX_INLINE_END);
        ck_assert_ptr_eq(box->inline_end->inline_end, box);
        fputs(" inline-end", stream);
    }
    if (box->list_marker != NULL)
        describe(stream, box->list_marker);
    for (const struct box *child = box->children; child != NULL; child = child->next) {
        ck_assert_ptr_eq(child->parent, box);
        describe(stream, child);
    }
    fputs(")\n", stream);
}

static char *snapshot_box(const struct box *box)
{
    char *text;
    size_t length;
    FILE *stream = open_memstream(&text, &length);
    ck_assert_ptr_nonnull(stream);
    describe(stream, box);
    ck_assert_int_eq(fclose(stream), 0);
    return text;
}

static char *snapshot(void)
{
    return snapshot_box(html.layout);
}

START_TEST(test_unpublished_ancestor_mappings)
{
    ck_assert_int_eq(begin(), NSERROR_OK);
    while (scheduled != NULL)
        run_slice();
    ck_assert_uint_ge(slices, 2);
    ck_assert_uint_eq(completions, 1);
    char *expected = snapshot();
    talloc_free(html.bctx);
    html.bctx = NULL;
    html.layout = NULL;
    ck_assert_int_eq(nscss_invalidate_document(html.document), NSERROR_OK);

    ck_assert_int_eq(begin(), NSERROR_OK);
    run_slice();
    ck_assert_ptr_nonnull(scheduled);
    const char *ids[] = {"root", "body", "outer", "link", "deep"};
    dom_node *nodes[5];
    struct box *boxes[5];
    for (unsigned i = 0; i < 5; i++) {
        nodes[i] = by_id(ids[i]);
        boxes[i] = box_for_node(nodes[i]);
        ck_assert_ptr_nonnull(boxes[i]);
        void *old;
        ck_assert_int_eq(
            dom_node_set_user_data(nodes[i], corestring_dom___ns_key_box_node_data, NULL, NULL, &old), DOM_NO_ERR);
    }
    while (scheduled != NULL)
        run_slice();
    ck_assert_uint_eq(completions, 2);
    for (unsigned i = 0; i < 5; i++) {
        /* Construction must not need to republish ancestors to finish. */
        ck_assert_ptr_null(box_for_node(nodes[i]));
        void *old;
        ck_assert_int_eq(
            dom_node_set_user_data(nodes[i], corestring_dom___ns_key_box_node_data, boxes[i], NULL, &old), DOM_NO_ERR);
        dom_node_unref(nodes[i]);
    }
    char *actual = snapshot();
    ck_assert_str_eq(actual, expected);
    free(actual);
    free(expected);
    dom_node *outside = by_id("outside"), *hidden = by_id("hidden");
    ck_assert_ptr_null(box_for_node(hidden));
    ck_assert_ptr_null(box_for_node(outside)->href);
    ck_assert_ptr_null(box_for_node(outside)->title);
    css_color color;
    css_computed_color(box_for_node(outside)->style, &color);
    ck_assert_uint_ne(color, 0xff123456);
    dom_node_unref(outside);
    dom_node_unref(hidden);
}
END_TEST

START_TEST(test_cancel_construction)
{
    ck_assert_int_eq(begin(), NSERROR_OK);
    if (_i != 0) {
        run_slice();
        ck_assert_ptr_nonnull(scheduled);
    }
    ck_assert_int_eq(cancel_dom_to_box(html.box_conversion_context), NSERROR_OK);
    html.box_conversion_context = NULL;
    ck_assert_ptr_null(scheduled);
    ck_assert_uint_eq(completions, 0);
}
END_TEST

START_TEST(test_initial_schedule_failure)
{
    reject_schedule = true;
    ck_assert_int_eq(begin(), NSERROR_NOMEM);
    ck_assert_ptr_null(html.box_conversion_context);
    ck_assert_uint_eq(completions, 0);
}
END_TEST

START_TEST(test_continuation_schedule_failure)
{
    ck_assert_int_eq(begin(), NSERROR_OK);
    reject_schedule = expect_failure = true;
    run_slice();
    ck_assert_ptr_null(scheduled);
    ck_assert_ptr_null(html.box_conversion_context);
    ck_assert_uint_eq(completions, 1);
}
END_TEST

/* URI and none share one computed bit; background-image's wider enum must
 * never be passed to the generated list-style-image setter. */
START_TEST(test_list_image_cascade)
{
    nsoption_set_bool(foreground_images, false);
    dom_node *body = by_id("body");
    dom_node *list = element(body, "ol", NULL, NULL);
    attribute(list, "style", "list-style-image:url(parent.png)");
    dom_node *item = element(list, "li", NULL, NULL);
    const char *styles[] = {"", "list-style-image:none", "list-style-image:url(child.png)", "list-style-image:inherit"};
    attribute(item, "style", styles[_i]);
    ck_assert_int_eq(begin(), NSERROR_OK);
    while (scheduled != NULL)
        run_slice();
    lwc_string *uri;
    ck_assert_int_eq(css_computed_list_style_image(box_for_node(item)->style, &uri), CSS_LIST_STYLE_IMAGE_URI);
    if (_i == 1) {
        ck_assert_ptr_null(uri);
    } else {
        ck_assert_ptr_nonnull(uri);
        ck_assert_str_eq(lwc_string_data(uri), _i == 2 ? "file:///child.png" : "file:///parent.png");
    }
    nsoption_set_bool(foreground_images, true);
    dom_node_unref(body);
}
END_TEST

#ifdef WISP_ENABLE_PARTIAL_UPDATES
/* Exercise the real selective-style lifecycle: its full reconstruction is what
 * first establishes owners for later retained-box work. Keep the old arena alive
 * until borrowed styles have been cloned and the active table discarded. */
static void *begin_owned_rebuild(dom_node **controls, size_t count)
{
    html.had_initial_layout = true;
    html.base.status = CONTENT_STATUS_DONE;
    ck_assert_int_eq(html_restyle_capture_inputs(&html), NSERROR_OK);
    html_restyle_end(&html, true);
    for (size_t i = 0; i < count; i++)
        html_restyle_record_checked(&html, controls[i]);
    html_restyle_begin(&html);
    ck_assert(html.restyle.partial);
    ck_assert_int_eq(html_restyle_invalidate(&html), NSERROR_OK);
    void *old = html.bctx;
    html.bctx = NULL;
    html.layout = NULL;
    html.restyling = true;
    ck_assert_int_eq(html_restyle_capture_inputs(&html), NSERROR_OK);
    ck_assert_int_eq(begin(), NSERROR_OK);
    return old;
}

static void finish_owned_rebuild(void *old, bool success)
{
    html.restyling = false;
    html_restyle_end(&html, success);
    talloc_free(old);
}

static void owned_allocation(void *owner, const void *allocation)
{
    if (allocation == NULL)
        return;
    const void *p = allocation;
    while (p != NULL && p != owner)
        p = talloc_parent(p);
    ck_assert_msg(p == owner, "Allocation %p escaped owner %p", allocation, owner);
}

static void owned_boxes(void *owner, const struct box *box)
{
    owned_allocation(owner, box);
    owned_allocation(owner, box->text);
    /* These fixtures define title/target inside the selected region. */
    owned_allocation(owner, box->title);
    owned_allocation(owner, box->target);
    if (box->list_marker != NULL)
        owned_boxes(owner, box->list_marker);
    for (const struct box *child = box->children; child != NULL; child = child->next)
        owned_boxes(owner, child);
}

START_TEST(test_live_region_owners)
{
    dom_node *outer = by_id("outer"), *link = by_id("link"), *outside = by_id("outside");
    const char *styles[] = {"display:block", "display:flex", "display:grid"};
    attribute(outer, "style", styles[_i]);
    attribute(outer, "class", "owned-boundary");
    /* Whitespace/text around two independent scopes must stay outside them. */
    dom_node *outside_control = element(outside, "span", "outside-control", NULL);
    text(outside_control, "sibling control");
    dom_node *body = by_id("body");
    text(body, "following scopes");
    dom_node *nested = by_id("deep");
    ck_assert_int_eq(begin(), NSERROR_OK);
    while (scheduled != NULL)
        run_slice();
    ck_assert_ptr_null(box_construct_region_owner(box_for_node(outer)));
    char *expected = snapshot();
    size_t plateau = 0;
    for (unsigned pass = 0; pass < 12; pass++) {
        /* Overlapping changes coalesce; descendants get no extra arena. */
        dom_node *controls[] = {link, nested, outside_control};
        void *old = begin_owned_rebuild(controls, 3);
        while (scheduled != NULL)
            run_slice();
        finish_owned_rebuild(old, true);
        char *actual = snapshot();
        ck_assert_str_eq(actual, expected);
        free(actual);
        struct box *box = box_for_node(outer), *sibling = box_for_node(outside);
        void *owner = box_construct_region_owner(box), *other = box_construct_region_owner(sibling);
        ck_assert_ptr_nonnull(owner);
        ck_assert_ptr_nonnull(other);
        ck_assert_ptr_ne(owner, other);
        ck_assert_ptr_eq(talloc_parent(owner), html.bctx);
        ck_assert_ptr_eq(talloc_parent(other), html.bctx);
        ck_assert_ptr_null(box_construct_region_owner(box_for_node(link)));
        owned_boxes(owner, box);
        owned_boxes(other, sibling);
        ck_assert_ptr_eq(talloc_parent(box_for_node(body)), html.bctx);
        ck_assert_ptr_eq(talloc_parent(box_for_node(body)->last), html.bctx);
        if (pass == 0)
            plateau = talloc_total_blocks(html.bctx);
        ck_assert_uint_eq(talloc_total_blocks(html.bctx), plateau);
    }

    /* Before layout adds cross-boundary links, detaching this physical subtree
     * is sufficient to test arena retirement. This is not a publication test. */
    struct box *box = box_for_node(outer), *parent = box->parent, *sibling = box_for_node(outside);
    void *owner = box_construct_region_owner(box);
    size_t remaining = talloc_total_blocks(html.bctx) - talloc_total_blocks(owner);
    if (box->prev != NULL)
        box->prev->next = box->next;
    else
        parent->children = box->next;
    if (box->next != NULL)
        box->next->prev = box->prev;
    else
        parent->last = box->prev;
    talloc_free(owner);
    ck_assert_uint_eq(talloc_total_blocks(html.bctx), remaining);
    ck_assert_ptr_null(box_for_node(outer));
    ck_assert_ptr_null(box_for_node(link));
    ck_assert_ptr_eq(box_for_node(outside), sibling);
    char *retained = snapshot(); /* Traverse and read styles after retirement. */
    free(retained);
    free(expected);
    dom_node_unref(outer);
    dom_node_unref(link);
    dom_node_unref(outside);
    dom_node_unref(body);
    dom_node_unref(nested);
}
END_TEST

START_TEST(test_live_owner_ineligible)
{
    const char *styles[] = {"display:none", "float:left", "position:absolute", "display:table", "display:inline",
        "display:block", "display:block"};
    dom_node *outer = by_id("outer"), *control = by_id("link"), *body = by_id("body");
    attribute(outer, "style", styles[_i]);
    if (_i == 5)
        attribute(body, "style", "display:inline"); /* Flattened DOM parent. */
    ck_assert_int_eq(begin(), NSERROR_OK);
    while (scheduled != NULL)
        run_slice();
    char *expected = snapshot();
    void *old = begin_owned_rebuild(&control, 1);
    if (_i == 6) {
        html_restyle_use_full(&html, "test_full_fallback");
        ck_assert_int_eq(html_restyle_invalidate(&html), NSERROR_OK);
    }
    while (scheduled != NULL)
        run_slice();
    finish_owned_rebuild(old, true);
    ck_assert_ptr_null(box_construct_region_owner(box_for_node(outer)));
    char *actual = snapshot();
    ck_assert_str_eq(actual, expected);
    free(actual);
    free(expected);
    dom_node_unref(outer);
    dom_node_unref(control);
    dom_node_unref(body);
}
END_TEST

START_TEST(test_live_owner_cancel_or_fail)
{
    ck_assert_int_eq(begin(), NSERROR_OK);
    while (scheduled != NULL)
        run_slice();
    dom_node *control = by_id("link"), *outer = by_id("outer");
    void *old = begin_owned_rebuild(&control, 1);
    if (_i == 1) {
        run_slice();
        ck_assert_ptr_nonnull(scheduled);
        ck_assert_ptr_nonnull(box_construct_region_owner(box_for_node(outer)));
    }
    if (_i == 2) {
        reject_schedule = expect_failure = true;
        run_slice();
        ck_assert_ptr_null(scheduled);
    } else {
        ck_assert_int_eq(cancel_dom_to_box(html.box_conversion_context), NSERROR_OK);
        html.box_conversion_context = NULL;
    }
    finish_owned_rebuild(old, false);
    dom_node_unref(control);
    dom_node_unref(outer);
}
END_TEST

static struct box_construct_region *region;
static void *region_context;
static nserror region_error;
static unsigned region_completions;

static void region_complete(html_content *content, nserror error, struct box_construct_region *result, void *pw)
{
    ck_assert_ptr_eq(content, &html);
    ck_assert_ptr_eq(pw, &region_context);
    region_context = NULL;
    region = result;
    region_error = error;
    region_completions++;
}

static nserror begin_region(dom_node *node)
{
    dom_node *parent;
    ck_assert_int_eq(dom_node_get_parent_node(node, &parent), DOM_NO_ERR);
    region = NULL;
    region_completions = 0;
    region_error = NSERROR_UNKNOWN;
    ck_assert_int_eq(nscss_invalidate_subtree(node, NULL), NSERROR_OK);
    nserror error = dom_to_box_region(
        node, &html, box_for_node(parent), html.bctx, region_complete, &region_context, &region_context);
    dom_node_unref(parent);
    return error;
}

static void build_document(void)
{
    ck_assert_int_eq(begin(), NSERROR_OK);
    while (scheduled != NULL)
        run_slice();
}

static void assert_live_mappings(const struct box *box)
{
    if (box->node != NULL)
        ck_assert_ptr_eq(box_for_node(box->node), box);
    for (const struct box *child = box->children; child != NULL; child = child->next)
        assert_live_mappings(child);
}

static void assert_live_tree(const char *expected, dom_node *node, struct box *old)
{
    char *actual = snapshot();
    ck_assert_str_eq(actual, expected);
    free(actual);
    ck_assert_ptr_eq(box_for_node(node), old);
    assert_live_mappings(html.layout);
    ck_assert_ptr_null(html.box_conversion_context);
    ck_assert_ptr_null(html.object_list);
    ck_assert_ptr_null(html.iframe);
    ck_assert_ptr_null(html.forms);
}

START_TEST(test_mapping_publication)
{
    dom_node *outer = by_id("outer"), *link = by_id("link"), *body = by_id("body"), *outside = by_id("outside");
    if (_i == 2)
        attribute(outer, "style", "display:none");
    build_document();
    void *previous = begin_owned_rebuild(&link, 1);
    while (scheduled != NULL)
        run_slice();
    finish_owned_rebuild(previous, true);
    struct box *old = box_for_node(outer), *sibling = box_for_node(outside);
    void *old_owner = box_construct_region_owner(old);
    ck_assert_msg(_i == 2 || old_owner != NULL, "Visible old scope lacks ownership");
    char *live = snapshot();
    attribute(outer, "style", _i == 1 ? "display:none" : "display:block");
    ck_assert_int_eq(begin_region(outer), NSERROR_OK);
    while (scheduled != NULL)
        run_slice();
    ck_assert_int_eq(region_error, NSERROR_OK);
    struct html_box_mapping_plan *plan;
    ck_assert_int_eq(html_box_prepare_mappings(&html, outer, old, region, html.bctx, &plan), NSERROR_OK);
    assert_live_tree(live, outer, old);
    ck_assert_int_eq(html_box_check_mappings(plan), NSERROR_OK);
    ck_assert_int_eq(html_box_apply_mappings(plan), NSERROR_INVALID);
    assert_live_tree(live, outer, old);
    html.base.locked = html.restyling = true;
    ck_assert_int_eq(html_box_apply_mappings(plan), NSERROR_OK);
    ck_assert_int_eq(html_box_apply_mappings(plan), NSERROR_INVALID);
    ck_assert_ptr_eq(box_for_node(outer), region->root);
    ck_assert_ptr_eq(box_for_node(outside), sibling);
    if (region->root != NULL)
        assert_live_mappings(region->root);
    else
        ck_assert_ptr_null(box_for_node(link)); /* Hidden descendants are cleared too. */

    /* Test-only splice before any layout: the full publisher still needs its
     * separate closure/resource/interaction checks. This root is body's first
     * child even when hidden, so no DOM-to-box insertion search is needed. */
    struct box *parent = box_for_node(body);
    struct box *next = old == NULL ? parent->children : old->next;
    if (region->root != NULL) {
        region->root->parent = parent;
        region->root->next = next;
        region->root->flags |= OWNS_REGION;
        parent->children = region->root;
        if (next != NULL)
            next->prev = region->root;
        else
            parent->last = region->root;
    } else {
        parent->children = next;
        if (next != NULL)
            next->prev = NULL;
        else
            parent->last = NULL;
    }
    talloc_free(old_owner);
    html_box_discard_mappings(plan);
    assert_live_mappings(html.layout); /* Old destructors cannot erase new keys. */
    free(live);
    dom_node_unref(outer);
    dom_node_unref(link);
    dom_node_unref(body);
    dom_node_unref(outside);
}
END_TEST

static nserror describe_mapping(dom_node *node, void *pw)
{
    fprintf(pw, "%p:%p\n", (void *)node, (void *)box_for_node(node));
    return NSERROR_OK;
}

static char *mapping_snapshot(void)
{
    char *buffer;
    size_t length;
    FILE *stream = open_memstream(&buffer, &length);
    ck_assert_ptr_nonnull(stream);
    ck_assert_int_eq(libdom_iterate_elements((dom_node *)html.document, describe_mapping, stream), NSERROR_OK);
    ck_assert_int_eq(fclose(stream), 0);
    return buffer;
}

static struct box *build_owned_document(void)
{
    build_document();
    dom_node *link = by_id("link"), *outer = by_id("outer");
    void *previous = begin_owned_rebuild(&link, 1);
    while (scheduled != NULL)
        run_slice();
    finish_owned_rebuild(previous, true);
    struct box *box = box_for_node(outer);
    ck_assert_ptr_nonnull(box_construct_region_owner(box));
    dom_node_unref(link);
    dom_node_unref(outer);
    return box;
}

START_TEST(test_boundary_candidate)
{
    dom_node *outer = by_id("outer");
    const char *display[] = {"display:block", "display:flex", "display:grid"};
    attribute(outer, "style", display[_i % 3]);
    attribute(outer, "class", "owned-boundary");
    struct box *old = build_owned_document();
    const char *reason;
    ck_assert_msg(html_box_check_boundary(&html, old, NULL, &reason) == NSERROR_OK, "Live boundary: %s", reason);
    if (_i >= 3)
        attribute(outer, "style", "display:none");
    ck_assert_int_eq(begin_region(outer), NSERROR_OK);
    while (scheduled != NULL)
        run_slice();
    ck_assert_int_eq(region_error, NSERROR_OK);
    char *before = mapping_snapshot();
    size_t blocks = talloc_total_blocks(html.bctx);
    for (unsigned i = 0; i < 4; i++) {
        ck_assert_msg(html_box_check_boundary(&html, old, region, &reason) == NSERROR_OK,
            "Candidate %d: %s", _i, reason);
        ck_assert_uint_eq(talloc_total_blocks(html.bctx), blocks);
    }
    char *after = mapping_snapshot();
    ck_assert_str_eq(after, before);
    free(before);
    free(after);
    box_construct_region_destroy(region);
    dom_node_unref(outer);
}
END_TEST

/* Corrupt one association at a time, restore it before destruction. These are
 * lifetime checks, not tests of how a particular CSS layout should look. */
START_TEST(test_boundary_crossings)
{
    struct box *old = build_owned_document();
    dom_node *outside_node = by_id("outside"), *link_node = by_id("link");
    struct box *outside = box_for_node(outside_node), *link = box_for_node(link_node);
    struct box saved_outside = *outside, saved_link = *link, saved_old = *old;
    struct box *outside_text = outside->children->children;
    char *saved_text = outside_text->text;
    css_computed_style *saved_style = outside_text->style;
    const char *expected;
    void *owner = box_construct_region_owner(old);
    struct box *marker = NULL;
    void *marker_owner = NULL;
    switch (_i) {
    case 0: outside->inline_end = link; expected = "inline_crossing"; break;
    case 1: link->inline_end = outside; expected = "inline_crossing"; break;
    case 2: outside->float_children = link; expected = "float_crossing"; break;
    case 3: link->next_float = outside; expected = "float_crossing"; break;
    case 4: link->float_container = outside; expected = "float_crossing"; break;
    case 5: outside->abs_containing_block = old; expected = "containing_block_crossing"; break;
    case 6: outside->title = old->title + 1; expected = "borrowed_storage"; break;
    case 7: outside->children->children->text = (char *)old->title + 2; expected = "borrowed_storage"; break;
    case 8:
        /* A borrowed generated style must have a surviving reference owner. */
        outside->children->children->style = link->styles->styles[CSS_PSEUDO_ELEMENT_BEFORE];
        expected = "borrowed_style";
        break;
    case 9: talloc_steal(html.bctx, link); expected = "box_ownership"; break;
    case 10: talloc_steal(owner, outside); expected = "box_ownership"; break;
    case 11:
        for (struct box *box = old->children; box != NULL; box = box->next) {
            if (box->children != NULL && box->children->list_marker != NULL) {
                marker = box->children->list_marker;
                break;
            }
        }
        ck_assert_ptr_nonnull(marker);
        marker_owner = talloc_parent(marker);
        talloc_steal(html.bctx, marker);
        expected = "box_ownership";
        break;
    default: old->last = NULL; expected = "tree_links"; break;
    }
    const char *reason;
    ck_assert_int_eq(html_box_check_boundary(&html, old, NULL, &reason), NSERROR_NOT_IMPLEMENTED);
    ck_assert_msg(strcmp(reason, expected) == 0, "Variant %d: expected %s, got %s", _i, expected, reason);
    *outside = saved_outside;
    *link = saved_link;
    *old = saved_old;
    outside_text->text = saved_text;
    outside_text->style = saved_style;
    if (_i == 9)
        talloc_steal(owner, link);
    if (_i == 10)
        talloc_steal(html.bctx, outside);
    if (marker != NULL)
        talloc_steal(marker_owner, marker);
    ck_assert_msg(html_box_check_boundary(&html, old, NULL, &reason) == NSERROR_OK, "Restored: %s", reason);
    dom_node_unref(outside_node);
    dom_node_unref(link_node);
}
END_TEST

START_TEST(test_boundary_candidate_borrows_old)
{
    struct box *old = build_owned_document();
    dom_node *outer = by_id("outer");
    ck_assert_int_eq(begin_region(outer), NSERROR_OK);
    while (scheduled != NULL)
        run_slice();
    ck_assert_int_eq(region_error, NSERROR_OK);
    const char *title = region->root->title;
    region->root->title = old->title;
    const char *reason;
    ck_assert_int_eq(html_box_check_boundary(&html, old, region, &reason), NSERROR_NOT_IMPLEMENTED);
    ck_assert_str_eq(reason, "borrowed_storage");
    region->root->title = title;
    ck_assert_int_eq(html_box_check_boundary(&html, old, region, &reason), NSERROR_OK);
    box_construct_region_destroy(region);
    dom_node_unref(outer);
}
END_TEST

static nserror boundary_font_width(const struct plot_font_style *style, const char *text, size_t length, int *width)
{
    *width = (int)length * 8;
    return NSERROR_OK;
}

static nserror boundary_font_split(const struct plot_font_style *style, const char *text, size_t length,
    int width, size_t *offset, int *actual)
{
    *offset = length;
    for (size_t i = 1; i < length; i++) {
        if (text[i] != ' ')
            continue;
        if (*offset == length || (int)i * 8 <= width)
            *offset = i;
        if ((int)i * 8 > width)
            break;
    }
    *actual = (int)*offset * 8;
    return NSERROR_OK;
}

static unsigned count_clones(const struct box *box)
{
    unsigned count = (box->flags & CLONE) != 0;
    for (const struct box *child = box->children; child != NULL; child = child->next)
        count += count_clones(child);
    return count;
}

START_TEST(test_boundary_after_layout)
{
    static const struct gui_layout_table fonts = {.width = boundary_font_width, .split = boundary_font_split};
    dom_node *outer = by_id("outer"), *outside = by_id("outside");
    const char *styles[] = {"display:block;overflow:hidden;width:180px",
        "display:flex;flex-direction:column;overflow:hidden;width:180px",
        "display:grid;grid-template-columns:180px;overflow:hidden;width:180px"};
    attribute(outer, "style", styles[_i]);
    text(outer,
        "many words that wrap across several lines within this narrow region and produce borrowed text fragments");
    text(outside,
        "more words that wrap across several lines outside the region and must remain valid after retirement");
    struct box *old = build_owned_document();
    html.font_func = &fonts;
    ck_assert(layout_document(&html, 300, 600));
    ck_assert_uint_gt(count_clones(old), 0);
    ck_assert_uint_gt(count_clones(box_for_node(outside)), 0);
    const char *reason;
    ck_assert_msg(html_box_check_boundary(&html, old, NULL, &reason) == NSERROR_OK,
        "Laid out variant %d: %s", _i, reason);
    ck_assert_int_eq(begin_region(outer), NSERROR_OK);
    while (scheduled != NULL)
        run_slice();
    ck_assert_int_eq(region_error, NSERROR_OK);
    ck_assert_msg(html_box_check_boundary(&html, old, region, &reason) == NSERROR_OK,
        "Replacement after layout %d: %s", _i, reason);
    box_construct_region_destroy(region);
    dom_node_unref(outer);
    dom_node_unref(outside);
}
END_TEST

#ifdef WISP_TEST_ALLOC_FAILURE
START_TEST(test_boundary_allocation_failures)
{
    struct box *old = build_owned_document();
    dom_node *outer = by_id("outer");
    ck_assert_int_eq(begin_region(outer), NSERROR_OK);
    while (scheduled != NULL)
        run_slice();
    ck_assert_int_eq(region_error, NSERROR_OK);
    char *before = snapshot();
    size_t blocks = talloc_total_blocks(html.bctx);
    unsigned limit;
    for (limit = 0; limit < 32; limit++) {
        malloc_limit(limit);
        nserror err = html_box_check_boundary(&html, old, region, NULL);
        malloc_limit(UINT_MAX);
        ck_assert_uint_eq(talloc_total_blocks(html.bctx), blocks);
        char *after = snapshot();
        ck_assert_str_eq(before, after);
        free(after);
        if (err == NSERROR_OK)
            break;
        ck_assert_int_eq(err, NSERROR_NOMEM);
    }
    ck_assert_uint_gt(limit, 0);
    ck_assert_uint_lt(limit, 32);
    realloc_limit(0);
    nserror err = html_box_check_boundary(&html, old, region, NULL);
    realloc_limit(UINT_MAX);
    ck_assert_int_eq(err, NSERROR_NOMEM);
    ck_assert_uint_eq(talloc_total_blocks(html.bctx), blocks);
    box_construct_region_destroy(region);
    dom_node_unref(outer);
    free(before);
}
END_TEST
#endif

START_TEST(test_mapping_cancel_or_stale)
{
    build_document();
    dom_node *outer = by_id("outer"), *outside = by_id("outside");
    struct box *old = box_for_node(outer);
    ck_assert_int_eq(begin_region(outer), NSERROR_OK);
    while (scheduled != NULL)
        run_slice();
    ck_assert_int_eq(region_error, NSERROR_OK);
    size_t blocks = talloc_total_blocks(html.bctx);
    struct html_box_mapping_plan *plan;
    ck_assert_int_eq(html_box_prepare_mappings(&html, outer, old, region, html.bctx, &plan), NSERROR_OK);
    void *saved_bctx = html.bctx;
    struct box *saved_layout = html.layout, *saved_children = region->root->children, *saved_old_children = old->children;
    void *ignored;
    switch (_i) {
    case 0: break; /* Successful preparation, then cancellation. */
    case 1: html.restyle.dom_generation++; break;
    case 2: html.bctx = NULL; break;
    case 3: html.layout = NULL; break;
    case 4:
        ck_assert_int_eq(dom_node_set_user_data(outer, corestring_dom___ns_key_box_node_data,
                             box_for_node(outside), NULL, &ignored), DOM_NO_ERR);
        break;
    case 5: region->root->children = NULL; break; /* Missing candidate mappings. */
    case 6: element(outer, "div", NULL, NULL); break; /* Unrecorded DOM node. */
    case 7: old->children = NULL; break; /* Missing live owning boxes. */
    }
    html.base.locked = html.restyling = true;
    char *before = mapping_snapshot();
    struct box *mapped = box_for_node(outer);
    if (_i != 0) {
        ck_assert_int_eq(html_box_check_mappings(plan), NSERROR_INVALID);
        ck_assert_int_eq(html_box_apply_mappings(plan), NSERROR_INVALID);
    }
    ck_assert_ptr_eq(box_for_node(outer), mapped);
    html_box_discard_mappings(plan);
    char *after = mapping_snapshot();
    ck_assert_str_eq(after, before);
    free(after);
    free(before);
    html.bctx = saved_bctx;
    html.layout = saved_layout;
    ck_assert_uint_eq(talloc_total_blocks(html.bctx), blocks);
    if (_i == 4)
        ck_assert_int_eq(dom_node_set_user_data(outer, corestring_dom___ns_key_box_node_data, old, NULL, &ignored),
            DOM_NO_ERR);
    region->root->children = saved_children;
    old->children = saved_old_children;
    box_construct_region_destroy(region);
    /* Cancellation never replaced any other element's existing mapping. */
    ck_assert_ptr_eq(box_for_node(outer), old);
    dom_node_unref(outer);
    dom_node_unref(outside);
}
END_TEST

START_TEST(test_mapping_reject_escaping_or_duplicate_nodes)
{
    build_document();
    dom_node *outer = by_id("outer"), *outside = by_id("outside");
    struct box *old = box_for_node(outer);
    ck_assert_int_eq(begin_region(outer), NSERROR_OK);
    while (scheduled != NULL)
        run_slice();
    ck_assert_int_eq(region_error, NSERROR_OK);
    /* The first child with a node is the flattened link; inline containers
     * themselves have no DOM identity. */
    struct box *child = region->root->children->children;
    ck_assert_ptr_nonnull(child->node);
    dom_node *saved = child->node;
    if (_i < 2)
        child->node = _i == 0 ? outer : outside;
    else
        region->root->next = box_for_node(outside);
    size_t blocks = talloc_total_blocks(html.bctx);
    struct html_box_mapping_plan *plan;
    nserror err = html_box_prepare_mappings(&html, outer, old, region, html.bctx, &plan);
    ck_assert_int_eq(err, _i == 0 ? NSERROR_INVALID : NSERROR_NOT_IMPLEMENTED);
    ck_assert_ptr_null(plan);
    ck_assert_uint_eq(talloc_total_blocks(html.bctx), blocks);
    ck_assert_ptr_eq(box_for_node(outer), old);
    child->node = saved;
    region->root->next = NULL;
    box_construct_region_destroy(region);
    dom_node_unref(outer);
    dom_node_unref(outside);
}
END_TEST

#ifdef WISP_TEST_ALLOC_FAILURE
START_TEST(test_mapping_allocation_failures)
{
    build_document();
    dom_node *outer = by_id("outer");
    struct box *old = box_for_node(outer);
    ck_assert_int_eq(begin_region(outer), NSERROR_OK);
    while (scheduled != NULL)
        run_slice();
    ck_assert_int_eq(region_error, NSERROR_OK);
    char *live = snapshot();
    size_t blocks = talloc_total_blocks(html.bctx);
    struct html_box_mapping_plan *plan = NULL;
    unsigned fail_after;
    for (fail_after = 0; fail_after < 512; fail_after++) {
        malloc_limit(fail_after);
        nserror err = html_box_prepare_mappings(&html, outer, old, region, html.bctx, &plan);
        malloc_limit(UINT_MAX);
        assert_live_tree(live, outer, old);
        if (err == NSERROR_OK)
            break;
        ck_assert_msg(err == NSERROR_NOMEM, "Allocation %u returned %d", fail_after, err);
        ck_assert_ptr_null(plan);
        ck_assert_uint_eq(talloc_total_blocks(html.bctx), blocks);
    }
    ck_assert_uint_lt(fail_after, 512);
    ck_assert_uint_gt(fail_after, 64); /* Failures include many reserved DOM slots. */
    html_box_discard_mappings(plan);
    realloc_limit(0);
    nserror err = html_box_prepare_mappings(&html, outer, old, region, html.bctx, &plan);
    realloc_limit(UINT_MAX);
    ck_assert_int_eq(err, NSERROR_NOMEM);
    ck_assert_ptr_null(plan);
    ck_assert_uint_eq(talloc_total_blocks(html.bctx), blocks);
    ck_assert_int_eq(html_box_prepare_mappings(&html, outer, old, region, html.bctx, &plan), NSERROR_OK);
    html.base.locked = html.restyling = true;
    malloc_limit(0);
    realloc_limit(0);
    err = html_box_apply_mappings(plan);
    html_box_discard_mappings(plan);
    malloc_limit(UINT_MAX);
    realloc_limit(UINT_MAX);
    ck_assert_int_eq(err, NSERROR_OK);
    assert_live_mappings(region->root);
    box_construct_region_destroy(region);
    free(live);
    dom_node_unref(outer);
}
END_TEST
#endif

/* Compare the same mixed subtree under block/flex/grid parents and with each
 * root formatter. Comparison includes generated boxes, normalization and styles. */
START_TEST(test_detached_region)
{
    const char *displays[] = {"block", "flex", "grid"};
    dom_node *outer = by_id("outer"), *body = by_id("body"), *split = by_id("split");
    attribute(outer, "style", _i < 3 ? "display:block" : "display:inline");
    char style[128];
    snprintf(style, sizeof(style), "display:%s;font-size:23px;--region-color:#abcdef", displays[_i % 3]);
    attribute(_i < 3 ? outer : body, "style", style);
    attribute(split, "style", "font-size:2rem;color:var(--region-color)");
    build_document();
    struct box *old = box_for_node(outer);
    char *expected = snapshot_box(old), *live = snapshot();
    size_t blocks = talloc_total_blocks(html.bctx);
    ck_assert_int_eq(begin_region(outer), NSERROR_OK);
    run_slice();
    ck_assert_ptr_nonnull(scheduled);
    ck_assert_uint_eq(region_completions, 0);
    assert_live_tree(live, outer, old);
    while (scheduled != NULL)
        run_slice();
    ck_assert_uint_eq(region_completions, 1);
    ck_assert_int_eq(region_error, NSERROR_OK);
    ck_assert_ptr_nonnull(region);
    ck_assert_ptr_null(region->root->parent);
    char *actual = snapshot_box(region->root);
    ck_assert_msg(strcmp(actual, expected) == 0, "Detached structure/styles differ for display variant %d", _i);
    free(actual);
    assert_live_tree(live, outer, old);
    box_construct_region_destroy(region);
    ck_assert_uint_eq(talloc_total_blocks(html.bctx), blocks);
    assert_live_tree(live, outer, old);
    free(expected);
    free(live);
    dom_node_unref(split);
    dom_node_unref(body);
    dom_node_unref(outer);
}
END_TEST

START_TEST(test_detached_inherited_parent)
{
    dom_node *body = by_id("body");
    dom_node *link = element(body, "a", "block-link", NULL);
    attribute(link, "style", "display:block;font-size:23px;color:#445566;opacity:0;--size:3em");
    attribute(link, "title", "parent title");
    attribute(link, "href", "parent.html");
    attribute(link, "target", "parent-target");
    dom_node *child = element(link, "div", "region-child", NULL);
    attribute(child, "style", "padding-left:var(--size)");
    text(child, "inherited text");
    text(element(child, "span", NULL, NULL), "inside span");
    build_document();
    char *expected = snapshot_box(box_for_node(child));
    ck_assert_int_eq(begin_region(child), NSERROR_OK);
    while (scheduled != NULL)
        run_slice();
    ck_assert_int_eq(region_error, NSERROR_OK);
    struct box *root = region->root;
    ck_assert_ptr_nonnull(root->href);
    ck_assert_str_eq(root->title, "parent title");
    ck_assert_str_eq(root->target, "parent-target");
    ck_assert(root->flags & IS_OPACITY_ZERO);
    char *actual = snapshot_box(root);
    ck_assert_str_eq(actual, expected);
    free(actual);
    free(expected);
    box_construct_region_destroy(region);
    dom_node_unref(body);
}
END_TEST

START_TEST(test_detached_visibility)
{
    build_document();
    dom_node *node = by_id(_i == 0 ? "outer" : "hidden");
    struct box *old = box_for_node(node);
    char *live = snapshot();
    attribute(node, "style", _i == 0 ? "display:none" : "display:block");
    ck_assert_int_eq(nscss_invalidate_subtree(node, NULL), NSERROR_OK);
    ck_assert_int_eq(begin_region(node), NSERROR_OK);
    while (scheduled != NULL)
        run_slice();
    ck_assert_int_eq(region_error, NSERROR_OK);
    ck_assert_ptr_nonnull(region);
    if (_i == 0)
        ck_assert_ptr_null(region->root);
    else
        ck_assert_ptr_nonnull(region->root);
    box_construct_region_destroy(region);
    assert_live_tree(live, node, old);
    free(live);
    dom_node_unref(node);
}
END_TEST

START_TEST(test_detached_reject_boundary)
{
    dom_node *outer = by_id("outer");
    const char *styles[] = {"display:inline", "float:left", "display:table", "position:absolute", "position:fixed"};
    if (_i < 5)
        attribute(outer, "style", styles[_i]);
    build_document();
    if (_i < 5) {
        ck_assert_int_eq(begin_region(outer), NSERROR_NOT_IMPLEMENTED);
    } else {
        /* A changed style can make an initially eligible root unsupported. */
        struct box *old = box_for_node(outer);
        char *live = snapshot();
        attribute(outer, "style", styles[_i - 5]);
        ck_assert_int_eq(nscss_invalidate_subtree(outer, NULL), NSERROR_OK);
        ck_assert_int_eq(begin_region(outer), NSERROR_OK);
        while (scheduled != NULL)
            run_slice();
        ck_assert_int_eq(region_error, NSERROR_NOT_IMPLEMENTED);
        ck_assert_ptr_null(region);
        assert_live_tree(live, outer, old);
        free(live);
    }
    ck_assert_ptr_null(scheduled);
    dom_node_unref(outer);
}
END_TEST

/* Add binding-bearing content only after the reference tree is built. No
 * fetch/form service is installed: any accidental request must fail this test. */
static const char *unsafe_tags[] = {"input", "select", "textarea", "canvas", "frameset", "body"};
static const char *resource_css[] = {"#outer {background-image:url(first.png)}",
    "#outer {background-image:linear-gradient(red,blue),url(second.png)}",
    "#outer li {list-style-image:url(marker.png)}", "#outer::before {content:url(before.png)}",
    "#outer::after {content:url(after.png)}", "#outer::after {content:'after';background-image:url(pseudo.png)}"};

START_TEST(test_detached_reject_bindings)
{
    nsoption_set_bool(background_images, true);
    nsoption_set_bool(foreground_images, true);
    build_document();
    dom_node *outer = by_id("outer");
    struct box *old = box_for_node(outer);
    char *live = snapshot();
    dom_node *node = element(outer, unsafe_tags[_i], NULL, NULL);
    attribute(node, "src", "unrequested.png");
    attribute(node, "type", "text");
    attribute(node, "checked", "checked");
    ck_assert_int_eq(nscss_invalidate_subtree(outer, NULL), NSERROR_OK);
    size_t blocks = talloc_total_blocks(html.bctx);
    ck_assert_int_eq(begin_region(outer), NSERROR_OK);
    while (scheduled != NULL)
        run_slice();
    ck_assert_msg(region_error == NSERROR_NOT_IMPLEMENTED, "Binding variant %d returned %d", _i, region_error);
    ck_assert_ptr_null(region);
    ck_assert_uint_eq(region_completions, 1);
    ck_assert_uint_eq(talloc_total_blocks(html.bctx), blocks);
    assert_live_tree(live, outer, old);
    free(live);
    dom_node_unref(outer);
}
END_TEST

static css_stylesheet *add_sheet(const char *css)
{
    css_stylesheet *extra;
    css_stylesheet_params params = {.params_version = CSS_STYLESHEET_PARAMS_VERSION_1,
        .level = CSS_LEVEL_DEFAULT,
        .charset = "UTF-8",
        .url = "file:///context.html",
        .resolve = nscss_resolve_url};
    ck_assert_int_eq(css_stylesheet_create(&params, &extra), CSS_OK);
    css_error error = css_stylesheet_append_data(extra, (const uint8_t *)css, strlen(css));
    ck_assert_msg(error == CSS_OK || error == CSS_NEEDDATA, "CSS parse failed: %d", error);
    ck_assert_int_eq(css_stylesheet_data_done(extra), CSS_OK);
    ck_assert_int_eq(css_select_ctx_append_sheet(html.select_ctx, extra, CSS_ORIGIN_AUTHOR, NULL), CSS_OK);
    return extra;
}

static void remove_sheet(css_stylesheet *extra)
{
    ck_assert_int_eq(css_select_ctx_remove_sheet(html.select_ctx, extra), CSS_OK);
    css_stylesheet_destroy(extra);
}

static struct box *region_box(struct box *box, dom_node *node)
{
    if (box->node == node && box->type != BOX_INLINE_END)
        return box;
    for (struct box *child = box->children; child != NULL; child = child->next) {
        struct box *found = region_box(child, node);
        if (found != NULL)
            return found;
    }
    return NULL;
}

/* Use the parser here to establish actual DOM form ownership, including its
 * control index. A candidate must neither reuse nor unlink those gadgets. */
START_TEST(test_detached_controls)
{
    const char *markup = "<html id=root><body id=body><div id=outer><form id=form>"
                         "<input id=radio type=radio name=tabs value=first>"
                         "<input id=checkbox type=CHECKBOX name=toggle checked value=on>"
                         "<input id=hidden type=hidden name=secret value=kept>"
                         "<input id=submit type=submit name=send value=Send>"
                         "<input id=reset type=reset name=reset value=Reset>"
                         "<input id=image type=image name=image src=button.png>"
                         "<button id=button name=button value=value><span>Native button</span></button>"
                         "</form><input id=formless type=button value=Outside></div></body></html>";
    dom_hubbub_parser *parser;
    dom_hubbub_parser_params params = {.enc = "UTF-8", .fix_enc = true};
    dom_node_unref(html.document);
    ck_assert_int_eq(dom_hubbub_parser_create(&params, &parser, &html.document), DOM_HUBBUB_OK);
    ck_assert_int_eq(dom_hubbub_parser_parse_chunk(parser, (const uint8_t *)markup, strlen(markup)), DOM_HUBBUB_OK);
    ck_assert_int_eq(dom_hubbub_parser_completed(parser), DOM_HUBBUB_OK);
    dom_hubbub_parser_destroy(parser);
    html.forms = html_forms_get_forms(html.encoding, (dom_html_document *)html.document);
    ck_assert_ptr_nonnull(html.forms);
    dom_node *outer = by_id("outer");
    for (unsigned i = 0; i < 160; i++)
        text(element(outer, "span", NULL, NULL), "tail");
    record_fetches = true;
    build_document();
    record_fetches = false;
    const char *ids[] = {"radio", "checkbox", "submit", "reset", "image", "button", "formless"};
    enum { count = sizeof(ids) / sizeof(ids[0]) };
    dom_node *nodes[count];
    struct form_control *controls[count], saved[count];
    for (unsigned i = 0; i < count; i++) {
        nodes[i] = by_id(ids[i]);
        controls[i] = box_for_node(nodes[i])->gadget;
        ck_assert_ptr_nonnull(controls[i]);
        ck_assert_ptr_eq(controls[i]->form, i == count - 1 ? NULL : html.forms);
        saved[i] = *controls[i];
    }
    struct form saved_form = *html.forms;
    char *live = snapshot(), *expected = snapshot_box(box_for_node(outer));
    size_t blocks = talloc_total_blocks(html.bctx);
    for (unsigned pass = 0; pass < 3; pass++) {
        if (pass > 0) {
            /* Checked state comes from the current DOM, not the old gadget or
             * the checked attribute. The displayed gadget remains unchanged. */
            ck_assert_int_eq(dom_html_input_element_set_checked((dom_html_input_element *)nodes[0], true), DOM_NO_ERR);
            ck_assert_int_eq(dom_html_input_element_set_checked((dom_html_input_element *)nodes[1], false), DOM_NO_ERR);
        }
        ck_assert_int_eq(begin_region(outer), NSERROR_OK);
        run_slice();
        ck_assert_ptr_nonnull(scheduled);
        if (_i == 1) {
            ck_assert_int_eq(cancel_dom_to_box(region_context), NSERROR_OK);
            region_context = NULL;
        } else if (_i == 2) {
            reject_schedule = true;
            run_slice();
            ck_assert_int_eq(region_error, NSERROR_NOMEM);
            ck_assert_ptr_null(region);
            reject_schedule = false;
        } else {
            while (scheduled != NULL)
                run_slice();
            ck_assert_int_eq(region_error, NSERROR_OK);
            if (pass == 0) {
                char *actual = snapshot_box(region->root);
                ck_assert_str_eq(actual, expected);
                free(actual);
            }
            for (unsigned i = 0; i < count; i++) {
                struct box *box = region_box(region->root, nodes[i]);
                ck_assert_ptr_nonnull(box);
                struct form_control *gadget = box->gadget;
                ck_assert_ptr_nonnull(gadget);
                ck_assert_ptr_ne(gadget, controls[i]);
                ck_assert_ptr_eq(gadget->box, box);
                ck_assert_ptr_eq(gadget->html, &html);
                ck_assert_ptr_null(gadget->form);
                ck_assert_ptr_null(gadget->prev);
                ck_assert_ptr_null(gadget->next);
                ck_assert_int_eq(gadget->selected, pass > 0 && i < 2 ? i == 0 : saved[i].selected);
            }
            ck_assert_ptr_nonnull(region->objects);
            ck_assert_ptr_eq(region->objects->box->node, nodes[4]);
            ck_assert_str_eq(nsurl_access(region->objects->url), "file:///button.png");
            struct html_form_binding_plan *bindings;
            ck_assert_int_eq(html_form_prepare_bindings(&html, box_for_node(outer), region->root, region, &bindings),
                NSERROR_OK);
            ck_assert_int_eq(html_form_check_bindings(bindings), NSERROR_OK);
            html_form_discard_bindings(bindings);
            box_construct_region_destroy(region);
        }
        ck_assert_mem_eq(html.forms, &saved_form, sizeof(saved_form));
        for (unsigned i = 0; i < count; i++) {
            ck_assert_mem_eq(controls[i], &saved[i], sizeof(saved[i]));
            if (controls[i]->form != NULL)
                ck_assert_ptr_eq(html_forms_get_control_for_node(html.forms, nodes[i]), controls[i]);
        }
        ck_assert_uint_eq(talloc_total_blocks(html.bctx), blocks);
        char *actual = snapshot();
        ck_assert_str_eq(actual, live);
        free(actual);
        assert_live_mappings(html.layout);
        ck_assert_uint_eq(html.num_objects, 1);
        ck_assert_int_eq(html.base.active, 1);
    }
    free(expected);
    free(live);
    for (unsigned i = 0; i < count; i++)
        dom_node_unref(nodes[i]);
    dom_node_unref(outer);
}
END_TEST

START_TEST(test_detached_reject_input_widgets)
{
    const char *types[] = {"text", "password", "file", "search", "unknown", ""};
    build_document();
    dom_node *outer = by_id("outer");
    dom_node *input = element(outer, "input", NULL, NULL);
    attribute(input, "type", types[_i]);
    attribute(input, "value", "untouched");
    size_t blocks = talloc_total_blocks(html.bctx);
    ck_assert_int_eq(begin_region(outer), NSERROR_OK);
    while (scheduled != NULL)
        run_slice();
    ck_assert_msg(region_error == NSERROR_NOT_IMPLEMENTED, "Input type %s returned %d", types[_i], region_error);
    ck_assert_ptr_null(region);
    ck_assert_uint_eq(talloc_total_blocks(html.bctx), blocks);
    dom_node_unref(outer);
}
END_TEST

static struct html_inline_svg *add_inline_svg(dom_node *parent, bool cached)
{
    dom_node *svg = element(parent, "svg", NULL, NULL);
    attribute(svg, "viewBox", "0 0 20 20");
    attribute(svg, "style", "width:20px;height:20px;color:#345678;fill:currentColor");
    dom_node *rect = element(svg, "rect", NULL, NULL);
    attribute(rect, "width", "20");
    attribute(rect, "height", "20");
    struct html_inline_svg *entry = calloc(1, sizeof(*entry));
    ck_assert_ptr_nonnull(entry);
    entry->node = dom_node_ref(svg);
    if (cached) {
        entry->svg_xml = strdup("<svg xmlns='http://www.w3.org/2000/svg'><rect width='20' height='20'/></svg>");
        ck_assert_ptr_nonnull(entry->svg_xml);
        entry->svg_xml_len = strlen(entry->svg_xml);
    }
    entry->next = html.inline_svgs;
    html.inline_svgs = entry;
    return entry;
}

START_TEST(test_detached_svg_buffers)
{
    dom_node *outer = by_id("outer");
    struct html_inline_svg *entry = add_inline_svg(outer, _i == 1);
    struct html_inline_svg saved = *entry;
    record_fetches = true;
    build_document();
    record_fetches = false;
    ck_assert_ptr_nonnull(fetched_buffer);
    char *expected = snapshot_box(box_for_node(outer));
    struct content_html_object saved_object = *html.object_list;
    size_t blocks = talloc_total_blocks(html.bctx);
    for (unsigned pass = 0; pass < 3; pass++) {
        ck_assert_int_eq(begin_region(outer), NSERROR_OK);
        while (scheduled != NULL)
            run_slice();
        ck_assert_int_eq(region_error, NSERROR_OK);
        struct box_construct_object *request = region->objects;
        ck_assert_ptr_nonnull(request);
        ck_assert_ptr_null(request->next);
        ck_assert_ptr_null(request->url);
        ck_assert_ptr_eq(request->box->node, entry->node);
        ck_assert_ptr_ne(request->buffer, entry->svg_xml);
        ck_assert_ptr_eq(talloc_parent(request->buffer), request);
        ck_assert_str_eq(request->mime_type, "image/svg+xml");
        ck_assert_int_eq(request->permitted_types, CONTENT_IMAGE);
        ck_assert_uint_eq(request->buffer_length, fetched_buffer_length);
        ck_assert_mem_eq(request->buffer, fetched_buffer, fetched_buffer_length);
        ck_assert_mem_eq(entry, &saved, sizeof(saved));
        char *actual = snapshot_box(region->root);
        ck_assert_str_eq(actual, expected);
        free(actual);
        /* Staged bytes are independent of the parser's optional cache. */
        if (entry->svg_xml != NULL) {
            entry->svg_xml[0] = '!';
            ck_assert_mem_eq(request->buffer, fetched_buffer, fetched_buffer_length);
            entry->svg_xml[0] = '<';
        }
        box_construct_region_destroy(region);
        ck_assert_uint_eq(talloc_total_blocks(html.bctx), blocks);
        ck_assert_mem_eq(html.object_list, &saved_object, sizeof(saved_object));
        ck_assert_uint_eq(html.num_objects, 1);
        ck_assert_int_eq(html.base.active, 1);
        assert_live_mappings(html.layout);
    }
    free(expected);
    dom_node_unref(outer);
}
END_TEST

static void compare_requests(const struct box_construct_region *candidate)
{
    const struct box_construct_object *request = candidate->objects;
    const struct content_html_object *live = html.object_list;
    unsigned count = 0;
    for (; request != NULL && live != NULL; request = request->next, live = live->next) {
        ck_assert(nsurl_compare(request->url, live->url, NSURL_COMPLETE));
        ck_assert_ptr_ne(request->box, live->box);
        ck_assert_ptr_eq(request->box->node, live->node);
        ck_assert_int_eq(request->permitted_types, live->permitted_types);
        ck_assert_int_eq(request->background, live->background);
        ck_assert_uint_eq(request->background_layer, live->background_layer);
        ck_assert_ptr_null(request->box->object);
        ck_assert_ptr_null(request->box->background);
        ck_assert_ptr_eq(talloc_parent(request), candidate);
        if (request->background && request->background_layer > 0) {
            ck_assert_ptr_nonnull(request->box->background_layers);
            ck_assert_ptr_null(request->box->background_layers[request->background_layer - 1]);
        }
        count++;
    }
    ck_assert_ptr_null(request);
    ck_assert_ptr_null(live);
    ck_assert_uint_eq(count, html.num_objects);
}

/* The normal builder and the detached builder must make the same request
 * decisions. Preserve duplicate URLs, generated requests without a DOM node,
 * URL changes, layer indices, image alternatives and object parameters. */
START_TEST(test_detached_resource_requests)
{
    nsoption_set_bool(background_images, true);
    nsoption_set_bool(foreground_images, true);
    css_stylesheet *extra = add_sheet(resource_css[_i]);
    dom_node *outer = by_id("outer");
    for (unsigned i = 0; i < 2; i++) {
        dom_node *img = element(outer, "img", NULL, NULL);
        attribute(img, "src", "same.png");
        attribute(img, "alt", " image   text ");
        attribute(img, "style", "width:40px;height:30px");
        attribute(img, "usemap", "#image-map");
    }
    dom_node *responsive = element(outer, "img", "responsive", NULL);
    attribute(responsive, "src", "fallback.png");
    attribute(responsive, "srcset", "small.png 400w, large.png 800w");
    dom_node *object = element(outer, "object", "object", NULL);
    attribute(object, "codebase", "objects/");
    attribute(object, "data", "plot.svg");
    dom_node *param = element(object, "param", NULL, NULL);
    attribute(param, "name", "quality");
    attribute(param, "value", "high");
    text(element(object, "div", "object-fallback", NULL), "fallback");
    dom_node *embed = element(outer, "embed", "embed", NULL);
    attribute(embed, "src", "embedded.svg");
    attribute(embed, "quality", "high");
    record_fetches = true;
    build_document();
    record_fetches = false;
    ck_assert_uint_eq(html.num_objects, _i == 2 ? 7 : 6);
    struct content_html_object *live_objects = html.object_list;
    struct box *old = box_for_node(outer);
    char *live = snapshot(), *expected = snapshot_box(old);
    unsigned count = html.num_objects;
    int active = html.base.active;
    size_t blocks = talloc_total_blocks(html.bctx);
    for (unsigned i = 0; i < 3; i++) {
        ck_assert_int_eq(begin_region(outer), NSERROR_OK);
        run_slice();
        ck_assert_ptr_nonnull(scheduled);
        ck_assert_ptr_eq(html.object_list, live_objects);
        while (scheduled != NULL)
            run_slice();
        ck_assert_int_eq(region_error, NSERROR_OK);
        compare_requests(region);
        char *actual = snapshot_box(region->root);
        ck_assert_str_eq(actual, expected);
        free(actual);
        box_construct_region_destroy(region);
        actual = snapshot();
        ck_assert_str_eq(actual, live);
        free(actual);
        ck_assert_ptr_eq(html.object_list, live_objects);
        ck_assert_uint_eq(html.num_objects, count);
        ck_assert_int_eq(html.base.active, active);
        ck_assert_uint_eq(talloc_total_blocks(html.bctx), blocks);
        assert_live_mappings(html.layout);
    }
    free(live);
    free(expected);
    dom_node_unref(outer);
    remove_sheet(extra);
}
END_TEST

START_TEST(test_detached_resource_identity)
{
    dom_node *outer = by_id("outer");
    dom_node *img = element(outer, "img", NULL, NULL);
    attribute(img, "src", "original.png");
    record_fetches = true;
    build_document();
    record_fetches = false;
    struct content_html_object *live = html.object_list;
    ck_assert_ptr_nonnull(live);
    ck_assert_ptr_null(live->next);
    if (_i == 0)
        live->box = NULL; /* Resource retained from a previously hidden panel. */
    else
        attribute(img, "src", "changed.png");
    struct content_html_object saved = *live;
    size_t blocks = talloc_total_blocks(html.bctx);
    ck_assert_int_eq(begin_region(outer), NSERROR_OK);
    while (scheduled != NULL)
        run_slice();
    ck_assert_int_eq(region_error, NSERROR_OK);
    const struct box_construct_object *request = region->objects;
    ck_assert_ptr_nonnull(request);
    ck_assert_ptr_null(request->next);
    ck_assert_ptr_eq(request->box->node, img);
    ck_assert_str_eq(nsurl_access(request->url), _i == 0 ? "file:///original.png" : "file:///changed.png");
    box_construct_region_destroy(region);
    ck_assert_mem_eq(live, &saved, sizeof(saved));
    ck_assert_str_eq(nsurl_access(live->url), "file:///original.png");
    ck_assert_uint_eq(talloc_total_blocks(html.bctx), blocks);
    assert_live_mappings(html.layout);
    dom_node_unref(outer);
}
END_TEST

#ifdef WISP_TEST_ALLOC_FAILURE
START_TEST(test_detached_control_allocation_failure)
{
    dom_node *outer = by_id("outer");
    dom_node *node = element(outer, _i == 0 ? "button" : "input", NULL, NULL);
    attribute(node, "type", "radio");
    attribute(node, "name", "control");
    attribute(node, "value", "value");
    struct form_control *control = NULL;
    for (unsigned fail_after = 0; fail_after < 2; fail_after++) {
        malloc_limit(fail_after);
        nserror error = html_forms_create_detached_control(node, &control);
        malloc_limit(UINT_MAX);
        ck_assert_int_eq(error, NSERROR_NOMEM);
        ck_assert_ptr_null(control); /* Never replace failure with a fake gadget. */
    }
    ck_assert_int_eq(html_forms_create_detached_control(node, &control), NSERROR_OK);
    ck_assert_ptr_null(control->form);
    ck_assert_str_eq(control->name, "control");
    ck_assert_str_eq(control->value, "value");
    form_free_control(control);
    dom_node_unref(outer);
}
END_TEST

START_TEST(test_staged_request_allocation_failure)
{
    build_document();
    dom_node *outer = by_id("outer");
    ck_assert_int_eq(begin_region(outer), NSERROR_OK);
    while (scheduled != NULL)
        run_slice();
    ck_assert_int_eq(region_error, NSERROR_OK);
    nsurl *url;
    ck_assert_int_eq(nsurl_create("file:///image.png", &url), NSERROR_OK);
    ck_assert(box_construct_request_object(&html, region, url, region->root, CONTENT_IMAGE, true, 0));
    struct box_construct_object *first = region->objects;
    size_t blocks = talloc_total_blocks(region);
    malloc_limit(0);
    bool ok = box_construct_request_object(&html, region, url, region->root, CONTENT_IMAGE, true, 0);
    malloc_limit(UINT_MAX);
    ck_assert(!ok);
    ck_assert_ptr_eq(region->objects, first);
    ck_assert_ptr_null(first->next);
    ck_assert_uint_eq(talloc_total_blocks(region), blocks);
    for (unsigned fail_after = 0; fail_after < 3; fail_after++) {
        const uint8_t svg[] = "<svg/>";
        malloc_limit(fail_after);
        ok = box_construct_request_buffer(
            &html, region, svg, sizeof(svg) - 1, "image/svg+xml", region->root, CONTENT_IMAGE);
        malloc_limit(UINT_MAX);
        ck_assert_msg(!ok, "Buffer staging should fail at allocation %u", fail_after);
        ck_assert_ptr_eq(region->objects, first);
        ck_assert_uint_eq(talloc_total_blocks(region), blocks);
    }
    ck_assert_ptr_null(html.object_list);
    ck_assert_int_eq(html.base.active, 0);
    box_construct_region_destroy(region);
    nsurl_unref(url);
    dom_node_unref(outer);
}
END_TEST
#endif

START_TEST(test_detached_iframes)
{
    dom_node *outer = by_id("outer");
    dom_node *iframe = element(outer, "iframe", "frame", NULL);
    attribute(iframe, "src", "frame.html");
    attribute(iframe, "name", "reader-frame");
    attribute(iframe, "frameborder", "0");
    attribute(iframe, "scrolling", "no");
    attribute(iframe, "marginwidth", "3");
    attribute(iframe, "marginheight", "5");
    text(element(iframe, "div", "frame-fallback", NULL), "fallback");
    dom_node *hidden = element(outer, "iframe", "hidden-frame", NULL);
    attribute(hidden, "src", "hidden.html");
    attribute(hidden, "style", "display:none");
    build_document();
    struct content_html_iframe *live = html.iframe;
    ck_assert_ptr_nonnull(live);
    ck_assert_ptr_null(live->next);
    struct content_html_iframe saved = *live;
    size_t blocks = talloc_total_blocks(html.bctx);
    for (unsigned i = 0; i < 3; i++) {
        ck_assert_int_eq(begin_region(outer), NSERROR_OK);
        while (scheduled != NULL)
            run_slice();
        ck_assert_int_eq(region_error, NSERROR_OK);
        const struct content_html_iframe *staged = region->iframes;
        ck_assert_ptr_nonnull(staged);
        ck_assert_ptr_null(staged->next);
        ck_assert_ptr_eq(talloc_parent(staged), region);
        ck_assert_ptr_ne(staged->box, live->box);
        ck_assert_ptr_eq(staged->box->node, iframe);
        ck_assert(nsurl_compare(staged->url, live->url, NSURL_COMPLETE));
        ck_assert_str_eq(staged->name, live->name);
        ck_assert_int_eq(staged->scrolling, live->scrolling);
        ck_assert_int_eq(staged->border, live->border);
        ck_assert_int_eq(staged->margin_width, live->margin_width);
        ck_assert_int_eq(staged->margin_height, live->margin_height);
        ck_assert_ptr_null(staged->box->iframe);
        box_construct_region_destroy(region);
        ck_assert_ptr_eq(html.iframe, live);
        ck_assert_mem_eq(live, &saved, sizeof(saved));
        ck_assert_uint_eq(talloc_total_blocks(html.bctx), blocks);
        assert_live_mappings(html.layout);
    }
    dom_node_unref(outer);
}
END_TEST

/* Retiring an incomplete candidate must release every staged URL/descriptor,
 * even after an earlier slice succeeded. No live service is installed. */
START_TEST(test_detached_staging_cleanup)
{
    dom_node *outer = by_id("outer");
    attribute(outer, "style", "background:url(tile.png)");
    dom_node *iframe = element(outer, "iframe", NULL, NULL);
    attribute(iframe, "src", "frame.html");
    struct html_inline_svg *svg = add_inline_svg(outer, false);
    dom_node *input = element(outer, "input", NULL, NULL);
    attribute(input, "type", "checkbox");
    attribute(input, "checked", "checked");
    dom_node *first, *inserted;
    ck_assert_int_eq(dom_node_get_first_child(outer, &first), DOM_NO_ERR);
    ck_assert_int_eq(dom_node_insert_before(outer, iframe, first, &inserted), DOM_NO_ERR);
    dom_node_unref(first);
    dom_node_unref(inserted);
    ck_assert_int_eq(dom_node_insert_before(outer, svg->node, iframe, &inserted), DOM_NO_ERR);
    dom_node_unref(inserted);
    ck_assert_int_eq(dom_node_insert_before(outer, input, iframe, &inserted), DOM_NO_ERR);
    dom_node_unref(inserted);
    record_fetches = true;
    build_document();
    record_fetches = false;
    struct content_html_object *objects = html.object_list;
    struct content_html_iframe *iframes = html.iframe;
    struct content_html_object saved_object = *objects;
    struct content_html_iframe saved_iframe = *iframes;
    char *live = snapshot();
    size_t blocks = talloc_total_blocks(html.bctx);
    if (_i == 2)
        element(outer, "input", NULL, NULL); /* Refused after resource staging. */
    ck_assert_int_eq(begin_region(outer), NSERROR_OK);
    if (_i == 1)
        reject_schedule = true;
    run_slice();
    if (_i == 0) {
        ck_assert_ptr_nonnull(scheduled);
        ck_assert_int_eq(cancel_dom_to_box(region_context), NSERROR_OK);
        region_context = NULL;
        ck_assert_uint_eq(region_completions, 0);
    } else {
        while (scheduled != NULL)
            run_slice();
        ck_assert_int_eq(region_error, _i == 1 ? NSERROR_NOMEM : NSERROR_NOT_IMPLEMENTED);
        ck_assert_ptr_null(region);
        ck_assert_uint_eq(region_completions, 1);
    }
    ck_assert_ptr_eq(html.object_list, objects);
    ck_assert_mem_eq(objects, &saved_object, sizeof(saved_object));
    ck_assert_ptr_eq(html.iframe, iframes);
    ck_assert_mem_eq(iframes, &saved_iframe, sizeof(saved_iframe));
    ck_assert_uint_eq(html.num_objects, 2);
    ck_assert_int_eq(html.base.active, 2);
    ck_assert_uint_eq(talloc_total_blocks(html.bctx), blocks);
    char *actual = snapshot();
    ck_assert_str_eq(actual, live);
    free(actual);
    free(live);
    dom_node_unref(outer);
}
END_TEST

START_TEST(test_detached_cancel_or_fail)
{
    build_document();
    dom_node *outer = by_id("outer");
    struct box *old = box_for_node(outer);
    char *live = snapshot();
    size_t blocks = talloc_total_blocks(html.bctx);
    if (_i == 2)
        reject_schedule = true;
    ck_assert_int_eq(begin_region(outer), _i == 2 ? NSERROR_NOMEM : NSERROR_OK);
    if (_i == 1 || _i == 4)
        run_slice();
    if (_i == 4) {
        html.restyle.partial = true;
        html.restyle.pending.full_reason = "test changed inputs";
        run_slice();
        ck_assert_int_eq(region_error, NSERROR_INVALID);
        ck_assert_uint_eq(region_completions, 1);
        html.restyle.partial = false;
        html.restyle.pending.full_reason = NULL;
    } else if (_i == 3) {
        reject_schedule = true;
        run_slice();
        ck_assert_int_eq(region_error, NSERROR_NOMEM);
        ck_assert_uint_eq(region_completions, 1);
    } else if (_i != 2) {
        ck_assert_int_eq(cancel_dom_to_box(region_context), NSERROR_OK);
        region_context = NULL;
    }
    if (_i != 3 && _i != 4)
        ck_assert_uint_eq(region_completions, 0);
    ck_assert_ptr_null(region_context);
    ck_assert_ptr_null(scheduled);
    ck_assert_uint_eq(talloc_total_blocks(html.bctx), blocks);
    assert_live_tree(live, outer, old);
    free(live);
    dom_node_unref(outer);
}
END_TEST
#endif

int main(void)
{
    static struct gui_misc_table misc = {.schedule = schedule};
    static struct wisp_table gui = {.misc = &misc};
    guit = &gui;
    if (nsoption_init(NULL, NULL, NULL) != NSERROR_OK || corestrings_init() != NSERROR_OK ||
        css_hint_init() != NSERROR_OK)
        return EXIT_FAILURE;
    Suite *suite = suite_create("Construction context");
    TCase *tc = tcase_create("Ancestry and lifetime");
    tcase_add_checked_fixture(tc, setup, teardown);
    tcase_add_test(tc, test_unpublished_ancestor_mappings);
    tcase_add_loop_test(tc, test_cancel_construction, 0, 2);
    tcase_add_test(tc, test_initial_schedule_failure);
    tcase_add_test(tc, test_continuation_schedule_failure);
    tcase_add_loop_test(tc, test_list_image_cascade, 0, 4);
#ifdef WISP_ENABLE_PARTIAL_UPDATES
    tcase_add_loop_test(tc, test_boundary_candidate, 0, 6);
    tcase_add_loop_test(tc, test_boundary_crossings, 0, 13);
    tcase_add_test(tc, test_boundary_candidate_borrows_old);
    tcase_add_loop_test(tc, test_boundary_after_layout, 0, 3);
#ifdef WISP_TEST_ALLOC_FAILURE
    tcase_add_test(tc, test_boundary_allocation_failures);
#endif
    tcase_add_loop_test(tc, test_mapping_publication, 0, 3);
    tcase_add_loop_test(tc, test_mapping_cancel_or_stale, 0, 8);
    tcase_add_loop_test(tc, test_mapping_reject_escaping_or_duplicate_nodes, 0, 3);
#ifdef WISP_TEST_ALLOC_FAILURE
    tcase_add_test(tc, test_mapping_allocation_failures);
#endif
    tcase_add_loop_test(tc, test_live_region_owners, 0, 3);
    tcase_add_loop_test(tc, test_live_owner_ineligible, 0, 7);
    tcase_add_loop_test(tc, test_live_owner_cancel_or_fail, 0, 3);
    tcase_add_loop_test(tc, test_detached_region, 0, 3);
    /* Inline roots are blockified only under flex/grid parents. */
    tcase_add_loop_test(tc, test_detached_region, 4, 6);
    tcase_add_test(tc, test_detached_inherited_parent);
    tcase_add_loop_test(tc, test_detached_visibility, 0, 2);
    tcase_add_loop_test(tc, test_detached_reject_boundary, 0, 10);
    tcase_add_loop_test(tc, test_detached_reject_bindings, 0, sizeof(unsafe_tags) / sizeof(unsafe_tags[0]));
    tcase_add_loop_test(tc, test_detached_cancel_or_fail, 0, 5);
    tcase_add_loop_test(tc, test_detached_resource_requests, 0, sizeof(resource_css) / sizeof(resource_css[0]));
    tcase_add_loop_test(tc, test_detached_resource_identity, 0, 2);
    tcase_add_test(tc, test_detached_iframes);
    tcase_add_loop_test(tc, test_detached_controls, 0, 3);
    tcase_add_loop_test(tc, test_detached_reject_input_widgets, 0, 6);
    tcase_add_loop_test(tc, test_detached_svg_buffers, 0, 2);
#ifdef WISP_TEST_ALLOC_FAILURE
    tcase_add_test(tc, test_staged_request_allocation_failure);
    tcase_add_loop_test(tc, test_detached_control_allocation_failure, 0, 2);
#endif
    tcase_add_loop_test(tc, test_detached_staging_cleanup, 0, 3);
#endif
    suite_add_tcase(suite, tc);
    SRunner *runner = srunner_create(suite);
    srunner_run_all(runner, CK_NORMAL);
    int failed = srunner_ntests_failed(runner);
    srunner_free(runner);
    css_hint_fini();
    corestrings_fini();
    nsoption_finalise(NULL, NULL);
    return failed ? EXIT_FAILURE : EXIT_SUCCESS;
}

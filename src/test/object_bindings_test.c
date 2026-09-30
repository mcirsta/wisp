/* Resource publication must preserve callback records until a complete plan
 * can be applied. Cache access is mocked; matching and rebinding are real. */
#include <check.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <wisp/content.h>
#include <wisp/content/hlcache.h>
#include <wisp/content/handlers/html/private.h>
#include <wisp/content/handlers/html/html.h>
#include <wisp/desktop/gui_internal.h>
#include <wisp/misc.h>
#include <wisp/utils/nsurl.h>
#include "content/handlers/html/box_construct.h"
#include "content/handlers/html/object.h"
#include "utils/talloc.h"
#ifdef WISP_TEST_ALLOC_FAILURE
#include "test/malloc_fig.h"
#endif

struct cached_image {
    content_type type;
    content_status status;
    const uint8_t *data;
    size_t length;
    lwc_string *mime;
};

static bool expect_fetch;
static unsigned fetches;

nserror hlcache_handle_retrieve_buffer(const uint8_t *data, size_t length, const char *mime_type,
    hlcache_handle_callback callback, void *pw, hlcache_child_context *child, content_type accepted_types,
    hlcache_handle **result)
{
    ck_assert_msg(expect_fetch, "Resource publication started a buffer fetch");
    fetches++;
    *result = NULL;
    return NSERROR_NOMEM;
}

content_type content_get_type(struct hlcache_handle *handle)
{
    return ((struct cached_image *)handle)->type;
}

content_status content_get_status(struct hlcache_handle *handle)
{
    return ((struct cached_image *)handle)->status;
}

const uint8_t *content_get_source_data(struct hlcache_handle *handle, size_t *length)
{
    struct cached_image *image = (struct cached_image *)handle;
    *length = image->length;
    return image->data;
}

lwc_string *content_get_mime_type(struct hlcache_handle *handle)
{
    struct cached_image *image = (struct cached_image *)handle;
    return image->mime == NULL ? NULL : lwc_string_ref(image->mime);
}

nserror content_open(
    struct hlcache_handle *handle, struct browser_window *bw, struct content *page, struct object_params *params)
{
    ck_abort_msg("Binding preparation/publication called content_open");
    return NSERROR_INVALID;
}

static nserror schedule(int delay, void (*callback)(void *), void *pw)
{
    ck_abort_msg("Binding preparation/publication scheduled a callback");
    return NSERROR_INVALID;
}

static html_content html;
static struct box root, old[3], next[3], outside;
static struct content_html_object objects[4];
static struct cached_image images[4];
static struct box_construct_region candidate;
static struct box_construct_object requests[3];
static uint8_t svg[] = "<svg fill='#123456'/>";
static void *owner;
static nsurl *url;
static int identities[3];

static struct hlcache_handle *handle(unsigned i)
{
    return (struct hlcache_handle *)&images[i];
}

static void setup(void)
{
    expect_fetch = false;
    fetches = 0;
    memset(&html, 0, sizeof(html));
    memset(&root, 0, sizeof(root));
    memset(old, 0, sizeof(old));
    memset(next, 0, sizeof(next));
    memset(objects, 0, sizeof(objects));
    memset(images, 0, sizeof(images));
    memset(&candidate, 0, sizeof(candidate));
    memset(requests, 0, sizeof(requests));
    memset(&outside, 0, sizeof(outside));
    owner = talloc_new(NULL);
    ck_assert_ptr_nonnull(owner);
    ck_assert_int_eq(nsurl_create("https://example.test/image.png", &url), NSERROR_OK);
    html.object_list = objects;
    html.num_objects = 4;
    html.bw = (struct browser_window *)&html; /* Identity only; never dereferenced. */
    candidate.objects = requests;
    root.type = BOX_BLOCK;
    for (unsigned i = 0; i < 4; i++) {
        images[i].type = CONTENT_IMAGE;
        images[i].status = CONTENT_STATUS_DONE;
        images[i].data = svg;
        images[i].length = sizeof(svg) - 1;
        ck_assert_int_eq(lwc_intern_string("image/svg+xml", 13, &images[i].mime), lwc_error_ok);
        objects[i].parent = &html.base;
        objects[i].box = i == 3 ? &outside : &old[i];
        objects[i].content = handle(i);
        objects[i].url = url;
        objects[i].permitted_types = CONTENT_IMAGE;
        objects[i].opened = objects[i].layout_resource = true;
        objects[i].next = i == 3 ? NULL : &objects[i + 1];
        if (i == 3)
            continue;
        old[i].parent = &root;
        old[i].type = next[i].type = BOX_INLINE_BLOCK;
        old[i].object = handle(i);
        old[i].node = next[i].node = (dom_node *)&identities[i];
        objects[i].node = old[i].node;
        requests[i].box = &next[i];
        requests[i].url = url;
        requests[i].permitted_types = CONTENT_IMAGE;
        requests[i].next = i == 2 ? NULL : &requests[i + 1];
    }
    outside.object = handle(3);
}

static void teardown(void)
{
    talloc_free(owner);
    nsurl_unref(url);
    for (unsigned i = 0; i < 4; i++)
        lwc_string_unref(images[i].mime);
}

static nserror prepare(struct html_object_binding_plan **plan)
{
    return html_object_prepare_bindings(&html, &root, &candidate, owner, plan);
}

static void lock_publication(void)
{
    html.base.locked = true;
    html.restyling = true;
}

START_TEST(test_rebind_and_retire)
{
    if (_i == 1) {
        /* Source/destination slots preserve CSS layer numbers, including gaps. */
        old[1].background_layers = talloc_zero_array(owner, struct hlcache_handle *, 3);
        next[1].background_layers = talloc_zero_array(owner, struct hlcache_handle *, 3);
        old[1].background_layers[2] = old[1].object;
        old[1].object = NULL;
        objects[1].background = requests[1].background = true;
        objects[1].background_layer = requests[1].background_layer = 3;
        old[2].background = old[2].object;
        old[2].object = NULL;
        objects[2].background = requests[2].background = true;
    } else if (_i == 2) {
        objects[1].url = NULL;
        requests[1].url = NULL;
        requests[1].buffer = svg;
        requests[1].buffer_length = sizeof(svg) - 1;
        requests[1].mime_type = "image/svg+xml";
    } else if (_i == 3) {
        objects[1].content = old[1].object = NULL; /* Preserve a failed URL. */
    } else if (_i == 4) {
        objects[1].box = NULL; /* Already loaded while this panel was hidden. */
    }
    struct content_html_object saved[4];
    memcpy(saved, objects, sizeof(saved));
    size_t blocks = talloc_total_blocks(owner);
    struct html_object_binding_plan *plan;
    ck_assert_int_eq(prepare(&plan), NSERROR_OK);
    ck_assert_mem_eq(objects, saved, sizeof(saved));
    for (unsigned i = 0; i < 3; i++) {
        ck_assert_ptr_null(next[i].object);
        ck_assert_ptr_null(next[i].background);
    }
    lock_publication();
    ck_assert_int_eq(html_object_apply_bindings(plan), NSERROR_OK);
    for (unsigned i = 0; i < 3; i++) {
        ck_assert_ptr_eq(objects[i].box, &next[i]);
        ck_assert_ptr_eq(objects[i].content, saved[i].content);
        ck_assert_ptr_eq(objects[i].parent, saved[i].parent);
        ck_assert_ptr_eq(objects[i].next, saved[i].next);
        ck_assert_ptr_eq(objects[i].node, saved[i].node);
        ck_assert_ptr_eq(objects[i].url, saved[i].url);
        ck_assert_int_eq(objects[i].opened, saved[i].opened);
        if (objects[i].background)
            ck_assert_ptr_eq(box_background_layer(&next[i], objects[i].background_layer), objects[i].content);
        else
            ck_assert_ptr_eq(next[i].object, objects[i].content);
        if (saved[i].box != NULL)
            ck_assert_ptr_null(
                saved[i].background ? box_background_layer(&old[i], saved[i].background_layer) : old[i].object);
    }
    ck_assert_mem_eq(&objects[3], &saved[3], sizeof(saved[3]));
    ck_assert_ptr_eq(outside.object, handle(3));
    ck_assert_uint_eq(html.num_objects, 4);
    ck_assert_int_eq(html.base.active, 0);
    ck_assert_int_eq(html_object_apply_bindings(plan), NSERROR_INVALID);
    html_object_discard_bindings(plan);
    ck_assert_uint_eq(talloc_total_blocks(owner), blocks);
    ck_assert_ptr_eq(next[0].object, handle(0));
}
END_TEST

START_TEST(test_discard_or_reject)
{
    nsurl *changed = NULL;
    switch (_i) {
    case 0:
        images[2].status = CONTENT_STATUS_LOADING;
        break;
    case 1:
        images[2].status = CONTENT_STATUS_READY;
        break;
    case 2:
        images[2].type = CONTENT_HTML;
        break;
    case 3:
        objects[2].permitted_types = CONTENT_ANY;
        break;
    case 4:
        objects[2].refresh_pending = true;
        break;
    case 5:
        objects[2].opened = false;
        break;
    case 6:
        ck_assert_int_eq(nsurl_create("https://example.test/changed.png", &changed), NSERROR_OK);
        requests[2].url = changed;
        break;
    case 7:
        objects[2].background_layer = 1;
        break;
    case 8:
        objects[2].layout_resource = false;
        break;
    case 9:
        objects[2].box = &outside;
        break;
    case 10:
        requests[2].permitted_types = CONTENT_ANY;
        break;
    case 11:
        break; /* Successful prepare followed by cancellation. */
    }
    struct content_html_object saved[4];
    memcpy(saved, objects, sizeof(saved));
    size_t blocks = talloc_total_blocks(owner);
    struct html_object_binding_plan *plan = NULL;
    nserror error = prepare(&plan);
    ck_assert_msg(error == (_i == 11 ? NSERROR_OK : NSERROR_NOT_IMPLEMENTED), "variant %d: %d", _i, error);
    html_object_discard_bindings(plan);
    ck_assert_mem_eq(objects, saved, sizeof(saved));
    ck_assert_uint_eq(talloc_total_blocks(owner), blocks);
    for (unsigned i = 0; i < 3; i++)
        ck_assert_ptr_null(next[i].object);
    if (changed != NULL)
        nsurl_unref(changed);
}
END_TEST

START_TEST(test_generated_duplicates)
{
    /* All resources share a URL and have no DOM node. A retained outside box
     * must keep its own callback record, and duplicates cannot share records. */
    for (unsigned i = 0; i < 4; i++)
        objects[i].node = NULL;
    for (unsigned i = 0; i < 3; i++)
        next[i].node = NULL;
    if (_i == 1)
        objects[2].box = &outside;
    struct html_object_binding_plan *plan;
    ck_assert_int_eq(prepare(&plan), _i == 1 ? NSERROR_NOT_IMPLEMENTED : NSERROR_OK);
    if (_i == 0) {
        lock_publication();
        ck_assert_int_eq(html_object_apply_bindings(plan), NSERROR_OK);
        ck_assert_ptr_ne(next[0].object, next[1].object);
        ck_assert_ptr_ne(next[0].object, next[2].object);
        ck_assert_ptr_ne(next[1].object, next[2].object);
    }
    ck_assert_ptr_eq(objects[3].box, &outside);
    html_object_discard_bindings(plan);
}
END_TEST

START_TEST(test_svg_identity)
{
    objects[2].url = NULL;
    requests[2].url = NULL;
    requests[2].buffer = svg;
    requests[2].buffer_length = sizeof(svg) - 1;
    requests[2].mime_type = "image/svg+xml";
    switch (_i) {
    case 0:
        images[2].data = (const uint8_t *)"<svg fill='#654321'/>";
        break;
    case 1:
        images[2].length--;
        break;
    case 2:
        requests[2].mime_type = "image/png";
        break;
    case 3:
        images[2].data = NULL;
        break;
    case 4:
        objects[2].content = NULL;
        break;
    }
    struct html_object_binding_plan *plan;
    ck_assert_int_eq(prepare(&plan), NSERROR_NOT_IMPLEMENTED);
    ck_assert_ptr_null(plan);
    ck_assert_ptr_eq(objects[2].box, &old[2]);
}
END_TEST

START_TEST(test_full_refresh_svg_identity)
{
    /* The full-refresh fallback also needs content identity: a new color/child
     * cannot silently rebind the previous SVG just because its DOM node matches. */
    dom_document *document;
    dom_element *node;
    dom_string *tag;
    ck_assert_int_eq(
        dom_implementation_create_document(DOM_IMPLEMENTATION_HTML, NULL, NULL, NULL, NULL, NULL, &document),
        DOM_NO_ERR);
    ck_assert_int_eq(dom_string_create((const uint8_t *)"svg", 3, &tag), DOM_NO_ERR);
    ck_assert_int_eq(dom_document_create_element(document, tag, &node), DOM_NO_ERR);
    dom_string_unref(tag);
    objects[2].node = next[2].node = (dom_node *)node;
    objects[2].url = NULL;
    objects[2].box = NULL;
    if (_i == 1)
        images[2].data = (const uint8_t *)"<svg fill='#654321'/>";
    else if (_i == 2)
        images[2].length--;
    else if (_i == 3)
        objects[2].content = NULL;
    expect_fetch = _i == 1 || _i == 2;
    bool ok = html_fetch_object_buffer(&html, svg, sizeof(svg) - 1, "image/svg+xml", &next[2], CONTENT_IMAGE);
    ck_assert_int_eq(ok, !expect_fetch);
    ck_assert_uint_eq(fetches, expect_fetch ? 1 : 0);
    ck_assert_ptr_eq(objects[2].box, expect_fetch ? NULL : &next[2]);
    ck_assert_ptr_eq(next[2].object, _i == 0 ? handle(2) : NULL);
    ck_assert_uint_eq(html.num_objects, 4);
    ck_assert_int_eq(html.base.active, 0);
    dom_node_unref(node);
    dom_node_unref(document);
}
END_TEST

START_TEST(test_stale_plan_is_atomic)
{
    struct html_object_binding_plan *plan;
    ck_assert_int_eq(prepare(&plan), NSERROR_OK);
    lock_publication();
    switch (_i) {
    case 0:
        objects[2].box = NULL;
        break;
    case 1:
        objects[2].content = NULL;
        break;
    case 2:
        images[2].status = CONTENT_STATUS_READY;
        break;
    case 3:
        html.object_list = &objects[1];
        break;
    case 4:
        objects[1].next = &objects[3];
        break; /* Missing middle record. */
    case 5:
        html.num_objects++;
        break;
    case 6:
        html.bw = NULL;
        break;
    case 7:
        html.base.locked = false;
        break;
    case 8:
        objects[2].opened = false;
        break;
    case 9:
        html.restyling = false;
        break;
    }
    struct content_html_object saved[4];
    memcpy(saved, objects, sizeof(saved));
    ck_assert_int_eq(html_object_apply_bindings(plan), NSERROR_INVALID);
    ck_assert_mem_eq(objects, saved, sizeof(saved));
    for (unsigned i = 0; i < 3; i++) {
        ck_assert_ptr_null(next[i].object);
        ck_assert_ptr_eq(old[i].object, handle(i));
    }
    html_object_discard_bindings(plan);
}
END_TEST

START_TEST(test_hide_show_plateau)
{
    lock_publication();
    size_t blocks = talloc_total_blocks(owner);
    for (unsigned pass = 0; pass < 100; pass++) {
        struct html_object_binding_plan *plan;
        candidate.objects = NULL;
        ck_assert_int_eq(prepare(&plan), NSERROR_OK);
        ck_assert_int_eq(html_object_apply_bindings(plan), NSERROR_OK);
        html_object_discard_bindings(plan);
        for (unsigned i = 0; i < 3; i++) {
            ck_assert_ptr_null(objects[i].box);
            requests[i].box = &old[i];
        }
        candidate.objects = requests;
        ck_assert_int_eq(html_object_prepare_bindings(&html, NULL, &candidate, owner, &plan), NSERROR_OK);
        ck_assert_int_eq(html_object_apply_bindings(plan), NSERROR_OK);
        html_object_discard_bindings(plan);
        for (unsigned i = 0; i < 3; i++)
            ck_assert_ptr_eq(old[i].object, handle(i));
        ck_assert_ptr_eq(objects[3].box, &outside);
        ck_assert_uint_eq(html.num_objects, 4);
        ck_assert_int_eq(html.base.active, 0);
        ck_assert_uint_eq(talloc_total_blocks(owner), blocks);
    }
}
END_TEST

#ifdef WISP_TEST_ALLOC_FAILURE
START_TEST(test_allocation_failure)
{
    struct html_object_binding_plan *plan;
    size_t blocks = talloc_total_blocks(owner);
    struct content_html_object saved[4];
    memcpy(saved, objects, sizeof(saved));
    malloc_limit(_i);
    nserror error = prepare(&plan);
    malloc_limit(UINT_MAX);
    ck_assert_int_eq(error, NSERROR_NOMEM);
    ck_assert_ptr_null(plan);
    ck_assert_mem_eq(objects, saved, sizeof(saved));
    ck_assert_uint_eq(talloc_total_blocks(owner), blocks);
    ck_assert_int_eq(prepare(&plan), NSERROR_OK);
    lock_publication();
    malloc_limit(0);
    error = html_object_apply_bindings(plan);
    malloc_limit(UINT_MAX);
    ck_assert_int_eq(error, NSERROR_OK); /* Publication performs no allocation. */
    html_object_discard_bindings(plan);
    ck_assert_uint_eq(talloc_total_blocks(owner), blocks);
}
END_TEST
#endif

int main(void)
{
    static struct gui_misc_table misc = {.schedule = schedule};
    static struct wisp_table gui = {.misc = &misc};
    guit = &gui;
    Suite *suite = suite_create("Prepared resource bindings");
    TCase *tc = tcase_create("Publication");
    tcase_add_checked_fixture(tc, setup, teardown);
    tcase_add_loop_test(tc, test_rebind_and_retire, 0, 5);
    tcase_add_loop_test(tc, test_discard_or_reject, 0, 12);
    tcase_add_loop_test(tc, test_generated_duplicates, 0, 2);
    tcase_add_loop_test(tc, test_svg_identity, 0, 5);
    tcase_add_loop_test(tc, test_full_refresh_svg_identity, 0, 4);
    tcase_add_loop_test(tc, test_stale_plan_is_atomic, 0, 10);
    tcase_add_test(tc, test_hide_show_plateau);
#ifdef WISP_TEST_ALLOC_FAILURE
    tcase_add_loop_test(tc, test_allocation_failure, 0, 2);
#endif
    suite_add_tcase(suite, tc);
    SRunner *runner = srunner_create(suite);
    srunner_run_all(runner, CK_NORMAL);
    int failed = srunner_ntests_failed(runner);
    srunner_free(runner);
    return failed ? EXIT_FAILURE : EXIT_SUCCESS;
}

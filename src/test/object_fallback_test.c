/* Exercise the real resource callbacks and shared fallback decision with a
 * controlled cache boundary. Network timing must not decide which paths run. */
#include <check.h>
#include <stdlib.h>
#include <string.h>
#include <dom/dom.h>
#include <libdom/bindings/hubbub/parser.h>
#include <wisp/content.h>
#include <wisp/content/hlcache.h>
#include <wisp/content/handlers/html/private.h>
#include <wisp/content/handlers/html/html.h>
#include <wisp/desktop/gui_internal.h>
#include <wisp/misc.h>
#include <wisp/utils/corestrings.h>
#include <wisp/utils/nsurl.h>
#include "content/handlers/html/box_special.h"
#include "content/handlers/html/object.h"
#include "utils/talloc.h"

static html_content html;
static struct box box;
static nsurl *url;
static dom_node *node;
static unsigned fetches, releases, restyles, opens;
static uint32_t request_flags;
static int cache_identity;
static nserror retrieve_error;
static bool synchronous_error;
static content_status resource_status;
static hlcache_handle_callback cache_callback;
static void *cache_pw;

static hlcache_handle *handle(void)
{
    return (hlcache_handle *)&cache_identity;
}

nsurl *content_get_url(struct content *content)
{
    ck_assert_ptr_eq(content, &html.base);
    return html.base_url;
}

content_type content_get_type(hlcache_handle *content)
{
    ck_assert_ptr_eq(content, handle());
    return CONTENT_IMAGE;
}

content_status content_get_status(hlcache_handle *content)
{
    ck_assert_ptr_eq(content, handle());
    return resource_status;
}

nserror content_open(hlcache_handle *content, struct browser_window *bw, struct content *parent,
    struct object_params *params)
{
    ck_assert_ptr_eq(content, handle());
    ck_assert_ptr_eq(parent, &html.base);
    ck_assert_ptr_eq(bw, html.bw);
    opens++;
    return NSERROR_OK;
}

nserror hlcache_handle_release(hlcache_handle *content)
{
    ck_assert_ptr_eq(content, handle());
    releases++;
    return NSERROR_OK;
}

void html_request_restyle(html_content *content)
{
    ck_assert_ptr_eq(content, &html);
    html.restyle_pending = true;
    restyles++;
}

static nserror schedule(int delay, void (*callback)(void *), void *pw)
{
    /* Only cancel a pending refresh or queue a deferred reformat. Restyling
     * itself is recorded above, so no construction runs inside the callback. */
    ck_assert(delay < 0 || callback == html_deferred_reformat);
    return NSERROR_OK;
}

static void fail_request(void)
{
    hlcache_event event = {.type = CONTENT_MSG_ERROR};
    ck_assert_int_eq(cache_callback(handle(), &event, cache_pw), NSERROR_OK);
}

nserror hlcache_handle_retrieve(nsurl *request_url, uint32_t flags, nsurl *referer, llcache_post_data *post,
    hlcache_handle_callback callback, void *pw, hlcache_child_context *child, content_type accepted_types,
    hlcache_handle **result)
{
    fetches++;
    request_flags = flags;
    ck_assert_int_eq(html.base.active, 1);
    if (retrieve_error != NSERROR_OK)
        return retrieve_error;
    cache_callback = callback;
    cache_pw = pw;
    *result = handle();
    if (synchronous_error)
        fail_request();
    return NSERROR_OK;
}

static dom_node *by_id(const char *id)
{
    dom_string *key;
    dom_node *result = NULL;
    ck_assert_int_eq(dom_string_create((const uint8_t *)id, strlen(id), &key), DOM_NO_ERR);
    ck_assert_int_eq(dom_document_get_element_by_id(html.document, key, &result), DOM_NO_ERR);
    dom_string_unref(key);
    ck_assert_ptr_nonnull(result);
    return result;
}

static void setup(void)
{
    memset(&html, 0, sizeof(html));
    memset(&box, 0, sizeof(box));
    fetches = releases = restyles = opens = 0;
    request_flags = 0;
    retrieve_error = NSERROR_OK;
    synchronous_error = false;
    resource_status = CONTENT_STATUS_LOADING;
    cache_callback = NULL;
    cache_pw = NULL;
    const char source[] = "<!doctype html><object id=object data=broken.png>fallback</object>"
                          "<object id=other data=broken.png>other</object><img id=image><embed id=embed>"
                          "<img id=image-alt src=broken.png alt='alternative'>";
    dom_hubbub_parser *parser;
    dom_hubbub_parser_params params = {.enc = "UTF-8", .fix_enc = true};
    ck_assert_int_eq(dom_hubbub_parser_create(&params, &parser, &html.document), DOM_HUBBUB_OK);
    ck_assert_int_eq(dom_hubbub_parser_parse_chunk(parser, (const uint8_t *)source, sizeof(source) - 1), DOM_HUBBUB_OK);
    ck_assert_int_eq(dom_hubbub_parser_completed(parser), DOM_HUBBUB_OK);
    dom_hubbub_parser_destroy(parser);
    ck_assert_int_eq(nsurl_create("https://example.test/", &html.base_url), NSERROR_OK);
    ck_assert_int_eq(nsurl_create("https://example.test/broken.png", &url), NSERROR_OK);
    node = by_id("object");
    box.node = node;
    box.type = BOX_BLOCK;
    html.base.status = CONTENT_STATUS_LOADING;
}

static void teardown(void)
{
    html_object_free_objects(&html);
    dom_node_unref(node);
    dom_node_unref(html.document);
    nsurl_unref(url);
    nsurl_unref(html.base_url);
}

enum failure_timing { VISIBLE, HIDDEN, REBUILDING, REBUILDING_HIDDEN, SYNCHRONOUS_CALLBACK, RETRIEVAL_ERROR, TIMINGS };

START_TEST(test_image_failure_changes_representation)
{
    dom_node *image = by_id("image-alt");
    box.node = image;
    box.flags |= IS_REPLACED;
    if (_i == SYNCHRONOUS_CALLBACK)
        synchronous_error = true;
    if (_i == RETRIEVAL_ERROR)
        retrieve_error = NSERROR_NO_FETCH_HANDLER;
    ck_assert(html_fetch_object(&html, url, &box, CONTENT_IMAGE, false));
    ck_assert(!(request_flags & HLCACHE_RETRIEVE_REQUIRE_OK));
    if (_i < SYNCHRONOUS_CALLBACK) {
        ck_assert_int_eq(html_object_get_state(&html, image, url), HTML_OBJECT_PENDING);
        if (_i == HIDDEN || _i == REBUILDING_HIDDEN)
            html.object_list->box = NULL;
        html.restyling = _i == REBUILDING || _i == REBUILDING_HIDDEN;
        fail_request();
    }
    ck_assert_msg(html_object_get_state(&html, image, url) == HTML_OBJECT_FAILED,
        "Image failure lost at timing %d", _i);
    ck_assert_msg(restyles == 1, "Image alt needs reconstruction at timing %d, got %u", _i, restyles);
    ck_assert_int_eq(html.base.active, 0);
    ck_assert_uint_eq(releases, _i == RETRIEVAL_ERROR ? 0 : 1);
    dom_node_unref(image);
}
END_TEST

START_TEST(test_failure_survives_boxes)
{
    box.flags |= IS_REPLACED;
    if (_i == SYNCHRONOUS_CALLBACK)
        synchronous_error = true;
    if (_i == RETRIEVAL_ERROR)
        retrieve_error = NSERROR_NO_FETCH_HANDLER;
    ck_assert(html_fetch_object(&html, url, &box, CONTENT_ANY, false));
    ck_assert(request_flags & HLCACHE_RETRIEVE_REQUIRE_OK);
    struct content_html_object *object = html.object_list;
    ck_assert_ptr_nonnull(object);
    if (_i < SYNCHRONOUS_CALLBACK) {
        ck_assert(html_object_get_state(&html, node, url) != HTML_OBJECT_FAILED);
        object->opened = true;
        if (_i == HIDDEN || _i == REBUILDING_HIDDEN)
            object->box = NULL;
        else
            box.object = handle(); /* A READY object can fail before DONE. */
        html.restyling = _i == REBUILDING || _i == REBUILDING_HIDDEN;
        fail_request();
    }
    ck_assert_msg(html_object_get_state(&html, node, url) == HTML_OBJECT_FAILED, "Lost failure at timing %d", _i);
    ck_assert_ptr_null(object->content);
    ck_assert_ptr_null(box.object);
    ck_assert(!object->opened);
    ck_assert_int_eq(html.base.active, 0);
    ck_assert_uint_eq(releases, _i == RETRIEVAL_ERROR ? 0 : 1);
    ck_assert_uint_eq(restyles, 1);
    ck_assert(html.restyle_pending);

    /* Discovery in an unboxed subtree and reconstruction share the decision. */
    void *owner = talloc_new(NULL);
    struct object_params *params = NULL;
    for (unsigned i = 0; i < 3; i++) {
        object->box = NULL;
        ck_assert(box_object_prepare(node, &html, owner, &params));
        ck_assert_ptr_null(params);
        ck_assert_uint_eq(talloc_total_blocks(owner), 1);
        ck_assert_uint_eq(fetches, 1);
        ck_assert_uint_eq(html.num_objects, 1);
    }
    /* The generic fetch reuse path must also retain the failed request. */
    ck_assert(html_fetch_object(&html, url, &box, CONTENT_ANY, false));
    ck_assert_ptr_eq(object->box, &box);
    ck_assert_uint_eq(fetches, 1);

    dom_node *other = by_id("other");
    ck_assert(box_object_prepare(other, &html, owner, &params));
    ck_assert_ptr_nonnull(params); /* Same URL on another element is independent. */
    talloc_free(params);
    dom_node_unref(other);
    dom_string *changed;
    ck_assert_int_eq(dom_string_create((const uint8_t *)"new.png", 7, &changed), DOM_NO_ERR);
    ck_assert_int_eq(dom_element_set_attribute(node, corestring_dom_data, changed), DOM_NO_ERR);
    dom_string_unref(changed);
    ck_assert(box_object_prepare(node, &html, owner, &params));
    ck_assert_ptr_nonnull(params);
    ck_assert(html_object_get_state(&html, node, params->data) == HTML_OBJECT_UNKNOWN);
    talloc_free(owner);
}
END_TEST

START_TEST(test_pending_failure_keeps_fallback)
{
    ck_assert(html_fetch_object(&html, url, &box, CONTENT_ANY, false));
    ck_assert_int_eq(html_object_get_state(&html, node, url), HTML_OBJECT_PENDING);
    ck_assert(!(box.flags & IS_REPLACED));
    fail_request();
    ck_assert_int_eq(html_object_get_state(&html, node, url), HTML_OBJECT_FAILED);
    ck_assert_ptr_null(box.object);
    ck_assert_int_eq(html.base.active, 0);
    ck_assert_uint_eq(releases, 1);
    ck_assert_uint_eq(restyles, 0);
    ck_assert(!html.restyle_pending);
}
END_TEST

START_TEST(test_pending_becomes_available)
{
    ck_assert_int_eq(html_object_get_state(&html, node, url), HTML_OBJECT_UNKNOWN);
    ck_assert(html_fetch_object(&html, url, &box, CONTENT_ANY, false));
    ck_assert_int_eq(html_object_get_state(&html, node, url), HTML_OBJECT_PENDING);
    struct content_html_object *object = html.object_list;
    if (_i == HIDDEN || _i == REBUILDING_HIDDEN)
        object->box = NULL;
    html.restyling = _i == REBUILDING || _i == REBUILDING_HIDDEN;

    /* Neither event may attach the resource to a tree of fallback children. */
    resource_status = CONTENT_STATUS_READY;
    hlcache_event event = {.type = CONTENT_MSG_READY};
    ck_assert_int_eq(cache_callback(handle(), &event, cache_pw), NSERROR_OK);
    ck_assert_int_eq(html_object_get_state(&html, node, url), HTML_OBJECT_AVAILABLE);
    ck_assert_ptr_null(box.object);
    ck_assert_int_eq(html.base.active, 1);
    ck_assert_uint_eq(restyles, 1);
    resource_status = CONTENT_STATUS_DONE;
    event.type = CONTENT_MSG_DONE;
    ck_assert_int_eq(cache_callback(handle(), &event, cache_pw), NSERROR_OK);
    ck_assert_ptr_null(box.object);
    ck_assert_int_eq(html.base.active, 0);
    ck_assert(html.restyle_pending);

    /* Reconstruction reuses the request and attaches its successful result. */
    object->box = NULL;
    box.flags |= IS_REPLACED;
    ck_assert(html_fetch_object(&html, url, &box, CONTENT_ANY, false));
    ck_assert_ptr_eq(box.object, handle());
    ck_assert_uint_eq(fetches, 1);
}
END_TEST

START_TEST(test_ready_resource_rebinds_before_done)
{
    html.bw = (struct browser_window *)&cache_identity;
    html.base.status = CONTENT_STATUS_READY;
    ck_assert(html_fetch_object(&html, url, &box, CONTENT_ANY, false));
    ck_assert_int_eq(html_object_open_objects(&html, html.bw), NSERROR_OK);
    ck_assert_uint_eq(opens, 0);
    hlcache_event event = {.type = CONTENT_MSG_LOADING};
    ck_assert_int_eq(cache_callback(handle(), &event, cache_pw), NSERROR_OK);
    ck_assert_uint_eq(opens, 0);
    resource_status = CONTENT_STATUS_READY;
    event.type = CONTENT_MSG_READY;
    ck_assert_int_eq(cache_callback(handle(), &event, cache_pw), NSERROR_OK);
    ck_assert_ptr_null(box.object);
    ck_assert_uint_eq(restyles, 1);

    /* An embedded HTML document may need its containing box's layout to
     * finish. Rebuilding must attach/open READY content, without waiting for
     * DONE or opening a document into its provisional fallback geometry. */
    html.object_list->box = NULL;
    box.flags |= IS_REPLACED;
    ck_assert(html_fetch_object(&html, url, &box, CONTENT_ANY, false));
    ck_assert_ptr_eq(box.object, handle());
    ck_assert_uint_eq(opens, 1);
    ck_assert_int_eq(html.base.active, 1);
    ck_assert_uint_eq(fetches, 1);
    ck_assert_int_eq(html_object_open_objects(&html, html.bw), NSERROR_OK);
    ck_assert_uint_eq(opens, 1);
}
END_TEST

START_TEST(test_decode_failure_after_done)
{
    box.flags |= IS_REPLACED;
    ck_assert(html_fetch_object(&html, url, &box, CONTENT_ANY, false));
    resource_status = CONTENT_STATUS_DONE;
    hlcache_event event = {.type = CONTENT_MSG_DONE};
    ck_assert_int_eq(cache_callback(handle(), &event, cache_pw), NSERROR_OK);
    ck_assert_int_eq(html.base.active, 0);
    ck_assert_ptr_eq(box.object, handle());
    if (_i == HIDDEN || _i == REBUILDING_HIDDEN) {
        html.object_list->box = NULL;
        box.object = NULL;
    }
    html.restyling = _i == REBUILDING || _i == REBUILDING_HIDDEN;
    /* An unrelated request must not be completed by this late failure. */
    html.base.active = 1;
    fail_request();
    ck_assert_msg(html.base.active == 1, "Late error changed active count at timing %d", _i);
    ck_assert_int_eq(html_object_get_state(&html, node, url), HTML_OBJECT_FAILED);
    ck_assert_ptr_null(box.object);
    ck_assert_uint_eq(restyles, 1);
    ck_assert_uint_eq(releases, 1);
}
END_TEST

START_TEST(test_non_object_failures)
{
    const char *id = _i == 0 ? "image" : _i == 1 ? "embed" : "object";
    dom_node *target = by_id(id);
    box.node = target;
    bool background = _i == 2 || _i == 3;
    content_type types = _i == 1 || _i == 4 ? CONTENT_ANY : CONTENT_IMAGE;
    hlcache_handle *layers[] = {handle()};
    box.background_layers = layers;
    if (_i == 3)
        ck_assert(html_fetch_background(&html, url, &box, types, 1));
    else
        ck_assert(html_fetch_object(&html, url, &box, types, background));
    ck_assert_msg(!!(request_flags & HLCACHE_RETRIEVE_REQUIRE_OK) == (_i == 4),
        "HTTP error policy must apply to object elements only (variant %d)", _i);
    if (background)
        box.background = handle();
    else
        box.object = handle();
    if (_i == 4)
        html.aborted = true;
    fail_request();
    ck_assert_uint_eq(restyles, 0);
    ck_assert_uint_eq(releases, 1);
    ck_assert_int_eq(html.base.active, 0);
    if (!background)
        ck_assert_ptr_null(box.object);
    else if (_i == 2)
        ck_assert_ptr_null(box.background);
    else
        ck_assert_ptr_null(layers[0]);
    dom_node_unref(target);
}
END_TEST

START_TEST(test_memory_failure_is_not_fallback)
{
    retrieve_error = NSERROR_NOMEM;
    ck_assert(!html_fetch_object(&html, url, &box, CONTENT_ANY, false));
    ck_assert_ptr_null(html.object_list);
    ck_assert_uint_eq(html.num_objects, 0);
    ck_assert_int_eq(html.base.active, 0);
    ck_assert_uint_eq(restyles, 0);
    ck_assert(html_object_get_state(&html, node, url) != HTML_OBJECT_FAILED);
}
END_TEST

START_TEST(test_null_handle_is_not_failure)
{
    ck_assert(html_fetch_object(&html, url, &box, CONTENT_ANY, false));
    /* Stopping a request clears its handle; it is not a load/decode failure. */
    html.object_list->content = NULL;
    ck_assert(html_object_get_state(&html, node, url) != HTML_OBJECT_FAILED);
}
END_TEST

START_TEST(test_completion_waits_for_fallback)
{
    html.base.status = CONTENT_STATUS_READY;
    html.restyle_pending = _i == 0;
    html.restyling = _i == 1;
    ck_assert_int_eq(html_proceed_to_done(&html), NSERROR_OK);
    ck_assert_int_eq(html.base.status, CONTENT_STATUS_READY);
}
END_TEST

int main(void)
{
    static struct gui_misc_table misc = {.schedule = schedule};
    static struct wisp_table gui = {.misc = &misc};
    guit = &gui;
    if (corestrings_init() != NSERROR_OK)
        return EXIT_FAILURE;
    Suite *suite = suite_create("Object resource fallback");
    TCase *tc = tcase_create("Failure lifetime");
    tcase_add_checked_fixture(tc, setup, teardown);
    tcase_add_loop_test(tc, test_failure_survives_boxes, 0, TIMINGS);
    tcase_add_loop_test(tc, test_image_failure_changes_representation, 0, TIMINGS);
    tcase_add_test(tc, test_pending_failure_keeps_fallback);
    tcase_add_loop_test(tc, test_pending_becomes_available, VISIBLE, REBUILDING_HIDDEN + 1);
    tcase_add_test(tc, test_ready_resource_rebinds_before_done);
    tcase_add_loop_test(tc, test_decode_failure_after_done, VISIBLE, REBUILDING_HIDDEN + 1);
    tcase_add_loop_test(tc, test_non_object_failures, 0, 5);
    tcase_add_test(tc, test_memory_failure_is_not_fallback);
    tcase_add_test(tc, test_null_handle_is_not_failure);
    tcase_add_loop_test(tc, test_completion_waits_for_fallback, 0, 2);
    suite_add_tcase(suite, tc);
    SRunner *runner = srunner_create(suite);
    srunner_run_all(runner, CK_NORMAL);
    int failed = srunner_ntests_failed(runner);
    srunner_free(runner);
    corestrings_fini();
    return failed ? EXIT_FAILURE : EXIT_SUCCESS;
}

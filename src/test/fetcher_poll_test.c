/* Exercise the registered CSS/JavaScript scheme operations without timers or
 * network I/O. Only the fetch core is substituted; list ownership, locking,
 * callbacks, aborts and cleanup run through the production fetchers. */
#include <check.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <wisp/content/fetch.h>
#include <wisp/content/handlers/html/private.h>
#include <wisp/utils/corestrings.h>
#include "content/fetchers.h"
#include "content/handlers/javascript/fetcher.h"

enum { CSS_FETCHER, JS_FETCHER, FETCHER_COUNT, MAX_FETCHES = 64 };
static struct {
    lwc_string *scheme;
    struct fetcher_operation_table ops;
} registered[FETCHER_COUNT];
static unsigned registrations;

struct fetch {
    unsigned kind;
    void *context;
    unsigned headers, data, finished, callbacks, removed, freed;
    long status;
    bool in_callback;
    void (*on_message)(const fetch_msg *, struct fetch *);
};
static struct fetch requests[MAX_FETCHES];
static unsigned request_count;
static struct fetch *abort_target;
static unsigned callback_trigger;

nserror fetcher_add(lwc_string *scheme, const struct fetcher_operation_table *ops)
{
    ck_assert_uint_lt(registrations, FETCHER_COUNT);
    registered[registrations].scheme = scheme;
    registered[registrations++].ops = *ops;
    ck_assert(ops->initialise(scheme));
    return NSERROR_OK;
}

void fetch_set_http_code(struct fetch *fetch, long code)
{
    fetch->status = code;
}

void fetch_remove_from_queues(struct fetch *fetch)
{
    ck_assert_uint_eq(fetch->removed++, 0);
}

void fetch_free(struct fetch *fetch)
{
    ck_assert(!fetch->in_callback);
    ck_assert_uint_eq(fetch->removed, 1);
    ck_assert_uint_eq(fetch->freed++, 0);
    registered[fetch->kind].ops.free(fetch->context);
    fetch->context = NULL;
}

void fetch_send_callback(const fetch_msg *message, struct fetch *fetch)
{
    ck_assert(!fetch->in_callback);
    ck_assert_uint_eq(fetch->freed, 0);
    fetch->in_callback = true;
    fetch->callbacks++;
    switch (message->type) {
    case FETCH_HEADER:
        fetch->headers++;
        ck_assert_uint_gt(message->data.header_or_data.len, 0);
        break;
    case FETCH_DATA:
        fetch->data++;
        ck_assert_uint_eq(message->data.header_or_data.len, strlen("p { color: red; }"));
        ck_assert_int_eq(
            memcmp(message->data.header_or_data.buf, "p { color: red; }", message->data.header_or_data.len), 0);
        break;
    case FETCH_FINISHED:
        ck_assert_uint_eq(fetch->finished++, 0);
        break;
    default:
        ck_abort_msg("Unexpected callback %d", message->type);
    }
    if (fetch->on_message != NULL)
        fetch->on_message(message, fetch);
    ck_assert_uint_eq(fetch->freed, 0);
    fetch->in_callback = false;
}

static struct fetch *add_request(unsigned kind)
{
    ck_assert_uint_lt(request_count, MAX_FETCHES);
    struct fetch *fetch = &requests[request_count++];
    nsurl *url;
    char address[64];
    fetch->kind = kind;
    if (kind == CSS_FETCHER) {
        dom_string *data;
        nsurl *base;
        uint32_t key;
        ck_assert_int_eq(nsurl_create("https://example.test/page", &base), NSERROR_OK);
        ck_assert_int_eq(
            dom_string_create((const uint8_t *)"p { color: red; }", strlen("p { color: red; }"), &data), DOM_NO_ERR);
        ck_assert_int_eq(html_css_fetcher_add_item(data, base, &key), NSERROR_OK);
        dom_string_unref(data);
        nsurl_unref(base);
        snprintf(address, sizeof(address), "x-ns-css:%u", key);
    } else {
        snprintf(address, sizeof(address), "javascript:void(%u)", request_count);
    }
    ck_assert_int_eq(nsurl_create(address, &url), NSERROR_OK);
    fetch->context = registered[kind].ops.setup(fetch, url, false, false, NULL, NULL);
    nsurl_unref(url);
    ck_assert_ptr_nonnull(fetch->context);
    ck_assert(registered[kind].ops.start(fetch->context));
    return fetch;
}

static void poll(unsigned kind)
{
    registered[kind].ops.poll(registered[kind].scheme);
}

static void check_completed(const struct fetch *fetch)
{
    ck_assert_uint_eq(fetch->headers, fetch->kind == CSS_FETCHER ? 3 : 0);
    ck_assert_uint_eq(fetch->data, fetch->kind == CSS_FETCHER ? 1 : 0);
    ck_assert_uint_eq(fetch->finished, 1);
    ck_assert_uint_eq(fetch->freed, 1);
    ck_assert_int_eq(fetch->status, fetch->kind == CSS_FETCHER ? 200 : 204);
}

static void setup(void)
{
    memset(registered, 0, sizeof(registered));
    memset(requests, 0, sizeof(requests));
    registrations = request_count = callback_trigger = 0;
    abort_target = NULL;
    ck_assert_int_eq(corestrings_init(), NSERROR_OK);
    ck_assert_int_eq(html_css_fetcher_register(), NSERROR_OK);
    ck_assert_int_eq(fetch_javascript_register(), NSERROR_OK);
}

static void teardown(void)
{
    for (unsigned i = 0; i < request_count; i++) {
        ck_assert_msg(requests[i].freed == 1, "Request %u was not freed", i);
        ck_assert_ptr_null(requests[i].context);
    }
    for (unsigned kind = 0; kind < FETCHER_COUNT; kind++) {
        poll(kind); /* Empty rings must remain safe. */
        registered[kind].ops.finalise(registered[kind].scheme);
        lwc_string_unref(registered[kind].scheme);
    }
    ck_assert_int_eq(corestrings_fini(), NSERROR_OK);
}

START_TEST(test_batch)
{
    unsigned kind = _i % FETCHER_COUNT;
    unsigned count = _i < FETCHER_COUNT ? 1 : 24;
    for (unsigned i = 0; i < count; i++)
        add_request(kind);
    poll(kind);
    for (unsigned i = 0; i < count; i++)
        check_completed(&requests[i]);
}
END_TEST

static void reenter(const fetch_msg *message, struct fetch *fetch)
{
    /* Every callback re-enters, including callbacks reached from nested polls.
     * The active contexts must stay alive and must not receive duplicate data. */
    poll(fetch->kind);
}

START_TEST(test_reentrant_batch)
{
    unsigned count = _i < FETCHER_COUNT ? 1 : 8;
    unsigned kind = _i % FETCHER_COUNT;
    for (unsigned i = 0; i < count; i++)
        add_request(kind)->on_message = reenter;
    poll(kind);
    for (unsigned i = 0; i < count; i++)
        check_completed(&requests[i]);
}
END_TEST

static void abort_during_callback(const fetch_msg *message, struct fetch *fetch)
{
    if (fetch->callbacks != callback_trigger)
        return;
    struct fetch *target = abort_target != NULL ? abort_target : fetch;
    registered[target->kind].ops.abort(target->context);
    poll(fetch->kind);
}

START_TEST(test_css_abort_during_callback)
{
    struct fetch *first = add_request(CSS_FETCHER);
    add_request(CSS_FETCHER);
    add_request(CSS_FETCHER);
    callback_trigger = _i + 1; /* Each of the three headers, data and finish. */
    first->on_message = abort_during_callback;
    poll(CSS_FETCHER);
    ck_assert_uint_eq(first->callbacks, callback_trigger);
    ck_assert_uint_eq(first->freed, 1);
    check_completed(&requests[1]);
    check_completed(&requests[2]);
}
END_TEST

START_TEST(test_abort_pending)
{
    unsigned kind = _i % FETCHER_COUNT;
    struct fetch *first = add_request(kind);
    abort_target = add_request(kind);
    add_request(kind);
    if (_i < FETCHER_COUNT) {
        registered[kind].ops.abort(first->context);
        registered[kind].ops.abort(abort_target->context);
    } else {
        first->on_message = abort_during_callback;
        callback_trigger = 1;
    }
    poll(kind);
    if (_i < FETCHER_COUNT)
        ck_assert_uint_eq(first->callbacks, 0);
    else
        check_completed(first);
    ck_assert_uint_eq(abort_target->callbacks, 0);
    ck_assert_uint_eq(abort_target->freed, 1);
    check_completed(&requests[2]);
}
END_TEST

static void enqueue_on_finish(const fetch_msg *message, struct fetch *fetch)
{
    if (message->type == FETCH_FINISHED)
        add_request(fetch->kind)->on_message = enqueue_on_finish;
}

START_TEST(test_callback_queue_is_bounded)
{
    unsigned kind = _i;
    for (unsigned i = 0; i < 3; i++)
        add_request(kind)->on_message = enqueue_on_finish;
    poll(kind);
    ck_assert_uint_eq(request_count, 6);
    for (unsigned i = 0; i < 3; i++) {
        check_completed(&requests[i]);
        ck_assert_uint_eq(requests[i + 3].callbacks, 0);
        requests[i + 3].on_message = NULL;
    }
    poll(kind);
    for (unsigned i = 3; i < request_count; i++)
        check_completed(&requests[i]);
}
END_TEST

int main(void)
{
    Suite *suite = suite_create("Scheme fetcher polling");
    TCase *test = tcase_create("batch, callbacks and lifetime");
    tcase_add_checked_fixture(test, setup, teardown);
    tcase_add_loop_test(test, test_batch, 0, 2 * FETCHER_COUNT);
    tcase_add_loop_test(test, test_reentrant_batch, 0, 2 * FETCHER_COUNT);
    tcase_add_loop_test(test, test_css_abort_during_callback, 0, 5);
    tcase_add_loop_test(test, test_abort_pending, 0, 2 * FETCHER_COUNT);
    tcase_add_loop_test(test, test_callback_queue_is_bounded, 0, FETCHER_COUNT);
    suite_add_tcase(suite, test);
    SRunner *runner = srunner_create(suite);
    srunner_run_all(runner, CK_NORMAL);
    int failed = srunner_ntests_failed(runner);
    srunner_free(runner);
    return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

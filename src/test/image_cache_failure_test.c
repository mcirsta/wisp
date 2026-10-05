/* Deferred decode errors must not call consumers inside paint/conversion. */
#include <check.h>
#include <stdlib.h>
#include <string.h>
#include <wisp/bitmap.h>
#include <wisp/content/content_protected.h>
#include <wisp/desktop/gui_internal.h>
#include <wisp/misc.h>
#include "content/handlers/image/image_cache.h"

static struct content content;
static unsigned conversions, errors, destroyed;
static nserror decode_error;
static void (*pending)(void *);
static void *pending_pw;
static int bitmap_storage;
static bool fail_schedule;

static nserror schedule(int delay, void (*callback)(void *), void *pw)
{
    if (delay == 0) {
        if (fail_schedule)
            return NSERROR_NOMEM;
        ck_assert_ptr_null(pending);
        pending = callback;
        pending_pw = pw;
    } else if (delay < 0 && pending == callback && pending_pw == pw) {
        pending = NULL;
    }
    return NSERROR_OK;
}

void content_broadcast_error(struct content *c, nserror error, const char *message)
{
    ck_assert_ptr_eq(c, &content);
    ck_assert_int_eq(c->status, CONTENT_STATUS_ERROR);
    ck_assert_int_eq(error, NSERROR_BAD_CONTENT);
    errors++;
}

static struct bitmap *convert(struct content *c, nserror *error)
{
    ck_assert_ptr_eq(c, &content);
    conversions++;
    *error = decode_error;
    return decode_error == NSERROR_OK ? (struct bitmap *)&bitmap_storage : NULL;
}

static void destroy(void *bitmap)
{
    ck_assert_ptr_eq(bitmap, &bitmap_storage);
    destroyed++;
}

static void run_pending(void)
{
    ck_assert_ptr_nonnull(pending);
    void (*callback)(void *) = pending;
    pending = NULL;
    callback(pending_pw);
}

static void setup(void)
{
    static struct gui_misc_table misc = {.schedule = schedule};
    static struct gui_bitmap_table bitmap = {.destroy = destroy};
    static struct wisp_table gui = {.misc = &misc, .bitmap = &bitmap};
    guit = &gui;
    memset(&content, 0, sizeof(content));
    content.status = CONTENT_STATUS_DONE;
    content.width = content.height = 1;
    content.size = 4;
    conversions = errors = destroyed = 0;
    pending = NULL;
    fail_schedule = false;
    decode_error = NSERROR_BAD_CONTENT;
    struct image_cache_parameters params = {.bg_clean_time = 1000};
    ck_assert_int_eq(image_cache_init(&params), NSERROR_OK);
}

static void teardown(void)
{
    ck_assert_int_eq(image_cache_fini(), NSERROR_OK);
    ck_assert_ptr_null(pending);
}

START_TEST(test_deferred_failure)
{
    ck_assert_int_eq(image_cache_add(&content, NULL, convert), NSERROR_OK);
    if (_i == 0)
        ck_assert_ptr_null(image_cache_get_bitmap(&content));
    else
        ck_assert(!image_cache_redraw(&content, NULL, NULL, NULL));
    ck_assert_uint_eq(conversions, 1);
    ck_assert_uint_eq(errors, 0);
    ck_assert_int_eq(content.status, CONTENT_STATUS_DONE);
    ck_assert_ptr_null(image_cache_get_bitmap(&content));
    ck_assert_uint_eq(conversions, 1);
    run_pending();
    ck_assert_uint_eq(errors, 1);
    ck_assert_ptr_null(image_cache_get_bitmap(&content));
    ck_assert_uint_eq(conversions, 1);
    ck_assert_ptr_null(pending);
}
END_TEST

START_TEST(test_allocation_failure_retries)
{
    decode_error = NSERROR_NOMEM;
    ck_assert_int_eq(image_cache_add(&content, NULL, convert), NSERROR_OK);
    ck_assert_ptr_null(image_cache_get_bitmap(&content));
    ck_assert_ptr_null(pending);
    decode_error = NSERROR_OK;
    ck_assert_ptr_eq(image_cache_get_bitmap(&content), &bitmap_storage);
    ck_assert_ptr_eq(image_cache_get_bitmap(&content), &bitmap_storage);
    ck_assert_uint_eq(conversions, 2);
    image_cache_purge_bitmaps();
    ck_assert_uint_eq(destroyed, 1);
    ck_assert_ptr_eq(image_cache_get_bitmap(&content), &bitmap_storage);
    ck_assert_uint_eq(conversions, 3);
    ck_assert_uint_eq(errors, 0);
}
END_TEST

START_TEST(test_teardown_cancels_failure)
{
    ck_assert_int_eq(image_cache_add(&content, NULL, convert), NSERROR_OK);
    ck_assert_ptr_null(image_cache_get_bitmap(&content));
    ck_assert_ptr_nonnull(pending);
    image_cache_destroy(&content);
    ck_assert_ptr_null(pending);
    ck_assert_uint_eq(errors, 0);
}
END_TEST

START_TEST(test_speculative_failure)
{
    image_cache_fini();
    struct image_cache_parameters params = {.bg_clean_time = 1000, .limit = 1024, .speculative_small = 1024};
    ck_assert_int_eq(image_cache_init(&params), NSERROR_OK);
    content.status = CONTENT_STATUS_LOADING;
    ck_assert_int_eq(image_cache_add(&content, NULL, convert), NSERROR_OK);
    ck_assert_uint_eq(conversions, 1);
    ck_assert_uint_eq(errors, 0);
    content.status = CONTENT_STATUS_DONE;
    run_pending();
    ck_assert_uint_eq(errors, 1);
}
END_TEST

START_TEST(test_notification_schedule_retries)
{
    fail_schedule = true;
    ck_assert_int_eq(image_cache_add(&content, NULL, convert), NSERROR_OK);
    ck_assert_ptr_null(image_cache_get_bitmap(&content));
    ck_assert_ptr_null(pending);
    fail_schedule = false;
    ck_assert_ptr_null(image_cache_get_bitmap(&content));
    ck_assert_uint_eq(conversions, 1);
    run_pending();
    ck_assert_uint_eq(errors, 1);
}
END_TEST

START_TEST(test_already_failed_content)
{
    ck_assert_int_eq(image_cache_add(&content, NULL, convert), NSERROR_OK);
    ck_assert_ptr_null(image_cache_get_bitmap(&content));
    content.status = CONTENT_STATUS_ERROR;
    run_pending();
    ck_assert_uint_eq(errors, 0);
}
END_TEST

int main(void)
{
    Suite *suite = suite_create("Image cache failures");
    TCase *tc = tcase_create("Deferred errors");
    tcase_add_checked_fixture(tc, setup, teardown);
    tcase_add_loop_test(tc, test_deferred_failure, 0, 2);
    tcase_add_test(tc, test_allocation_failure_retries);
    tcase_add_test(tc, test_teardown_cancels_failure);
    tcase_add_test(tc, test_speculative_failure);
    tcase_add_test(tc, test_notification_schedule_retries);
    tcase_add_test(tc, test_already_failed_content);
    suite_add_tcase(suite, tc);
    SRunner *runner = srunner_create(suite);
    srunner_run_all(runner, CK_NORMAL);
    int failed = srunner_ntests_failed(runner);
    srunner_free(runner);
    return failed ? EXIT_FAILURE : EXIT_SUCCESS;
}

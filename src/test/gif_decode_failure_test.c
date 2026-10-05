/* Real decoder regressions: failed composition is never a cached frame. */
#include <check.h>
#include <stdlib.h>
#include <string.h>
#include <nsgif.h>

static bool fail_allocation;
static unsigned allocations;

static void *create_bitmap(int width, int height)
{
    allocations++;
    return fail_allocation ? NULL : calloc((size_t)width * height, 4);
}

static uint8_t *get_buffer(void *bitmap)
{
    return bitmap;
}

/* Red and green palette, one red pixel: clear code, palette index zero.
 * Omitting the LZW end code is already recoverable by the decoder. */
static const uint8_t source[] = {
    'G', 'I', 'F', '8', '9', 'a', 1, 0, 1, 0, 0x80, 0, 0,
    255, 0, 0, 0, 255, 0,
    0x2c, 0, 0, 0, 0, 1, 0, 1, 0, 0, 2, 1, 0x44, 0, 0x3b
};

static nsgif_t *load_gif(const uint8_t *data, size_t size)
{
    const nsgif_bitmap_cb_vt callbacks = {
        .create = create_bitmap, .destroy = free, .get_buffer = get_buffer
    };
    nsgif_t *gif;
    fail_allocation = false;
    allocations = 0;
    ck_assert_int_eq(nsgif_create(&callbacks, NSGIF_BITMAP_FMT_R8G8B8A8, &gif), NSGIF_OK);
    ck_assert_int_eq(nsgif_data_scan(gif, size, data), NSGIF_OK);
    nsgif_data_complete(gif);
    return gif;
}

static void assert_red(const uint8_t *bitmap)
{
    ck_assert_ptr_nonnull(bitmap);
    ck_assert_uint_eq(bitmap[0], 255);
    ck_assert_uint_eq(bitmap[1], 0);
    ck_assert_uint_eq(bitmap[2], 0);
    ck_assert_uint_eq(bitmap[3], 255);
}

START_TEST(test_bad_frame_is_not_cached)
{
    uint8_t data[sizeof(source)];
    memcpy(data, source, sizeof(data));
    data[31] = 0x3c; /* Clear code followed by invalid dictionary index 7. */
    nsgif_t *gif = load_gif(data, sizeof(data));
    for (unsigned attempt = 0; attempt < 3; attempt++) {
        void *bitmap = NULL;
        ck_assert_msg(nsgif_frame_decode(gif, 0, &bitmap) == NSGIF_ERR_DATA_FRAME,
            "Corrupt frame became a successful cache hit on attempt %u", attempt);
        ck_assert_ptr_null(bitmap);
    }
    nsgif_destroy(gif);
}
END_TEST

START_TEST(test_allocation_failure_can_retry)
{
    nsgif_t *gif = load_gif(source, sizeof(source));
    void *bitmap = NULL;
    fail_allocation = true;
    for (unsigned attempt = 0; attempt < 2; attempt++)
        ck_assert_int_eq(nsgif_frame_decode(gif, 0, &bitmap), NSGIF_ERR_OOM);
    ck_assert_ptr_null(bitmap);
    fail_allocation = false;
    ck_assert_int_eq(nsgif_frame_decode(gif, 0, &bitmap), NSGIF_OK);
    assert_red(bitmap);
    ck_assert_uint_eq(allocations, 3);
    void *cached = NULL;
    ck_assert_int_eq(nsgif_frame_decode(gif, 0, &cached), NSGIF_OK);
    ck_assert_ptr_eq(cached, bitmap);
    ck_assert_uint_eq(allocations, 3);
    nsgif_destroy(gif);
}
END_TEST

START_TEST(test_recompose_after_bad_later_frame)
{
    /* Two frames share one compositing bitmap. The bad second frame clears
     * the first via its disposal method before failing. */
    uint8_t data[56];
    const uint8_t control[] = {0x21, 0xf9, 4, 9, 1, 0, 1, 0};
    memcpy(data, source, 19);
    memcpy(data + 19, control, sizeof(control));
    memcpy(data + 27, source + 19, 14);
    memcpy(data + 41, source + 19, 15);
    data[53] = 0x3c;
    nsgif_t *gif = load_gif(data, sizeof(data));
    void *bitmap = NULL;
    /* Also test jumping directly to the corrupt second frame. */
    if (_i == 0) {
        ck_assert_int_eq(nsgif_frame_decode(gif, 0, &bitmap), NSGIF_OK);
        assert_red(bitmap);
    }
    ck_assert_int_eq(nsgif_frame_decode(gif, 1, &bitmap), NSGIF_ERR_DATA_FRAME);
    ck_assert_int_eq(nsgif_frame_decode(gif, 1, &bitmap), NSGIF_ERR_DATA_FRAME);
    ck_assert_int_eq(nsgif_frame_decode(gif, 0, &bitmap), NSGIF_OK);
    assert_red(bitmap);
    nsgif_destroy(gif);
}
END_TEST

START_TEST(test_usable_truncation_is_preserved)
{
    uint8_t data[sizeof(source)];
    memcpy(data, source, sizeof(data));
    data[6] = data[24] = 2; /* Two-pixel canvas/frame, only one pixel supplied. */
    nsgif_t *gif = load_gif(data, sizeof(data));
    void *bitmap = NULL;
    ck_assert_int_eq(nsgif_frame_decode(gif, 0, &bitmap), NSGIF_OK);
    assert_red(bitmap);
    ck_assert_uint_eq(((uint8_t *)bitmap)[7], 0);
    nsgif_destroy(gif);
}
END_TEST

int main(void)
{
    Suite *suite = suite_create("GIF decoding failures");
    TCase *tc = tcase_create("Frame validity");
    tcase_add_test(tc, test_bad_frame_is_not_cached);
    tcase_add_test(tc, test_allocation_failure_can_retry);
    tcase_add_loop_test(tc, test_recompose_after_bad_later_frame, 0, 2);
    tcase_add_test(tc, test_usable_truncation_is_preserved);
    suite_add_tcase(suite, tc);
    SRunner *runner = srunner_create(suite);
    srunner_run_all(runner, CK_NORMAL);
    int failed = srunner_ntests_failed(runner);
    srunner_free(runner);
    return failed ? EXIT_FAILURE : EXIT_SUCCESS;
}

/**
 * Test the clip intersection helper functions for CSS transforms.
 *
 * These tests verify the behavior of the clip intersection logic that was
 * refactored from html_redraw_box to allow unit testing. The key behavior
 * being tested is that transformed boxes skip clip intersection (using the
 * parent clip directly) while non-transformed boxes do the normal intersection.
 *
 * This is a regression test for the hotnews.ro author-img rendering fix.
 */

#include <check.h>
#include <stdlib.h>
#include <string.h>

#include "content/handlers/html/redraw_helpers.h"
#include "wisp/types.h"

/**
 * Test: Non-transformed box clip is intersected with parent clip
 *
 * Simulates a 150x150 box at position (30, 0) within a 64x64 clip.
 * The intersection should reduce the visible area to (30, 0, 64, 64).
 */
START_TEST(test_box_clip_intersection_non_transform)
{
    /* Box at (30, 0) with size 150x150 */
    struct rect box_r = {.x0 = 30, .y0 = 0, .x1 = 180, .y1 = 150};
    /* Parent clip is 64x64 starting at origin */
    struct rect parent_clip = {.x0 = 0, .y0 = 0, .x1 = 64, .y1 = 64};

    clip_result_t result = html_clip_intersect_box(&box_r, &parent_clip, false);

    ck_assert_int_eq(result, CLIP_RESULT_OK);
    /* Intersection should be (30, 0, 64, 64) */
    ck_assert_int_eq(box_r.x0, 30);
    ck_assert_int_eq(box_r.y0, 0);
    ck_assert_int_eq(box_r.x1, 64);
    ck_assert_int_eq(box_r.y1, 64);
}
END_TEST

/**
 * Test: Transformed box clip uses parent clip directly (no intersection)
 *
 * Even though the box extends beyond the clip, with transform=true,
 * the parent clip should be used as-is without modification.
 */
START_TEST(test_box_clip_with_transform_uses_parent)
{
    /* Box at (30, 0) with size 150x150 - would normally be clipped */
    struct rect box_r = {.x0 = 30, .y0 = 0, .x1 = 180, .y1 = 150};
    /* Parent clip is 64x64 starting at origin */
    struct rect parent_clip = {.x0 = 0, .y0 = 0, .x1 = 64, .y1 = 64};

    clip_result_t result = html_clip_intersect_box(&box_r, &parent_clip, true);

    ck_assert_int_eq(result, CLIP_RESULT_OK);
    /* With transform=true, box_r should be exactly the parent clip */
    ck_assert_int_eq(box_r.x0, 0);
    ck_assert_int_eq(box_r.y0, 0);
    ck_assert_int_eq(box_r.x1, 64);
    ck_assert_int_eq(box_r.y1, 64);
}
END_TEST

/**
 * Test: Box completely outside clip returns empty
 */
START_TEST(test_box_clip_empty_result)
{
    /* Box at (200, 200) - completely outside the clip */
    struct rect box_r = {.x0 = 200, .y0 = 200, .x1 = 300, .y1 = 300};
    /* Parent clip is 64x64 starting at origin */
    struct rect parent_clip = {.x0 = 0, .y0 = 0, .x1 = 64, .y1 = 64};

    clip_result_t result = html_clip_intersect_box(&box_r, &parent_clip, false);

    ck_assert_int_eq(result, CLIP_RESULT_EMPTY);
}
END_TEST

/**
 * Test: Object-fit clip intersection for non-transformed box
 */
START_TEST(test_object_fit_clip_non_transform)
{
    /* Object clip starts as the full viewport */
    struct rect object_clip = {.x0 = 0, .y0 = 0, .x1 = 1000, .y1 = 1000};
    /* Box content bounds: 64x64 at (100, 100) */
    int box_left = 100, box_top = 100, box_right = 164, box_bottom = 164;

    clip_result_t result = html_clip_intersect_object_fit(
        &object_clip, box_left, box_top, box_right, box_bottom, false);

    ck_assert_int_eq(result, CLIP_RESULT_OK);
    /* Should be clipped to box bounds */
    ck_assert_int_eq(object_clip.x0, 100);
    ck_assert_int_eq(object_clip.y0, 100);
    ck_assert_int_eq(object_clip.x1, 164);
    ck_assert_int_eq(object_clip.y1, 164);
}
END_TEST

/**
 * Test: Object-fit clip skips intersection for transformed box
 */
START_TEST(test_object_fit_clip_with_transform)
{
    /* Object clip starts as a specific area */
    struct rect object_clip = {.x0 = 50, .y0 = 50, .x1 = 200, .y1 = 200};
    /* Box content bounds: 64x64 at (100, 100) */
    int box_left = 100, box_top = 100, box_right = 164, box_bottom = 164;

    clip_result_t result = html_clip_intersect_object_fit(&object_clip, box_left, box_top, box_right, box_bottom, true);

    ck_assert_int_eq(result, CLIP_RESULT_OK);
    /* With transform=true, clip should NOT be modified */
    ck_assert_int_eq(object_clip.x0, 50);
    ck_assert_int_eq(object_clip.y0, 50);
    ck_assert_int_eq(object_clip.x1, 200);
    ck_assert_int_eq(object_clip.y1, 200);
}
END_TEST

/**
 * Test: Hotnews scenario - transform: translateX(-50%) scale(0.4)
 *
 * This replicates the exact hotnews.ro author-img scenario:
 * - Container: 64x64 at position (1621, 926)
 * - Image: 150x150 positioned at left:48% (~30px) within container
 * - Transform: translateX(-50%) scale(0.4)
 *
 * Without the fix, the clip would be incorrectly reduced to 34x64.
 * With the fix, the parent's 64x64 clip is used.
 */
START_TEST(test_hotnews_author_img_scenario)
{
    /* Image box would be at x=1651 (1621 + 30), 150px wide */
    struct rect box_r = {.x0 = 1651, .y0 = 926, .x1 = 1801, .y1 = 1076};
    /* Parent clip is 64x64 starting at (1621, 926) */
    struct rect parent_clip = {.x0 = 1621, .y0 = 926, .x1 = 1685, .y1 = 990};

    /* With transform=true, we should get the full parent clip (64x64) */
    clip_result_t result = html_clip_intersect_box(&box_r, &parent_clip, true);

    ck_assert_int_eq(result, CLIP_RESULT_OK);
    /* Verify clip is 64x64, not reduced */
    int width = box_r.x1 - box_r.x0;
    int height = box_r.y1 - box_r.y0;
    ck_assert_int_eq(width, 64);
    ck_assert_int_eq(height, 64);
}
END_TEST

Suite *clip_intersection_suite(void)
{
    Suite *s = suite_create("clip_intersection");
    TCase *tc = tcase_create("transform_clip");

    tcase_add_test(tc, test_box_clip_intersection_non_transform);
    tcase_add_test(tc, test_box_clip_with_transform_uses_parent);
    tcase_add_test(tc, test_box_clip_empty_result);
    tcase_add_test(tc, test_object_fit_clip_non_transform);
    tcase_add_test(tc, test_object_fit_clip_with_transform);
    tcase_add_test(tc, test_hotnews_author_img_scenario);

    suite_add_tcase(s, tc);
    return s;
}

int main(void)
{
    Suite *s = clip_intersection_suite();
    SRunner *sr = srunner_create(s);
    srunner_run_all(sr, CK_ENV);
    int failed = srunner_ntests_failed(sr);
    srunner_free(sr);
    return (failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}

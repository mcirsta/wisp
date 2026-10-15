/* CSSOM View bounding-box union rules, including zero-area fragments. */
#include <check.h>
#include <stdlib.h>
#include <wisp/content/handlers/html/private.h>
#include <wisp/content/handlers/html/box.h>
#include "content/handlers/html/box_geometry.h"

START_TEST(test_fragment_union)
{
    html_content html = {0};
    struct box container = {.type = BOX_INLINE_CONTAINER};
    struct box end = {.type = BOX_INLINE_END, .parent = &container, .x = 30, .y = 40, .height = 10, .flags = NEW_LINE};
    struct box start = {
        .type = BOX_INLINE, .parent = &container, .height = 10, .width = 20, .next = &end, .inline_end = &end};
    struct box_rect bounds;
    /* A nonempty first line plus a vertical zero-width final fragment. */
    box_geometry_bounds(&html, &start, true, &bounds);
    ck_assert_double_eq(bounds.x0, 0);
    ck_assert_double_eq(bounds.y0, 0);
    ck_assert_double_eq(bounds.x1, 30);
    ck_assert_double_eq(bounds.y1, 50);

    /* When every fragment has zero area, the first fragment is the result. */
    start.width = 0;
    box_geometry_bounds(&html, &start, true, &bounds);
    ck_assert_double_eq(bounds.x1, 0);
    ck_assert_double_eq(bounds.y1, 10);
    /* Offsets union every principal fragment, including empty ones. */
    box_geometry_bounds(&html, &start, false, &bounds);
    ck_assert_double_eq(bounds.x1, 30);
    ck_assert_double_eq(bounds.y1, 50);
}
END_TEST

START_TEST(test_empty_and_absent_box)
{
    html_content html = {0};
    struct box box = {.type = BOX_BLOCK, .x = 54, .y = 31};
    struct box_rect bounds;
    box_geometry_bounds(&html, &box, true, &bounds);
    ck_assert_double_eq(bounds.x0, 54);
    ck_assert_double_eq(bounds.x1, 54);
    ck_assert_double_eq(bounds.y0, 31);
    ck_assert_double_eq(bounds.y1, 31);
    box_geometry_bounds(&html, NULL, true, &bounds);
    ck_assert_double_eq(bounds.x0, 0);
    ck_assert_double_eq(bounds.x1, 0);
    ck_assert_double_eq(bounds.y0, 0);
    ck_assert_double_eq(bounds.y1, 0);
}
END_TEST

int main(void)
{
    Suite *suite = suite_create("CSSOM View geometry");
    TCase *tc = tcase_create("Border fragments");
    tcase_add_test(tc, test_fragment_union);
    tcase_add_test(tc, test_empty_and_absent_box);
    suite_add_tcase(suite, tc);
    SRunner *runner = srunner_create(suite);
    srunner_run_all(runner, CK_NORMAL);
    int failed = srunner_ntests_failed(runner);
    srunner_free(runner);
    return failed ? EXIT_FAILURE : EXIT_SUCCESS;
}

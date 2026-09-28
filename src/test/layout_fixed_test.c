/* Coordinate regressions for fixed boxes and their normal/positioned children. */
#include <check.h>
#include <stdlib.h>

#include <wisp/content/handlers/html/private.h>
#include <wisp/content/handlers/html/box_inspect.h>
#include "desktop/scrollbar.h"

START_TEST(test_fixed_coordinates)
{
    struct html_viewport viewport = {.box = {.type = BOX_BLOCK, .flags = IS_VIEWPORT}};
    struct box document = {.type = BOX_BLOCK, .x = 7, .y = 11};
    struct box parent = {.type = BOX_BLOCK, .parent = &document, .x = 200, .y = 300};
    struct box fixed = {.type = BOX_BLOCK, .parent = &parent,
        .abs_containing_block = &viewport.box, .x = 20, .y = 30};
    struct box normal = {.type = BOX_BLOCK, .parent = &fixed, .x = 5, .y = 6};
    struct box absolute = {.type = BOX_BLOCK, .parent = &normal,
        .abs_containing_block = &fixed, .x = 8, .y = 9};
    struct box text = {.type = BOX_TEXT, .parent = &absolute, .x = 2, .y = 3};
    int x, y;
    int sx = _i ? 100 : 0, sy = _i ? 700 : 0;
    box_coords_at_scroll(&fixed, &x, &y, sx, sy);
    ck_assert_msg(x == 20 + sx && y == 30 + sy, "fixed: (%d,%d), scroll (%d,%d)", x, y, sx, sy);
    box_coords_at_scroll(&normal, &x, &y, sx, sy);
    ck_assert_msg(x == 25 + sx && y == 36 + sy, "normal child: (%d,%d)", x, y);
    box_coords_at_scroll(&text, &x, &y, sx, sy);
    ck_assert_msg(x == 30 + sx && y == 42 + sy, "nested absolute child: (%d,%d)", x, y);
    box_coords_unscrolled(&text, &x, &y);
    ck_assert_msg(x == 30 && y == 42, "viewport-contained layout child: (%d,%d)", x, y);

    /* A transformed containing block remains part of document coordinates. */
    fixed.abs_containing_block = &parent;
    box_coords_at_scroll(&text, &x, &y, sx, sy);
    ck_assert_msg(x == 237 && y == 353, "ancestor-contained fixed child: (%d,%d)", x, y);
}
END_TEST

static void scrollbar_callback(void *data, struct scrollbar_msg_data *message)
{
    (void)data;
    (void)message;
}

START_TEST(test_unscrolled_coordinates)
{
    struct box document = {.type = BOX_BLOCK, .x = 7, .y = 11};
    struct box parent = {.type = BOX_BLOCK, .parent = &document, .x = 200, .y = 300};
    struct box floating = {.type = BOX_FLOAT_RIGHT, .parent = &parent,
        .float_container = &document, .x = 400, .y = 500};
    struct box normal = {.type = BOX_BLOCK, .parent = &floating, .x = 5, .y = 6};
    struct box absolute = {.type = BOX_BLOCK, .parent = &normal,
        .abs_containing_block = &parent, .x = 20, .y = 30};
    struct box text = {.type = BOX_TEXT, .parent = &absolute, .x = 2, .y = 3};
    int x, y;

    ck_assert_int_eq(scrollbar_create(true, 100, 1000, 100, NULL, scrollbar_callback, &document.scroll_x), NSERROR_OK);
    ck_assert_int_eq(scrollbar_create(false, 100, 1000, 100, NULL, scrollbar_callback, &parent.scroll_y), NSERROR_OK);
    scrollbar_set(document.scroll_x, 40, false);
    scrollbar_set(parent.scroll_y, 70, false);

    box_coords(&text, &x, &y);
    ck_assert_msg(x == 189 && y == 274, "scrolled positioned descendant: (%d,%d)", x, y);
    box_coords_unscrolled(&text, &x, &y);
    ck_assert_msg(x == 229 && y == 344, "layout positioned descendant: (%d,%d)", x, y);
    box_coords_unscrolled(&normal, &x, &y);
    ck_assert_msg(x == 412 && y == 517, "layout float descendant: (%d,%d)", x, y);

    scrollbar_destroy(document.scroll_x);
    scrollbar_destroy(parent.scroll_y);
}
END_TEST

int main(void)
{
    Suite *suite = suite_create("Fixed coordinates");
    TCase *tc = tcase_create("Containing blocks");
    tcase_add_loop_test(tc, test_fixed_coordinates, 0, 2);
    tcase_add_test(tc, test_unscrolled_coordinates);
    suite_add_tcase(suite, tc);
    SRunner *runner = srunner_create(suite);
    srunner_run_all(runner, CK_NORMAL);
    int failed = srunner_ntests_failed(runner);
    srunner_free(runner);
    return failed ? EXIT_FAILURE : EXIT_SUCCESS;
}

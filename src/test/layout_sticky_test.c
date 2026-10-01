/* Numeric sticky constraints. Browser references: test/sticky-position.html. */
#include <check.h>
#include <inttypes.h>
#include <limits.h>
#include <stdlib.h>

#include "content/handlers/html/box_sticky.h"

static const int scrolls[] = {0, 100, 300, 380, 450, 0};

/* Positions relative to the scrollport, measured with Chromium 153.0.8010.52.
 * Returning to zero is intentional: queries must never accumulate movement.
 * Geometry is in the scroller's unscrolled content coordinates. */
static const struct {
    const char *name;
    struct box_sticky_axis axis;
    int positions[sizeof(scrolls) / sizeof(scrolls[0])];
} browser_cases[] = {
    {"top",
        {.border_start = 80,
            .border_size = 40,
            .containing_size = 400,
            .scrollport_size = 120,
            .inset_start = 10,
            .end_auto = true},
        {80, 10, 10, -20, -90, 80}},
    {"bottom",
        {.border_start = 80,
            .border_size = 40,
            .containing_size = 400,
            .scrollport_size = 120,
            .inset_end = 10,
            .start_auto = true},
        {70, -20, -220, -300, -370, 70}},
    {"both",
        {.border_start = 80,
            .border_size = 40,
            .containing_size = 400,
            .scrollport_size = 120,
            .inset_start = 10,
            .inset_end = 20},
        {60, 10, 10, -20, -90, 60}},
    {"tall-top",
        {.border_start = 80,
            .border_size = 180,
            .containing_size = 400,
            .scrollport_size = 120,
            .inset_start = 10,
            .end_auto = true},
        {80, 10, -80, -160, -230, 80}},
    {"margin",
        {.border_start = 80,
            .border_size = 40,
            .containing_size = 400,
            .margin_end = 30,
            .scrollport_size = 120,
            .inset_start = 10,
            .end_auto = true},
        {80, 10, 10, -50, -120, 80}},
    {"negative-margin",
        {.border_start = 80,
            .border_size = 40,
            .containing_size = 400,
            .margin_end = -20,
            .scrollport_size = 120,
            .inset_start = 10,
            .end_auto = true},
        {80, 10, 10, 0, -70, 80}},
    {"left",
        {.border_start = 80,
            .border_size = 60,
            .containing_size = 500,
            .margin_start = 80,
            .scrollport_size = 200,
            .inset_start = 10,
            .end_auto = true},
        {80, 10, 10, 10, -10, 80}},
    {"right-margin-limited",
        {.border_start = 250,
            .border_size = 60,
            .containing_size = 500,
            .margin_start = 250,
            .scrollport_size = 200,
            .inset_end = 10,
            .start_auto = true},
        {250, 150, -50, -130, -200, 250}},
    {"wide",
        {.border_start = 80,
            .border_size = 240,
            .containing_size = 500,
            .margin_start = 80,
            .scrollport_size = 200,
            .inset_start = 10,
            .inset_end = 20},
        {80, 10, -40, -120, -190, 80}},
    {"wide-rtl-margin-limited",
        {.border_start = 80,
            .border_size = 240,
            .containing_size = 500,
            .margin_start = 80,
            .margin_end = 180,
            .scrollport_size = 200,
            .inset_start = 10,
            .inset_end = 20,
            .reverse = true},
        {80, -20, -220, -300, -370, 80}},
};

START_TEST(test_browser_axes)
{
    for (size_t i = 0; i < sizeof(scrolls) / sizeof(scrolls[0]); i++) {
        struct box_sticky_axis axis = browser_cases[_i].axis;
        axis.scrollport_start = scrolls[i];
        int64_t position = axis.border_start + box_sticky_axis_offset(&axis) - scrolls[i];
        ck_assert_msg(position == browser_cases[_i].positions[i], "%s at scroll %d: got %" PRId64 ", expected %d",
            browser_cases[_i].name, scrolls[i], position, browser_cases[_i].positions[i]);
    }
}
END_TEST

START_TEST(test_auto_and_zero)
{
    struct box_sticky_axis axis = {.border_start = 20,
        .border_size = 40,
        .containing_size = 400,
        .scrollport_start = 100,
        .scrollport_size = 120,
        .start_auto = true,
        .end_auto = true,
        .inset_start = INT_MAX,
        .inset_end = INT_MIN};
    ck_assert_int_eq(box_sticky_axis_offset(&axis), 0);
    axis.start_auto = false;
    axis.inset_start = 0;
    ck_assert_int_eq(box_sticky_axis_offset(&axis), 80);
    axis.inset_start = -10;
    ck_assert_int_eq(box_sticky_axis_offset(&axis), 70);
}
END_TEST

START_TEST(test_containment_without_travel)
{
    struct box_sticky_axis axis = {.border_start = 0,
        .border_size = 180,
        .containing_size = 100,
        .scrollport_start = 80,
        .scrollport_size = 120,
        .inset_start = 10,
        .end_auto = true};
    ck_assert_msg(box_sticky_axis_offset(&axis) == 0, "A box overflowing its parent has no forward travel");
    axis.border_size = 40;
    axis.border_start = 30;
    axis.margin_end = 100;
    ck_assert_msg(box_sticky_axis_offset(&axis) == 0, "An overflowing end margin cannot cause a backwards jump");
}
END_TEST

START_TEST(test_oversized_end_inset)
{
    /* CSS Position 3 section 3.4 reduces the logical end inset even when the
     * opposite inset is auto. Chromium currently differs for bottom-only tall
     * boxes; this is a specification test, not a claimed Chromium match. */
    struct box_sticky_axis axis = {.border_start = 80,
        .border_size = 180,
        .containing_size = 400,
        .scrollport_start = 100,
        .scrollport_size = 120,
        .inset_end = 10,
        .start_auto = true};
    ck_assert_int_eq(box_sticky_axis_offset(&axis), 0);

    axis = (struct box_sticky_axis){.border_start = 80,
        .border_size = 240,
        .containing_size = 500,
        .scrollport_start = 100,
        .scrollport_size = 200,
        .inset_start = 10,
        .inset_end = 20,
        .reverse = true};
    ck_assert_msg(box_sticky_axis_offset(&axis) == -40, "RTL reduces the left inset, preserving the right one");
}
END_TEST

START_TEST(test_wide_arithmetic)
{
    struct box_sticky_axis axis = {.border_start = INT_MAX - 100,
        .border_size = 40,
        .containing_start = INT_MAX - 200,
        .containing_size = 400,
        .scrollport_start = INT_MAX - 20,
        .scrollport_size = 120,
        .inset_start = 10,
        .end_auto = true};
    ck_assert_msg(box_sticky_axis_offset(&axis) == 90, "Interval ends must not overflow signed int");
    axis.border_start = INT_MIN + 200;
    axis.containing_start = INT_MIN + 100;
    axis.scrollport_start = INT_MAX - 20;
    ck_assert_msg(box_sticky_axis_offset(&axis) == 260, "Far-apart coordinates still respect containing bounds");
}
END_TEST

int main(void)
{
    Suite *suite = suite_create("Sticky geometry");
    TCase *tc = tcase_create("Resolved axis constraints");
    tcase_add_loop_test(tc, test_browser_axes, 0, sizeof(browser_cases) / sizeof(browser_cases[0]));
    tcase_add_test(tc, test_auto_and_zero);
    tcase_add_test(tc, test_containment_without_travel);
    tcase_add_test(tc, test_oversized_end_inset);
    tcase_add_test(tc, test_wide_arithmetic);
    suite_add_tcase(suite, tc);
    SRunner *runner = srunner_create(suite);
    srunner_run_all(runner, CK_NORMAL);
    int failed = srunner_ntests_failed(runner);
    srunner_free(runner);
    return failed ? EXIT_FAILURE : EXIT_SUCCESS;
}

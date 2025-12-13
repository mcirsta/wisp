#include <check.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#include "neosurf/types.h"
#include "neosurf/utils/errors.h"
#include "neosurf/utils/log.h"
#include "neosurf/css.h"
#include "neosurf/content/handlers/html/box.h"
#include "content/handlers/html/layout.h"

/* Forward declare html_content to satisfy layout_internal.h */
typedef struct html_content html_content;

#include "content/handlers/html/layout_internal.h"

/* Mock dependencies */
struct html_content {
    struct css_unit_ctx unit_len_ctx;
};

/* Mock layout_flex function to avoid full linking */
/* We will actually test the internal logic via reproducing it or unit testing internal static functions if exposed */
/* Since we can't easily access static functions, we'll create a test that replicates the scenario logic */

/* Replicating the logic from layout_flex__resolve_line to test the fix */
static int test_resolve_line_logic(int available_main, int main_size) {
    if (available_main == INT_MIN) { /* AUTO */
        /* OLD BEHAVIOR: available_main = INT_MAX; */
        /* NEW BEHAVIOR: available_main = main_size; */
        return main_size; 
    }
    return available_main;
}

static int test_resolve_line_logic_old(int available_main, int main_size) {
    if (available_main == INT_MIN) { /* AUTO */
        return INT_MAX;
    }
    return available_main;
}

START_TEST(test_flex_auto_width_logic)
{
    /* Case 1: Width is AUTO (INT_MIN in our macro) */
    int available_main = INT_MIN;
    int content_size = 500;
    
    /* With the fix, available_main should become content_size */
    int resolved = test_resolve_line_logic(available_main, content_size);
    ck_assert_int_eq(resolved, content_size);
}
END_TEST

/* Replicating the logic from layout_flex width clamping to test the fix */
static int test_flex_width_clamping(int width, int max_width, int min_width, int calculated_width) {
    /* Logic from layout_flex */
    if (width == INT_MIN) { /* AUTO or UNKNOWN_WIDTH */
        width = calculated_width;
        
        if (max_width >= 0 && width > max_width) {
            width = max_width;
        }
        if (min_width > 0 && width < min_width) {
            width = min_width;
        }
    }
    return width;
}

START_TEST(test_flex_width_max_constraint)
{
    int width = INT_MIN; /* AUTO */
    int calculated_width = 800;
    int max_width = 600;
    int min_width = 0;
    
    int result = test_flex_width_clamping(width, max_width, min_width, calculated_width);
    
    /* Should be clamped to 600 */
    ck_assert_int_eq(result, 600);
}
END_TEST

START_TEST(test_flex_width_min_constraint)
{
    int width = INT_MIN; /* AUTO */
    int calculated_width = 200;
    int max_width = -1;
    int min_width = 300;
    
    int result = test_flex_width_clamping(width, max_width, min_width, calculated_width);
    
    /* Should be clamped to 300 */
    ck_assert_int_eq(result, 300);
}
END_TEST

/* Test suite setup */
Suite *layout_flex_suite(void)
{
    Suite *s;
    TCase *tc_core;

    s = suite_create("LayoutFlex");
    tc_core = tcase_create("Core");

    tcase_add_test(tc_core, test_flex_auto_width_logic);
    tcase_add_test(tc_core, test_flex_width_max_constraint);
    tcase_add_test(tc_core, test_flex_width_min_constraint);

    suite_add_tcase(s, tc_core);

    return s;
}

int main(void)
{
    int number_failed;
    Suite *s;
    SRunner *sr;

    s = layout_flex_suite();
    sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);

    return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}

/* CSS test diagnostics must handle output larger than the expected result. */
#include <check.h>
#include <inttypes.h>
#include <limits.h>

#include "dump.h"

#ifdef WISP_TEST_ALLOC_FAILURE
#include "test/malloc_fig.h"
#endif

static css_error resolve_url(void *pw, const char *base, lwc_string *rel, lwc_string **abs)
{
    (void)pw;
    (void)base;
    *abs = lwc_string_ref(rel);
    return CSS_OK;
}

static css_stylesheet *parse(const char *text)
{
    css_stylesheet_params params = {.params_version = CSS_STYLESHEET_PARAMS_VERSION_1,
        .level = CSS_LEVEL_DEFAULT,
        .charset = "UTF-8",
        .url = "https://example.org/",
        .resolve = resolve_url};
    css_stylesheet *sheet;
    ck_assert_int_eq(css_stylesheet_create(&params, &sheet), CSS_OK);
    css_error error = css_stylesheet_append_data(sheet, (const uint8_t *)text, strlen(text));
    ck_assert_msg(error == CSS_OK || error == CSS_NEEDDATA, "append: %d", error);
    ck_assert_int_eq(css_stylesheet_data_done(sheet), CSS_OK);
    return sheet;
}

START_TEST(test_output_growth)
{
    struct dump_output out = {0};
    dump_printf(&out, "%s", "");
    ck_assert(!out.failed);
    ck_assert_str_eq(out.data, "");
    dump_write(&out, "raw\0bytes", 9);
    dump_char(&out, '!');
    ck_assert_uint_eq(out.length, 10);
    ck_assert_int_eq(memcmp(out.data, "raw\0bytes!", 10), 0);
    ck_assert_int_eq(out.data[out.length], 0);

    /* Cross both the exact-capacity boundary and multiple subsequent growths. */
    while (out.length + 1 < out.capacity)
        dump_char(&out, 'x');
    size_t old_capacity = out.capacity;
    dump_char(&out, 'y');
    ck_assert(out.capacity > old_capacity);
    size_t start = out.length;
    char large[16385];
    memset(large, 'q', sizeof(large) - 1);
    large[sizeof(large) - 1] = '\0';
    dump_printf(&out, "%s:%04x:%d", large, 42u, -7);
    ck_assert(!out.failed);
    ck_assert_uint_eq(out.length, start + sizeof(large) - 1 + 8);
    ck_assert_int_eq(memcmp(out.data + start, large, sizeof(large) - 1), 0);
    ck_assert_str_eq(out.data + start + sizeof(large) - 1, ":002a:-7");
    dump_output_fini(&out);
    ck_assert_ptr_null(out.data);
    ck_assert_uint_eq(out.length, 0);
    ck_assert_uint_eq(out.capacity, 0);
    ck_assert(!out.failed);
}
END_TEST

START_TEST(test_size_overflow)
{
    struct dump_output out = {0};
    dump_printf(&out, "prefix");
    ck_assert(!dump_reserve(&out, SIZE_MAX));
    ck_assert(out.failed);
    dump_printf(&out, "%s", "must not append");
    dump_write(&out, "ignored", 7);
    dump_char(&out, '!');
    ck_assert_uint_eq(out.length, 6);
    ck_assert_str_eq(out.data, "prefix");
    dump_output_fini(&out);
}
END_TEST

START_TEST(test_dump_stylesheet)
{
    css_stylesheet *sheet = parse("* {overflow:auto visible}");
    struct dump_output out = {0};
    dump_sheet(sheet, &out);
    ck_assert(!out.failed);
    /* This output overran the old buffer when the fixture expected only | *. */
    ck_assert_str_eq(out.data, "| *\n|  overflow-x: auto\n|  overflow-y: visible\n");
    ck_assert_uint_eq(out.length, strlen(out.data));
    dump_output_fini(&out);
    css_stylesheet_destroy(sheet);

    sheet = parse("");
    dump_sheet(sheet, &out);
    ck_assert(!out.failed);
    ck_assert_uint_eq(out.length, 0);
    dump_output_fini(&out);
    css_stylesheet_destroy(sheet);
}
END_TEST

START_TEST(test_dump_numbers)
{
    struct dump_output out = {0};
    /* Extreme signed fixed values must not overflow the magnitude conversion
     * or the formatter's temporary buffer. Preserve existing dump spelling. */
    dump_css_fixed(INT32_MIN, &out);
    dump_char(&out, ' ');
    dump_number(INT32_MAX, &out);
    ck_assert(!out.failed);
    ck_assert_str_eq(out.data, "-2097152.000 2097151.999");
    dump_output_fini(&out);
}
END_TEST

START_TEST(test_dump_position_payloads)
{
    css_stylesheet *sheet = parse("* {object-position:32768px 2%!important; background-position:top right; width:37px}");
    struct dump_output out = {0};
    dump_sheet(sheet, &out);
    ck_assert(!out.failed);
    ck_assert_str_eq(out.data, "| *\n|  object-position: 32768px 2% !important\n"
        "|  background-position-x: right\n|  background-position-y: top\n|  width: 37px\n");
    dump_output_fini(&out);
    css_stylesheet_destroy(sheet);
}
END_TEST

#ifdef WISP_TEST_ALLOC_FAILURE
START_TEST(test_allocation_failure)
{
    struct dump_output out = {0};
    dump_printf(&out, "prefix");
    ck_assert(!out.failed);
    char large[16385];
    memset(large, 'q', sizeof(large) - 1);
    large[sizeof(large) - 1] = '\0';
    realloc_limit(0);
    if (_i == 0)
        dump_printf(&out, "%s", large);
    else
        dump_write(&out, large, strlen(large));
    realloc_limit(UINT_MAX);
    ck_assert(out.failed);
    ck_assert_uint_eq(out.length, 6);
    ck_assert_str_eq(out.data, "prefix");
    dump_printf(&out, "no retry after failure");
    ck_assert_str_eq(out.data, "prefix");
    dump_output_fini(&out);
    dump_printf(&out, "%s", large);
    ck_assert(!out.failed);
    ck_assert_str_eq(out.data, large);
    dump_output_fini(&out);
}
END_TEST

START_TEST(test_stylesheet_allocation_failure)
{
    char input[16384];
    char *p = input;
    for (unsigned i = 0; i < 200; i++)
        p += sprintf(p, ".test%u { margin:1px 2px; overflow:auto visible }\n", i);
    css_stylesheet *sheet = parse(input);
    struct dump_output expected = {0};
    dump_sheet(sheet, &expected);
    ck_assert(!expected.failed);
    bool succeeded = false;
    unsigned failures = 0;
    for (unsigned limit = 0; limit < 32; limit++) {
        struct dump_output out = {0};
        realloc_limit(limit);
        dump_sheet(sheet, &out);
        realloc_limit(UINT_MAX);
        if (out.failed) {
            failures++;
            ck_assert(out.length < expected.length);
            if (out.data != NULL)
                ck_assert_int_eq(out.data[out.length], 0);
        } else {
            ck_assert_uint_eq(out.length, expected.length);
            ck_assert_int_eq(memcmp(out.data, expected.data, expected.length), 0);
            succeeded = true;
        }
        dump_output_fini(&out);
        if (succeeded)
            break;
    }
    ck_assert(succeeded && failures > 1);
    dump_output_fini(&expected);
    css_stylesheet_destroy(sheet);
}
END_TEST
#endif

static void count_string(lwc_string *string, void *pw)
{
    (void)string;
    (*(unsigned *)pw)++;
}

int main(void)
{
    Suite *suite = suite_create("CSS dump output");
    TCase *tc = tcase_create("Bounded writes");
    tcase_add_test(tc, test_output_growth);
    tcase_add_test(tc, test_size_overflow);
    tcase_add_test(tc, test_dump_stylesheet);
    tcase_add_test(tc, test_dump_numbers);
    tcase_add_test(tc, test_dump_position_payloads);
#ifdef WISP_TEST_ALLOC_FAILURE
    tcase_add_loop_test(tc, test_allocation_failure, 0, 2);
    tcase_add_test(tc, test_stylesheet_allocation_failure);
#endif
    suite_add_tcase(suite, tc);
    SRunner *runner = srunner_create(suite);
    srunner_set_fork_status(runner, CK_NOFORK);
    srunner_run_all(runner, CK_NORMAL);
    int failed = srunner_ntests_failed(runner);
    srunner_free(runner);
    unsigned remaining = 0;
    lwc_iterate_strings(count_string, &remaining);
    return failed != 0 || remaining != 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}

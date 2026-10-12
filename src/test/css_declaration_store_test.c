/* CSSOM 6.6.1 native declaration editing: specification-defined assignments,
 * removals, logical ordering and ownership. No browser reference baselines. */
#include <check.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "parse/declaration_store.h"
#include "parse/property.h"

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

static css_declaration_list *parse(const char *css, bool chunks)
{
    css_stylesheet_params params = {.params_version = CSS_STYLESHEET_PARAMS_VERSION_1,
        .level = CSS_LEVEL_DEFAULT,
        .url = "https://example.org/",
        .resolve = resolve_url};
    css_declaration_list *list;
    ck_assert_int_eq(css__declaration_list_create(&params, &list), CSS_OK);
    size_t length = strlen(css);
    for (size_t pos = 0; pos < length;) {
        size_t n = chunks ? 1 : length;
        css_error error = css__declaration_list_append(list, (const uint8_t *)css + pos, n);
        ck_assert_msg(error == CSS_OK || error == CSS_NEEDDATA, "parse %s: %d", css, error);
        pos += n;
    }
    ck_assert_int_eq(css__declaration_list_complete(list), CSS_OK);
    return list;
}

static css_declaration_store *create(const char *css, bool chunks)
{
    css_declaration_list *list = parse(css, chunks);
    css_declaration_store *store;
    ck_assert_int_eq(css__declaration_store_create(list, &store), CSS_OK);
    css__declaration_list_destroy(list);
    return store;
}

static bool set(css_declaration_store *store, const char *css, bool chunks)
{
    css_declaration_list *list = parse(css, chunks);
    ck_assert_uint_eq(css__declaration_list_count(list), 1);
    bool changed;
    ck_assert_int_eq(css__declaration_store_set(store, css__declaration_list_get(list, 0), &changed), CSS_OK);
    css__declaration_list_destroy(list);
    return changed;
}

static void check_text(const css_declaration_store *store, const char *expected)
{
    lwc_string *text;
    ck_assert_int_eq(css__declaration_store_serialize(store, &text), CSS_OK);
    ck_assert_msg(strcmp(lwc_string_data(text), expected) == 0, "expected %s\ngot %s", expected, lwc_string_data(text));
    lwc_string_unref(text);
}

static void check_value(const css_declaration_store *store, const char *name, const char *expected, bool priority)
{
    lwc_string *text;
    ck_assert_int_eq(css__declaration_store_value(store, name, strlen(name), &text), CSS_OK);
    ck_assert_msg(
        strcmp(lwc_string_data(text), expected) == 0, "%s: expected %s, got %s", name, expected, lwc_string_data(text));
    lwc_string_unref(text);
    bool important;
    ck_assert_int_eq(css__declaration_store_priority(store, name, strlen(name), &important), CSS_OK);
    ck_assert_msg(important == priority, "%s: expected priority %d, got %d", name, priority, important);
}

static void check_order(const css_declaration_store *store, const char *names)
{
    for (size_t i = 0; i < css__declaration_store_count(store); i++) {
        const css_declaration_contribution *value = css__declaration_store_get(store, i);
        size_t n = strlen(value->name);
        ck_assert_msg(strncmp(value->name, names, n) == 0 && (names[n] == ' ' || names[n] == '\0'),
            "item %zu: got %s, expected remaining %s", i, value->name, names);
        names += n;
        if (*names == ' ')
            names++;
        /* Owned values have no association with a discarded compiled sheet. */
        ck_assert_uint_eq(value->source->bytecode_length, 0);
    }
    ck_assert_str_eq(names, "");
    ck_assert_ptr_null(css__declaration_store_get(store, css__declaration_store_count(store)));
}

static const struct {
    const char *initial, *assignment, *expected, *order;
} assignments[] = {
    {"", "color:red", "color: red;", "color"},
    {"width:1px;color:red;width:2px", "width:3px", "color: red; width: 3px;", "color width"},
    {"width:1px;color:red", "width:2px", "width: 2px; color: red;", "width color"},
    {"width:1px!important;color:red", "WIDTH:2px", "width: 2px; color: red;", "width color"},
    {"width:1px;color:red", "width:2px!important", "width: 2px !important; color: red;", "width color"},
    {"margin:1px!important;color:red", "margin-left:2px",
        "margin-top: 1px !important; margin-right: 1px !important; margin-bottom: 1px !important; "
        "margin-left: 2px; color: red;",
        "margin-top margin-right margin-bottom margin-left color"},
    {"margin:1px!important;color:red", "margin:2px", "margin: 2px; color: red;",
        "margin-top margin-right margin-bottom margin-left color"},
    {"margin-left:3px;color:red", "margin:2px", "margin: 2px; color: red;",
        "margin-left color margin-top margin-right margin-bottom"},
    {"margin-top:1px;margin-block-start:2px;color:red", "margin-top:3px",
        "margin-block-start: 2px; color: red; margin-top: 3px;", "margin-block-start color margin-top"},
    {"margin-top:1px;margin-block-start:2px;color:red", "margin-top:1px",
        "margin-block-start: 2px; color: red; margin-top: 1px;", "margin-block-start color margin-top"},
    {"margin-block-start:2px;margin-top:1px;color:red", "margin-block-start:3px",
        "margin-top: 1px; color: red; margin-block-start: 3px;", "margin-top color margin-block-start"},
    {"margin-top:1px;padding-inline:2px;color:red", "margin-top:3px",
        "margin-top: 3px; padding-inline: 2px; color: red;",
        "margin-top padding-inline-start padding-inline-end color"},
    {"margin:1px;margin-inline:2px;color:red", "margin:3px", "margin-inline: 2px; color: red; margin: 3px;",
        "margin-inline-start margin-inline-end color margin-top margin-right margin-bottom margin-left"},
    {"margin-inline:2px;margin:1px;color:red", "margin-inline:3px", "margin: 1px; color: red; margin-inline: 3px;",
        "margin-top margin-right margin-bottom margin-left color margin-inline-start margin-inline-end"},
    {"width:10px;inline-size:12px;color:red", "width:15px", "inline-size: 12px; color: red; width: 15px;",
        "inline-size color width"},
    {"top:1px;inset-inline:2px;color:red", "top:3px", "inset-inline: 2px; color: red; top: 3px;",
        "inset-inline-start inset-inline-end color top"},
    {"grid-gap:1px;color:red", "GRID-ROW-GAP:2px", "gap: 2px 1px; color: red;", "row-gap column-gap color"},
    {"--Tone:Old!important;--tone:other;color:red", "--Tone:MiXeD/**/new",
        "--Tone: MiXeD/**/new; --tone: other; color: red;", "--Tone --tone color"},
    {"--a\\:b:old;--other:two", "--a\\:b: new", "--a\\:b: new; --other: two;", "--a:b --other"},
    {"margin:var(--x)!important;color:red", "margin-left:2px",
        "margin-top:  !important; margin-right:  !important; margin-bottom:  !important; margin-left: 2px; color: red;",
        "margin-top margin-right margin-bottom margin-left color"},
    {"margin:var(--x);margin-left:2px;color:red", "margin:var(--x)", "margin: var(--x); color: red;",
        "margin-top margin-right margin-bottom margin-left color"},
    {"background:red;color:blue", "background-image:url(a)", "background: url(\"a\") red; color: blue;",
        "background-image background-position background-repeat background-attachment background-color color"},
    {"color:red", "--empty: ", "color: red; --empty: ;", "color --empty"},
};
enum { ASSIGNMENT_COUNT = sizeof(assignments) / sizeof(assignments[0]) };

START_TEST(test_assignments)
{
    size_t index = (size_t)_i % ASSIGNMENT_COUNT;
    bool chunks = _i >= ASSIGNMENT_COUNT;
    css_declaration_store *store = create(assignments[index].initial, chunks);
    ck_assert_msg(set(store, assignments[index].assignment, chunks), "assignment %zu was not applied", index);
    check_text(store, assignments[index].expected);
    check_order(store, assignments[index].order);
    ck_assert_msg(!set(store, assignments[index].assignment, chunks), "exact repeat %zu wasn't a no-op", index);
    check_text(store, assignments[index].expected);
    css__declaration_store_destroy(store);
}
END_TEST

static const struct {
    const char *initial, *name, *old_value, *expected;
    bool important, changed;
} removals[] = {
    {"", "width", "", "", false, false},
    {"width:1px;color:red", "WIDTH", "1px", "color: red;", false, true},
    {"width:1px!important;color:red", "w\\69 dth", "", "width: 1px !important; color: red;", false, false},
    {"margin:1px!important;color:red", "MARGIN", "1px", "color: red;", true, true},
    {"margin:1px!important;margin-left:2px;color:red", "margin", "1px", "color: red;", true, true},
    {"margin:1px;margin-left:2px!important;color:red", "margin", "", "color: red;", false, true},
    {"margin-left:2px!important;color:red", "margin", "", "color: red;", false, true},
    {"margin:var(--x);margin-left:2px;color:red", "margin", "", "color: red;", false, true},
    {"margin:var(--x)!important", "margin-left", "",
        "margin-top:  !important; margin-right:  !important; "
        "margin-bottom:  !important;",
        true, true},
    {"margin-inline:1px 2px!important;color:red", "MARGIN-INLINE-START", "1px",
        "margin-inline-end: 2px !important; color: red;", true, true},
    {"grid-gap:2px!important;color:red", "GRID-GAP", "2px", "color: red;", true, true},
    {"--Tone: MiXeD/**/Tokens!important;--tone:other", "--Tone", "MiXeD/**/Tokens", "--tone: other;", true, true},
    {"--Tone:MiXeD", "--tone", "", "--Tone: MiXeD;", false, false},
    {"--a\\:b:one;--other:two", "--a:b", "one", "--other: two;", false, true},
    {"--a\\:b:one", "--a\\:b", "", "--a\\:b: one;", false, false},
};
enum { REMOVAL_COUNT = sizeof(removals) / sizeof(removals[0]) };

START_TEST(test_removals)
{
    const size_t index = (size_t)_i % REMOVAL_COUNT;
    css_declaration_store *store = create(removals[index].initial, _i >= REMOVAL_COUNT);
    check_value(store, removals[index].name, removals[index].old_value, removals[index].important);
    bool changed;
    ck_assert_int_eq(
        css__declaration_store_remove(store, removals[index].name, strlen(removals[index].name), &changed), CSS_OK);
    ck_assert_msg(changed == removals[index].changed, "removal %zu: unexpected changed flag", index);
    check_text(store, removals[index].expected);
    check_value(store, removals[index].name, "", false);
    ck_assert_int_eq(
        css__declaration_store_remove(store, removals[index].name, strlen(removals[index].name), &changed), CSS_OK);
    ck_assert(!changed);
    css__declaration_store_destroy(store);
}
END_TEST

START_TEST(test_shorthand_catalogue)
{
    size_t count;
    const struct css_prop_entry *const *properties = css_prop_shorthands(&count);
    for (size_t i = 0; i < count; i++) {
        const struct css_prop_entry *property = properties[i];
        char css[128], expected[128];
        snprintf(css, sizeof(css), "%s:INITIAL!important", property->name);
        css_declaration_store *store = create("color:red!important", false);
        ck_assert(set(store, css, false));
        check_value(store, property->name, "initial", true);
        for (size_t j = 0; j < property->longhand_count; j++)
            check_value(store, property->longhands[j], "initial", true);
        snprintf(css, sizeof(css), "%s:var(--v)", property->name);
        ck_assert(set(store, css, false));
        check_value(store, property->name, "var(--v)", false);
        snprintf(expected, sizeof(expected), "color: red !important; %s: var(--v);", property->name);
        check_text(store, expected);
        bool changed;
        ck_assert_int_eq(
            css__declaration_store_remove(store, property->name, strlen(property->name), &changed), CSS_OK);
        ck_assert(changed);
        check_text(store, "color: red !important;");
        css__declaration_store_destroy(store);
    }
}
END_TEST

START_TEST(test_native_state_survives_readback_limits)
{
    css_declaration_store *store = create("margin:var(--m);width:calc(100% - 2px);color:red", false);
    ck_assert(set(store, "margin-left:2px", false));
    const css_declaration *pending = css__declaration_store_get(store, 0)->source;
    lwc_string *out = (void *)1;
    ck_assert_int_eq(css__declaration_store_serialize(store, &out), CSS_INVALID);
    ck_assert_ptr_null(out);
    ck_assert_int_eq(css__declaration_store_value(store, "width", 5, &out), CSS_INVALID);
    ck_assert_ptr_null(out);
    check_value(store, "color", "red", false);
    check_value(store, "margin", "", false);
    ck_assert_ptr_eq(pending, css__declaration_store_get(store, 0)->source);
    ck_assert(pending->deferred);
    ck_assert_str_eq(lwc_string_data(pending->source_value), "var(--m)");
    /* Assign the preserved shorthand borrowed from this store itself. */
    bool changed;
    ck_assert_int_eq(css__declaration_store_set(store, pending, &changed), CSS_OK);
    ck_assert(changed);
    check_value(store, "margin", "var(--m)", false);
    ck_assert_int_eq(css__declaration_store_remove(store, "width", 5, &changed), CSS_OK);
    ck_assert(changed);
    check_text(store, "margin: var(--m); color: red;");
    css__declaration_store_destroy(store);
}
END_TEST

START_TEST(test_length_and_borrowed_names)
{
    css_declaration_store *store = create("width:2px;--a\\:b:one;--other:two", false);
    lwc_string *out;
    ck_assert_int_eq(css__declaration_store_value(store, "width\0junk", 10, &out), CSS_OK);
    ck_assert_str_eq(lwc_string_data(out), "");
    lwc_string_unref(out);
    ck_assert_int_eq(css__declaration_store_value(store, "WIDTH-trailing", 5, &out), CSS_OK);
    ck_assert_str_eq(lwc_string_data(out), "2px");
    lwc_string_unref(out);
    const char *name = css__declaration_store_get(store, 1)->name;
    bool changed;
    ck_assert_int_eq(css__declaration_store_remove(store, name, strlen(name), &changed), CSS_OK);
    ck_assert(changed);
    check_text(store, "width: 2px; --other: two;");
    css__declaration_store_destroy(store);
}
END_TEST

static void count_string(lwc_string *string, void *pw)
{
    (void)string;
    (*(size_t *)pw)++;
}

static void find_pending_string(lwc_string *string, void *pw)
{
    if (strcmp(lwc_string_data(string), "var(--retained-until-last-member)") == 0)
        *(bool *)pw = true;
}

START_TEST(test_shared_source_lifetime)
{
    css_declaration_store *store = create("margin:var(--retained-until-last-member);color:red", false);
    const char *members[] = {"margin-top", "margin-right", "margin-bottom", "margin-left"};
    const css_declaration *source = css__declaration_store_get(store, 0)->source;
    for (size_t i = 1; i < 4; i++)
        ck_assert_ptr_eq(css__declaration_store_get(store, i)->source, source);
    for (size_t i = 0; i < 4; i++) {
        bool changed;
        ck_assert_int_eq(css__declaration_store_remove(store, members[i], strlen(members[i]), &changed), CSS_OK);
        ck_assert(changed);
        bool retained = false;
        lwc_iterate_strings(find_pending_string, &retained);
        ck_assert_msg(retained == (i < 3), "source lifetime after removing %s", members[i]);
    }
    check_text(store, "color: red;");
    css__declaration_store_destroy(store);
}
END_TEST

START_TEST(test_repeated_edits_release_sources)
{
    css_declaration_store *store = create("margin:var(--m);--value:token-1000", false);
    size_t initial = 0;
    lwc_iterate_strings(count_string, &initial);
    for (unsigned i = 1001; i < 1257; i++) {
        char css[64];
        snprintf(css, sizeof(css), "--value:token-%u", i);
        ck_assert(set(store, css, false));
        size_t retained = 0;
        lwc_iterate_strings(count_string, &retained);
        ck_assert_msg(retained == initial, "edit %u retained %zu strings, initially %zu", i, retained, initial);
        ck_assert_uint_eq(css__declaration_store_count(store), 5);
    }
    check_value(store, "margin", "var(--m)", false);
    check_value(store, "--value", "token-1256", false);
    css__declaration_store_destroy(store);
}
END_TEST

START_TEST(test_bad_arguments)
{
    css_declaration_store *store = (void *)1;
    ck_assert_int_eq(css__declaration_store_create(NULL, &store), CSS_BADPARM);
    ck_assert_ptr_null(store);
    ck_assert_int_eq(css__declaration_store_create(NULL, NULL), CSS_BADPARM);
    bool flag = true;
    ck_assert_int_eq(css__declaration_store_set(NULL, NULL, &flag), CSS_BADPARM);
    ck_assert(!flag);
    ck_assert_int_eq(css__declaration_store_set(NULL, NULL, NULL), CSS_BADPARM);
    ck_assert_int_eq(css__declaration_store_remove(NULL, "", 0, &flag), CSS_BADPARM);
    ck_assert_int_eq(css__declaration_store_remove(NULL, "", 0, NULL), CSS_BADPARM);
    ck_assert_int_eq(css__declaration_store_priority(NULL, "", 0, &flag), CSS_BADPARM);
    ck_assert_int_eq(css__declaration_store_priority(NULL, "", 0, NULL), CSS_BADPARM);
    lwc_string *out = (void *)1;
    ck_assert_int_eq(css__declaration_store_value(NULL, "", 0, &out), CSS_BADPARM);
    ck_assert_ptr_null(out);
    ck_assert_int_eq(css__declaration_store_value(NULL, "", 0, NULL), CSS_BADPARM);
    ck_assert_int_eq(css__declaration_store_serialize(NULL, &out), CSS_BADPARM);
    ck_assert_ptr_null(out);
    ck_assert_int_eq(css__declaration_store_serialize(NULL, NULL), CSS_BADPARM);
    ck_assert_uint_eq(css__declaration_store_count(NULL), 0);
    ck_assert_ptr_null(css__declaration_store_get(NULL, 0));
    css__declaration_store_destroy(NULL);
}
END_TEST

#ifdef WISP_TEST_ALLOC_FAILURE
static void (*const allocators[])(unsigned) = {malloc_limit, calloc_limit, realloc_limit};

START_TEST(test_create_allocation_failure)
{
    css_declaration_list *list = parse(
        "margin:1px; margin-left:var(--m); width:2px; --X:Token!important;--X:lost", false);
    bool succeeded = false;
    for (unsigned n = 0; n < 64; n++) {
        css_declaration_store *store = (void *)1;
        allocators[_i](n);
        css_error error = css__declaration_store_create(list, &store);
        allocators[_i](UINT_MAX);
        if (error == CSS_OK) {
            ck_assert_uint_gt(n, 0);
            check_value(store, "--X", "Token", true);
            check_value(store, "margin-left", "var(--m)", false);
            css__declaration_store_destroy(store);
            succeeded = true;
            break;
        }
        ck_assert_msg(error == CSS_NOMEM, "create allocator %d failure %u: %d", _i, n, error);
        ck_assert_ptr_null(store);
    }
    ck_assert(succeeded);
    css__declaration_list_destroy(list);
}
END_TEST

START_TEST(test_set_allocation_failure)
{
    css_declaration_store *store = create("margin-left:1px!important; margin-inline:2px; color:red", false);
    css_declaration_list *list = parse("margin:var(--m)", false);
    const css_declaration_contribution *old = css__declaration_store_get(store, 0);
    bool succeeded = false;
    for (unsigned n = 0; n < 64; n++) {
        bool changed = true;
        allocators[_i](n);
        css_error error = css__declaration_store_set(store, css__declaration_list_get(list, 0), &changed);
        allocators[_i](UINT_MAX);
        if (error == CSS_OK) {
            ck_assert(changed);
            ck_assert_msg(n > 0 || _i == 1, "expected fault coverage for allocator %d", _i);
            check_text(store, "margin-inline: 2px; color: red; margin: var(--m);");
            succeeded = true;
            break;
        }
        ck_assert_msg(error == CSS_NOMEM, "set allocator %d failure %u: %d", _i, n, error);
        ck_assert(!changed);
        ck_assert_ptr_eq(old, css__declaration_store_get(store, 0));
        check_text(store, "margin-left: 1px !important; margin-inline: 2px; color: red;");
        check_order(store, "margin-left margin-inline-start margin-inline-end color");
    }
    ck_assert(succeeded);
    /* Repeating the exact assignment and removing it require no allocation. */
    bool changed;
    allocators[_i](0);
    css_error error = css__declaration_store_set(store, css__declaration_list_get(list, 0), &changed);
    allocators[_i](UINT_MAX);
    ck_assert_int_eq(error, CSS_OK);
    ck_assert(!changed);
    allocators[_i](0);
    error = css__declaration_store_remove(store, "margin", 6, &changed);
    allocators[_i](UINT_MAX);
    ck_assert_int_eq(error, CSS_OK);
    ck_assert(changed);
    css__declaration_list_destroy(list);
    css__declaration_store_destroy(store);
}
END_TEST
#endif

static void unexpected_string(lwc_string *string, void *pw)
{
    (*(unsigned *)pw)++;
    fprintf(stderr, "Leaked interned string: %s\n", lwc_string_data(string));
}

int main(void)
{
    Suite *suite = suite_create("CSS declaration store");
    TCase *tc = tcase_create("Native editing");
    tcase_add_loop_test(tc, test_assignments, 0, ASSIGNMENT_COUNT * 2);
    tcase_add_loop_test(tc, test_removals, 0, REMOVAL_COUNT * 2);
    tcase_add_test(tc, test_shorthand_catalogue);
    tcase_add_test(tc, test_native_state_survives_readback_limits);
    tcase_add_test(tc, test_length_and_borrowed_names);
    tcase_add_test(tc, test_repeated_edits_release_sources);
    tcase_add_test(tc, test_shared_source_lifetime);
    tcase_add_test(tc, test_bad_arguments);
#ifdef WISP_TEST_ALLOC_FAILURE
    tcase_add_loop_test(tc, test_create_allocation_failure, 0, 3);
    tcase_add_loop_test(tc, test_set_allocation_failure, 0, 3);
#endif
    suite_add_tcase(suite, tc);
    SRunner *runner = srunner_create(suite);
    srunner_set_fork_status(runner, CK_NOFORK);
    srunner_run_all(runner, CK_NORMAL);
    int failed = srunner_ntests_failed(runner);
    srunner_free(runner);
    unsigned leaked = 0;
    lwc_iterate_strings(unexpected_string, &leaked);
    return failed || leaked ? EXIT_FAILURE : EXIT_SUCCESS;
}

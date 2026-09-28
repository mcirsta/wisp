/* Class tokens used by CSS selectors must split on HTML ASCII whitespace. */
#include <check.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <dom/dom.h>
#include <libwapcaplet/libwapcaplet.h>

static dom_document *document;
static dom_element *element;
static dom_string *class_name;

static void setup(void)
{
    dom_string *tag;
    ck_assert_int_eq(
        dom_implementation_create_document(DOM_IMPLEMENTATION_HTML, NULL, NULL, NULL, NULL, NULL, &document),
        DOM_NO_ERR);
    ck_assert_int_eq(dom_string_create((const uint8_t *)"div", 3, &tag), DOM_NO_ERR);
    ck_assert_int_eq(dom_document_create_element(document, tag, &element), DOM_NO_ERR);
    dom_string_unref(tag);
    ck_assert_int_eq(dom_string_create((const uint8_t *)"class", 5, &class_name), DOM_NO_ERR);
}

static void teardown(void)
{
    dom_string_unref(class_name);
    dom_node_unref(element);
    dom_node_unref(document);
}

static void set_classes(const char *value)
{
    dom_string *s;
    ck_assert_int_eq(dom_string_create((const uint8_t *)value, strlen(value), &s), DOM_NO_ERR);
    ck_assert_int_eq(dom_element_set_attribute(element, class_name, s), DOM_NO_ERR);
    dom_string_unref(s);
}

static void expect_classes(dom_element *node, const char *const *expected, uint32_t count, const char *description)
{
    lwc_string **classes;
    uint32_t actual;
    ck_assert_int_eq(dom_element_get_classes(node, &classes, &actual), DOM_NO_ERR);
    ck_assert_msg(actual == count, "%s: expected %u classes, got %u", description, count, actual);
    for (uint32_t i = 0; i < count; i++) {
        ck_assert_msg(lwc_string_length(classes[i]) == strlen(expected[i]) &&
                memcmp(lwc_string_data(classes[i]), expected[i], strlen(expected[i])) == 0,
            "%s: incorrect class token %u", description, i);
        bool match;
        ck_assert_int_eq(dom_element_has_class(node, classes[i], &match), DOM_NO_ERR);
        ck_assert_msg(match, "%s: parsed class token %u does not match", description, i);
        lwc_string_unref(classes[i]);
    }
    /* The array belongs to libdom; only its returned string references are ours. */
}

static const struct {
    const char *name, *separator;
} separators[] = {
    {"space", " "},
    {"tab", "\t"},
    {"line feed", "\n"},
    {"form feed", "\f"},
    {"carriage return", "\r"},
    {"CRLF", "\r\n"},
    {"mixed whitespace", " \t\n\f\r "},
};

START_TEST(test_class_separators)
{
    const char *sep = separators[_i].separator;
    char value[128];
    snprintf(value, sizeof(value), "%sfirst%ssecond%s", sep, sep, sep);
    set_classes(value);
    const char *expected[] = {"first", "second"};
    expect_classes(element, expected, 2, separators[_i].name);
}
END_TEST

START_TEST(test_empty_classes)
{
    set_classes(_i == 0 ? "" : " \t\n\r\f ");
    expect_classes(element, NULL, 0, "empty or whitespace-only attribute");
}
END_TEST

static const char *non_separators[] = {"\v", "\xc2\xa0", "\xe2\x80\x83"};

START_TEST(test_non_separators)
{
    char token[64];
    snprintf(token, sizeof(token), "first%ssecond", non_separators[_i]);
    set_classes(token);
    const char *expected[] = {token};
    expect_classes(element, expected, 1, "vertical tab or Unicode space inside class name");
    lwc_string *first;
    bool match;
    ck_assert_int_eq(lwc_intern_string("first", 5, &first), lwc_error_ok);
    ck_assert_int_eq(dom_element_has_class(element, first, &match), DOM_NO_ERR);
    ck_assert_msg(!match, "non-HTML whitespace must not split a class name (variant %d)", _i);
    lwc_string_unref(first);
}
END_TEST

START_TEST(test_class_cache_updates_and_clone)
{
    set_classes("old");
    set_classes("new\nsecond\t");
    const char *expected[] = {"new", "second"};
    expect_classes(element, expected, 2, "updated attribute");

    dom_node *clone;
    ck_assert_int_eq(dom_node_clone_node(element, false, &clone), DOM_NO_ERR);
    expect_classes((dom_element *)clone, expected, 2, "cloned element");
    dom_node_unref(clone);

    ck_assert_int_eq(dom_element_remove_attribute(element, class_name), DOM_NO_ERR);
    expect_classes(element, NULL, 0, "removed attribute");
}
END_TEST

int main(void)
{
    Suite *suite = suite_create("DOM class whitespace");
    TCase *tc = tcase_create("Class cache");
    tcase_add_checked_fixture(tc, setup, teardown);
    tcase_add_loop_test(tc, test_class_separators, 0, sizeof(separators) / sizeof(separators[0]));
    tcase_add_loop_test(tc, test_empty_classes, 0, 2);
    tcase_add_loop_test(tc, test_non_separators, 0, sizeof(non_separators) / sizeof(non_separators[0]));
    tcase_add_test(tc, test_class_cache_updates_and_clone);
    suite_add_tcase(suite, tc);
    SRunner *runner = srunner_create(suite);
    srunner_run_all(runner, CK_NORMAL);
    int failed = srunner_ntests_failed(runner);
    srunner_free(runner);
    return failed ? EXIT_FAILURE : EXIT_SUCCESS;
}

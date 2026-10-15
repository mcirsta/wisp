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

static dom_nodelist *query_classes(dom_node *root, const char *query)
{
    dom_string *str;
    dom_nodelist *list;
    ck_assert_int_eq(dom_string_create((const uint8_t *)query, strlen(query), &str), DOM_NO_ERR);
    ck_assert_int_eq(dom_node_get_elements_by_class_name(root, str, &list), DOM_NO_ERR);
    dom_string_unref(str);
    return list;
}

static void expect_matches(dom_nodelist *list, uint32_t expected, const char *description)
{
    uint32_t length;
    ck_assert_int_eq(dom_nodelist_get_length(list, &length), DOM_NO_ERR);
    ck_assert_msg(length == expected, "%s: expected %u matches, got %u", description, expected, length);
    dom_node *node = NULL;
    ck_assert_int_eq(dom_nodelist_item(list, expected, &node), DOM_NO_ERR);
    ck_assert_ptr_null(node);
}

START_TEST(test_class_query_live)
{
    dom_node *added;
    ck_assert_int_eq(dom_node_append_child(document, element, &added), DOM_NO_ERR);
    dom_node_unref(added);
    set_classes("one\ntwo");
    dom_nodelist *list = query_classes((dom_node *)document, " \tone\r\ntwo one\f");
    expect_matches(list, 1, "all tokens, whitespace and duplicates");
    dom_node *first;
    ck_assert_int_eq(dom_nodelist_item(list, 0, &first), DOM_NO_ERR);
    ck_assert_ptr_eq(first, element);
    dom_node_unref(first);
    dom_nodelist *cached = query_classes((dom_node *)document, " \tone\r\ntwo one\f");
    ck_assert_ptr_eq(cached, list);
    dom_nodelist_unref(cached);
    dom_nodelist *empty = query_classes((dom_node *)document, " \t\f\n\r");
    expect_matches(empty, 0, "empty query never matches");
    dom_nodelist_unref(empty);
    empty = query_classes((dom_node *)element, "one");
    expect_matches(empty, 0, "root is excluded");
    dom_nodelist_unref(empty);
    set_classes("one");
    expect_matches(list, 0, "must contain every query token");
    set_classes("three two one");
    expect_matches(list, 1, "extra element classes allowed");
    ck_assert_int_eq(dom_element_remove_attribute(element, class_name), DOM_NO_ERR);
    expect_matches(list, 0, "attribute removal is live");
    dom_nodelist_unref(list);
}
END_TEST

START_TEST(test_class_query_modes)
{
    dom_node *added;
    ck_assert_int_eq(dom_node_append_child(document, element, &added), DOM_NO_ERR);
    dom_node_unref(added);
    set_classes("ONE tWo \xc3\x89 one\vtwo one\xc2\xa0two");
    dom_nodelist *list = query_classes((dom_node *)document, "one TWO");
    dom_document_quirks_mode modes[] = {
        DOM_DOCUMENT_QUIRKS_MODE_NONE, DOM_DOCUMENT_QUIRKS_MODE_LIMITED, DOM_DOCUMENT_QUIRKS_MODE_FULL};
    for (unsigned i = 0; i < sizeof(modes) / sizeof(*modes); i++) {
        ck_assert_int_eq(dom_document_set_quirks_mode(document, modes[i]), DOM_NO_ERR);
        expect_matches(list, modes[i] == DOM_DOCUMENT_QUIRKS_MODE_FULL, "only full quirks folds ASCII case");
    }
    dom_nodelist_unref(list);
    const char *queries[] = {"\xc3\x89", "\xc3\xa9", "one\vtwo", "one\xc2\xa0two", "one,two"};
    const uint32_t expected[] = {1, 0, 1, 1, 0};
    for (unsigned i = 0; i < sizeof(queries) / sizeof(*queries); i++) {
        list = query_classes((dom_node *)document, queries[i]);
        expect_matches(list, expected[i], "non-ASCII case and non-whitespace preserved");
        dom_nodelist_unref(list);
    }
}
END_TEST

START_TEST(test_class_query_adoption)
{
    dom_string *tag;
    dom_element *child;
    ck_assert_int_eq(dom_string_create((const uint8_t *)"span", 4, &tag), DOM_NO_ERR);
    ck_assert_int_eq(dom_document_create_element(document, tag, &child), DOM_NO_ERR);
    dom_string_unref(tag);
    dom_string *value;
    ck_assert_int_eq(dom_string_create((const uint8_t *)"UPPER", 5, &value), DOM_NO_ERR);
    ck_assert_int_eq(dom_element_set_attribute(child, class_name, value), DOM_NO_ERR);
    dom_string_unref(value);
    dom_node *added;
    ck_assert_int_eq(dom_node_append_child(element, child, &added), DOM_NO_ERR);
    dom_node_unref(added);
    dom_nodelist *list = query_classes((dom_node *)element, "upper");
    expect_matches(list, 0, "standards before adoption");
    dom_document *other;
    ck_assert_int_eq(
        dom_implementation_create_document(DOM_IMPLEMENTATION_HTML, NULL, NULL, NULL, NULL, NULL, &other), DOM_NO_ERR);
    ck_assert_int_eq(dom_document_set_quirks_mode(other, DOM_DOCUMENT_QUIRKS_MODE_FULL), DOM_NO_ERR);
    ck_assert_int_eq(dom_document_adopt_node(other, element, &added), DOM_NO_ERR);
    dom_node_unref(added);
    expect_matches(list, 1, "uses adopted root's current document mode");
    dom_nodelist_unref(list);
    dom_node_unref(child);
    dom_node_unref(other);
}
END_TEST

START_TEST(test_class_cache_embedded_null)
{
    const char bytes[] = "before\0after other";
    dom_string *value;
    ck_assert_int_eq(dom_string_create((const uint8_t *)bytes, sizeof(bytes) - 1, &value), DOM_NO_ERR);
    ck_assert_int_eq(dom_element_set_attribute(element, class_name, value), DOM_NO_ERR);
    dom_string_unref(value);
    lwc_string **classes;
    uint32_t count;
    ck_assert_int_eq(dom_element_get_classes(element, &classes, &count), DOM_NO_ERR);
    ck_assert_uint_eq(count, 2);
    ck_assert_uint_eq(lwc_string_length(classes[0]), 12);
    ck_assert_int_eq(memcmp(lwc_string_data(classes[0]), bytes, 12), 0);
    for (uint32_t i = 0; i < count; i++)
        lwc_string_unref(classes[i]);
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
    tcase_add_test(tc, test_class_cache_embedded_null);
    tcase_add_test(tc, test_class_query_live);
    tcase_add_test(tc, test_class_query_modes);
    tcase_add_test(tc, test_class_query_adoption);
    suite_add_tcase(suite, tc);
    SRunner *runner = srunner_create(suite);
    srunner_run_all(runner, CK_NORMAL);
    int failed = srunner_ntests_failed(runner);
    srunner_free(runner);
    return failed ? EXIT_FAILURE : EXIT_SUCCESS;
}

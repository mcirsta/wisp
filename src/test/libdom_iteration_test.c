/* DOM walks must cover hidden elements too, without repeated indexed scans. */
#include <check.h>
#include <stdlib.h>
#include <string.h>

#include <wisp/utils/errors.h>
#include "utils/libdom.h"

static dom_document *document;

static dom_string *string(const char *text)
{
    dom_string *s;
    ck_assert_int_eq(dom_string_create((const uint8_t *)text, strlen(text), &s), DOM_NO_ERR);
    return s;
}

/* Return a borrowed pointer, owned by its parent. */
static dom_node *append_element(dom_node *parent, const char *name)
{
    dom_element *element;
    dom_node *inserted;
    dom_string *tag = string(name);
    ck_assert_int_eq(dom_document_create_element(document, tag, &element), DOM_NO_ERR);
    ck_assert_int_eq(dom_node_append_child(parent, element, &inserted), DOM_NO_ERR);
    dom_string_unref(tag);
    dom_node_unref(inserted);
    dom_node_unref(element);
    return (dom_node *)element;
}

struct visits {
    dom_node **expected;
    unsigned count;
    unsigned visited;
    unsigned stop_at;
};

static nserror visit(dom_node *node, void *pw)
{
    struct visits *v = pw;
    ck_assert_msg(v->visited < v->count, "Unexpected element at visit %u", v->visited);
    if (v->expected != NULL)
        ck_assert_msg(node == v->expected[v->visited], "Wrong element at visit %u", v->visited);
    v->visited++;
    return v->visited == v->stop_at ? NSERROR_STOPPED : NSERROR_OK;
}

static void setup(void)
{
    ck_assert_int_eq(
        dom_implementation_create_document(DOM_IMPLEMENTATION_HTML, NULL, NULL, NULL, NULL, NULL, &document),
        DOM_NO_ERR);
}

static void teardown(void)
{
    dom_node_unref(document);
}

START_TEST(test_order_and_subtree_boundary)
{
    dom_node *root = append_element((dom_node *)document, "div");
    dom_node *first = append_element(root, "section");
    dom_node *leaf = append_element(first, "span");
    dom_string *text = string("text between elements");
    dom_node *inserted;
    dom_text *text_node;
    dom_comment *comment;
    ck_assert_int_eq(dom_document_create_text_node(document, text, &text_node), DOM_NO_ERR);
    ck_assert_int_eq(dom_node_append_child(first, text_node, &inserted), DOM_NO_ERR);
    dom_node_unref(inserted);
    dom_node_unref(text_node);
    ck_assert_int_eq(dom_document_create_comment(document, text, &comment), DOM_NO_ERR);
    ck_assert_int_eq(dom_node_append_child(root, comment, &inserted), DOM_NO_ERR);
    dom_node_unref(inserted);
    dom_node_unref(comment);
    dom_string_unref(text);
    dom_node *second = append_element(root, "section");
    dom_node *hidden = append_element(second, "div");
    dom_node *deep = append_element(hidden, "span");
    dom_string *key = string("style"), *value = string("display:none");
    ck_assert_int_eq(dom_element_set_attribute(hidden, key, value), DOM_NO_ERR);
    dom_string_unref(key);
    dom_string_unref(value);
    dom_node *expected[] = {root, first, leaf, second, hidden, deep};
    struct visits v = {.expected = expected, .count = 6};
    ck_assert_int_eq(libdom_iterate_elements((dom_node *)document, visit, &v), NSERROR_OK);
    ck_assert_uint_eq(v.visited, 6);

    v = (struct visits){.expected = expected + 1, .count = 2};
    ck_assert_int_eq(libdom_iterate_elements(first, visit, &v), NSERROR_OK);
    ck_assert_uint_eq(v.visited, 2); /* Must not visit first's next sibling. */
    v = (struct visits){.expected = expected + 2, .count = 1};
    ck_assert_int_eq(libdom_iterate_elements(leaf, visit, &v), NSERROR_OK);
    ck_assert_uint_eq(v.visited, 1);

    v = (struct visits){.expected = expected, .count = 6, .stop_at = 3};
    ck_assert_int_eq(libdom_iterate_elements(root, visit, &v), NSERROR_STOPPED);
    ck_assert_uint_eq(v.visited, 3);
    v.visited = v.stop_at = 0;
    ck_assert_int_eq(libdom_iterate_elements(root, visit, &v), NSERROR_OK);
    ck_assert_uint_eq(v.visited, 6);
}
END_TEST

static nserror replace_children(dom_node *node, void *pw)
{
    struct visits *v = pw;
    /* Model textarea synchronization: replace its children during the callback. */
    if (v->visited == 1) {
        dom_string *value = string("updated value");
        ck_assert_int_eq(dom_node_set_text_content(node, value), DOM_NO_ERR);
        dom_string_unref(value);
    }
    return visit(node, pw);
}

START_TEST(test_children_read_after_callback)
{
    dom_node *root = append_element((dom_node *)document, "div");
    dom_node *changing = append_element(root, "div");
    append_element(changing, "span");
    dom_node *last = append_element(root, "span");
    dom_node *expected[] = {root, changing, last};
    struct visits v = {.expected = expected, .count = 3};
    ck_assert_int_eq(libdom_iterate_elements(root, replace_children, &v), NSERROR_OK);
    ck_assert_uint_eq(v.visited, 3);
}
END_TEST

START_TEST(test_deep_tree)
{
    enum { DEPTH = 4096 };
    dom_node *root = append_element((dom_node *)document, "div");
    dom_node *node = root;
    for (unsigned i = 1; i < DEPTH; i++)
        node = append_element(node, "div");
    struct visits v = {.count = DEPTH};
    ck_assert_int_eq(libdom_iterate_elements((dom_node *)document, visit, &v), NSERROR_OK);
    ck_assert_uint_eq(v.visited, DEPTH);

    /* Remove from the leaf upward so libdom's recursive destructor is not what
     * this test measures (or the source of a sanitizer stack overflow). */
    node = dom_node_ref(node);
    while (node != root) {
        dom_node *parent, *removed;
        ck_assert_int_eq(dom_node_get_parent_node(node, &parent), DOM_NO_ERR);
        ck_assert_int_eq(dom_node_remove_child(parent, node, &removed), DOM_NO_ERR);
        dom_node_unref(removed);
        dom_node_unref(node);
        node = parent;
    }
    dom_node_unref(node);
}
END_TEST

START_TEST(test_empty_and_invalid)
{
    struct visits v = {0};
    ck_assert_int_eq(libdom_iterate_elements((dom_node *)document, visit, &v), NSERROR_OK);
    ck_assert_uint_eq(v.visited, 0);
    ck_assert_int_eq(libdom_iterate_elements(NULL, visit, &v), NSERROR_BAD_PARAMETER);
    ck_assert_int_eq(libdom_iterate_elements((dom_node *)document, NULL, &v), NSERROR_BAD_PARAMETER);
}
END_TEST

int main(void)
{
    Suite *suite = suite_create("DOM element traversal");
    TCase *tc = tcase_create("Traversal");
    tcase_add_checked_fixture(tc, setup, teardown);
    tcase_add_test(tc, test_order_and_subtree_boundary);
    tcase_add_test(tc, test_children_read_after_callback);
    tcase_add_test(tc, test_deep_tree);
    tcase_add_test(tc, test_empty_and_invalid);
    suite_add_tcase(suite, tc);
    SRunner *runner = srunner_create(suite);
    srunner_run_all(runner, CK_NORMAL);
    int failures = srunner_ntests_failed(runner);
    srunner_free(runner);
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

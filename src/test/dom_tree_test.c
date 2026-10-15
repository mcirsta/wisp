/* Native fragment ownership must work without JavaScript holding the document. */
#include <check.h>
#include <stdlib.h>
#include <dom/dom.h>
#include <limits.h>
#ifdef WISP_TEST_ALLOC_FAILURE
#include "test/malloc_fig.h"
#endif

START_TEST(test_fragment_keeps_owner_alive)
{
    dom_document *doc = NULL, *owner = NULL;
    dom_document_fragment *fragment = NULL;
    dom_node *root = NULL, *old = NULL, *result = NULL;
    dom_string *name = NULL;
    ck_assert_int_eq(
        dom_implementation_create_document(DOM_IMPLEMENTATION_HTML, NULL, NULL, NULL, NULL, NULL, &doc), DOM_NO_ERR);
    ck_assert_int_eq(dom_string_create((const uint8_t *)"div", 3, &name), DOM_NO_ERR);
    ck_assert_int_eq(dom_document_create_element(doc, name, &root), DOM_NO_ERR);
    ck_assert_int_eq(dom_node_append_child(doc, root, &result), DOM_NO_ERR);
    dom_node_unref(result);
    ck_assert_int_eq(dom_document_create_document_fragment(doc, &fragment), DOM_NO_ERR);
    if (_i & 1) {
        ck_assert_int_eq(dom_document_create_element(doc, name, &old), DOM_NO_ERR);
        ck_assert_int_eq(dom_node_append_child(fragment, old, &result), DOM_NO_ERR);
        dom_node_unref(result);
        dom_node_unref(old);
    }
    if (_i & 2) {
        ck_assert_int_eq(dom_document_create_element(doc, name, &old), DOM_NO_ERR);
        ck_assert_int_eq(dom_node_append_child(root, old, &result), DOM_NO_ERR);
        dom_node_unref(result);
        ck_assert_int_eq(dom_node_replace_child(root, fragment, old, &result), DOM_NO_ERR);
        dom_node_unref(old);
    } else {
        ck_assert_int_eq(dom_node_append_child(root, fragment, &result), DOM_NO_ERR);
    }
    dom_node_unref(result);
    dom_node_unref(root);
    dom_node_unref(doc);
    /* All nodes except the emptied, detached fragment are now unreferenced.
     * Removing the fragment from libdom's pending list during insertion would
     * free its document above and leave its owner pointer dangling. */
    ck_assert_int_eq(dom_node_get_owner_document(fragment, &owner), DOM_NO_ERR);
    ck_assert_ptr_eq(owner, doc);
    ck_assert_int_eq(dom_node_set_text_content(fragment, name), DOM_NO_ERR);
    dom_string *text = NULL;
    ck_assert_int_eq(dom_node_get_text_content(fragment, &text), DOM_NO_ERR);
    ck_assert_msg(dom_string_isequal(text, name), "Fragment reuse failed after %s of %s fragment",
        (_i & 2) ? "replacement" : "insertion", (_i & 1) ? "populated" : "empty");
    dom_string_unref(text);
    dom_string_unref(name);
    dom_node_unref(owner);
    dom_node_unref(fragment);
}
END_TEST

START_TEST(test_document_collection_tracks_root)
{
    dom_document *doc = NULL;
    dom_html_collection *forms = NULL, *other = NULL;
    dom_node *root = NULL, *result = NULL, *item = NULL;
    dom_string *name = NULL;
    uint32_t length;
    ck_assert_int_eq(
        dom_implementation_create_document(DOM_IMPLEMENTATION_HTML, NULL, NULL, NULL, NULL, NULL, &doc), DOM_NO_ERR);
    /* Document collections are valid even before the document has a root. */
    ck_assert_int_eq(dom_html_document_get_forms(doc, &forms), DOM_NO_ERR);
    ck_assert_int_eq(dom_html_collection_get_length(forms, &length), DOM_NO_ERR);
    ck_assert_uint_eq(length, 0);
    ck_assert_int_eq(dom_html_document_get_images(doc, &other), DOM_NO_ERR);
    dom_html_collection_unref(other);
    ck_assert_int_eq(dom_html_document_get_links(doc, &other), DOM_NO_ERR);
    dom_html_collection_unref(other);
    ck_assert_int_eq(dom_html_document_get_anchors(doc, &other), DOM_NO_ERR);
    dom_html_collection_unref(other);
    ck_assert_int_eq(dom_html_document_get_applets(doc, &other), DOM_NO_ERR);
    dom_html_collection_unref(other);
    ck_assert_int_eq(dom_string_create((const uint8_t *)"form", 4, &name), DOM_NO_ERR);
    ck_assert_int_eq(dom_document_create_element(doc, name, &root), DOM_NO_ERR);
    ck_assert_int_eq(dom_node_append_child(doc, root, &result), DOM_NO_ERR);
    dom_node_unref(result);
    ck_assert_int_eq(dom_html_collection_get_length(forms, &length), DOM_NO_ERR);
    ck_assert_uint_eq(length, 1);
    ck_assert_int_eq(dom_html_collection_item(forms, 0, &item), DOM_NO_ERR);
    ck_assert_ptr_eq(item, root);
    dom_node_unref(item);
    ck_assert_int_eq(dom_node_remove_child(doc, root, &result), DOM_NO_ERR);
    dom_node_unref(result);
    ck_assert_int_eq(dom_html_collection_get_length(forms, &length), DOM_NO_ERR);
    ck_assert_uint_eq(length, 0);
    ck_assert_int_eq(dom_html_collection_item(forms, 0, &item), DOM_NO_ERR);
    ck_assert_ptr_null(item);
    ck_assert_int_eq(dom_node_append_child(doc, root, &result), DOM_NO_ERR);
    dom_node_unref(result);
    ck_assert_int_eq(dom_html_collection_get_length(forms, &length), DOM_NO_ERR);
    ck_assert_uint_eq(length, 1);
    dom_node_unref(root);
    dom_string_unref(name);
    dom_html_collection_unref(forms);
    dom_node_unref(doc);
}
END_TEST

static dom_node *make_element(dom_document *doc, const char *tag)
{
    dom_string *name = NULL;
    dom_node *node = NULL;
    ck_assert_int_eq(dom_string_create((const uint8_t *)tag, strlen(tag), &name), DOM_NO_ERR);
    ck_assert_int_eq(dom_document_create_element(doc, name, &node), DOM_NO_ERR);
    dom_string_unref(name);
    return node;
}

static void append_node(dom_node *parent, dom_node *child)
{
    dom_node *result = NULL;
    ck_assert_int_eq(dom_node_append_child(parent, child, &result), DOM_NO_ERR);
    dom_node_unref(result);
}

START_TEST(test_replace_all)
{
    dom_document *doc = NULL;
    ck_assert_int_eq(
        dom_implementation_create_document(DOM_IMPLEMENTATION_HTML, NULL, NULL, NULL, NULL, NULL, &doc), DOM_NO_ERR);
    dom_node *root = make_element(doc, "div"), *old = make_element(doc, "p"), *child = make_element(doc, "b");
    dom_document_fragment *fragment = NULL;
    ck_assert_int_eq(dom_document_create_document_fragment(doc, &fragment), DOM_NO_ERR);
    dom_node *parent = _i & 1 ? (dom_node *)fragment : root;
    append_node(parent, old);
    append_node(old, child);
    dom_node *replacement = child;
    dom_document_fragment *incoming = NULL;
    if (_i & 2) {
        ck_assert_int_eq(dom_document_create_document_fragment(doc, &incoming), DOM_NO_ERR);
        append_node((dom_node *)incoming, child);
        replacement = (dom_node *)incoming;
    }
    ck_assert_int_eq(dom_node_replace_all(parent, replacement), DOM_NO_ERR);
    dom_node *first = NULL, *last = NULL, *owner = NULL;
    ck_assert_int_eq(dom_node_get_first_child(parent, &first), DOM_NO_ERR);
    ck_assert_int_eq(dom_node_get_last_child(parent, &last), DOM_NO_ERR);
    ck_assert_ptr_eq(first, child);
    ck_assert_ptr_eq(last, child);
    dom_node_unref(first);
    dom_node_unref(last);
    ck_assert_int_eq(dom_node_get_parent_node(old, &owner), DOM_NO_ERR);
    ck_assert_ptr_null(owner);
    if (incoming != NULL) {
        ck_assert_int_eq(dom_node_get_first_child(incoming, &first), DOM_NO_ERR);
        ck_assert_ptr_null(first);
    }
    ck_assert_int_eq(dom_node_replace_all(parent, NULL), DOM_NO_ERR);
    ck_assert_int_eq(dom_node_get_parent_node(child, &owner), DOM_NO_ERR);
    ck_assert_ptr_null(owner);
    ck_assert_int_eq(dom_node_get_first_child(parent, &first), DOM_NO_ERR);
    ck_assert_ptr_null(first);
    dom_node_unref(root);
    dom_node_unref(fragment);
    dom_node_unref(old);
    dom_node_unref(child);
    dom_node_unref(incoming);
    dom_node_unref(doc);
}
END_TEST

START_TEST(test_replace_all_validation)
{
    dom_document *doc = NULL, *foreign = NULL;
    ck_assert_int_eq(
        dom_implementation_create_document(DOM_IMPLEMENTATION_HTML, NULL, NULL, NULL, NULL, NULL, &doc), DOM_NO_ERR);
    ck_assert_int_eq(
        dom_implementation_create_document(DOM_IMPLEMENTATION_HTML, NULL, NULL, NULL, NULL, NULL, &foreign),
        DOM_NO_ERR);
    dom_node *root = make_element(doc, "div"), *old = make_element(doc, "p"), *other = make_element(foreign, "b");
    append_node(root, old);
    ck_assert_int_eq(dom_node_replace_all(root, root), DOM_HIERARCHY_REQUEST_ERR);
    ck_assert_int_eq(dom_node_replace_all(old, root), DOM_HIERARCHY_REQUEST_ERR);
    ck_assert_int_eq(dom_node_replace_all(root, (dom_node *)doc), DOM_HIERARCHY_REQUEST_ERR);
    ck_assert_int_eq(dom_node_replace_all(NULL, NULL), DOM_INVALID_ACCESS_ERR);
    dom_node *first = NULL;
    ck_assert_int_eq(dom_node_get_first_child(root, &first), DOM_NO_ERR);
    ck_assert_ptr_eq(first, old);
    dom_node_unref(first);
    ck_assert_int_eq(dom_node_replace_all(root, other), DOM_NO_ERR);
    ck_assert_int_eq(dom_node_get_first_child(root, &first), DOM_NO_ERR);
    ck_assert_ptr_eq(first, other);
    dom_node_unref(first);
    dom_document *owner = NULL;
    ck_assert_int_eq(dom_node_get_owner_document(other, &owner), DOM_NO_ERR);
    ck_assert_ptr_eq(owner, doc);
    dom_node_unref(owner);
    dom_node *host = make_element(doc, "template");
    dom_document_fragment *content = NULL;
    ck_assert_int_eq(dom_html_template_element_get_content((dom_html_template_element *)host, &content), DOM_NO_ERR);
    ck_assert_int_eq(dom_node_replace_all((dom_node *)content, host), DOM_HIERARCHY_REQUEST_ERR);
    dom_node_unref(content);
    dom_node_unref(host);
    dom_node_unref(root);
    dom_node_unref(old);
    dom_node_unref(other);
    dom_node_unref(doc);
    dom_node_unref(foreign);
}
END_TEST

#ifdef WISP_TEST_ALLOC_FAILURE
START_TEST(test_text_replacement_allocation_failure)
{
    dom_document *doc = NULL;
    ck_assert_int_eq(
        dom_implementation_create_document(DOM_IMPLEMENTATION_HTML, NULL, NULL, NULL, NULL, NULL, &doc), DOM_NO_ERR);
    dom_node *root = make_element(doc, "div"), *old = make_element(doc, "b");
    append_node(root, old);
    dom_string *value = NULL;
    ck_assert_int_eq(dom_string_create((const uint8_t *)"replacement", 11, &value), DOM_NO_ERR);
    malloc_limit(0);
    dom_exception err = dom_node_set_text_content(root, value);
    malloc_limit(UINT_MAX);
    ck_assert_int_eq(err, DOM_NO_MEM_ERR);
    dom_node *first = NULL;
    ck_assert_int_eq(dom_node_get_first_child(root, &first), DOM_NO_ERR);
    ck_assert_ptr_eq(first, old);
    dom_node_unref(first);
    ck_assert_int_eq(dom_node_set_text_content(root, value), DOM_NO_ERR);
    dom_string *result = NULL;
    ck_assert_int_eq(dom_node_get_text_content(root, &result), DOM_NO_ERR);
    ck_assert(dom_string_isequal(value, result));
    dom_string_unref(result);
    dom_string_unref(value);
    dom_node_unref(root);
    dom_node_unref(old);
    dom_node_unref(doc);
}
END_TEST
#endif

START_TEST(test_contains_attribute_ownership)
{
    dom_document *doc = NULL;
    ck_assert_int_eq(
        dom_implementation_create_document(DOM_IMPLEMENTATION_HTML, NULL, NULL, NULL, NULL, NULL, &doc), DOM_NO_ERR);
    dom_node *root = make_element(doc, "div");
    append_node((dom_node *)doc, root);
    dom_string *name = NULL;
    ck_assert_int_eq(dom_string_create((const uint8_t *)"id", 2, &name), DOM_NO_ERR);
    ck_assert_int_eq(dom_element_set_attribute(root, name, name), DOM_NO_ERR);
    dom_attr *attr = NULL;
    ck_assert_int_eq(dom_element_get_attribute_node(root, name, &attr), DOM_NO_ERR);
    ck_assert_ptr_nonnull(attr);
    dom_element *owner = NULL;
    ck_assert_int_eq(dom_attr_get_owner_element(attr, &owner), DOM_NO_ERR);
    ck_assert_ptr_eq(owner, root);
    dom_node_unref(owner);
    bool contained = true;
    ck_assert_int_eq(dom_node_contains(root, attr, &contained), DOM_NO_ERR);
    ck_assert_msg(!contained, "An attribute's owner element is not its DOM parent");
    ck_assert_int_eq(dom_node_contains(doc, attr, &contained), DOM_NO_ERR);
    ck_assert_msg(!contained, "An owned attribute is not a document descendant");
    ck_assert_int_eq(dom_node_contains(attr, attr, &contained), DOM_NO_ERR);
    ck_assert_msg(contained, "Containment includes the attribute itself");
    ck_assert_int_eq(dom_node_contains(root, NULL, &contained), DOM_NO_ERR);
    ck_assert_msg(!contained, "Null is not a descendant");
    dom_node_unref(attr);
    dom_string_unref(name);
    dom_node_unref(root);
    dom_node_unref(doc);
}
END_TEST

int main(void)
{
    Suite *suite = suite_create("DOM tree editing");
    TCase *tc = tcase_create("Fragment lifetime");
    tcase_add_loop_test(tc, test_fragment_keeps_owner_alive, 0, 4);
    tcase_add_test(tc, test_document_collection_tracks_root);
    tcase_add_loop_test(tc, test_replace_all, 0, 4);
    tcase_add_test(tc, test_replace_all_validation);
    tcase_add_test(tc, test_contains_attribute_ownership);
#ifdef WISP_TEST_ALLOC_FAILURE
    tcase_add_test(tc, test_text_replacement_allocation_failure);
#endif
    suite_add_tcase(suite, tc);
    SRunner *runner = srunner_create(suite);
    srunner_run_all(runner, CK_NORMAL);
    int failed = srunner_ntests_failed(runner);
    srunner_free(runner);
    return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

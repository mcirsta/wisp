/* Native fragment ownership must work without JavaScript holding the document. */
#include <check.h>
#include <stdlib.h>
#include <dom/dom.h>

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

int main(void)
{
    Suite *suite = suite_create("DOM tree editing");
    TCase *tc = tcase_create("Fragment lifetime");
    tcase_add_loop_test(tc, test_fragment_keeps_owner_alive, 0, 4);
    tcase_add_test(tc, test_document_collection_tracks_root);
    suite_add_tcase(suite, tc);
    SRunner *runner = srunner_create(suite);
    srunner_run_all(runner, CK_NORMAL);
    int failed = srunner_ntests_failed(runner);
    srunner_free(runner);
    return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

/* Native ownership follows DOM adopt/clone, including source-document teardown.
 * https://dom.spec.whatwg.org/#concept-node-adopt
 * https://dom.spec.whatwg.org/#concept-node-clone */
#include <check.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <dom/dom.h>
#include <dom/events/events.h>
#ifdef WISP_TEST_ALLOC_FAILURE
#include "test/malloc_fig.h"
#endif

static dom_string *string(const char *text)
{
    dom_string *s = NULL;
    ck_assert_int_eq(dom_string_create((const uint8_t *)text, strlen(text), &s), DOM_NO_ERR);
    return s;
}

static dom_document *document(bool html)
{
    dom_document *doc = NULL;
    ck_assert_int_eq(dom_implementation_create_document(
                         html ? DOM_IMPLEMENTATION_HTML : DOM_IMPLEMENTATION_CORE, NULL, NULL, NULL, NULL, NULL, &doc),
        DOM_NO_ERR);
    return doc;
}

static dom_node *element(dom_document *doc, const char *name)
{
    dom_node *node = NULL;
    dom_string *s = string(name);
    ck_assert_int_eq(dom_document_create_element(doc, s, &node), DOM_NO_ERR);
    dom_string_unref(s);
    return node;
}

static void append(dom_node *parent, dom_node *child)
{
    dom_node *result = NULL;
    ck_assert_int_eq(dom_node_append_child(parent, child, &result), DOM_NO_ERR);
    ck_assert_ptr_eq(result, child);
    dom_node_unref(result);
}

static void owner(dom_node *node, dom_document *expected)
{
    dom_document *doc = NULL;
    ck_assert_int_eq(dom_node_get_owner_document(node, &doc), DOM_NO_ERR);
    ck_assert_ptr_eq(doc, expected);
    dom_node_unref(doc);
}

static void name_is(dom_node *node, const char *expected)
{
    dom_string *s = NULL;
    ck_assert_int_eq(dom_node_get_node_name(node, &s), DOM_NO_ERR);
    ck_assert_msg(
        dom_string_byte_length(s) == strlen(expected) && memcmp(dom_string_data(s), expected, strlen(expected)) == 0,
        "Expected nodeName %s", expected);
    dom_string_unref(s);
}

static void counted(dom_event *event, void *pw)
{
    (void)event;
    (*(unsigned *)pw)++;
}

static void dispatch(dom_node *node, dom_string *type)
{
    dom_event *event = NULL;
    bool success;
    ck_assert_int_eq(dom_event_create(&event), DOM_NO_ERR);
    ck_assert_int_eq(dom_event_init(event, type, true, false), DOM_NO_ERR);
    ck_assert_int_eq(dom_event_target_dispatch_event(node, event, &success), DOM_NO_ERR);
    dom_event_unref(event);
}

START_TEST(test_adopt_preserves_identity_and_state)
{
    dom_document *src = document(true), *dst = document((_i & 1) != 0);
    dom_node *parent = element(src, "div"), *input = element(src, "input"), *result = NULL;
    dom_string *key = string("value"), *value = string("kept"), *type = string("probe");
    dom_attr *attr = NULL;
    dom_node *text = NULL;
    unsigned calls = 0;
    void *previous = NULL, *data = NULL;
    dom_event_listener *listener;
    append((dom_node *)src, parent);
    append(parent, input);
    ck_assert_int_eq(dom_element_set_attribute(input, key, value), DOM_NO_ERR);
    ck_assert_int_eq(dom_element_get_attribute_node(input, key, &attr), DOM_NO_ERR);
    ck_assert_int_eq(dom_node_get_first_child(attr, &text), DOM_NO_ERR);
    ck_assert_ptr_nonnull(text);
    ck_assert_int_eq(dom_html_input_element_set_checked((dom_html_input_element *)input, true), DOM_NO_ERR);
    ck_assert_int_eq(dom_node_set_user_data(input, key, &calls, NULL, &previous), DOM_NO_ERR);
    ck_assert_int_eq(dom_event_listener_create(counted, &calls, &listener), DOM_NO_ERR);
    ck_assert_int_eq(dom_event_target_add_event_listener(input, type, listener, false), DOM_NO_ERR);
    dom_event_listener_unref(listener);
    name_is(input, "INPUT"); /* Populate the HTML nodeName cache first. */
    ck_assert_int_eq(dom_document_adopt_node(dst, input, &result), DOM_NO_ERR);
    ck_assert_ptr_eq(result, input);
    dom_node_unref(result);
    ck_assert_int_eq(dom_node_get_first_child(parent, &result), DOM_NO_ERR);
    ck_assert_ptr_null(result);
    ck_assert_int_eq(dom_node_get_parent_node(input, &result), DOM_NO_ERR);
    ck_assert_ptr_null(result);
    owner(input, dst);
    owner((dom_node *)attr, dst);
    owner(text, dst);
    dom_node_unref(parent);
    dom_node_unref(src);
    bool checked = false;
    ck_assert_int_eq(dom_html_input_element_get_checked((dom_html_input_element *)input, &checked), DOM_NO_ERR);
    ck_assert(checked);
    dom_string *actual = NULL;
    ck_assert_int_eq(dom_html_input_element_get_value((dom_html_input_element *)input, &actual), DOM_NO_ERR);
    ck_assert(dom_string_isequal(actual, value));
    dom_string_unref(actual);
    name_is(input, (_i & 1) ? "INPUT" : "input");
    ck_assert_int_eq(dom_node_get_user_data(input, key, &data), DOM_NO_ERR);
    ck_assert_ptr_eq(data, &calls);
    dispatch(input, type);
    ck_assert_uint_eq(calls, 1);
    if (_i & 2)
        append((dom_node *)dst, input);
    dom_node_unref(dst); /* Detached nodes/attributes keep the new owner alive. */
    owner(input, dst);
    ck_assert_int_eq(dom_element_set_attribute(input, key, type), DOM_NO_ERR);
    dom_node_unref(text);
    dom_node_unref(attr);
    dom_node_unref(input);
    dom_string_unref(key);
    dom_string_unref(value);
    dom_string_unref(type);
}
END_TEST

START_TEST(test_import_and_clone)
{
    /* Both destination types, shallow/deep, and cloneNode through the same path. */
    dom_document *src = document(true), *dst = document((_i & 1) != 0);
    dom_node *node = element(src, "input"), *child = NULL, *copy = NULL, *copy_child = NULL;
    dom_string *key = string("value"), *value = string("copied"), *type = string("probe");
    dom_attr *attr = NULL, *copy_attr = NULL;
    unsigned calls = 0;
    dom_event_listener *listener;
    void *previous = NULL, *data = NULL;
    ck_assert_int_eq(dom_element_set_attribute(node, key, value), DOM_NO_ERR);
    ck_assert_int_eq(dom_document_create_text_node(src, value, &child), DOM_NO_ERR);
    append(node, child);
    ck_assert_int_eq(dom_html_input_element_set_checked((dom_html_input_element *)node, true), DOM_NO_ERR);
    ck_assert_int_eq(dom_node_set_user_data(node, key, &calls, NULL, &previous), DOM_NO_ERR);
    ck_assert_int_eq(dom_event_listener_create(counted, &calls, &listener), DOM_NO_ERR);
    ck_assert_int_eq(dom_event_target_add_event_listener(node, type, listener, false), DOM_NO_ERR);
    dom_event_listener_unref(listener);
    if (_i & 4)
        ck_assert_int_eq(dom_node_clone_node(node, (_i & 2) != 0, &copy), DOM_NO_ERR);
    else
        ck_assert_int_eq(dom_document_import_node(dst, node, (_i & 2) != 0, &copy), DOM_NO_ERR);
    dom_document *expected = (_i & 4) ? src : dst;
    ck_assert_ptr_ne(copy, node);
    owner(copy, expected);
    owner(node, src);
    ck_assert_int_eq(dom_element_get_attribute_node(node, key, &attr), DOM_NO_ERR);
    ck_assert_int_eq(dom_element_get_attribute_node(copy, key, &copy_attr), DOM_NO_ERR);
    ck_assert_ptr_nonnull(copy_attr);
    ck_assert_ptr_ne(attr, copy_attr);
    owner((dom_node *)attr, src);
    owner((dom_node *)copy_attr, expected);
    ck_assert_int_eq(dom_node_get_first_child(copy_attr, &copy_child), DOM_NO_ERR);
    ck_assert_ptr_nonnull(copy_child);
    owner(copy_child, expected);
    dom_node_unref(copy_child);
    ck_assert_int_eq(dom_node_get_first_child(copy, &copy_child), DOM_NO_ERR);
    ck_assert_msg((copy_child != NULL) == ((_i & 2) != 0), "deep flag mismatch in variant %d", _i);
    if (copy_child != NULL) {
        ck_assert_ptr_ne(copy_child, child);
        owner(copy_child, expected);
        dom_node_unref(copy_child);
    }
    bool checked;
    ck_assert_int_eq(dom_html_input_element_get_checked((dom_html_input_element *)copy, &checked), DOM_NO_ERR);
    ck_assert(checked);
    ck_assert_int_eq(dom_node_get_user_data(copy, key, &data), DOM_NO_ERR);
    ck_assert_ptr_null(data);
    dispatch(copy, type);
    ck_assert_uint_eq(calls, 0);
    dispatch(node, type);
    ck_assert_uint_eq(calls, 1);
    dom_node_unref(attr);
    dom_node_unref(child);
    dom_node_unref(node);
    dom_node_unref(src);
    dom_node_unref(dst);
    dom_string *actual = NULL;
    ck_assert_int_eq(dom_html_input_element_get_value((dom_html_input_element *)copy, &actual), DOM_NO_ERR);
    ck_assert(dom_string_isequal(actual, value));
    dom_string_unref(actual);
    dom_node_unref(copy_attr);
    dom_node_unref(copy);
    dom_string_unref(key);
    dom_string_unref(value);
    dom_string_unref(type);
}
END_TEST

START_TEST(test_attached_attribute_adoption)
{
    dom_document *src = document(true), *dst = document(false);
    dom_node *node = element(src, "div"), *result = NULL;
    dom_string *key = string("title"), *value = string("retained");
    dom_attr *attr = NULL;
    dom_element *element_owner = NULL;
    ck_assert_int_eq(dom_element_set_attribute(node, key, value), DOM_NO_ERR);
    ck_assert_int_eq(dom_element_get_attribute_node(node, key, &attr), DOM_NO_ERR);
    ck_assert_int_eq(dom_document_adopt_node(dst, attr, &result), DOM_NO_ERR);
    ck_assert_ptr_eq(result, attr);
    dom_node_unref(result);
    /* ownerElement is not parentNode in the modern DOM adoption algorithm. */
    ck_assert_int_eq(dom_attr_get_owner_element(attr, &element_owner), DOM_NO_ERR);
    ck_assert_ptr_eq(element_owner, node);
    dom_node_unref(element_owner);
    owner((dom_node *)attr, dst);
    owner(node, src);
    ck_assert_int_eq(dom_document_adopt_node(src, node, &result), DOM_NO_ERR);
    dom_node_unref(result);
    owner((dom_node *)attr, dst);
    dom_node_unref(dst);
    if (_i == 0) {
        ck_assert_int_eq(dom_element_remove_attribute_node(node, attr, (dom_attr **)&result), DOM_NO_ERR);
        ck_assert_ptr_eq(result, attr);
        dom_node_unref(result);
    } else if (_i == 1) {
        ck_assert_int_eq(dom_document_adopt_node(dst, node, &result), DOM_NO_ERR);
        dom_node_unref(result);
        owner(node, dst);
    } else {
        ck_assert_int_eq(dom_document_adopt_node(src, attr, &result), DOM_NO_ERR);
        dom_node_unref(result);
        owner((dom_node *)attr, src);
    }
    dom_node_unref(src);
    dom_node_unref(node);
    ck_assert_int_eq(dom_attr_set_value(attr, key), DOM_NO_ERR);
    dom_node_unref(attr);
    dom_string_unref(key);
    dom_string_unref(value);
}
END_TEST

START_TEST(test_leaf_fragment_and_doctype)
{
    dom_document *src = document(false), *dst = document(true);
    dom_node *node = NULL, *copy = NULL, *result = NULL;
    dom_string *value = string("data"), *target = string("target");
    switch (_i) {
    case 0:
        ck_assert_int_eq(dom_document_create_text_node(src, value, &node), DOM_NO_ERR);
        break;
    case 1:
        ck_assert_int_eq(dom_document_create_comment(src, value, &node), DOM_NO_ERR);
        break;
    case 2:
        ck_assert_int_eq(dom_document_create_cdata_section(src, value, &node), DOM_NO_ERR);
        break;
    case 3:
        ck_assert_int_eq(dom_document_create_processing_instruction(src, target, value, &node), DOM_NO_ERR);
        break;
    case 4:
        ck_assert_int_eq(dom_document_create_document_fragment(src, &node), DOM_NO_ERR);
        break;
    case 5:
        ck_assert_int_eq(
            dom_implementation_create_document_type("html", "public", "system", (dom_document_type **)&node),
            DOM_NO_ERR);
        break;
    }
    ck_assert_ptr_nonnull(node);
    ck_assert_int_eq(dom_document_import_node(dst, node, false, &copy), DOM_NO_ERR);
    ck_assert_ptr_ne(copy, node);
    owner(copy, dst);
    ck_assert_int_eq(dom_document_adopt_node(dst, node, &result), DOM_NO_ERR);
    ck_assert_ptr_eq(result, node);
    dom_node_unref(result);
    owner(node, dst);
    if (_i == 5) {
        dom_string *id = NULL;
        ck_assert_int_eq(dom_document_type_get_public_id(copy, &id), DOM_NO_ERR);
        ck_assert_str_eq(dom_string_data(id), "public");
        dom_string_unref(id);
        append((dom_node *)dst, node);
    }
    dom_node_unref(src);
    dom_node_unref(dst);
    dom_node_unref(copy);
    dom_node_unref(node);
    dom_string_unref(value);
    dom_string_unref(target);
}
END_TEST

START_TEST(test_same_document_and_rejected_document)
{
    dom_document *doc = document(true);
    dom_node *parent = element(doc, "div"), *child = element(doc, "span"), *result = NULL;
    append(parent, child);
    ck_assert_int_eq(dom_document_adopt_node(doc, child, &result), DOM_NO_ERR);
    ck_assert_ptr_eq(result, child);
    dom_node_unref(result);
    ck_assert_int_eq(dom_node_get_first_child(parent, &result), DOM_NO_ERR);
    ck_assert_ptr_null(result);
    ck_assert_int_eq(dom_document_adopt_node(doc, doc, &result), DOM_NOT_SUPPORTED_ERR);
    ck_assert_ptr_null(result);
    ck_assert_int_eq(dom_document_import_node(doc, doc, true, &result), DOM_NOT_SUPPORTED_ERR);
    ck_assert_ptr_null(result);
    dom_node_unref(parent);
    dom_node_unref(child);
    dom_node_unref(doc);
}
END_TEST

START_TEST(test_namespace_and_live_queries)
{
    dom_document *src = document(false), *dst = document(true);
    dom_node *root = NULL, *input = NULL, *result = NULL;
    dom_string *ns = string("http://www.w3.org/1999/xhtml"), *qualified = string("h:div");
    dom_string *lower = string("input"), *upper = string("INPUT"), *value = string("namespaced");
    dom_nodelist *before = NULL, *after = NULL;
    ck_assert_int_eq(dom_document_create_element_ns(src, ns, qualified, &root), DOM_NO_ERR);
    ck_assert_int_eq(dom_document_create_element_ns(src, ns, lower, &input), DOM_NO_ERR);
    ck_assert_int_eq(dom_html_input_element_set_value((dom_html_input_element *)input, value), DOM_NO_ERR);
    append(root, input);
    name_is(root, "h:div");
    name_is(input, "input");
    ck_assert_int_eq(dom_element_get_elements_by_tag_name(root, upper, &before), DOM_NO_ERR);
    uint32_t count;
    ck_assert_int_eq(dom_nodelist_get_length(before, &count), DOM_NO_ERR);
    ck_assert_uint_eq(count, 0);
    ck_assert_int_eq(dom_document_adopt_node(dst, root, &result), DOM_NO_ERR);
    dom_node_unref(result);
    name_is(root, "H:DIV");
    name_is(input, "INPUT");
    ck_assert_int_eq(dom_element_get_elements_by_tag_name(root, upper, &after), DOM_NO_ERR);
    ck_assert_int_eq(dom_nodelist_get_length(after, &count), DOM_NO_ERR);
    ck_assert_uint_eq(count, 1);
    /* The filter is selected when the collection is created. The DOM Standard
     * permits caching only while the root's node-document type is unchanged. */
    ck_assert_int_eq(dom_nodelist_get_length(before, &count), DOM_NO_ERR);
    ck_assert_uint_eq(count, 0);
    dom_node_unref(src);
    dom_node_unref(dst);
    dom_nodelist_unref(before);
    ck_assert_int_eq(dom_nodelist_item(after, 0, &result), DOM_NO_ERR);
    ck_assert_ptr_eq(result, input);
    dom_node_unref(result);
    dom_nodelist_unref(after);
    dom_string *actual = NULL;
    ck_assert_int_eq(dom_html_input_element_get_value((dom_html_input_element *)input, &actual), DOM_NO_ERR);
    ck_assert(dom_string_isequal(actual, value));
    dom_string_unref(actual);
    dom_node_unref(root);
    dom_node_unref(input);
    dom_string_unref(ns);
    dom_string_unref(qualified);
    dom_string_unref(lower);
    dom_string_unref(upper);
    dom_string_unref(value);
}
END_TEST

/* Table construction APIs create HTML-namespace children independently of
 * document type: https://html.spec.whatwg.org/multipage/tables.html */
START_TEST(test_adopted_table_keeps_typed_children)
{
    dom_document *src = document(true), *dst = document(_i != 0);
    dom_node *table = element(src, "table"), *adopted = NULL;
    dom_html_collection *rows = NULL;
    dom_html_element *row = NULL, *cell = NULL, *caption = NULL;
    ck_assert_int_eq(dom_html_table_element_get_rows((dom_html_table_element *)table, &rows), DOM_NO_ERR);
    ck_assert_int_eq(dom_document_adopt_node(dst, table, &adopted), DOM_NO_ERR);
    ck_assert_ptr_eq(adopted, table);
    dom_node_unref(adopted);
    dom_node_unref(src);
    ck_assert_int_eq(dom_html_table_element_create_caption((dom_html_table_element *)table, &caption), DOM_NO_ERR);
    ck_assert_int_eq(dom_html_table_element_insert_row((dom_html_table_element *)table, -1, &row), DOM_NO_ERR);
    ck_assert_int_eq(dom_html_table_row_element_insert_cell((dom_html_table_row_element *)row, -1, &cell), DOM_NO_ERR);
    uint32_t count;
    ck_assert_int_eq(dom_html_collection_get_length(rows, &count), DOM_NO_ERR);
    ck_assert_uint_eq(count, 1);
    dom_html_collection_unref(rows);
    name_is((dom_node *)row, _i ? "TR" : "tr");
    name_is((dom_node *)cell, _i ? "TD" : "td");
    dom_string *ns = NULL, *html = string("http://www.w3.org/1999/xhtml");
    ck_assert_int_eq(dom_node_get_namespace(cell, &ns), DOM_NO_ERR);
    ck_assert(dom_string_isequal(ns, html));
    dom_string_unref(ns);
    dom_string_unref(html);
    ck_assert_int_eq(dom_html_table_cell_element_set_col_span((dom_html_table_cell_element *)cell, 2), DOM_NO_ERR);
    dom_ulong span;
    ck_assert_int_eq(dom_html_table_cell_element_get_col_span((dom_html_table_cell_element *)cell, &span), DOM_NO_ERR);
    ck_assert_uint_eq(span, 2);
    owner((dom_node *)caption, dst);
    owner((dom_node *)row, dst);
    owner((dom_node *)cell, dst);
    dom_node_unref(caption);
    dom_node_unref(row);
    dom_node_unref(cell);
    dom_node_unref(table);
    dom_node_unref(dst);
}
END_TEST

#ifdef WISP_TEST_ALLOC_FAILURE
START_TEST(test_import_allocation_failures)
{
    dom_document *src = document(true), *dst = document(true);
    dom_node *node = element(src, "div"), *child = element(src, "input"), *copy = NULL;
    dom_string *key = string("class"), *value = string("first second");
    ck_assert_int_eq(dom_element_set_attribute(node, key, value), DOM_NO_ERR);
    ck_assert_int_eq(dom_element_set_attribute(child, key, value), DOM_NO_ERR);
    append(node, child);
    unsigned budget;
    for (budget = 0; budget < 128; budget++) {
        malloc_limit(budget);
        dom_exception err = dom_document_import_node(dst, node, true, &copy);
        malloc_limit(UINT_MAX);
        if (err == DOM_NO_ERR)
            break;
        ck_assert_int_eq(err, DOM_NO_MEM_ERR);
        ck_assert_ptr_null(copy);
        owner(node, src);
        owner(child, src);
    }
    ck_assert_uint_gt(budget, 0); /* Ensure allocation injection actually ran. */
    ck_assert_msg(budget < 128, "Import never completed during allocation-failure sweep");
    ck_assert_ptr_nonnull(copy);
    owner(copy, dst);
    dom_node_unref(copy);
    dom_node_unref(node);
    dom_node_unref(child);
    dom_node_unref(src);
    dom_node_unref(dst);
    dom_string_unref(key);
    dom_string_unref(value);
}
END_TEST

START_TEST(test_adopt_prepares_before_detaching)
{
    dom_document *src = document(true), *dst = document(false);
    dom_node *parent = element(src, "div"), *input = element(src, "input"), *result = NULL;
    append(parent, input);
    calloc_limit(_i);
    dom_exception err = dom_document_adopt_node(dst, input, &result);
    calloc_limit(UINT_MAX);
    ck_assert_int_eq(err, DOM_NO_MEM_ERR);
    ck_assert_ptr_null(result);
    owner(input, src);
    ck_assert_int_eq(dom_node_get_parent_node(input, &result), DOM_NO_ERR);
    ck_assert_ptr_eq(result, parent);
    dom_node_unref(result);
    /* Failure leaves a retryable destination and the original tree intact. */
    ck_assert_int_eq(dom_document_adopt_node(dst, input, &result), DOM_NO_ERR);
    ck_assert_ptr_eq(result, input);
    dom_node_unref(result);
    owner(input, dst);
    dom_node_unref(parent);
    dom_node_unref(input);
    dom_node_unref(src);
    dom_node_unref(dst);
}
END_TEST
#endif

int main(void)
{
    Suite *suite = suite_create("DOM adoption and import");
    TCase *tc = tcase_create("Identity and document lifetime");
    tcase_add_loop_test(tc, test_adopt_preserves_identity_and_state, 0, 4);
    tcase_add_loop_test(tc, test_import_and_clone, 0, 8);
    tcase_add_loop_test(tc, test_attached_attribute_adoption, 0, 3);
    tcase_add_loop_test(tc, test_leaf_fragment_and_doctype, 0, 6);
    tcase_add_test(tc, test_same_document_and_rejected_document);
    tcase_add_test(tc, test_namespace_and_live_queries);
    tcase_add_loop_test(tc, test_adopted_table_keeps_typed_children, 0, 2);
#ifdef WISP_TEST_ALLOC_FAILURE
    tcase_add_test(tc, test_import_allocation_failures);
    tcase_add_loop_test(tc, test_adopt_prepares_before_detaching, 0, 2);
#endif
    suite_add_tcase(suite, tc);
    SRunner *runner = srunner_create(suite);
    srunner_run_all(runner, CK_NORMAL);
    int failed = srunner_ntests_failed(runner);
    srunner_free(runner);
    return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

/* DOM/HTML cloning steps and failure cleanup, without JS wrapper assumptions.
 * https://dom.spec.whatwg.org/#concept-node-clone
 * https://html.spec.whatwg.org/#script-processing-model */
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
    dom_node_unref(result);
}

static void owner(dom_node *node, dom_document *expected)
{
    dom_document *doc = NULL;
    ck_assert_int_eq(dom_node_get_owner_document(node, &doc), DOM_NO_ERR);
    ck_assert_ptr_eq(doc, expected);
    dom_node_unref(doc);
}

START_TEST(test_document_clone)
{
    bool html = (_i & 1) != 0, deep = (_i & 2) != 0;
    dom_document *source = document(html);
    dom_node *root = element(source, "html"), *child = element(source, "span"), *copy = NULL, *result = NULL;
    dom_string *uri = string("https://example.test/source"), *value = NULL;
    append((dom_node *)source, root);
    append(root, child);
    ck_assert_int_eq(dom_document_set_uri(source, uri), DOM_NO_ERR);
    ck_assert_int_eq(dom_document_set_quirks_mode(source, DOM_DOCUMENT_QUIRKS_MODE_FULL), DOM_NO_ERR);
    ck_assert_int_eq(dom_node_clone_node(source, deep, &copy), DOM_NO_ERR);
    ck_assert_ptr_ne(copy, source);
    ck_assert_int_eq(dom_document_is_html((dom_document *)copy), html);
    owner(copy, NULL);
    ck_assert_int_eq(dom_document_get_uri(copy, &value), DOM_NO_ERR);
    ck_assert(dom_string_isequal(uri, value));
    dom_string_unref(value);
    dom_document_quirks_mode quirks;
    ck_assert_int_eq(dom_document_get_quirks_mode(copy, &quirks), DOM_NO_ERR);
    ck_assert_int_eq(quirks, DOM_DOCUMENT_QUIRKS_MODE_FULL);
    ck_assert_int_eq(dom_document_get_document_element(copy, &result), DOM_NO_ERR);
    if (deep) {
        ck_assert_ptr_nonnull(result);
        ck_assert_ptr_ne(result, root);
        owner(result, (dom_document *)copy);
        dom_node *descendant = NULL;
        ck_assert_int_eq(dom_node_get_first_child(result, &descendant), DOM_NO_ERR);
        owner(descendant, (dom_document *)copy);
        dom_node_unref(descendant);
    } else {
        ck_assert_ptr_null(result);
    }
    dom_node_unref(result);
    /* Releasing the source must not invalidate the independent clone. */
    dom_node_unref(child);
    dom_node_unref(root);
    dom_node_unref(source);
    dom_node *new_child = element((dom_document *)copy, "div");
    owner(new_child, (dom_document *)copy);
    dom_node_unref(new_child);
    dom_node_unref(copy);
    dom_string_unref(uri);
}
END_TEST

START_TEST(test_script_clone_flags)
{
    dom_document *source = document(true), *destination = document(true);
    dom_node *script = element(source, "script"), *copy = NULL;
    bool started = (_i & 1) != 0, importing = (_i & 2) != 0;
    dom_html_script_element_flags flags = DOM_HTML_SCRIPT_ELEMENT_FLAG_PARSER_INSERTED |
        DOM_HTML_SCRIPT_ELEMENT_FLAG_READY_TO_BE_PARSER_EXECUTED | DOM_HTML_SCRIPT_ELEMENT_FLAG_FROM_EXTERNAL;
    if (started)
        flags |= DOM_HTML_SCRIPT_ELEMENT_FLAG_ALREADY_STARTED;
    ck_assert_int_eq(dom_html_script_element_set_flags((dom_html_script_element *)script, flags), DOM_NO_ERR);
    ck_assert_int_eq(importing ? dom_document_import_node(destination, script, true, &copy)
                               : dom_node_clone_node(script, true, &copy),
        DOM_NO_ERR);
    ck_assert_int_eq(dom_html_script_element_get_flags((dom_html_script_element *)copy, &flags), DOM_NO_ERR);
    ck_assert_uint_eq(flags,
        DOM_HTML_SCRIPT_ELEMENT_FLAG_NON_BLOCKING | (started ? DOM_HTML_SCRIPT_ELEMENT_FLAG_ALREADY_STARTED : 0));
    dom_node_unref(script);
    dom_node_unref(source);
    dom_node_unref(copy);
    dom_node_unref(destination);
}
END_TEST

/* Inserting nodes from a clone adopts them after validation, including all
 * fragment children and template contents, without changing wrapper identity. */
START_TEST(test_cross_document_insertion)
{
    dom_document *src = document(true), *dst = document((_i & 1) != 0);
    dom_node *root = element(dst, "root"), *old = element(dst, "old");
    dom_node *host = element(src, "template"), *leaf = element(src, "input"), *result = NULL;
    dom_document_fragment *contents = NULL, *fragment = NULL;
    ck_assert_int_eq(dom_html_template_element_get_content((dom_html_template_element *)host, &contents), DOM_NO_ERR);
    append((dom_node *)contents, leaf);
    append(root, old);
    dom_node *incoming = host;
    if (_i & 2) {
        ck_assert_int_eq(dom_document_create_document_fragment(src, &fragment), DOM_NO_ERR);
        append((dom_node *)fragment, host);
        incoming = (dom_node *)fragment;
    }
    if (_i & 4)
        ck_assert_int_eq(dom_node_replace_child(root, incoming, old, &result), DOM_NO_ERR);
    else
        ck_assert_int_eq(dom_node_insert_before(root, incoming, old, &result), DOM_NO_ERR);
    dom_node_unref(result);
    owner(host, dst);
    if (fragment != NULL)
        owner((dom_node *)fragment, dst);
    dom_document *inert = NULL;
    ck_assert_int_eq(dom_node_get_owner_document(contents, &inert), DOM_NO_ERR);
    ck_assert_ptr_ne(inert, src);
    owner(leaf, inert);
    dom_node_unref(inert);
    ck_assert_int_eq(dom_node_get_first_child(root, &result), DOM_NO_ERR);
    ck_assert_ptr_eq(result, host);
    dom_node_unref(result);
    dom_node_unref(src);
    ck_assert_int_eq(dom_node_clone_node(root, true, &result), DOM_NO_ERR);
    dom_node_unref(result);
    dom_node_unref(root);
    dom_node_unref(old);
    dom_node_unref(host);
    dom_node_unref(leaf);
    dom_node_unref(contents);
    dom_node_unref(fragment);
    dom_node_unref(dst);
}
END_TEST

START_TEST(test_invalid_insertion_does_not_adopt)
{
    dom_document *src = document(true), *dst = document(true);
    dom_node *root = element(dst, "html"), *host = element(src, "html"), *result = NULL;
    append((dom_node *)src, host);
    append((dom_node *)dst, root);
    ck_assert_int_eq(dom_node_append_child(dst, host, &result), DOM_HIERARCHY_REQUEST_ERR);
    ck_assert_ptr_null(result);
    owner(host, src);
    ck_assert_int_eq(dom_node_get_parent_node(host, &result), DOM_NO_ERR);
    ck_assert_ptr_eq(result, src);
    dom_node_unref(result);
    dom_node_unref(root);
    dom_node_unref(host);
    dom_node_unref(src);
    dom_node_unref(dst);
}
END_TEST

struct notification_clone {
    dom_node *node;
    dom_exception result;
    unsigned calls;
};

static void clone_during_notification(dom_event *event, void *pw)
{
    (void)event;
    struct notification_clone *state = pw;
    dom_node *copy = NULL;
    state->result = dom_node_clone_node(state->node, true, &copy);
    state->calls++;
    dom_node_unref(copy);
}

START_TEST(test_clone_is_not_a_source_mutation)
{
    dom_document *doc = document(true);
    dom_node *root = element(doc, "div"), *child = element(doc, "span");
    dom_string *event = string("DOMNodeInserted");
    dom_event_listener *listener = NULL;
    struct notification_clone state = {.node = root};
    ck_assert_int_eq(dom_event_listener_create(clone_during_notification, &state, &listener), DOM_NO_ERR);
    ck_assert_int_eq(dom_event_target_add_event_listener(root, event, listener, false), DOM_NO_ERR);
    append(root, child);
    ck_assert_uint_eq(state.calls, 1);
    ck_assert_int_eq(state.result, DOM_NO_ERR);
    dom_event_listener_unref(listener);
    dom_string_unref(event);
    dom_node_unref(child);
    dom_node_unref(root);
    dom_node_unref(doc);
}
END_TEST

#ifdef WISP_TEST_ALLOC_FAILURE
START_TEST(test_insertion_adoption_failure)
{
    dom_document *src = document(true), *dst = document(false);
    dom_node *holder = element(src, "div"), *child = element(src, "template");
    dom_node *root = element(dst, "root"), *old = element(dst, "old"), *result = NULL;
    append(holder, child);
    append(root, old);
    calloc_limit(0); /* Destination HTML name tables must be prepared first. */
    dom_exception err = _i == 0 ? dom_node_append_child(root, child, &result)
        : _i == 1               ? dom_node_replace_child(root, child, old, &result)
                                : dom_node_replace_all(root, child);
    calloc_limit(UINT_MAX);
    ck_assert_int_eq(err, DOM_NO_MEM_ERR);
    ck_assert_ptr_null(result);
    owner(child, src);
    ck_assert_int_eq(dom_node_get_parent_node(child, &result), DOM_NO_ERR);
    ck_assert_ptr_eq(result, holder);
    dom_node_unref(result);
    ck_assert_int_eq(dom_node_get_first_child(root, &result), DOM_NO_ERR);
    ck_assert_ptr_eq(result, old);
    dom_node_unref(result);
    append(root, child); /* Retry is valid after allocation recovers. */
    owner(child, dst);
    dom_node_unref(holder);
    dom_node_unref(child);
    dom_node_unref(root);
    dom_node_unref(old);
    dom_node_unref(src);
    dom_node_unref(dst);
}
END_TEST

START_TEST(test_document_clone_allocation_failures)
{
    dom_document *doc = document(true);
    dom_node *root = element(doc, "html"), *script = element(doc, "script"), *copy = NULL;
    dom_string *key = string("id"), *value = string("probe");
    append((dom_node *)doc, root);
    append(root, script);
    ck_assert_int_eq(dom_element_set_attribute(script, key, value), DOM_NO_ERR);
    unsigned budget;
    for (budget = 0; budget < 512; budget++) {
        if (_i == 0)
            malloc_limit(budget);
        else
            calloc_limit(budget);
        dom_exception err = dom_node_clone_node(doc, true, &copy);
        malloc_limit(UINT_MAX);
        calloc_limit(UINT_MAX);
        if (err == DOM_NO_ERR)
            break;
        ck_assert_int_eq(err, DOM_NO_MEM_ERR);
        ck_assert_ptr_null(copy);
        dom_node *found = NULL;
        ck_assert_int_eq(dom_document_get_element_by_id(doc, value, &found), DOM_NO_ERR);
        ck_assert_ptr_eq(found, script);
        dom_node_unref(found);
    }
    ck_assert_uint_gt(budget, 0);
    ck_assert_uint_lt(budget, 512);
    dom_node_unref(copy);
    dom_node_unref(script);
    dom_node_unref(root);
    dom_node_unref(doc);
    dom_string_unref(key);
    dom_string_unref(value);
}
END_TEST
#endif

int main(void)
{
    Suite *suite = suite_create("DOM cloning");
    TCase *tc = tcase_create("Document and element cloning steps");
    tcase_add_loop_test(tc, test_document_clone, 0, 4);
    tcase_add_loop_test(tc, test_script_clone_flags, 0, 4);
    tcase_add_loop_test(tc, test_cross_document_insertion, 0, 8);
    tcase_add_test(tc, test_invalid_insertion_does_not_adopt);
    tcase_add_test(tc, test_clone_is_not_a_source_mutation);
#ifdef WISP_TEST_ALLOC_FAILURE
    tcase_add_loop_test(tc, test_document_clone_allocation_failures, 0, 2);
    tcase_add_loop_test(tc, test_insertion_adoption_failure, 0, 3);
#endif
    suite_add_tcase(suite, tc);
    SRunner *runner = srunner_create(suite);
    srunner_run_all(runner, CK_NORMAL);
    int failed = srunner_ntests_failed(runner);
    srunner_free(runner);
    return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

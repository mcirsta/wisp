/* Template ownership, adoption and cloning follow HTML, independently of parsing.
 * https://html.spec.whatwg.org/multipage/scripting.html#the-template-element
 * https://dom.spec.whatwg.org/#concept-tree-host-including-inclusive-ancestor */
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
    dom_string *s = string(name), *ns = string("http://www.w3.org/1999/xhtml");
    ck_assert_int_eq(dom_document_create_element_ns(doc, ns, s, &node), DOM_NO_ERR);
    dom_string_unref(s);
    dom_string_unref(ns);
    return node;
}

static dom_node *contents(dom_node *node)
{
    dom_document_fragment *fragment = NULL;
    ck_assert_int_eq(dom_html_template_element_get_content((dom_html_template_element *)node, &fragment), DOM_NO_ERR);
    ck_assert_ptr_nonnull(fragment);
    return (dom_node *)fragment;
}

static dom_document *owner(dom_node *node)
{
    dom_document *doc = NULL;
    ck_assert_int_eq(dom_node_get_owner_document(node, &doc), DOM_NO_ERR);
    return doc;
}

static void has_owner(dom_node *node, dom_document *expected)
{
    dom_document *actual = owner(node);
    ck_assert_ptr_eq(actual, expected);
    dom_node_unref(actual);
}

static void append(dom_node *parent, dom_node *child)
{
    dom_node *result = NULL;
    ck_assert_int_eq(dom_node_append_child(parent, child, &result), DOM_NO_ERR);
    ck_assert_ptr_eq(result, child);
    dom_node_unref(result);
}

static dom_node *first(dom_node *parent)
{
    dom_node *child = NULL;
    ck_assert_int_eq(dom_node_get_first_child(parent, &child), DOM_NO_ERR);
    return child;
}

static void empty(dom_node *node)
{
    ck_assert_ptr_null(first(node));
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

START_TEST(test_inert_owner_and_nested_templates)
{
    dom_document *doc = document(_i != 0);
    dom_node *a = element(doc, "template"), *b = element(doc, "template");
    dom_node *ac = contents(a), *bc = contents(b), *same = contents(a);
    ck_assert_ptr_eq(ac, same);
    dom_node_unref(same);
    dom_document *inert = owner(ac);
    ck_assert_ptr_ne(inert, doc);
    has_owner(bc, inert);
    dom_node *nested = element(inert, "template"), *nc = contents(nested);
    has_owner(nc, inert);
    append(ac, nested);
    /* A normal append to the element does not enter its content fragment. */
    dom_node *ordinary = element(doc, "span"), *hidden = element(inert, "b");
    append(a, ordinary);
    append(nc, hidden);
    same = first(a);
    ck_assert_ptr_eq(same, ordinary);
    dom_node_unref(same);
    name_is(hidden, _i ? "B" : "b");
    dom_string *star = string("*");
    dom_nodelist *list = NULL;
    ck_assert_int_eq(dom_element_get_elements_by_tag_name(a, star, &list), DOM_NO_ERR);
    uint32_t length;
    ck_assert_int_eq(dom_nodelist_get_length(list, &length), DOM_NO_ERR);
    ck_assert_uint_eq(length, 1);
    dom_nodelist_unref(list);
    dom_string_unref(star);
    dom_node_unref(ordinary);
    dom_node_unref(hidden);
    dom_node_unref(nested);
    dom_node_unref(nc);
    dom_node_unref(a);
    dom_node_unref(b);
    dom_node_unref(doc);
    /* Retained contents keep their document and children alive independently. */
    has_owner(ac, inert);
    same = first(ac);
    ck_assert_ptr_eq(same, nested);
    dom_node_unref(same);
    dom_node_unref(inert);
    dom_node_unref(ac);
    dom_node_unref(bc);
}
END_TEST

static void count_event(dom_event *event, void *pw)
{
    (void)event;
    (*(unsigned *)pw)++;
}

START_TEST(test_host_is_only_used_for_cycle_checks)
{
    dom_document *doc = document(true);
    dom_node *outer = element(doc, "template"), *content = contents(outer);
    dom_document *inert = owner(content);
    dom_node *inner = element(inert, "template"), *ic = contents(inner), *leaf = element(inert, "div");
    append(content, inner);
    append(ic, leaf);
    dom_node *result = NULL;
    ck_assert_int_eq(dom_node_get_parent_node(content, &result), DOM_NO_ERR);
    ck_assert_ptr_null(result);
    /* The ordinary ancestor and nested host cases, including fragments. */
    ck_assert_int_eq(dom_node_append_child(content, outer, &result), DOM_HIERARCHY_REQUEST_ERR);
    ck_assert_int_eq(dom_node_append_child(leaf, outer, &result), DOM_HIERARCHY_REQUEST_ERR);
    ck_assert_int_eq(dom_node_append_child(leaf, inner, &result), DOM_HIERARCHY_REQUEST_ERR);
    ck_assert_int_eq(dom_node_append_child(leaf, content, &result), DOM_HIERARCHY_REQUEST_ERR);
    ck_assert_int_eq(dom_node_replace_child(ic, outer, leaf, &result), DOM_HIERARCHY_REQUEST_ERR);
    unsigned calls = 0;
    dom_event_listener *listener;
    dom_string *type = string("probe");
    ck_assert_int_eq(dom_event_listener_create(count_event, &calls, &listener), DOM_NO_ERR);
    ck_assert_int_eq(dom_event_target_add_event_listener(outer, type, listener, false), DOM_NO_ERR);
    ck_assert_int_eq(dom_event_target_add_event_listener(inner, type, listener, false), DOM_NO_ERR);
    ck_assert_int_eq(dom_event_target_add_event_listener(ic, type, listener, false), DOM_NO_ERR);
    dom_event_listener_unref(listener);
    dom_event *event;
    bool success;
    ck_assert_int_eq(dom_event_create(&event), DOM_NO_ERR);
    ck_assert_int_eq(dom_event_init(event, type, true, false), DOM_NO_ERR);
    ck_assert_int_eq(dom_event_target_dispatch_event(leaf, event, &success), DOM_NO_ERR);
    ck_assert_uint_eq(calls, 1); /* Bubbles to contents, never its template host. */
    dom_event_unref(event);
    dom_string_unref(type);
    dom_node_unref(inner);
    dom_node_unref(leaf);
    dom_node_unref(ic);
    dom_node_unref(inert);
    dom_node_unref(outer);
    dom_node_unref(doc);
    /* Clearing the host on destruction avoids dangling ancestry pointers. */
    dom_node *copy = NULL;
    ck_assert_int_eq(dom_node_clone_node(content, true, &copy), DOM_NO_ERR);
    dom_node_unref(copy);
    dom_node_unref(content);
}
END_TEST

START_TEST(test_template_adoption)
{
    dom_document *src = document(true), *dst = document((_i & 1) != 0);
    dom_node *parent = element(src, "div"), *a = element(src, "template"), *ac = contents(a);
    dom_document *old_inert = owner(ac);
    dom_node *inner = element(old_inert, "template"), *ic = contents(inner), *input = element(old_inert, "input");
    dom_node *ordinary = element(src, "span");
    append(parent, a);
    append(a, ordinary);
    append(ac, inner);
    append(ic, input);
    ck_assert_int_eq(dom_html_input_element_set_checked((dom_html_input_element *)input, true), DOM_NO_ERR);
    dom_node *result = NULL;
    dom_node *root = (_i & 2) ? parent : a;
    ck_assert_int_eq(dom_document_adopt_node(dst, root, &result), DOM_NO_ERR);
    ck_assert_ptr_eq(result, root);
    dom_node_unref(result);
    has_owner(a, dst);
    has_owner(ordinary, dst);
    dom_document *new_inert = owner(ac);
    ck_assert_ptr_ne(new_inert, old_inert);
    ck_assert_ptr_ne(new_inert, dst);
    has_owner(inner, new_inert);
    has_owner(ic, new_inert);
    has_owner(input, new_inert);
    result = contents(a);
    ck_assert_ptr_eq(result, ac);
    dom_node_unref(result);
    result = contents(inner);
    ck_assert_ptr_eq(result, ic);
    dom_node_unref(result);
    dom_node_unref(src);
    dom_node_unref(old_inert);
    name_is(input, (_i & 1) ? "INPUT" : "input");
    bool checked = false;
    ck_assert_int_eq(dom_html_input_element_get_checked((dom_html_input_element *)input, &checked), DOM_NO_ERR);
    ck_assert(checked);
    dom_node_unref(dst);
    dom_node_unref(new_inert);
    dom_node_unref(ordinary);
    dom_node_unref(parent);
    dom_node_unref(a);
    dom_node_unref(inner);
    dom_node_unref(input);
    dom_node_unref(ic);
    dom_node_unref(ac);
}
END_TEST

START_TEST(test_clone_and_import_templates)
{
    bool deep = (_i & 1) != 0, clone = (_i & 2) != 0;
    dom_document *src = document(true), *dst = document((_i & 4) != 0);
    dom_node *a = element(src, "template"), *ac = contents(a);
    dom_document *inert = owner(ac);
    dom_node *inner = element(inert, "template"), *ic = contents(inner), *input = element(inert, "input");
    dom_node *ordinary = element(src, "span"), *copy = NULL;
    append(a, ordinary);
    append(ac, inner);
    append(ic, input);
    ck_assert_int_eq(dom_html_input_element_set_checked((dom_html_input_element *)input, true), DOM_NO_ERR);
    if (clone)
        ck_assert_int_eq(dom_node_clone_node(a, deep, &copy), DOM_NO_ERR);
    else
        ck_assert_int_eq(dom_document_import_node(dst, a, deep, &copy), DOM_NO_ERR);
    ck_assert_ptr_ne(copy, a);
    has_owner(copy, clone ? src : dst);
    dom_node *cc = contents(copy);
    ck_assert_ptr_ne(cc, ac);
    dom_document *copy_inert = owner(cc);
    if (clone)
        ck_assert_ptr_eq(copy_inert, inert);
    else
        ck_assert_ptr_ne(copy_inert, inert);
    if (!deep) {
        empty(cc);
        empty(copy);
    } else {
        dom_node *ci = first(cc), *cic = contents(ci), *input_copy = first(cic), *ordinary_copy = first(copy);
        has_owner(ci, copy_inert);
        has_owner(input_copy, copy_inert);
        has_owner(ordinary_copy, clone ? src : dst);
        ck_assert_ptr_ne(ci, inner);
        ck_assert_ptr_ne(input_copy, input);
        bool checked = false;
        ck_assert_int_eq(
            dom_html_input_element_get_checked((dom_html_input_element *)input_copy, &checked), DOM_NO_ERR);
        ck_assert(checked);
        /* Copy establishes its own host, independently of the source. */
        dom_node *result = NULL;
        ck_assert_int_eq(dom_node_append_child(cic, copy, &result), DOM_HIERARCHY_REQUEST_ERR);
        dom_node_unref(ci);
        dom_node_unref(cic);
        dom_node_unref(input_copy);
        dom_node_unref(ordinary_copy);
    }
    dom_node_unref(src);
    dom_node_unref(dst);
    dom_node_unref(inert);
    dom_node_unref(copy_inert);
    dom_node_unref(ordinary);
    dom_node_unref(input);
    dom_node_unref(inner);
    dom_node_unref(a);
    dom_node_unref(ac);
    dom_node_unref(ic);
    dom_node_unref(cc);
    dom_node_unref(copy);
}
END_TEST

START_TEST(test_contents_independently_adopted_or_cloned)
{
    dom_document *src = document(true), *dst = document(false);
    dom_node *a = element(src, "template"), *ac = contents(a), *result = NULL;
    /* Contents are a normal DocumentFragment: public adoption moves them,
     * whereas cloning them produces a fragment without a template host. */
    ck_assert_int_eq(dom_document_adopt_node(dst, ac, &result), DOM_NO_ERR);
    ck_assert_ptr_eq(result, ac);
    dom_node_unref(result);
    has_owner(ac, dst);
    ck_assert_int_eq(dom_document_adopt_node(src, a, &result), DOM_NO_ERR);
    dom_node_unref(result);
    has_owner(ac, dst); /* No adopting steps for a same-document adoption. */
    dom_node *copy = NULL;
    ck_assert_int_eq(dom_node_clone_node(ac, true, &copy), DOM_NO_ERR);
    ck_assert_int_eq(dom_document_adopt_node(dst, a, &result), DOM_NO_ERR);
    dom_node_unref(result);
    append(copy, a); /* A copied fragment must not retain the source host. */
    dom_node_unref(a);
    dom_node_unref(ac);
    dom_node_unref(copy);
    dom_node_unref(src);
    dom_node_unref(dst);
}
END_TEST

START_TEST(test_only_html_lowercase_template_is_special)
{
    static const char *names[] = {"template", "TEMPLATE", "t:template", "template"};
    static const char *namespaces[] = {
        "urn:foreign", "http://www.w3.org/1999/xhtml", "http://www.w3.org/1999/xhtml", NULL};
    dom_document *doc = document(true);
    dom_node *node = NULL;
    dom_string *name = string(names[_i]), *ns = namespaces[_i] ? string(namespaces[_i]) : NULL;
    ck_assert_int_eq(dom_document_create_element_ns(doc, ns, name, &node), DOM_NO_ERR);
    dom_document_fragment *fragment = NULL;
    ck_assert_int_eq(dom_html_template_element_get_content((dom_html_template_element *)node, &fragment),
        _i == 2 ? DOM_NO_ERR : DOM_NOT_SUPPORTED_ERR);
    dom_node_unref(fragment);
    dom_node_unref(node);
    dom_string_unref(ns);
    dom_string_unref(name);
    /* createElement performs HTML case normalization before choosing the type. */
    name = string("TEMPLATE");
    ck_assert_int_eq(dom_document_create_element(doc, name, &node), DOM_NO_ERR);
    dom_node *content = contents(node);
    dom_node_unref(content);
    dom_node_unref(node);
    dom_string_unref(name);
    dom_node_unref(doc);
}
END_TEST

START_TEST(test_contents_and_host_teardown)
{
    /* All release orders, attached and detached hosts, and contents adopted
     * into their host document exercise both pending lists without cycles. */
    dom_document *doc = document(true);
    dom_node *host = element(doc, "template"), *content = contents(host), *result = NULL;
    if (_i & 1)
        append((dom_node *)doc, host);
    if (_i & 2) {
        ck_assert_int_eq(dom_document_adopt_node(doc, content, &result), DOM_NO_ERR);
        dom_node_unref(result);
    }
    if (_i & 4)
        dom_node_unref(doc);
    if (_i & 8) {
        dom_node_unref(content);
        dom_node_unref(host);
    } else {
        dom_node_unref(host);
        /* A retained fragment remains usable when its host is released. */
        dom_document *owner_doc = owner(content);
        dom_node *child = element(owner_doc, "span");
        append(content, child);
        dom_node_unref(child);
        dom_node_unref(owner_doc);
        dom_node_unref(content);
    }
    if (!(_i & 4))
        dom_node_unref(doc);
}
END_TEST

#ifdef WISP_TEST_ALLOC_FAILURE
START_TEST(test_template_allocation_failures)
{
    /* Recreate the destination for every budget: never let a previous attempt
     * warm its inert-document cache and accidentally skip allocation paths. */
    dom_document *src = document(true);
    dom_node *parent = element(src, "div"), *a = element(src, "template"), *ac = contents(a);
    dom_document *inert = owner(ac);
    dom_node *nested = element(inert, "template"), *nc = contents(nested), *input = element(inert, "input");
    append(parent, a);
    append(ac, nested);
    append(nc, input);
    dom_string *name = string("template"), *ns = string("http://www.w3.org/1999/xhtml");
    unsigned budget;
    unsigned operation = _i % 3;
    bool calloc_failures = (_i / 3) % 2 != 0;
    bool html = _i / 6 != 0;
    for (budget = 0; budget < 512; budget++) {
        dom_document *dst = document(html);
        dom_node *result = NULL;
        if (calloc_failures)
            calloc_limit(budget);
        else
            malloc_limit(budget);
        dom_exception err;
        if (operation == 0)
            err = dom_document_create_element_ns(dst, ns, name, &result);
        else if (operation == 1)
            err = dom_document_import_node(dst, a, true, &result);
        else
            err = dom_document_adopt_node(dst, parent, &result);
        malloc_limit(UINT_MAX);
        calloc_limit(UINT_MAX);
        ck_assert_msg(err == DOM_NO_ERR || err == DOM_NO_MEM_ERR,
            "operation=%u html=%d calloc=%d budget=%u: exception %u", operation, html, calloc_failures, budget, err);
        if (err != DOM_NO_ERR) {
            ck_assert_ptr_null(result);
            has_owner(a, src);
            has_owner(ac, inert);
            has_owner(input, inert);
            dom_node *still_attached = first(parent);
            ck_assert_ptr_eq(still_attached, a);
            dom_node_unref(still_attached);
            /* A failed preparation must leave the same destination retryable. */
            dom_node *probe = element(dst, "template");
            dom_node_unref(probe);
        }
        dom_node_unref(result);
        dom_node_unref(dst);
        if (err == DOM_NO_ERR)
            break;
    }
    ck_assert_uint_gt(budget, 0);
    ck_assert_msg(budget < 512, "Template operation %u never completed", operation);
    dom_string_unref(name);
    dom_string_unref(ns);
    dom_node_unref(src);
    dom_node_unref(inert);
    dom_node_unref(parent);
    dom_node_unref(a);
    dom_node_unref(ac);
    dom_node_unref(nested);
    dom_node_unref(nc);
    dom_node_unref(input);
}
END_TEST
#endif

int main(void)
{
    Suite *suite = suite_create("Template contents");
    TCase *tc = tcase_create("Native ownership");
    tcase_add_loop_test(tc, test_inert_owner_and_nested_templates, 0, 2);
    tcase_add_test(tc, test_host_is_only_used_for_cycle_checks);
    tcase_add_loop_test(tc, test_template_adoption, 0, 4);
    tcase_add_loop_test(tc, test_clone_and_import_templates, 0, 8);
    tcase_add_test(tc, test_contents_independently_adopted_or_cloned);
    tcase_add_loop_test(tc, test_only_html_lowercase_template_is_special, 0, 4);
    tcase_add_loop_test(tc, test_contents_and_host_teardown, 0, 16);
#ifdef WISP_TEST_ALLOC_FAILURE
    tcase_add_loop_test(tc, test_template_allocation_failures, 0, 12);
#endif
    suite_add_tcase(suite, tc);
    SRunner *runner = srunner_create(suite);
    srunner_run_all(runner, CK_NORMAL);
    int failed = srunner_ntests_failed(runner);
    srunner_free(runner);
    return failed ? EXIT_FAILURE : EXIT_SUCCESS;
}

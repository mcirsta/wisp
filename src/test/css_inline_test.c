/* DOM ownership and transactional CSSOM publication, including native callers. */
#include <check.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <wisp/utils/corestrings.h>
#include "content/handlers/css/inline.h"
#ifdef WISP_TEST_ALLOC_FAILURE
#include "test/malloc_fig.h"
#endif

static css_stylesheet_params params;

static dom_string *string(const char *text)
{
    dom_string *result;
    ck_assert_int_eq(dom_string_create((const uint8_t *)text, strlen(text), &result), DOM_NO_ERR);
    return result;
}

static dom_document *document(void)
{
    dom_document *doc;
    ck_assert_int_eq(
        dom_implementation_create_document(DOM_IMPLEMENTATION_HTML, NULL, NULL, NULL, NULL, NULL, &doc), DOM_NO_ERR);
    return doc;
}

static dom_node *element(dom_document *doc, const char *css)
{
    dom_node *node;
    dom_string *name = string("div");
    ck_assert_int_eq(dom_document_create_element(doc, name, &node), DOM_NO_ERR);
    dom_string_unref(name);
    if (css != NULL) {
        dom_string *text = string(css);
        ck_assert_int_eq(dom_element_set_attribute(node, corestring_dom_style, text), DOM_NO_ERR);
        dom_string_unref(text);
    }
    return node;
}

static struct nscss_inline_style *style(dom_node *node)
{
    struct nscss_inline_style *result;
    ck_assert_int_eq(nscss_inline_style_get(node, &params, &result), DOM_NO_ERR);
    return result;
}

static css_declarations *pending(void)
{
    css_declarations *d;
    const char *text = "margin:var(--m);color:red";
    ck_assert_int_eq(css_declarations_parse(&params, (const uint8_t *)text, strlen(text), &d), CSS_OK);
    bool changed;
    ck_assert_int_eq(css_declarations_set(d, &params, "margin-top", 10, "8px", 3, "", 0, &changed), CSS_OK);
    ck_assert(changed);
    return d;
}

static void check_count(dom_node *node, size_t expected)
{
    ck_assert_uint_eq(css_declarations_count(nscss_inline_style_declarations(style(node))), expected);
}

START_TEST(test_native_ownership)
{
    dom_document *src = document(), *dst = document();
    dom_node *node = element(src, NULL), *copy = NULL, *imported = NULL, *adopted = NULL;
    struct nscss_inline_style *original = style(node);
    css_declarations *d = pending();
    bool changed;
    ck_assert_int_eq(nscss_inline_style_commit(node, original, &d, &changed), DOM_NO_ERR);
    ck_assert(changed);
    ck_assert_ptr_null(d);
    check_count(node, 5);
    ck_assert_int_eq(dom_node_clone_node(node, true, &copy), DOM_NO_ERR);
    ck_assert_int_eq(dom_document_import_node(dst, node, true, &imported), DOM_NO_ERR);
    /* DOM cloning copies serialized attributes. CSSOM specifies no additional
     * cloning steps to copy pending values that have empty serialized values. */
    check_count(copy, 2);
    check_count(imported, 2);
    ck_assert_ptr_ne(style(copy), original);
    ck_assert_int_eq(dom_document_adopt_node(dst, node, &adopted), DOM_NO_ERR);
    ck_assert_ptr_eq(adopted, node);
    dom_node_unref(adopted);
    ck_assert_ptr_eq(style(node), original);
    dom_node_unref(copy);
    dom_node_unref(src);
    check_count(node, 5);
    bool present;
    css_stylesheet *compiled = nscss_inline_style_for_element(node, "https://other.example/", false, &present);
    ck_assert(present);
    ck_assert_ptr_nonnull(compiled);
    dom_node_unref(node);
    dom_node_unref(imported);
    dom_node_unref(dst);
    ck_assert_int_eq(css_stylesheet_destroy(compiled), CSS_OK);
}
END_TEST

START_TEST(test_static_path_and_lazy_reads)
{
    dom_document *doc = document();
    dom_node *node = element(doc, NULL);
    bool present;
    ck_assert_ptr_null(nscss_inline_style_for_element(node, "https://example.org/", false, &present));
    ck_assert(!present);
    dom_string *text = string("WIDTH: 12px; invalid: here");
    ck_assert_int_eq(dom_element_set_attribute(node, corestring_dom_style, text), DOM_NO_ERR);
    for (unsigned pass = 0; pass < 2; pass++) {
        css_stylesheet *sheet = nscss_inline_style_for_element(node, "https://example.org/", false, &present);
        ck_assert(present);
        ck_assert_ptr_nonnull(sheet);
        css_stylesheet_destroy(sheet);
        void *data = NULL;
        ck_assert_int_eq(dom_node_get_user_data(node, corestring_dom___ns_key_inline_style, &data), DOM_NO_ERR);
        ck_assert_msg((data != NULL) == (pass != 0), "static selection should not allocate editable state");
        check_count(node, 1);
        dom_string *attribute;
        ck_assert_int_eq(dom_element_get_attribute(node, corestring_dom_style, &attribute), DOM_NO_ERR);
        ck_assert(dom_string_isequal(text, attribute));
        dom_string_unref(attribute);
    }
    /* Unrelated attributes must not discard pending contributions. */
    css_declarations *d = pending();
    bool changed;
    ck_assert_int_eq(nscss_inline_style_commit(node, style(node), &d, &changed), DOM_NO_ERR);
    ck_assert_int_eq(dom_element_set_attribute(node, corestring_dom_class, text), DOM_NO_ERR);
    check_count(node, 5);
    dom_string_unref(text);
    dom_node_unref(node);
    dom_node_unref(doc);
}
END_TEST

#ifdef WISP_TEST_ALLOC_FAILURE
START_TEST(test_publication_failure)
{
    void (*allocators[])(unsigned) = {malloc_limit, calloc_limit, realloc_limit};
    bool succeeded = false;
    for (unsigned n = 0; n < 512; n++) {
        dom_document *doc = document();
        dom_node *node = element(doc, _i < 3 ? "width: 10px" : NULL);
        struct nscss_inline_style *original = style(node);
        css_declarations *d = pending();
        bool changed;
        allocators[_i % 3](n);
        dom_exception error = nscss_inline_style_commit(node, original, &d, &changed);
        allocators[_i % 3](UINT_MAX);
        ck_assert_msg(error == DOM_NO_ERR || error == DOM_NO_MEM_ERR, "allocator %d at %u: %d", _i, n, error);
        if (changed) {
            ck_assert_ptr_null(d);
            check_count(node, 5);
        } else {
            ck_assert_ptr_nonnull(d);
            check_count(node, _i < 3 ? 1 : 0);
        }
        css_declarations_destroy(d);
        dom_node_unref(node);
        dom_node_unref(doc);
        if (error == DOM_NO_ERR) {
            ck_assert(changed);
            succeeded = true;
            break;
        }
    }
    ck_assert(succeeded);
}
END_TEST
#endif

static void setup(void)
{
    ck_assert_int_eq(corestrings_init(), NSERROR_OK);
    nscss_inline_params(&params, "https://example.org/", false);
}

static void teardown(void)
{
    ck_assert_int_eq(corestrings_fini(), NSERROR_OK);
}

int main(void)
{
    Suite *suite = suite_create("DOM inline styles");
    TCase *tc = tcase_create("Ownership and publication");
    tcase_add_checked_fixture(tc, setup, teardown);
    tcase_add_test(tc, test_native_ownership);
    tcase_add_test(tc, test_static_path_and_lazy_reads);
#ifdef WISP_TEST_ALLOC_FAILURE
    tcase_add_loop_test(tc, test_publication_failure, 0, 6);
#endif
    suite_add_tcase(suite, tc);
    SRunner *runner = srunner_create(suite);
    srunner_set_fork_status(runner, CK_NOFORK);
    srunner_run_all(runner, CK_NORMAL);
    int failed = srunner_ntests_failed(runner);
    srunner_free(runner);
    return failed ? EXIT_FAILURE : EXIT_SUCCESS;
}

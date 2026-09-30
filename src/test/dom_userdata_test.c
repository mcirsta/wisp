/* Prepared user-data entries must stay invisible until allocation-free publish. */
#include <check.h>
#include <limits.h>
#include <stdlib.h>
#include <dom/dom.h>
#include <dom/core/implementation.h>
#ifdef WISP_TEST_ALLOC_FAILURE
#include "test/malloc_fig.h"
#endif

static dom_document *doc;
static dom_node *node;
static dom_string *key, *other;
static int a, b, c;
static unsigned callbacks;

static void deleted(dom_node_operation op, dom_string *k, void *data, dom_node *src, dom_node *dst)
{
    ck_assert_int_eq(op, DOM_NODE_DELETED);
    ck_assert_ptr_eq(data, &b);
    callbacks++;
}

static void setup(void)
{
    callbacks = 0;
    ck_assert_int_eq(
        dom_implementation_create_document(DOM_IMPLEMENTATION_HTML, NULL, NULL, NULL, NULL, NULL, &doc), DOM_NO_ERR);
    ck_assert_int_eq(dom_string_create((const uint8_t *)"first", 5, &key), DOM_NO_ERR);
    ck_assert_int_eq(dom_string_create((const uint8_t *)"second", 6, &other), DOM_NO_ERR);
    ck_assert_int_eq(dom_document_create_element(doc, key, (dom_element **)&node), DOM_NO_ERR);
}

static void teardown(void)
{
    dom_node_unref(node);
    dom_node_unref(doc);
    dom_string_unref(key);
    dom_string_unref(other);
}

static void *get(dom_string *name)
{
    void *value;
    ck_assert_int_eq(dom_node_get_user_data(node, name, &value), DOM_NO_ERR);
    return value;
}

START_TEST(test_prepare_publish_and_delete)
{
    void *old;
    if (_i != 0)
        ck_assert_int_eq(dom_node_set_user_data(node, key, &a, NULL, &old), DOM_NO_ERR);
    ck_assert_int_eq(dom_node_set_user_data(node, other, &c, NULL, &old), DOM_NO_ERR);
    struct dom_user_data *entry;
    ck_assert_int_eq(dom_node_prepare_user_data(key, &b, deleted, &entry), DOM_NO_ERR);
    ck_assert_ptr_eq(get(key), _i ? &a : NULL);
    dom_node_set_prepared_user_data(node, entry, &old);
    ck_assert_ptr_eq(old, _i ? &a : NULL);
    ck_assert_ptr_eq(get(key), &b);
    ck_assert_ptr_eq(get(other), &c);
    ck_assert_uint_eq(callbacks, 0);
    if (_i < 2) {
        ck_assert_int_eq(dom_node_prepare_user_data(key, NULL, NULL, &entry), DOM_NO_ERR);
        dom_node_set_prepared_user_data(node, entry, &old);
        ck_assert_ptr_eq(old, &b);
        ck_assert_ptr_null(get(key));
        ck_assert_uint_eq(callbacks, 0); /* Assignment/removal does not call handlers. */
        ck_assert_ptr_eq(get(other), &c);
    }
    dom_node_unref(node);
    node = NULL;
    ck_assert_uint_eq(callbacks, _i == 2 ? 1 : 0);
}
END_TEST

START_TEST(test_discard_and_key_lifetime)
{
    struct dom_user_data *entry;
    ck_assert_int_eq(dom_node_prepare_user_data(key, &a, deleted, &entry), DOM_NO_ERR);
    dom_node_discard_prepared_user_data(entry);
    dom_node_discard_prepared_user_data(NULL);
    ck_assert_ptr_null(get(key));
    ck_assert_uint_eq(callbacks, 0);
    ck_assert_int_eq(dom_node_prepare_user_data(key, &b, deleted, &entry), DOM_NO_ERR);
    dom_string_unref(key);
    key = NULL; /* Preparation owns a reference independently of the caller. */
    void *old;
    dom_node_set_prepared_user_data(node, entry, &old);
    ck_assert_ptr_null(old);
    ck_assert_int_eq(dom_string_create((const uint8_t *)"first", 5, &key), DOM_NO_ERR);
    ck_assert_ptr_eq(get(key), &b);
    dom_node_unref(node);
    node = NULL;
    ck_assert_uint_eq(callbacks, 1);
}
END_TEST

#ifdef WISP_TEST_ALLOC_FAILURE
START_TEST(test_no_allocation_at_publish)
{
    struct dom_user_data *entry = NULL;
    malloc_limit(0);
    dom_exception error = dom_node_prepare_user_data(key, &a, NULL, &entry);
    malloc_limit(UINT_MAX);
    ck_assert_int_eq(error, DOM_NO_MEM_ERR);
    ck_assert_ptr_null(entry);
    ck_assert_ptr_null(get(key));
    ck_assert_int_eq(dom_node_prepare_user_data(key, &b, NULL, &entry), DOM_NO_ERR);
    void *old;
    malloc_limit(0);
    dom_node_set_prepared_user_data(node, entry, &old);
    dom_exception removed = dom_node_set_user_data(node, other, NULL, NULL, &old);
    malloc_limit(UINT_MAX);
    ck_assert_int_eq(removed, DOM_NO_ERR);
    ck_assert_ptr_null(old);
    ck_assert_ptr_eq(get(key), &b);
}
END_TEST
#endif

int main(void)
{
    Suite *s = suite_create("Prepared DOM user data");
    TCase *tc = tcase_create("Publish and discard");
    tcase_add_checked_fixture(tc, setup, teardown);
    tcase_add_loop_test(tc, test_prepare_publish_and_delete, 0, 3);
    tcase_add_test(tc, test_discard_and_key_lifetime);
#ifdef WISP_TEST_ALLOC_FAILURE
    tcase_add_test(tc, test_no_allocation_at_publish);
#endif
    suite_add_tcase(s, tc);
    SRunner *runner = srunner_create(s);
    srunner_run_all(runner, CK_NORMAL);
    int failed = srunner_ntests_failed(runner);
    srunner_free(runner);
    return failed ? EXIT_FAILURE : EXIT_SUCCESS;
}

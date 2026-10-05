/* WHATWG HTML details name groups: expected states follow the attribute-change
 * and insertion algorithms, independently of any browser's rendering. */
#include <check.h>
#include <stdlib.h>
#include <string.h>
#include <dom/dom.h>
#include <libdom/bindings/hubbub/parser.h>

static dom_document *document;
static dom_string *open_name, *name_name;

static dom_string *string(const char *text)
{
    dom_string *s;
    ck_assert_int_eq(dom_string_create((const uint8_t *)text, strlen(text), &s), DOM_NO_ERR);
    return s;
}

static void setup(void)
{
    ck_assert_int_eq(
        dom_implementation_create_document(DOM_IMPLEMENTATION_HTML, NULL, NULL, NULL, NULL, NULL, &document),
        DOM_NO_ERR);
    open_name = string("open");
    name_name = string("name");
}

static void teardown(void)
{
    dom_string_unref(open_name);
    dom_string_unref(name_name);
    dom_node_unref(document);
}

static void set(dom_node *node, dom_string *name, const char *text)
{
    dom_string *value = string(text);
    ck_assert_int_eq(dom_element_set_attribute(node, name, value), DOM_NO_ERR);
    dom_string_unref(value);
}

static void append(dom_node *parent, dom_node *child)
{
    dom_node *result;
    ck_assert_int_eq(dom_node_append_child(parent, child, &result), DOM_NO_ERR);
    dom_node_unref(result);
}

static dom_node *create(const char *tag)
{
    dom_node *node;
    dom_string *name = string(tag);
    ck_assert_int_eq(dom_document_create_element(document, name, &node), DOM_NO_ERR);
    dom_string_unref(name);
    return node;
}

static dom_node *details(dom_node *parent, const char *name, bool open)
{
    dom_node *node = create("details");
    if (name != NULL)
        set(node, name_name, name);
    if (open)
        set(node, open_name, "");
    if (parent != NULL)
        append(parent, node);
    return node;
}

static void expect(dom_node *node, bool expected, const char *reason)
{
    bool actual;
    ck_assert_int_eq(dom_element_has_attribute(node, open_name, &actual), DOM_NO_ERR);
    ck_assert_msg(actual == expected, "%s: expected open=%d, got %d", reason, expected, actual);
}

START_TEST(test_open_and_name_changes)
{
    dom_node *root = create("div"), *a = details(root, "group", true), *b = details(root, "group", true);
    expect(a, true, "insertion preserves existing member");
    expect(b, false, "insertion closes incoming member");
    set(b, open_name, "false");
    expect(a, false, "adding boolean open closes peer");
    expect(b, true, "open=false still opens");
    set(b, open_name, "changed");
    expect(b, true, "changing existing open value keeps it open");
    ck_assert_int_eq(dom_element_remove_attribute(b, open_name), DOM_NO_ERR);
    expect(a, false, "closing does not open another member");
    set(a, open_name, "");
    set(b, name_name, "different");
    set(b, open_name, "");
    set(b, name_name, "group");
    expect(a, true, "name change preserves existing member");
    expect(b, false, "name change closes joining member");
    ck_assert_int_eq(dom_element_remove_attribute(b, name_name), DOM_NO_ERR);
    set(b, open_name, "");
    set(b, name_name, "group");
    expect(b, false, "adding name also closes joining member");
    dom_node_unref(a);
    dom_node_unref(b);
    dom_node_unref(root);
}
END_TEST

static const struct {
    const char *a, *b;
    bool grouped;
} names[] = {{NULL, NULL, false}, {"", "", false}, {"x", "X", false}, {" x", "x", false}, {"a b", "b", false},
    {" ", " ", true}, {"a b", "a b", true}, {"é", "é", true}};
START_TEST(test_exact_nonempty_names)
{
    dom_node *root = create("div");
    dom_node *a = details(root, names[_i].a, true), *b = details(root, names[_i].b, true);
    expect(a, true, "first member stays open");
    expect(b, !names[_i].grouped, "names match exactly and must be nonempty");
    dom_node_unref(a);
    dom_node_unref(b);
    dom_node_unref(root);
}
END_TEST

START_TEST(test_insertion_and_tree_scope)
{
    dom_node *root = create("div"), *a = details(root, "g", true);
    dom_node *wrapper = create("section"), *b = details(wrapper, "g", true), *result;
    expect(b, true, "separate detached trees do not share a group");
    ck_assert_int_eq(dom_node_insert_before(root, wrapper, a, &result), DOM_NO_ERR);
    dom_node_unref(result);
    expect(a, true, "existing member wins even when incoming subtree is earlier");
    expect(b, false, "insertion processes descendants");
    ck_assert_int_eq(dom_node_remove_child(root, wrapper, &result), DOM_NO_ERR);
    dom_node_unref(result);
    set(b, open_name, "");
    expect(a, true, "detached subtree no longer closes document peer");
    expect(b, true, "detached subtree can open");
    /* Replacement removes the old member before applying insertion steps. */
    ck_assert_int_eq(dom_node_replace_child(root, wrapper, a, &result), DOM_NO_ERR);
    dom_node_unref(result);
    expect(b, true, "replacing the open peer leaves incoming member open");
    append(root, a);
    expect(a, false, "reinsertion closes a formerly detached open member");
    dom_node *fragment;
    ck_assert_int_eq(dom_document_create_document_fragment(document, &fragment), DOM_NO_ERR);
    dom_node *c = details(fragment, "g", true);
    append(root, fragment);
    expect(c, false, "fragment insertion joins destination group");
    expect(b, true, "fragment leaves destination open member intact");
    dom_node *clone;
    ck_assert_int_eq(dom_node_clone_node(b, true, &clone), DOM_NO_ERR);
    expect(clone, true, "detached clone preserves open");
    append(root, clone);
    expect(clone, false, "clone insertion enforces exclusivity");
    dom_node_unref(clone);
    dom_node_unref(fragment);
    dom_node_unref(c);
    dom_node_unref(a);
    dom_node_unref(b);
    dom_node_unref(wrapper);
    dom_node_unref(root);
}
END_TEST

START_TEST(test_attribute_nodes_and_namespaces)
{
    dom_node *root = create("div"), *a = details(root, "g", true), *b = details(root, "other", true);
    dom_attr *attr, *old;
    dom_string *value = string("g"), *ns = string("urn:test");
    ck_assert_int_eq(dom_element_get_attribute_node(b, name_name, &attr), DOM_NO_ERR);
    if (_i == 0)
        ck_assert_int_eq(dom_attr_set_value(attr, value), DOM_NO_ERR);
    else if (_i == 1)
        ck_assert_int_eq(dom_node_set_node_value(attr, value), DOM_NO_ERR);
    else
        ck_assert_int_eq(dom_node_set_text_content(attr, value), DOM_NO_ERR);
    expect(b, false, "Attr.value name change closes joining member");
    dom_node_unref(attr);
    ck_assert_int_eq(dom_document_create_attribute(document, open_name, &attr), DOM_NO_ERR);
    ck_assert_int_eq(dom_attr_set_value(attr, value), DOM_NO_ERR);
    ck_assert_int_eq(dom_element_set_attribute_node(b, attr, &old), DOM_NO_ERR);
    ck_assert_ptr_null(old);
    expect(a, false, "setAttributeNode open closes peer");
    expect(b, true, "setAttributeNode opens target");
    dom_node_unref(attr);
    /* Replacing a sole attribute also exercises ownership of the list head. */
    dom_node *single = create("details");
    set(single, name_name, "old");
    ck_assert_int_eq(dom_document_create_attribute(document, name_name, &attr), DOM_NO_ERR);
    ck_assert_int_eq(dom_attr_set_value(attr, value), DOM_NO_ERR);
    ck_assert_int_eq(dom_element_set_attribute_node(single, attr, &old), DOM_NO_ERR);
    ck_assert_ptr_nonnull(old);
    dom_node_unref(old);
    dom_node_unref(attr);
    append(root, single);
    set(single, open_name, "");
    expect(b, false, "replacement attribute remains in the live list");
    expect(single, true, "replacement name participates in grouping");
    ck_assert_int_eq(dom_element_set_attribute_ns(a, ns, open_name, value), DOM_NO_ERR);
    expect(a, false, "namespaced open is not HTML open");
    expect(single, true, "namespaced open does not close peer");
    ck_assert_int_eq(dom_element_set_attribute_ns(b, ns, name_name, value), DOM_NO_ERR);
    expect(single, true, "namespaced name has no grouping effect");
    dom_node *foreign;
    dom_string *tag = string("details");
    ck_assert_int_eq(dom_document_create_element_ns(document, ns, tag, &foreign), DOM_NO_ERR);
    set(foreign, name_name, "g");
    set(foreign, open_name, "");
    append(root, foreign);
    expect(foreign, true, "foreign details is not an HTML disclosure");
    expect(single, true, "foreign details leaves HTML peer open");
    ck_assert_int_eq(dom_element_set_attribute_ns(a, NULL, open_name, value), DOM_NO_ERR);
    expect(a, true, "null-namespace open participates");
    expect(single, false, "null-namespace open closes peer");
    dom_node_unref(foreign);
    dom_string_unref(tag);
    dom_string_unref(ns);
    dom_string_unref(value);
    dom_node_unref(single);
    dom_node_unref(a);
    dom_node_unref(b);
    dom_node_unref(root);
}
END_TEST

START_TEST(test_parser)
{
    dom_node_unref(document);
    dom_hubbub_parser *parser;
    dom_hubbub_parser_params params = {.enc = "UTF-8", .fix_enc = true};
    const char *source = "<!doctype html><details id=a name=g open></details>"
                         "<div><details id=b open name=g></details></div><details id=c name=G open></details>";
    ck_assert_int_eq(dom_hubbub_parser_create(&params, &parser, &document), DOM_HUBBUB_OK);
    /* Chunking must not change parser-insertion precedence. */
    for (size_t i = 0; i < strlen(source); i++)
        ck_assert_int_eq(dom_hubbub_parser_parse_chunk(parser, (const uint8_t *)source + i, 1), DOM_HUBBUB_OK);
    ck_assert_int_eq(dom_hubbub_parser_completed(parser), DOM_HUBBUB_OK);
    dom_hubbub_parser_destroy(parser);
    const char *ids[] = {"a", "b", "c"};
    for (size_t i = 0; i < 3; i++) {
        dom_string *id = string(ids[i]);
        dom_node *node;
        ck_assert_int_eq(dom_document_get_element_by_id(document, id, &node), DOM_NO_ERR);
        ck_assert_ptr_nonnull(node);
        expect(node, i != 1, "parser preserves first open member despite attribute order");
        dom_node_unref(node);
        dom_string_unref(id);
    }
}
END_TEST

int main(void)
{
    Suite *suite = suite_create("Details name groups");
    TCase *tc = tcase_create("HTML attribute-change and insertion steps");
    tcase_add_checked_fixture(tc, setup, teardown);
    tcase_add_test(tc, test_open_and_name_changes);
    tcase_add_loop_test(tc, test_exact_nonempty_names, 0, sizeof(names) / sizeof(names[0]));
    tcase_add_test(tc, test_insertion_and_tree_scope);
    tcase_add_loop_test(tc, test_attribute_nodes_and_namespaces, 0, 3);
    tcase_add_test(tc, test_parser);
    suite_add_tcase(suite, tc);
    SRunner *runner = srunner_create(suite);
    srunner_run_all(runner, CK_NORMAL);
    int failed = srunner_ntests_failed(runner);
    srunner_free(runner);
    return failed ? EXIT_FAILURE : EXIT_SUCCESS;
}

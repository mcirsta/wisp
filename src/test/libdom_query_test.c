/* DOM Standard: qualified-name and namespace queries, backed by live trees. */
#include <check.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <dom/dom.h>
#include <dom/html/html_table_element.h>
#include <dom/html/html_tablerow_element.h>
#include <dom/html/html_tablesection_element.h>
#include <dom/html/html_select_element.h>

static const char *html_ns = "http://www.w3.org/1999/xhtml";
static const char *svg_ns = "http://www.w3.org/2000/svg";
static dom_document *document;

static dom_string *string(const char *text)
{
    dom_string *value = NULL;
    if (text != NULL)
        ck_assert_int_eq(dom_string_create((const uint8_t *)text, strlen(text), &value), DOM_NO_ERR);
    return value;
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
    dom_namespace_finalise();
}

static dom_node *element(dom_document *doc, const char *namespace, const char *name, bool ns)
{
    dom_string *space = string(namespace), *tag = string(name);
    dom_element *node = NULL;
    dom_exception err = ns ? dom_document_create_element_ns(doc, space, tag, &node)
                           : dom_document_create_element(doc, tag, &node);
    ck_assert_msg(err == DOM_NO_ERR, "Creating {%s}%s: %d", namespace == NULL ? "" : namespace, name, err);
    dom_string_unref(space);
    dom_string_unref(tag);
    return (dom_node *)node;
}

static void append(dom_node *parent, dom_node *child)
{
    dom_node *inserted = NULL;
    ck_assert_int_eq(dom_node_append_child(parent, child, &inserted), DOM_NO_ERR);
    ck_assert_ptr_eq(inserted, child);
    dom_node_unref(inserted);
}

static void name_is(dom_node *node, const char *local, const char *qualified)
{
    dom_string *name = NULL;
    ck_assert_int_eq(dom_node_get_local_name(node, &name), DOM_NO_ERR);
    ck_assert_str_eq(dom_string_data(name), local);
    dom_string_unref(name);
    /* Repeated reads exercise the cached qualified name. */
    for (unsigned i = 0; i < 2; i++) {
        ck_assert_int_eq(dom_node_get_node_name(node, &name), DOM_NO_ERR);
        ck_assert_str_eq(dom_string_data(name), qualified);
        dom_string_unref(name);
        ck_assert_int_eq(dom_element_get_tag_name(node, &name), DOM_NO_ERR);
        ck_assert_str_eq(dom_string_data(name), qualified);
        dom_string_unref(name);
    }
}

static dom_nodelist *query(dom_node *root, const char *namespace, const char *name, bool ns)
{
    dom_node_type type;
    ck_assert_int_eq(dom_node_get_node_type(root, &type), DOM_NO_ERR);
    dom_string *space = string(namespace), *tag = string(name);
    dom_nodelist *list = NULL;
    dom_exception err;
    if (type == DOM_DOCUMENT_NODE)
        err = ns ? dom_document_get_elements_by_tag_name_ns(root, space, tag, &list)
                 : dom_document_get_elements_by_tag_name(root, tag, &list);
    else
        err = ns ? dom_element_get_elements_by_tag_name_ns(root, space, tag, &list)
                 : dom_element_get_elements_by_tag_name(root, tag, &list);
    ck_assert_msg(err == DOM_NO_ERR, "Query {%s}%s: %d", namespace == NULL ? "" : namespace, name, err);
    dom_string_unref(space);
    dom_string_unref(tag);
    return list;
}

struct visit {
    dom_node **expected;
    unsigned count, seen, stop;
};

static bool visit(dom_node *node, void *data)
{
    struct visit *v = data;
    ck_assert_uint_lt(v->seen, v->count);
    ck_assert_ptr_eq(node, v->expected[v->seen++]);
    return v->seen != v->stop;
}

static void check_list(dom_nodelist *list, dom_node **expected, unsigned count, const char *label)
{
    uint32_t length = UINT32_MAX;
    ck_assert_int_eq(dom_nodelist_get_length(list, &length), DOM_NO_ERR);
    ck_assert_msg(length == count, "%s: length %u, expected %u", label, length, count);
    for (unsigned i = 0; i <= count; i++) {
        dom_node *node = NULL;
        ck_assert_int_eq(dom_nodelist_item(list, i, &node), DOM_NO_ERR);
        ck_assert_msg(node == (i < count ? expected[i] : NULL), "%s: item %u disagrees", label, i);
        dom_node_unref(node);
    }
    dom_node *last = NULL;
    ck_assert_int_eq(dom_nodelist_item(list, UINT32_MAX, &last), DOM_NO_ERR);
    ck_assert_ptr_null(last);
    struct visit v = {.expected = expected, .count = count};
    dom_nodelist_foreach(list, visit, &v);
    ck_assert_uint_eq(v.seen, count);
    if (count > 1) {
        v.seen = 0;
        v.stop = 1;
        dom_nodelist_foreach(list, visit, &v);
        ck_assert_uint_eq(v.seen, 1);
    }
}

START_TEST(test_names_and_specialization)
{
    struct {
        const char *namespace, *input, *local, *qualified;
        bool ns;
    } cases[] = {
        {NULL, "DiV", "div", "DIV", false},
        {html_ns, "DiV", "DiV", "DIV", true},
        {html_ns, "p:span", "span", "P:SPAN", true},
        {svg_ns, "linearGradient", "linearGradient", "linearGradient", true},
        {"urn:Test", "p:MiXeD", "MiXeD", "p:MiXeD", true},
        {NULL, "MiXeD", "MiXeD", "MiXeD", true},
        {"", "MiXeD", "MiXeD", "MiXeD", true},
        {NULL, "x:Thing", "x:thing", "X:THING", false},
    };
    for (unsigned i = 0; i < sizeof(cases) / sizeof(*cases); i++) {
        dom_node *node = element(document, cases[i].namespace, cases[i].input, cases[i].ns);
        name_is(node, cases[i].local, cases[i].qualified);
        dom_node *copy = NULL;
        ck_assert_int_eq(dom_node_clone_node(node, false, &copy), DOM_NO_ERR);
        name_is(copy, cases[i].local, cases[i].qualified);
        dom_node_unref(copy);
        dom_node_unref(node);
    }
    dom_node *node = element(document, "urn:Test", "a:Mixed", true);
    name_is(node, "Mixed", "a:Mixed");
    dom_string *prefix = string("b");
    ck_assert_int_eq(dom_node_set_prefix(node, prefix), DOM_NO_ERR);
    name_is(node, "Mixed", "b:Mixed");
    dom_string_unref(prefix);
    ck_assert_int_eq(dom_node_set_prefix(node, NULL), DOM_NO_ERR);
    name_is(node, "Mixed", "Mixed");
    dom_node_unref(node);
}
END_TEST

START_TEST(test_query_matrix)
{
    dom_node *root = element(document, NULL, "main", false);
    append((dom_node *)document, root);
    dom_node *nodes[] = {
        element(document, NULL, "div", false),
        element(document, html_ns, "DIV", true),
        element(document, svg_ns, "linearGradient", true),
        element(document, "urn:Test", "p:Mixed", true),
        element(document, NULL, "Mixed", true),
        element(document, "urn:Test", "Mixed", true),
        element(document, html_ns, "p:span", true),
        element(document, html_ns, "P:span", true),
        element(document, NULL, "span", false),
    };
    for (unsigned i = 0; i < 8; i++)
        append(root, nodes[i]);
    append(nodes[7], nodes[8]); /* Include a deeper descendant. */
    struct {
        const char *namespace, *name;
        bool ns;
        unsigned mask;
    } cases[] = {
        {NULL, "*", false, 0x1ff},
        {NULL, "div", false, 1},
        {NULL, "DIV", false, 1},
        {NULL, "linearGradient", false, 4},
        {NULL, "lineargradient", false, 0},
        {NULL, "Mixed", false, 0x30},
        {NULL, "MIXED", false, 0},
        {NULL, "p:Mixed", false, 8},
        {NULL, "P:Mixed", false, 0},
        {NULL, "p:span", false, 0x40},
        {NULL, "P:SPAN", false, 0x40},
        {NULL, "span", false, 0x100},
        {NULL, "", false, 0},
        {NULL, "main", false, 0},
        {html_ns, "div", true, 1},
        {html_ns, "DIV", true, 2},
        {html_ns, "span", true, 0x1c0},
        {html_ns, "p:span", true, 0},
        {svg_ns, "linearGradient", true, 4},
        {svg_ns, "lineargradient", true, 0},
        {"urn:Test", "Mixed", true, 0x28},
        {"urn:test", "Mixed", true, 0},
        {NULL, "Mixed", true, 0x10},
        {"", "Mixed", true, 0x10},
        {NULL, "*", true, 0x10},
        {"*", "Mixed", true, 0x38},
        {"*", "*", true, 0x1ff},
        {"urn:Test", "*", true, 0x28},
    };
    for (unsigned i = 0; i < sizeof(cases) / sizeof(*cases); i++) {
        dom_nodelist *list = query(root, cases[i].namespace, cases[i].name, cases[i].ns);
        dom_node *expected[9];
        unsigned count = 0;
        for (unsigned n = 0; n < 9; n++) {
            if (cases[i].mask & (1u << n))
                expected[count++] = nodes[n];
        }
        check_list(list, expected, count, cases[i].name);
        dom_nodelist_unref(list);
    }
    dom_nodelist *list = query((dom_node *)document, NULL, "MAIN", false);
    check_list(list, &root, 1, "document includes root element");
    dom_nodelist_unref(list);
    /* Cached lists with different case must not alias each other. */
    dom_nodelist *mixed = query(root, NULL, "Mixed", false), *upper = query(root, NULL, "MIXED", false);
    dom_node *expected[] = {nodes[4], nodes[5]};
    check_list(mixed, expected, 2, "Mixed cache entry");
    check_list(upper, NULL, 0, "MIXED cache entry");
    dom_nodelist_unref(mixed);
    dom_nodelist_unref(upper);
    for (unsigned i = 0; i < 9; i++)
        dom_node_unref(nodes[i]);
    dom_node_unref(root);
}
END_TEST

START_TEST(test_xml_and_live_subtree)
{
    dom_document *xml = NULL;
    ck_assert_int_eq(
        dom_implementation_create_document(DOM_IMPLEMENTATION_CORE, NULL, NULL, NULL, NULL, NULL, &xml), DOM_NO_ERR);
    dom_node *root = element(xml, NULL, "Root", false);
    dom_node *upper = element(xml, html_ns, "DIV", true), *lower = element(xml, html_ns, "div", true);
    append((dom_node *)xml, root);
    append(root, upper);
    append(upper, lower);
    name_is(lower, "div", "div"); /* HTML namespace in an XML document is not HTML casing. */
    dom_nodelist *all = query(root, NULL, "*", false), *exact = query(root, NULL, "div", false);
    dom_node *expected[] = {upper, lower};
    check_list(all, expected, 2, "XML descendants");
    check_list(exact, &lower, 1, "XML query case");
    dom_node *removed = NULL;
    ck_assert_int_eq(dom_node_remove_child(root, upper, &removed), DOM_NO_ERR);
    dom_node_unref(removed);
    check_list(all, NULL, 0, "live removal");
    append(root, lower);
    check_list(exact, &lower, 1, "live insertion");
    ck_assert_int_eq(dom_node_remove_child(xml, root, &removed), DOM_NO_ERR);
    dom_node_unref(removed);
    check_list(exact, &lower, 1, "detached root remains live");
    /* A sibling outside the query root must not be visited. */
    dom_node *sibling = element(xml, NULL, "div", false), *outer = element(xml, NULL, "Outer", false);
    append(outer, root);
    append(outer, sibling);
    check_list(exact, &lower, 1, "subtree boundary");
    dom_nodelist_unref(all);
    dom_nodelist_unref(exact);
    dom_node_unref(outer);
    dom_node_unref(sibling);
    dom_node_unref(lower);
    dom_node_unref(upper);
    dom_node_unref(root);
    dom_node_unref(xml);
}
END_TEST

START_TEST(test_foreign_root_and_empty_namespace)
{
    dom_node *svg = element(document, svg_ns, "svg", true);
    dom_node *child = element(document, NULL, "DiV", false);
    append(svg, child);
    dom_nodelist *list = query(svg, NULL, "DIV", false);
    check_list(list, &child, 1, "HTML matching depends on document type, not root namespace");
    dom_nodelist_unref(list);
    dom_node_unref(child);
    dom_node_unref(svg);

    dom_document *xml = NULL;
    ck_assert_int_eq(
        dom_implementation_create_document(DOM_IMPLEMENTATION_CORE, NULL, NULL, NULL, NULL, NULL, &xml), DOM_NO_ERR);
    dom_node *root = element(xml, NULL, "root", false);
    child = element(xml, "", "Mixed", true);
    append(root, child);
    dom_string *namespace = NULL;
    ck_assert_int_eq(dom_node_get_namespace(child, &namespace), DOM_NO_ERR);
    ck_assert_ptr_null(namespace);
    list = query(root, NULL, "*", true);
    check_list(list, &child, 1, "empty creation namespace becomes null in XML too");
    dom_nodelist_unref(list);
    dom_node_unref(child);
    dom_node_unref(root);
    dom_node_unref(xml);
}
END_TEST

START_TEST(test_internal_html_name_lookups)
{
    dom_node *root = element(document, NULL, "html", false);
    dom_node *foreign_title = element(document, svg_ns, "title", true);
    dom_node *foreign_body = element(document, "urn:Test", "body", true);
    dom_node *title = element(document, html_ns, "h:title", true);
    dom_node *body = element(document, html_ns, "h:body", true);
    append((dom_node *)document, root);
    append(root, foreign_title);
    append(root, foreign_body);
    append(root, title);
    append(root, body);
    dom_string *text = string("Actual HTML title"), *result = NULL;
    ck_assert_int_eq(dom_node_set_text_content(title, text), DOM_NO_ERR);
    ck_assert_int_eq(dom_html_document_get_title(document, &result), DOM_NO_ERR);
    ck_assert_ptr_nonnull(result);
    ck_assert(dom_string_isequal(result, text));
    dom_string_unref(result);
    dom_string_unref(text);
    dom_html_element *found = NULL;
    ck_assert_int_eq(dom_html_document_get_body(document, &found), DOM_NO_ERR);
    ck_assert_ptr_eq((dom_node *)found, body);
    dom_node_unref(found);
    dom_node_unref(body);
    dom_node_unref(title);
    dom_node_unref(foreign_body);
    dom_node_unref(foreign_title);
    dom_node_unref(root);
}
END_TEST

START_TEST(test_table_and_select_factories)
{
    dom_node *table = element(document, NULL, "TABLE", false), *caption = NULL, *head = NULL;
    dom_node *foot = NULL, *body = NULL, *row = NULL, *cell = NULL;
    /* Inserting into an empty table constructs its tbody and first row. */
    ck_assert_int_eq(
        dom_html_table_element_insert_row((dom_html_table_element *)table, -1, (dom_html_element **)&row), DOM_NO_ERR);
    ck_assert_int_eq(dom_node_get_parent_node(row, &body), DOM_NO_ERR);
    ck_assert_int_eq(
        dom_html_table_element_create_caption((dom_html_table_element *)table, (dom_html_element **)&caption),
        DOM_NO_ERR);
    ck_assert_int_eq(
        dom_html_table_element_create_t_head((dom_html_table_element *)table, (dom_html_element **)&head), DOM_NO_ERR);
    ck_assert_int_eq(
        dom_html_table_element_create_t_foot((dom_html_table_element *)table, (dom_html_element **)&foot), DOM_NO_ERR);
    dom_node_unref(row);
    ck_assert_int_eq(dom_html_table_section_element_insert_row(
                         (dom_html_table_section_element *)body, -1, (dom_html_element **)&row),
        DOM_NO_ERR);
    ck_assert_int_eq(
        dom_html_table_row_element_insert_cell((dom_html_table_row_element *)row, -1, (dom_html_element **)&cell),
        DOM_NO_ERR);
    name_is(caption, "caption", "CAPTION");
    name_is(head, "thead", "THEAD");
    name_is(foot, "tfoot", "TFOOT");
    name_is(body, "tbody", "TBODY");
    name_is(row, "tr", "TR");
    name_is(cell, "td", "TD");
    dom_nodelist *list = query(table, html_ns, "td", true);
    check_list(list, &cell, 1, "generated table elements use real local names");
    dom_nodelist_unref(list);
    dom_node_unref(cell);
    dom_node_unref(row);
    dom_node_unref(body);
    dom_node_unref(foot);
    dom_node_unref(head);
    dom_node_unref(caption);
    dom_node_unref(table);
    dom_node *select = element(document, NULL, "SELECT", false), *option = element(document, NULL, "OPTION", false);
    append(select, option);
    uint32_t length;
    ck_assert_int_eq(dom_html_select_element_get_length((dom_html_select_element *)select, &length), DOM_NO_ERR);
    ck_assert_uint_eq(length, 1);
    dom_node_unref(option);
    dom_node_unref(select);
}
END_TEST

int main(void)
{
    Suite *suite = suite_create("DOM element names and queries");
    TCase *tc = tcase_create("Live names");
    tcase_add_checked_fixture(tc, setup, teardown);
    tcase_add_test(tc, test_names_and_specialization);
    tcase_add_test(tc, test_query_matrix);
    tcase_add_test(tc, test_xml_and_live_subtree);
    tcase_add_test(tc, test_foreign_root_and_empty_namespace);
    tcase_add_test(tc, test_internal_html_name_lookups);
    tcase_add_test(tc, test_table_and_select_factories);
    suite_add_tcase(suite, tc);
    SRunner *runner = srunner_create(suite);
    srunner_run_all(runner, CK_NORMAL);
    int failed = srunner_ntests_failed(runner);
    srunner_free(runner);
    return failed ? EXIT_FAILURE : EXIT_SUCCESS;
}

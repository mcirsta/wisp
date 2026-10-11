/* HTML fragment serialization regressions, derived from the HTML Standard.
 * https://html.spec.whatwg.org/multipage/parsing.html#serialising-html-fragments */
#include <check.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <dom/dom.h>
#include <libdom/bindings/hubbub/parser.h>
#ifdef WISP_TEST_ALLOC_FAILURE
#include "test/malloc_fig.h"
#endif

#define HTML "http://www.w3.org/1999/xhtml"
#define SVG "http://www.w3.org/2000/svg"
#define MATH "http://www.w3.org/1998/Math/MathML"
#define XML "http://www.w3.org/XML/1998/namespace"
#define XMLNS "http://www.w3.org/2000/xmlns/"
#define XLINK "http://www.w3.org/1999/xlink"

static dom_document *document;

static dom_string *string(const char *text)
{
    dom_string *s = NULL;
    ck_assert_int_eq(dom_string_create((const uint8_t *)text, strlen(text), &s), DOM_NO_ERR);
    return s;
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

static dom_document *owner(dom_node *node)
{
    dom_document *doc = NULL;
    ck_assert_int_eq(dom_node_get_owner_document(node, &doc), DOM_NO_ERR);
    return doc;
}

static dom_node *element(dom_document *doc, const char *ns, const char *name)
{
    dom_node *node = NULL;
    dom_string *space = ns ? string(ns) : NULL, *local = string(name);
    ck_assert_int_eq(dom_document_create_element_ns(doc, space, local, &node), DOM_NO_ERR);
    dom_string_unref(space);
    dom_string_unref(local);
    return node;
}

static void append(dom_node *parent, dom_node *child)
{
    dom_node *result = NULL;
    ck_assert_int_eq(dom_node_append_child(parent, child, &result), DOM_NO_ERR);
    dom_node_unref(result);
}

static void text(dom_node *parent, const char *value)
{
    dom_document *doc = owner(parent);
    dom_string *s = string(value);
    dom_node *node = NULL;
    ck_assert_int_eq(dom_document_create_text_node(doc, s, &node), DOM_NO_ERR);
    append(parent, node);
    dom_node_unref(node);
    dom_string_unref(s);
    dom_node_unref(doc);
}

static dom_node *contents(dom_node *host)
{
    dom_document_fragment *fragment = NULL;
    ck_assert_int_eq(dom_html_template_element_get_content((dom_html_template_element *)host, &fragment), DOM_NO_ERR);
    return (dom_node *)fragment;
}

static void attribute(dom_node *node, const char *ns, const char *name, const char *value)
{
    dom_string *space = ns ? string(ns) : NULL, *key = string(name), *data = string(value);
    ck_assert_int_eq(dom_element_set_attribute_ns(node, space, key, data), DOM_NO_ERR);
    dom_string_unref(space);
    dom_string_unref(key);
    dom_string_unref(data);
}

static dom_string *serialize(dom_node *node, bool scripting)
{
    dom_string *result = NULL;
    ck_assert_int_eq(dom_html_serialize_fragment(node, scripting, &result), DOM_NO_ERR);
    ck_assert_ptr_nonnull(result);
    return result;
}

static void expect(dom_node *node, bool scripting, const char *expected)
{
    dom_string *result = serialize(node, scripting);
    ck_assert_msg(dom_string_byte_length(result) == strlen(expected) &&
            memcmp(dom_string_data(result), expected, strlen(expected)) == 0,
        "Expected [%s], got [%s], scripting=%d", expected, dom_string_data(result), scripting);
    dom_string_unref(result);
}

static dom_node *parse(const char *context, const char *markup, bool scripting)
{
    dom_node *node = element(document, HTML, context);
    dom_hubbub_parser_params params = {.enable_script = scripting};
    dom_hubbub_parser *parser = NULL;
    dom_document_fragment *fragment = NULL;
    ck_assert_int_eq(
        dom_hubbub_fragment_parser_create(&params, (dom_element *)node, &parser, &fragment), DOM_HUBBUB_OK);
    ck_assert_int_eq(dom_hubbub_parser_parse_chunk(parser, (const uint8_t *)markup, strlen(markup)), DOM_HUBBUB_OK);
    ck_assert_int_eq(dom_hubbub_parser_completed(parser), DOM_HUBBUB_OK);
    dom_hubbub_parser_destroy(parser);
    dom_node *target = strcmp(context, "template") == 0 ? contents(node) : dom_node_ref(node);
    append(target, (dom_node *)fragment);
    dom_node_unref(target);
    dom_node_unref(fragment);
    return node;
}

static const struct {
    const char *context, *input, *expected;
} parsed_cases[] = {
    {"div", "", ""},
    {"div", "one <B ID=X>two</B> three", "one <b id=\"X\">two</b> three"},
    {"div", "&amp;&lt;&gt;&quot;'&nbsp;ș😀", "&amp;&lt;&gt;\"'&nbsp;ș😀"},
    {"div", "<i title='&amp;&lt;&gt;&quot;&nbsp;'>T</i>", "<i title=\"&amp;&lt;&gt;&quot;&nbsp;\">T</i>"},
    {"div", "<input disabled><br/><img src='relative?a&b'>", "<input disabled=\"\"><br><img src=\"relative?a&amp;b\">"},
    {"div", "<p>A<p>B", "<p>A</p><p>B</p>"},
    {"div", "<!--<x>&--><span></span>", "<!--<x>&--><span></span>"},
    {"div", "<script>if (a < b && c > d) x='&';</script>", "<script>if (a < b && c > d) x='&';</script>"},
    {"div", "<style>a>b{content:'<&>'}</style>", "<style>a>b{content:'<&>'}</style>"},
    {"div", "<textarea>&lt;A&gt;&amp;</textarea>", "<textarea>&lt;A&gt;&amp;</textarea>"},
    {"table", "<tr><td>A</td></tr>", "<tbody><tr><td>A</td></tr></tbody>"},
    {"template", "<tr><td>A</td></tr>", "<tr><td>A</td></tr>"},
    {"div", "<template>A<template><b>B</b></template>C</template>D",
        "<template>A<template><b>B</b></template>C</template>D"},
    {"div", "<svg><linearGradient viewBox='0 0 1 1'/><path/></svg>",
        "<svg><linearGradient viewBox=\"0 0 1 1\"></linearGradient><path></path></svg>"},
    {"div", "<math><mi>x</mi><annotation-xml encoding='text/html'><P>Y</P></annotation-xml></math>",
        "<math><mi>x</mi><annotation-xml encoding=\"text/html\"><p>Y</p></annotation-xml></math>"},
    {"div", "<svg><script>&lt;&amp;&gt;</script></svg>", "<svg><script>&lt;&amp;&gt;</script></svg>"},
    {"div", "<command>A</command><custom-box is=demo>B</custom-box>",
        "<command>A</command><custom-box is=\"demo\">B</custom-box>"},
};

START_TEST(test_parsed_markup)
{
    const size_t count = sizeof(parsed_cases) / sizeof(parsed_cases[0]);
    size_t index = _i % count;
    bool scripting = _i < count;
    dom_node *node = parse(parsed_cases[index].context, parsed_cases[index].input, scripting);
    expect(node, scripting, parsed_cases[index].expected);
    /* Repeated reads must be stable and leave the tree alone. */
    expect(node, scripting, parsed_cases[index].expected);
    dom_node_unref(node);
}
END_TEST

static const char *void_names[] = {"area", "base", "basefont", "bgsound", "br", "col", "embed", "frame", "hr", "img",
    "input", "keygen", "link", "meta", "param", "source", "track", "wbr"};

START_TEST(test_void_elements)
{
    const char *name = void_names[_i];
    dom_node *root = element(document, HTML, "div"), *child = element(document, HTML, name);
    text(child, "ignored child");
    append(root, child);
    char expected[64];
    snprintf(expected, sizeof(expected), "<%s>", name);
    expect(root, true, expected);
    expect(child, true, "");
    dom_node *retained = NULL;
    ck_assert_int_eq(dom_node_get_first_child(child, &retained), DOM_NO_ERR);
    ck_assert_ptr_nonnull(retained);
    dom_node_unref(retained);
    dom_node_unref(child);
    dom_node_unref(root);
}
END_TEST

static const char *raw_names[] = {"script", "style", "xmp", "iframe", "noembed", "noframes", "plaintext", "noscript",
    "textarea", "title", "pre", "listing", "SCRIPT"};

START_TEST(test_text_context)
{
    const size_t count = sizeof(raw_names) / sizeof(raw_names[0]);
    const size_t index = _i % count;
    bool scripting = _i < count;
    dom_node *node = element(document, HTML, raw_names[index]);
    text(node, "\n<&>\"'\xc2\xa0");
    bool raw = index < 7 || (index == 7 && scripting);
    expect(node, scripting, raw ? "\n<&>\"'\xc2\xa0" : "\n&lt;&amp;&gt;\"'&nbsp;");
    dom_node_unref(node);
}
END_TEST

START_TEST(test_namespace_rules)
{
    dom_node *root = element(document, HTML, "div");
    static const struct {
        const char *ns, *name, *expected;
    } cases[] = {
        {HTML, "h:div", "<div>&lt;&amp;&gt;</div>"},
        {SVG, "s:br", "<br>&lt;&amp;&gt;</br>"},
        {MATH, "m:img", "<img>&lt;&amp;&gt;</img>"},
        {"urn:test", "x:Mixed", "<x:Mixed>&lt;&amp;&gt;</x:Mixed>"},
        {NULL, "script", "<script>&lt;&amp;&gt;</script>"},
        {SVG, "script", "<script>&lt;&amp;&gt;</script>"},
        {HTML, "BR", "<BR>&lt;&amp;&gt;</BR>"},
        {NULL, "br", "<br>&lt;&amp;&gt;</br>"},
    };
    dom_node *child = element(document, cases[_i].ns, cases[_i].name);
    text(child, "<&>");
    append(root, child);
    expect(root, true, cases[_i].expected);
    dom_node_unref(child);
    dom_node_unref(root);
}
END_TEST

START_TEST(test_attribute_names_and_values)
{
    dom_node *root = element(document, HTML, "div"), *child = element(document, SVG, "use");
    attribute(child, XML, "other:lang", "ro");
    attribute(child, XMLNS, "xmlns", SVG);
    attribute(child, XMLNS, "xmlns:q", "urn:q");
    attribute(child, XLINK, "other:href", "#shape");
    attribute(child, "urn:other", "o:Mixed", "<&\">'");
    attribute(child, HTML, "h:attr", "value");
    /* A no-namespace name can contain a colon when set without setAttributeNS. */
    dom_string *name = string("raw:name"), *value = string("first");
    ck_assert_int_eq(dom_element_set_attribute(child, name, value), DOM_NO_ERR);
    dom_attr *attr = NULL;
    ck_assert_int_eq(dom_element_get_attribute_node(child, name, &attr), DOM_NO_ERR);
    text((dom_node *)attr, "&second");
    dom_string_unref(name);
    dom_string_unref(value);
    dom_node_unref(attr);
    append(root, child);
    expect(root, true,
        "<use xml:lang=\"ro\" xmlns=\"" SVG "\" xmlns:q=\"urn:q\" xlink:href=\"#shape\" "
        "o:Mixed=\"&lt;&amp;&quot;&gt;'\" h:attr=\"value\" raw:name=\"first&amp;second\"></use>");
    dom_node_unref(child);
    dom_node_unref(root);
}
END_TEST

START_TEST(test_template_boundaries)
{
    dom_node *root = element(document, HTML, "div"), *host = element(document, HTML, "template");
    dom_node *content = contents(host);
    dom_document *inert = owner(content);
    dom_node *nested = element(inert, HTML, "template"), *nc = contents(nested);
    text(host, "ordinary children are excluded");
    text(content, "A");
    append(content, nested);
    text(nc, "B<&>");
    text(content, "C");
    append(root, host);
    text(root, "D");
    const char *expected = "A<template>B&lt;&amp;&gt;</template>C";
    expect(root, true, "<template>A<template>B&lt;&amp;&gt;</template>C</template>D");
    expect(host, true, expected);
    expect(content, true, expected);
    expect(nc, true, "B&lt;&amp;&gt;");
    dom_node_unref(root);
    dom_node_unref(host);
    /* A retained fragment no longer has a host, but still serializes normally. */
    expect(content, true, expected);
    dom_node_unref(nested);
    dom_node_unref(nc);
    dom_node_unref(inert);
    dom_node_unref(content);
}
END_TEST

START_TEST(test_template_noscript)
{
    dom_node *host = element(document, HTML, "template"), *content = contents(host);
    dom_document *inert = owner(content);
    dom_node *noscript = element(inert, HTML, "noscript");
    text(noscript, "<&>");
    append(content, noscript);
    expect(host, true, "<noscript>&lt;&amp;&gt;</noscript>");
    expect(noscript, true, "&lt;&amp;&gt;");
    /* Scripting follows the actual owner: contents can be independently adopted. */
    dom_node *adopted = NULL;
    ck_assert_int_eq(dom_document_adopt_node(document, content, &adopted), DOM_NO_ERR);
    dom_node_unref(adopted);
    expect(host, true, "<noscript><&></noscript>");
    expect(host, false, "<noscript>&lt;&amp;&gt;</noscript>");
    dom_node_unref(noscript);
    dom_node_unref(content);
    dom_node_unref(inert);
    dom_node_unref(host);
}
END_TEST

START_TEST(test_document_and_non_element_nodes)
{
    dom_document_type *doctype = NULL;
    ck_assert_int_eq(dom_implementation_create_document_type("html", "public id", "system id", &doctype), DOM_NO_ERR);
    append((dom_node *)document, (dom_node *)doctype);
    dom_node *root = element(document, HTML, "html");
    append((dom_node *)document, root);
    dom_string *data = string("not sanitized --> &"), *target = string("go");
    dom_node *comment = NULL, *pi = NULL;
    ck_assert_int_eq(dom_document_create_comment(document, data, &comment), DOM_NO_ERR);
    append(root, comment);
    ck_assert_int_eq(dom_document_create_processing_instruction(document, target, data, &pi), DOM_NO_ERR);
    append(root, pi);
    expect(
        (dom_node *)document, true, "<!DOCTYPE html><html><!--not sanitized --> &--><?go not sanitized --> &?></html>");
    dom_node_unref(pi);
    dom_node_unref(comment);
    dom_node_unref(root);
    dom_node_unref(doctype);
    dom_string_unref(data);
    dom_string_unref(target);
}
END_TEST

START_TEST(test_cdata_and_embedded_null)
{
    dom_document *xml = NULL;
    ck_assert_int_eq(
        dom_implementation_create_document(DOM_IMPLEMENTATION_CORE, NULL, NULL, NULL, NULL, NULL, &xml), DOM_NO_ERR);
    dom_node *root = element(xml, SVG, "g");
    const char bytes[] = "a\0<&>\xc2\xa0z";
    dom_string *data = NULL;
    ck_assert_int_eq(dom_string_create((const uint8_t *)bytes, sizeof(bytes) - 1, &data), DOM_NO_ERR);
    dom_cdata_section *cdata = NULL;
    ck_assert_int_eq(dom_document_create_cdata_section(xml, data, &cdata), DOM_NO_ERR);
    append(root, (dom_node *)cdata);
    dom_string *result = serialize(root, true);
    const char expected[] = "a\0&lt;&amp;&gt;&nbsp;z";
    ck_assert_uint_eq(dom_string_byte_length(result), sizeof(expected) - 1);
    ck_assert_int_eq(memcmp(dom_string_data(result), expected, sizeof(expected) - 1), 0);
    dom_string_unref(result);
    dom_string_unref(data);
    dom_node_unref(cdata);
    dom_node_unref(root);
    dom_node_unref(xml);
}
END_TEST

START_TEST(test_deep_and_wide)
{
    enum { DEPTH = 2048, ATTRIBUTES = 256 };
    dom_node *nodes[DEPTH];
    dom_document *doc = (dom_document *)dom_node_ref(document);
    for (unsigned i = 0; i < DEPTH; i++) {
        nodes[i] = element(doc, HTML, _i && i % 7 == 0 ? "template" : "i");
        if (i != 0) {
            dom_node *parent = _i && (i - 1) % 7 == 0 ? contents(nodes[i - 1]) : dom_node_ref(nodes[i - 1]);
            append(parent, nodes[i]);
            dom_node_unref(parent);
        }
        if (_i && i % 7 == 0) {
            dom_node *content = contents(nodes[i]);
            dom_node_unref(doc);
            doc = owner(content);
            dom_node_unref(content);
        }
    }
    dom_node *leaf = _i && (DEPTH - 1) % 7 == 0 ? contents(nodes[DEPTH - 1]) : dom_node_ref(nodes[DEPTH - 1]);
    text(leaf, "LEAF");
    dom_node_unref(leaf);
    /* Many attributes exercise direct list iteration and output-buffer growth. */
    for (unsigned i = 0; i < ATTRIBUTES; i++) {
        char name[32];
        snprintf(name, sizeof(name), "data-%u", i);
        attribute(nodes[DEPTH - 1], NULL, name, "&\"<>");
    }
    dom_string *a = serialize(nodes[0], true), *b = serialize(nodes[0], true);
    ck_assert(dom_string_isequal(a, b));
    ck_assert_ptr_nonnull(strstr(dom_string_data(a), "LEAF"));
    ck_assert_ptr_nonnull(strstr(dom_string_data(a), "data-255=\"&amp;&quot;&lt;&gt;\""));
    size_t opens = 0, closes = 0;
    const char *bytes = dom_string_data(a);
    for (size_t i = 0; i < dom_string_byte_length(a); i++) {
        if (bytes[i] == '<') {
            if (bytes[i + 1] == '/')
                closes++;
            else
                opens++;
        }
    }
    ck_assert_uint_eq(opens, DEPTH - 1);
    ck_assert_uint_eq(closes, DEPTH - 1);
    dom_string_unref(a);
    dom_string_unref(b);
    /* Release from the top with each child still referenced, so this test's
     * teardown doesn't impose recursive destruction on the native DOM. */
    for (unsigned i = 0; i < DEPTH; i++)
        dom_node_unref(nodes[i]);
    dom_node_unref(doc);
}
END_TEST

START_TEST(test_errors_leave_output_null)
{
    dom_node *root = element(document, HTML, "div"), *legacy = NULL;
    dom_string *name = string("legacy"), *result = (dom_string *)root;
    ck_assert_int_eq(dom_html_serialize_fragment(NULL, true, &result), DOM_INVALID_ACCESS_ERR);
    ck_assert_ptr_null(result);
    ck_assert_int_eq(dom_html_serialize_fragment(root, true, NULL), DOM_INVALID_ACCESS_ERR);
    ck_assert_int_eq(dom_document_create_entity_reference(document, name, &legacy), DOM_NO_ERR);
    append(root, legacy);
    ck_assert_int_eq(dom_html_serialize_fragment(root, true, &result), DOM_NOT_SUPPORTED_ERR);
    ck_assert_ptr_null(result);
    ck_assert_int_eq(dom_html_serialize_fragment(legacy, true, &result), DOM_NOT_SUPPORTED_ERR);
    ck_assert_ptr_null(result);
    dom_node_unref(legacy);
    dom_node_unref(root);
    dom_string_unref(name);
}
END_TEST

#ifdef WISP_TEST_ALLOC_FAILURE
START_TEST(test_allocation_failures)
{
    dom_node *root = parse("div", "<template><b a='&amp;&lt;&gt;&quot;'>text</b></template><p>tail</p>", true);
    char long_text[4096];
    memset(long_text, '&', sizeof(long_text) - 1);
    long_text[sizeof(long_text) - 1] = 0;
    text(root, long_text);
    dom_string *expected = serialize(root, true);
    unsigned budget;
    for (budget = 0; budget < 128; budget++) {
        dom_string *result = NULL;
        if (_i)
            realloc_limit(budget);
        else
            malloc_limit(budget);
        dom_exception err = dom_html_serialize_fragment(root, true, &result);
        realloc_limit(UINT_MAX);
        malloc_limit(UINT_MAX);
        ck_assert_msg(
            err == DOM_NO_ERR || err == DOM_NO_MEM_ERR, "allocator=%d budget=%u: exception %u", _i, budget, err);
        if (err == DOM_NO_ERR) {
            ck_assert(dom_string_isequal(result, expected));
            dom_string_unref(result);
            break;
        }
        ck_assert_ptr_null(result);
        /* Output is discarded on failure and the same tree stays retryable. */
        result = serialize(root, true);
        ck_assert(dom_string_isequal(result, expected));
        dom_string_unref(result);
    }
    ck_assert_uint_gt(budget, 0);
    ck_assert_uint_lt(budget, 128);
    dom_string_unref(expected);
    dom_node_unref(root);
}
END_TEST
#endif

int main(void)
{
    Suite *suite = suite_create("HTML fragment serialization");
    TCase *tc = tcase_create("Standards, ownership and allocation");
    tcase_set_timeout(tc, 30);
    tcase_add_checked_fixture(tc, setup, teardown);
    tcase_add_loop_test(tc, test_parsed_markup, 0, 2 * sizeof(parsed_cases) / sizeof(parsed_cases[0]));
    tcase_add_loop_test(tc, test_void_elements, 0, sizeof(void_names) / sizeof(void_names[0]));
    tcase_add_loop_test(tc, test_text_context, 0, 2 * sizeof(raw_names) / sizeof(raw_names[0]));
    tcase_add_loop_test(tc, test_namespace_rules, 0, 8);
    tcase_add_test(tc, test_attribute_names_and_values);
    tcase_add_test(tc, test_template_boundaries);
    tcase_add_test(tc, test_template_noscript);
    tcase_add_test(tc, test_document_and_non_element_nodes);
    tcase_add_test(tc, test_cdata_and_embedded_null);
    tcase_add_loop_test(tc, test_deep_and_wide, 0, 2);
    tcase_add_test(tc, test_errors_leave_output_null);
#ifdef WISP_TEST_ALLOC_FAILURE
    tcase_add_loop_test(tc, test_allocation_failures, 0, 2);
#endif
    suite_add_tcase(suite, tc);
    SRunner *runner = srunner_create(suite);
    srunner_run_all(runner, CK_NORMAL);
    int failed = srunner_ntests_failed(runner);
    srunner_free(runner);
    return failed ? EXIT_FAILURE : EXIT_SUCCESS;
}

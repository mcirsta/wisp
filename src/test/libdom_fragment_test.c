/* HTML fragment algorithm regressions. Expected trees come from the insertion
 * modes in https://html.spec.whatwg.org/multipage/parsing.html, not a browser. */
#include <check.h>
#include <stdlib.h>
#include <string.h>
#include <dom/dom.h>
#include <dom/html/html_input_element.h>
#include <dom/html/html_script_element.h>
#include <libdom/bindings/hubbub/parser.h>

static dom_document *document;

static dom_string *string(const char *text)
{
    dom_string *s = NULL;
    ck_assert_int_eq(dom_string_create((const uint8_t *)text, strlen(text), &s), DOM_NO_ERR);
    return s;
}

static dom_node *element(const char *name)
{
    dom_string *s = string(name);
    dom_node *node = NULL;
    ck_assert_int_eq(dom_document_create_element(document, s, &node), DOM_NO_ERR);
    dom_string_unref(s);
    return node;
}

static void append(dom_node *parent, dom_node *child)
{
    dom_node *result = NULL;
    ck_assert_int_eq(dom_node_append_child(parent, child, &result), DOM_NO_ERR);
    dom_node_unref(result);
}

static void setup(void)
{
    ck_assert_int_eq(dom_implementation_create_document(
        DOM_IMPLEMENTATION_HTML, NULL, NULL, NULL, NULL, NULL, &document), DOM_NO_ERR);
}

static void teardown(void)
{
    dom_node_unref(document);
    dom_namespace_finalise();
}

/* Small test-only tree notation: names and text, no attributes or escaping. */
static char tree[8192];
static size_t used;

static void emit(const char *text, size_t len)
{
    ck_assert_msg(len < sizeof(tree) - used, "Test tree exceeds buffer");
    memcpy(tree + used, text, len);
    used += len;
    tree[used] = 0;
}

static void describe(dom_node *node)
{
    dom_node_type type;
    dom_string *name = NULL;
    ck_assert_int_eq(dom_node_get_node_type(node, &type), DOM_NO_ERR);
    if (type == DOM_ELEMENT_NODE) {
        ck_assert_int_eq(dom_node_get_local_name(node, &name), DOM_NO_ERR);
        emit("<", 1);
        emit(dom_string_data(name), dom_string_byte_length(name));
        emit(">", 1);
    } else if (type == DOM_TEXT_NODE || type == DOM_COMMENT_NODE) {
        dom_string *text = NULL;
        ck_assert_int_eq(dom_node_get_node_value(node, &text), DOM_NO_ERR);
        if (type == DOM_COMMENT_NODE)
            emit("<!--", 4);
        emit(dom_string_data(text), dom_string_byte_length(text));
        if (type == DOM_COMMENT_NODE)
            emit("-->", 3);
        dom_string_unref(text);
    }
    dom_node *child = NULL;
    ck_assert_int_eq(dom_node_get_first_child(node, &child), DOM_NO_ERR);
    while (child != NULL) {
        describe(child);
        dom_node *next = NULL;
        ck_assert_int_eq(dom_node_get_next_sibling(child, &next), DOM_NO_ERR);
        dom_node_unref(child);
        child = next;
    }
    if (name != NULL) {
        emit("</", 2);
        emit(dom_string_data(name), dom_string_byte_length(name));
        emit(">", 1);
        dom_string_unref(name);
    }
}

static dom_node *parse(dom_node *context, const char *input, size_t chunk, bool scripting)
{
    dom_hubbub_parser_params params = {.enable_script = scripting};
    dom_hubbub_parser *parser = NULL;
    dom_document_fragment *fragment = NULL;
    ck_assert_int_eq(dom_hubbub_fragment_parser_create(&params, (dom_element *)context, &parser, &fragment),
        DOM_HUBBUB_OK);
    for (size_t offset = 0, len = strlen(input); offset < len;) {
        size_t take = len - offset < chunk ? len - offset : chunk;
        ck_assert_int_eq(dom_hubbub_parser_parse_chunk(parser, (const uint8_t *)input + offset, take), DOM_HUBBUB_OK);
        offset += take;
    }
    ck_assert_int_eq(dom_hubbub_parser_completed(parser), DOM_HUBBUB_OK);
    /* Completion is idempotent and must not duplicate the result. */
    ck_assert_int_eq(dom_hubbub_parser_completed(parser), DOM_HUBBUB_OK);
    dom_hubbub_parser_destroy(parser);
    return (dom_node *)fragment;
}

static const struct {
    const char *context, *input, *expected;
    bool scripting;
} cases[] = {
    {"div", "", ""},
    {"div", "\xEF\xBB\xBF<b>ș😀</b>", "\xEF\xBB\xBF<b>ș😀</b>"},
    {"div", " leading <b>text</b><!--end-->", " leading <b>text</b><!--end-->"},
    {"div", "<body id=x><p>one<p>two</body></html>tail", "<p>one</p><p>twotail</p>"},
    {"div", "<td>cell</td><tr>row</tr>", "cellrow"},
    {"div", "<title>x</title><span>y</span>", "<title>x</title><span>y</span>"},
    {"div", "<b>one<i>two</b>three", "<b>one<i>two</i></b><i>three</i>"},
    {"table", "<tr><td>A<td>B", "<tbody><tr><td>A</td><td>B</td></tr></tbody>"},
    {"table", "</table><table><tr><td>C", "<tbody><tr><td>C</td></tr></tbody>"},
    {"table", "<col><tr><td>C", "<colgroup><col></col></colgroup><tbody><tr><td>C</td></tr></tbody>"},
    {"table", "prefix<tr><td>C</td></tr>suffix", "prefix<tbody><tr><td>C</td></tr></tbody>suffix"},
    {"tbody", "<td>C</td><tr><td>D", "<tr><td>C</td></tr><tr><td>D</td></tr>"},
    {"tbody", "</tbody><tbody><tr><td>C", "<tr><td>C</td></tr>"},
    {"tr", "<td>A<td>B", "<td>A</td><td>B</td>"},
    {"tr", "</tr><tr><td>A</td></table><td>B", "<td>A</td><td>B</td>"},
    {"td", "<p>A<p>B", "<p>A</p><p>B</p>"},
    {"colgroup", "</colgroup><col><p>ignored", "<col></col>"},
    {"caption", "<b>C</b>", "<b>C</b>"},
    {"caption", "</caption><tr><td>C</td></tr></table>", "C"},
    {"textarea", "\n&lt;x&gt;</textarea><b>&amp;", "\n<x></textarea><b>&"},
    {"textarea", "\r\nx\ry", "\nx\ny"},
    {"title", "&amp;<b></title>z", "&<b></title>z"},
    {"style", "&amp;<b></style>z", "&amp;<b></style>z"},
    {"script", "<!--<script>--></script>&amp;", "<!--<script>--></script>&amp;", true},
    {"iframe", "<b>&amp;</iframe>z", "<b>&amp;</iframe>z"},
    {"xmp", "<b>&amp;</xmp>z", "<b>&amp;</xmp>z"},
    {"noembed", "<b>&amp;</noembed>z", "<b>&amp;</noembed>z"},
    {"noframes", "<b>&amp;</noframes>z", "<b>&amp;</noframes>z"},
    {"plaintext", "<b>&amp;", "<b>&amp;"},
    {"noscript", "<b>&amp;", "<b>&amp;", true},
    {"noscript", "<b>&amp;", "<b>&</b>", false},
    {"head", "<meta><title>A</title><p>B", "<meta></meta><title>A</title><p>B</p>"},
    {"html", "<title>A</title><p>B", "<head><title>A</title></head><body><p>B</p></body>"},
    {"html", "<body>x</body></html><!--end-->", "<head></head><body>x</body><!--end-->"},
    {"frameset", "<frameset><frame></frameset><frame>", "<frameset><frame></frame></frameset><frame></frame>"},
};

START_TEST(test_context)
{
    const size_t count = sizeof(cases) / sizeof(cases[0]);
    size_t variant = _i % count;
    size_t chunk = _i < (int)count ? 4096 : 1;
    dom_node *context = element(cases[variant].context);
    dom_node *original = element("old-child");
    append(context, original);
    dom_node *fragment = parse(context, cases[variant].input, chunk, cases[variant].scripting);
    used = 0;
    tree[0] = 0;
    describe(fragment);
    ck_assert_msg(strcmp(tree, cases[variant].expected) == 0, "case %zu <%s>, chunk %zu: expected %s, got %s",
        variant, cases[variant].context, chunk, cases[variant].expected, tree);
    dom_node *child = NULL;
    ck_assert_int_eq(dom_node_get_first_child(context, &child), DOM_NO_ERR);
    ck_assert_ptr_eq(child, original);
    dom_node_unref(child);
    dom_node_unref(original);
    dom_node_unref(fragment);
    dom_node_unref(context);
}
END_TEST

START_TEST(test_context_local_name)
{
    dom_string *ns = string("http://www.w3.org/1999/xhtml");
    dom_string *name = string(_i == 0 ? "h:textarea" : "TEXTAREA");
    dom_element *context = NULL;
    ck_assert_int_eq(dom_document_create_element_ns(document, ns, name, &context), DOM_NO_ERR);
    dom_node *fragment = parse((dom_node *)context, "<b>&amp;</b>", 1, true);
    used = 0;
    tree[0] = 0;
    describe(fragment);
    ck_assert_str_eq(tree, "<b>&</b>");
    dom_node *first = NULL;
    dom_node_type type;
    ck_assert_int_eq(dom_node_get_first_child(fragment, &first), DOM_NO_ERR);
    ck_assert_int_eq(dom_node_get_node_type(first, &type), DOM_NO_ERR);
    ck_assert_int_eq(type, _i == 0 ? DOM_TEXT_NODE : DOM_ELEMENT_NODE);
    dom_node_unref(first);
    dom_node_unref(fragment);
    dom_node_unref(context);
    dom_string_unref(ns);
    dom_string_unref(name);
}
END_TEST

START_TEST(test_form_context)
{
    dom_node *form = element("form"), *context = element("div");
    append(form, context);
    dom_node *fragment = parse(_i == 0 ? form : context, "<form><input></form><form><input></form>", 1, true);
    used = 0;
    describe(fragment);
    ck_assert_str_eq(tree, "<input></input><form><input></input></form>");
    dom_node *input = NULL;
    ck_assert_int_eq(dom_node_get_first_child(fragment, &input), DOM_NO_ERR);
    dom_html_form_element *owner = NULL;
    ck_assert_int_eq(dom_html_input_element_get_form((dom_html_input_element *)input, &owner), DOM_NO_ERR);
    ck_assert_ptr_null(owner); /* No association with an ancestor in another tree. */
    append(form, fragment);
    ck_assert_int_eq(dom_html_input_element_get_form((dom_html_input_element *)input, &owner), DOM_NO_ERR);
    ck_assert_ptr_eq(owner, form);
    dom_node_unref(owner);
    dom_node_unref(input);
    dom_node_unref(fragment);
    dom_node_unref(context);
    dom_node_unref(form);
}
END_TEST

static dom_hubbub_error unexpected_script(void *ctx, dom_node *script)
{
    ck_abort_msg("Fragment parser executed a script");
    return DOM_HUBBUB_OK;
}

START_TEST(test_inert_and_abandoned)
{
    dom_node *context = element("div");
    dom_hubbub_parser_params params = {.enable_script = true, .script = unexpected_script};
    dom_hubbub_parser *parser = NULL;
    dom_document_fragment *fragment = NULL;
    ck_assert_int_eq(dom_hubbub_fragment_parser_create(&params, (dom_element *)context, &parser, &fragment),
        DOM_HUBBUB_OK);
    const char *html = _i == 2 ? "<script>run()" : "<script src=x>run()</script><p>open";
    ck_assert_int_eq(dom_hubbub_parser_parse_chunk(parser, (const uint8_t *)html, strlen(html)), DOM_HUBBUB_OK);
    if (_i != 1) {
        ck_assert_int_eq(dom_hubbub_parser_completed(parser), DOM_HUBBUB_OK);
        dom_node *script = NULL;
        ck_assert_int_eq(dom_node_get_first_child(fragment, &script), DOM_NO_ERR);
        dom_html_script_element_flags flags;
        ck_assert_int_eq(dom_html_script_element_get_flags((dom_html_script_element *)script, &flags), DOM_NO_ERR);
        ck_assert(flags & DOM_HTML_SCRIPT_ELEMENT_FLAG_ALREADY_STARTED);
        dom_node_unref(script);
    }
    /* Ownership must also work when the caller releases its references first. */
    dom_node_unref(context);
    dom_node_unref(fragment);
    dom_hubbub_parser_destroy(parser);
}
END_TEST

START_TEST(test_foreign_descendant)
{
    dom_node *context = element("div");
    dom_node *fragment =
        parse(context, "<svg><linearGradient viewBox='0 0 1 1'></linearGradient></svg><p>end", 1, true);
    dom_node *svg = NULL, *gradient = NULL, *after = NULL;
    ck_assert_int_eq(dom_node_get_first_child(fragment, &svg), DOM_NO_ERR);
    ck_assert_int_eq(dom_node_get_first_child(svg, &gradient), DOM_NO_ERR);
    dom_string *ns = NULL, *local = NULL;
    ck_assert_int_eq(dom_node_get_namespace(gradient, &ns), DOM_NO_ERR);
    ck_assert_str_eq(dom_string_data(ns), "http://www.w3.org/2000/svg");
    ck_assert_int_eq(dom_node_get_local_name(gradient, &local), DOM_NO_ERR);
    ck_assert_str_eq(dom_string_data(local), "linearGradient");
    dom_string_unref(ns);
    dom_string_unref(local);
    ck_assert_int_eq(dom_node_get_next_sibling(svg, &after), DOM_NO_ERR);
    ck_assert_int_eq(dom_node_get_namespace(after, &ns), DOM_NO_ERR);
    ck_assert_str_eq(dom_string_data(ns), "http://www.w3.org/1999/xhtml");
    dom_string_unref(ns);
    dom_node_unref(after);
    dom_node_unref(gradient);
    dom_node_unref(svg);
    dom_node_unref(fragment);
    dom_node_unref(context);
}
END_TEST

START_TEST(test_unsupported_context)
{
    dom_string *ns = string("http://www.w3.org/2000/svg"), *name = string("svg");
    dom_element *context = NULL;
    if (_i == 0)
        ck_assert_int_eq(dom_document_create_element_ns(document, ns, name, &context), DOM_NO_ERR);
    else
        context = (dom_element *)element(_i == 1 ? "template" : "select");
    dom_hubbub_parser_params params = {0};
    dom_hubbub_parser *parser = NULL;
    dom_document_fragment *fragment = NULL;
    ck_assert_int_eq(dom_hubbub_fragment_parser_create(&params, context, &parser, &fragment),
        DOM_HUBBUB_HUBBUB_ERR_INVALID);
    ck_assert_ptr_null(parser);
    ck_assert_ptr_null(fragment);
    dom_node_unref(context);
    dom_string_unref(ns);
    dom_string_unref(name);
}
END_TEST

int main(void)
{
    Suite *suite = suite_create("HTML fragments");
    TCase *tc = tcase_create("Context, ownership and inert parsing");
    tcase_add_checked_fixture(tc, setup, teardown);
    tcase_add_loop_test(tc, test_context, 0, 2 * (sizeof(cases) / sizeof(cases[0])));
    tcase_add_loop_test(tc, test_context_local_name, 0, 2);
    tcase_add_loop_test(tc, test_form_context, 0, 2);
    tcase_add_loop_test(tc, test_inert_and_abandoned, 0, 3);
    tcase_add_test(tc, test_foreign_descendant);
    tcase_add_loop_test(tc, test_unsupported_context, 0, 3);
    suite_add_tcase(suite, tc);
    SRunner *runner = srunner_create(suite);
    srunner_run_all(runner, CK_NORMAL);
    int failed = srunner_ntests_failed(runner);
    srunner_free(runner);
    return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

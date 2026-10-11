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
    ck_assert_int_eq(
        dom_implementation_create_document(DOM_IMPLEMENTATION_HTML, NULL, NULL, NULL, NULL, NULL, &document),
        DOM_NO_ERR);
}

static void teardown(void)
{
    dom_node_unref(document);
    dom_namespace_finalise();
}

/* Small test-only tree notation: names and text, no attributes or escaping. */
static char tree[8192];
static size_t used;
static bool describe_namespaces;

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
        if (describe_namespaces) {
            dom_string *ns = NULL;
            ck_assert_int_eq(dom_node_get_namespace(node, &ns), DOM_NO_ERR);
            if (strcmp(dom_string_data(ns), "http://www.w3.org/2000/svg") == 0)
                emit("svg:", 4);
            else if (strcmp(dom_string_data(ns), "http://www.w3.org/1998/Math/MathML") == 0)
                emit("math:", 5);
            else
                ck_assert_str_eq(dom_string_data(ns), "http://www.w3.org/1999/xhtml");
            dom_string_unref(ns);
        }
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
    ck_assert_int_eq(
        dom_hubbub_fragment_parser_create(&params, (dom_element *)context, &parser, &fragment), DOM_HUBBUB_OK);
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
    {"select", "<option>A<option>B", "<option>A</option><option>B</option>"},
    {"select", "<button><span>Pick</span></button><div><option>A</option></div>",
        "<button><span>Pick</span></button><div><option>A</option></div>"},
    /* The context is not on the stack: only the current option is implicitly
     * closed, and optgroups don't receive the in-scope-select behavior. */
    {"select", "<optgroup><option>A<optgroup><option>B",
        "<optgroup><option>A</option><optgroup><option>B</option></optgroup></optgroup>"},
    {"select", "<select><input><option>A</select><option>B", "<option>A</option><option>B</option>"},
    {"select", "<div><input><select><option>A</div><option>B", "<div><option>A</option></div><option>B</option>"},
    {"select", "<textarea>text</textarea><hr><option>A", "<textarea>text</textarea><hr></hr><option>A</option>"},
    {"select", "<option>A<hr><option>B", "<option>A<hr></hr></option><option>B</option>"},
    {"select", "<table><tr><td>A</td></tr></table><option>B",
        "<table><tbody><tr><td>A</td></tr></tbody></table><option>B</option>"},
    {"select", "<table><input type=hidden><tr><td>A</table>",
        "<table><input></input><tbody><tr><td>A</td></tr></tbody></table>"},
    {"optgroup", "<option>A<option>B", "<option>A</option><option>B</option>"},
    {"option", "<span>A</span><option>B", "<span>A</span><option>B</option>"},
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
    ck_assert_msg(strcmp(tree, cases[variant].expected) == 0, "case %zu <%s>, chunk %zu: expected %s, got %s", variant,
        cases[variant].context, chunk, cases[variant].expected, tree);
    dom_node *child = NULL;
    ck_assert_int_eq(dom_node_get_first_child(context, &child), DOM_NO_ERR);
    ck_assert_ptr_eq(child, original);
    dom_node_unref(child);
    dom_node_unref(original);
    dom_node_unref(fragment);
    dom_node_unref(context);
}
END_TEST

/* Current HTML "in body" select rules also apply to whole documents. Each
 * fixture runs both there and in a div fragment, whole and byte-by-byte. */
static const struct {
    const char *label, *input, *expected;
} select_cases[] = {
    {"implicit options", "<select><option>A<option>B</select><p>end",
        "<select><option>A</option><option>B</option></select><p>end</p>"},
    {"implicit groups", "<select><optgroup><option>A<optgroup><option>B</select>",
        "<select><optgroup><option>A</option></optgroup><optgroup><option>B</option></optgroup></select>"},
    {"explicit options keep group", "<select><optgroup><option>A</option><option>B</option></optgroup></select>",
        "<select><optgroup><option>A</option><option>B</option></optgroup></select>"},
    {"explicit group closes option", "<select><optgroup><option>A</optgroup><option>B</select>",
        "<select><optgroup><option>A</option></optgroup><option>B</option></select>"},
    {"rich contents", "<select><button><span>Pick</span></button><div><option><span>A</span></option></div></select>",
        "<select><button><span>Pick</span></button><div><option><span>A</span></option></div></select>"},
    {"nested select closes outer", "<select><optgroup><option>A<select><option>B</select><p>end",
        "<select><optgroup><option>A</option></optgroup></select><option>B<p>end</p></option>"},
    {"input closes select", "<select><optgroup><option>A<input><p>end",
        "<select><optgroup><option>A</option></optgroup></select><input></input><p>end</p>"},
    {"textarea stays inside select", "<select><textarea>text</textarea><option>A</select>",
        "<select><textarea>text</textarea><option>A</option></select>"},
    {"separator closes group", "<select><optgroup><option>A<hr><option>B</select>",
        "<select><optgroup><option>A</option></optgroup><hr></hr><option>B</option></select>"},
    {"separator closes paragraph and option", "<select><option><p>A<hr><option>B</select>",
        "<select><option><p>A</p></option><hr></hr><option>B</option></select>"},
    {"option closes implied paragraph", "<select><option><p>A<option>B</select>",
        "<select><option><p>A</p></option><option>B</option></select>"},
    {"option does not close span", "<select><option><span>A<option>B</select>",
        "<select><option><span>A<option>B</option></span></option></select>"},
    {"option end closes span", "<select><optgroup><option><span>A</option><option>B</select>",
        "<select><optgroup><option><span>A</span></option><option>B</option></optgroup></select>"},
    {"group end stops at special element", "<select><optgroup><div></optgroup><option>A</select>",
        "<select><optgroup><div><option>A</option></div></optgroup></select>"},
    {"select is a scope boundary", "<div><select></div><option>A</select><span>B</span></div>",
        "<div><select><option>A</option></select><span>B</span></div>"},
    {"paragraph outside select stays open", "<p><select><option><div>A</div></select>B",
        "<p><select><option><div>A</div></option></select>B</p>"},
    {"button is not a normal scope boundary", "<select><button><span>A<select>B",
        "<select><button><span>A</span></button></select>B"},
    {"select end crosses button", "<select><button>A</select>B", "<select><button>A</button></select>B"},
    {"input crosses button", "<select><button>A<input>B", "<select><button>A</button></select><input></input>B"},
    {"button scope still protects paragraph", "<p>A<button><div>B</div></button>C",
        "<p>A<button><div>B</div></button>C</p>"},
    {"list item scope", "<ul><li>A<ul></li><li>B</ul>C</ul>", "<ul><li>A<ul><li>B</li></ul>C</li></ul>"},
    {"cell select", "<table><tr><td><select><option>A</select>B</td></tr></table>",
        "<table><tbody><tr><td><select><option>A</option></select>B</td></tr></tbody></table>"},
    {"cell close with open select", "<table><tr><td><select><option>A<td>B</table>",
        "<table><tbody><tr><td><select><option>A</option></select></td><td>B</td></tr></tbody></table>"},
    {"foster select before table", "<table><select><option>A</select><tr><td>B</table>",
        "<select><option>A</option></select><table><tbody><tr><td>B</td></tr></tbody></table>"},
    {"row clears fostered select", "<table><select><option>A<tr><td>B</table>",
        "<select><option>A</option></select><table><tbody><tr><td>B</td></tr></tbody></table>"},
    {"table within select", "<select><table><tr><td>A</td></tr></table><option>B</select>",
        "<select><table><tbody><tr><td>A</td></tr></tbody></table><option>B</option></select>"},
    {"table scope shields outer select", "<select><table><tr><td><select><option>A</select></table>B</select>",
        "<select><table><tbody><tr><td><select><option>A</option></select></td></tr></tbody></table>B</select>"},
    {"hidden input in table retains select", "<table><select><option>A<input type=hidden><option>B</select></table>",
        "<select><option>A<input></input></option><option>B</option></select><table></table>"},
    {"table hidden inputs are void", "<table><input type=hidden><input type=HIDDEN name=hidden><tr><td>A</table>",
        "<table><input></input><input></input><tbody><tr><td>A</td></tr></tbody></table>"},
    {"unrelated hidden value is not a hidden input", "<table><input name=hidden type=text><tr><td>A</table>",
        "<input></input><table><tbody><tr><td>A</td></tr></tbody></table>"},
    {"tainted table still accepts hidden input", "<table><div>A</div><input type=hidden><tr><td>B</table>",
        "<div>A</div><table><input></input><tbody><tr><td>B</td></tr></tbody></table>"},
    {"caption select", "<table><caption><select><option>A</select>B</caption></table>",
        "<table><caption><select><option>A</option></select>B</caption></table>"},
    {"form end outside select scope", "<form><select></form><option>A</select><input></form>",
        "<form><select><option>A</option></select><input></input></form>"},
    {"form inside select", "<select><form><option>A</form><option>B</select>",
        "<select><form><option>A</option></form><option>B</option></select>"},
    {"option outside select only closes current", "<datalist><option><span>A<option>B</datalist>",
        "<datalist><option><span>A<option>B</option></span></option></datalist>"},
    {"formatting reconstruction", "<select><option><b>A</option><option>B</select>C",
        "<select><option><b>A</b></option><b><option>B</option></b></select><b>C</b>"},
    {"button does not add formatting marker", "<select><b><button>A</select>B",
        "<select><b><button>A</button></b></select><b>B</b>"},
    {"button end preserves active formatting", "<select><button><b>A</button>B</select>C",
        "<select><button><b>A</b></button><b>B</b></select><b>C</b>"},
    {"ignored unmatched closing tags", "</select></option></optgroup><select></optgroup><option>A</select>",
        "<select><option>A</option></select>"},
};

START_TEST(test_select_document_and_fragment)
{
    const size_t count = sizeof(select_cases) / sizeof(select_cases[0]);
    size_t variant = _i % count, mode = _i / count;
    size_t chunk = (mode & 1) ? 1 : 4096;
    dom_node *root = NULL, *context = NULL;
    dom_document *parsed = NULL;
    if (mode < 2) {
        context = element("div");
        root = parse(context, select_cases[variant].input, chunk, false);
    } else {
        dom_hubbub_parser_params params = {.enc = "UTF-8", .fix_enc = true};
        dom_hubbub_parser *parser = NULL;
        ck_assert_int_eq(dom_hubbub_parser_create(&params, &parser, &parsed), DOM_HUBBUB_OK);
        const char *prefix = "<!doctype html><body>";
        ck_assert_int_eq(dom_hubbub_parser_parse_chunk(parser, (const uint8_t *)prefix, strlen(prefix)), DOM_HUBBUB_OK);
        const char *input = select_cases[variant].input;
        for (size_t offset = 0, len = strlen(input); offset < len;) {
            size_t take = len - offset < chunk ? len - offset : chunk;
            ck_assert_int_eq(
                dom_hubbub_parser_parse_chunk(parser, (const uint8_t *)input + offset, take), DOM_HUBBUB_OK);
            offset += take;
        }
        ck_assert_int_eq(dom_hubbub_parser_completed(parser), DOM_HUBBUB_OK);
        dom_hubbub_parser_destroy(parser);
        ck_assert_int_eq(dom_html_document_get_body(parsed, &root), DOM_NO_ERR);
    }
    used = 0;
    tree[0] = 0;
    /* Compare the children, omitting either the body or fragment root. */
    dom_node *child = NULL;
    ck_assert_int_eq(dom_node_get_first_child(root, &child), DOM_NO_ERR);
    while (child != NULL) {
        describe(child);
        dom_node *next = NULL;
        ck_assert_int_eq(dom_node_get_next_sibling(child, &next), DOM_NO_ERR);
        dom_node_unref(child);
        child = next;
    }
    ck_assert_msg(strcmp(tree, select_cases[variant].expected) == 0, "%s, %s, chunk %zu: expected %s, got %s",
        select_cases[variant].label, mode < 2 ? "fragment" : "document", chunk, select_cases[variant].expected, tree);
    dom_node_unref(root);
    dom_node_unref(context);
    dom_node_unref(parsed);
}
END_TEST

static const struct {
    const char *space, *context, *encoding, *input, *expected;
} foreign_cases[] = {
    {"svg", "svg", NULL, "<g><path></path><circle/></g><rect/>",
        "<svg:g><svg:path></path><svg:circle></circle></g><svg:rect></rect>"},
    {"svg", "g", NULL, "<g><path></g><circle/>", "<svg:g><svg:path></path></g><svg:circle></circle>"},
    {"svg", "svg", NULL, "<g></unknown><path/></g><rect/>", "<svg:g><svg:path></path></g><svg:rect></rect>"},
    {"svg", "svg", NULL, "</svg><circle/>", "<svg:circle></circle>"},
    {"svg", "svg", NULL, "<g><div>X</div><circle/>", "<svg:g></g><div>X</div><svg:circle></circle>"},
    {"svg", "svg", NULL, "<foreignObject><p>X</p></foreignObject><circle/>",
        "<svg:foreignObject><p>X</p></foreignObject><svg:circle></circle>"},
    {"svg", "foreignObject", NULL, "<p>X</p><svg><path/></svg><span>Y</span>",
        "<p>X</p><svg:svg><svg:path></path></svg><span>Y</span>"},
    {"svg", "foreignobject", NULL, "<a>X</a>", "<svg:a>X</a>"},
    {"svg", "title", NULL, "<b>X</b>&amp;<a>Y</a>", "<b>X</b>&<a>Y</a>"},
    {"svg", "desc", NULL, "<span>X</span>", "<span>X</span>"},
    {"svg", "s:foreignObject", NULL, "<a>X</a>", "<a>X</a>"},
    {"svg", "svg", NULL, "<![CDATA[", ""},
    {"svg", "svg", NULL, "<![CDATA[]]>", ""},
    {"svg", "script", NULL, "<g/>&amp;", "<svg:g></g>&"},
    {"svg", "svg", NULL, "<font><g/></font><font color=red>X</font>", "<svg:font><svg:g></g></font><font>X</font>"},
    {"svg", "svg", NULL, "<g></p><circle/>", "<svg:g></g><p></p><svg:circle></circle>"},
    {"svg", "svg", NULL, "<g></br><circle/>", "<svg:g></g><br></br><svg:circle></circle>"},
    {"svg", "svg", NULL, "<lineargradient><fefuncg/></LINEARGRADIENT><fedropshadow/>",
        "<svg:linearGradient><svg:feFuncG></feFuncG></linearGradient><svg:feDropShadow></feDropShadow>"},
    {"svg", "svg", NULL, "<![CDATA[<x>&amp;]]><g/>", "<x>&amp;<svg:g></g>"},
    {"svg", "foreignObject", NULL, "<![CDATA[<x>]]><b>Y</b>", "<x><b>Y</b>"},
    {"svg", "svg", NULL, "<![CDATA[A]]]>B", "A]B"},
    {"svg", "svg", NULL, "<![CDATA[A]]]]>B", "A]]B"},
    {"svg", "svg", NULL, "<![CDATA[A]]", "A]]"},
    {"svg", "svg", NULL, "<![cdata[A]]><g/>", "<!--[cdata[A]]--><svg:g></g>"},
    {"svg", "svg", NULL, "<foreignObject><![CDATA[x]]><div><![CDATA[y]]></div><![CDATA[z]]></foreignObject>",
        "<svg:foreignObject>x<div><!--[CDATA[y]]--></div>z</foreignObject>"},
    {"math", "math", NULL, "<mrow><mn>1</mn><mi>x</mi></mrow>", "<math:mrow><math:mn>1</mn><math:mi>x</mi></mrow>"},
    {"math", "mi", NULL, "<b>X</b><mglyph/><malignmark/><span>Y</span>",
        "<b>X</b><math:mglyph></mglyph><math:malignmark></malignmark><span>Y</span>"},
    {"math", "mtext", NULL, "<svg><g/></svg>", "<svg:svg><svg:g></g></svg>"},
    {"math", "annotation-xml", "TeXt/HtMl", "<a>X</a><math><mi>x</mi></math>",
        "<a>X</a><math:math><math:mi>x</mi></math>"},
    {"math", "annotation-xml", "application/xhtml+xml", "<div>X</div>", "<div>X</div>"},
    {"math", "annotation-xml", "application/xml", "<a>X</a><svg><g/></svg>", "<math:a>X</a><svg:svg><svg:g></g></svg>"},
    {"math", "annotation-xml", NULL, "<svg><g/></svg><a>X</a>", "<svg:svg><svg:g></g></svg><math:a>X</a>"},
    {"math", "math", NULL, "<mrow><p>X</p><mi>Y</mi>", "<math:mrow></mrow><p>X</p><math:mi>Y</mi>"},
    {"math", "MATH", NULL, "<mi>X</mi>", "<math:mi>X</mi>"},
};

static const char *namespace_uri(const char *space)
{
    return strcmp(space, "svg") == 0 ? "http://www.w3.org/2000/svg" : "http://www.w3.org/1998/Math/MathML";
}

START_TEST(test_foreign_contexts)
{
    size_t count = sizeof(foreign_cases) / sizeof(foreign_cases[0]);
    size_t v = _i % count, chunk = _i < (int)count ? 4096 : 1;
    dom_string *ns = string(namespace_uri(foreign_cases[v].space));
    dom_string *name = string(foreign_cases[v].context);
    dom_element *context = NULL;
    ck_assert_int_eq(dom_document_create_element_ns(document, ns, name, &context), DOM_NO_ERR);
    if (foreign_cases[v].encoding != NULL) {
        dom_string *attr = string("encoding"), *value = string(foreign_cases[v].encoding);
        ck_assert_int_eq(dom_element_set_attribute(context, attr, value), DOM_NO_ERR);
        dom_string_unref(attr);
        dom_string_unref(value);
    }
    dom_node *fragment = parse((dom_node *)context, foreign_cases[v].input, chunk, true);
    used = 0;
    tree[0] = 0;
    describe_namespaces = true;
    describe(fragment);
    ck_assert_msg(strcmp(tree, foreign_cases[v].expected) == 0, "%s:%s case %zu, chunk %zu: expected %s, got %s",
        foreign_cases[v].space, foreign_cases[v].context, v, chunk, foreign_cases[v].expected, tree);
    describe_namespaces = false;
    dom_node_unref(fragment);
    dom_node_unref(context);
    dom_string_unref(ns);
    dom_string_unref(name);
}
END_TEST

/* Exercise the same token rules through normal documents and HTML fragments,
 * with both whole input and input split at every byte boundary. */
static dom_node *parse_body(const char *input, unsigned mode, dom_document **parsed)
{
    size_t chunk = mode & 1 ? 1 : 4096;
    if (mode < 2) {
        dom_node *context = element("div");
        dom_node *root = parse(context, input, chunk, true);
        dom_node_unref(context);
        return root;
    }
    dom_hubbub_parser_params params = {.enc = "UTF-8", .fix_enc = true, .enable_script = true};
    dom_hubbub_parser *parser = NULL;
    ck_assert_int_eq(dom_hubbub_parser_create(&params, &parser, parsed), DOM_HUBBUB_OK);
    const char *prefix = "<!doctype html><body>";
    ck_assert_int_eq(dom_hubbub_parser_parse_chunk(parser, (const uint8_t *)prefix, strlen(prefix)), DOM_HUBBUB_OK);
    for (size_t offset = 0, len = strlen(input); offset < len;) {
        size_t take = len - offset < chunk ? len - offset : chunk;
        ck_assert_int_eq(dom_hubbub_parser_parse_chunk(parser, (const uint8_t *)input + offset, take), DOM_HUBBUB_OK);
        offset += take;
    }
    ck_assert_int_eq(dom_hubbub_parser_completed(parser), DOM_HUBBUB_OK);
    dom_hubbub_parser_destroy(parser);
    dom_node *body = NULL;
    ck_assert_int_eq(dom_html_document_get_body(*parsed, &body), DOM_NO_ERR);
    return body;
}

static const struct {
    const char *label, *input, *expected;
} foreign_document_cases[] = {
    {"different unknown SVG names", "<svg><g><path></g><rect/></svg><p>end",
        "<svg:svg><svg:g><svg:path></path></g><svg:rect></rect></svg><p>end</p>"},
    {"different unknown HTML names", "<one><two></one><three>X</three>", "<one><two></two></one><three>X</three>"},
    {"literal colons in parser names", "<svg><foo:bar><foo:baz/></foo:bar><path/></svg><a:b:c>X</a:b:c>",
        "<svg:svg><svg:foo:bar><svg:foo:baz></foo:baz></foo:bar><svg:path></path></svg><a:b:c>X</a:b:c>"},
    {"HTML breakout", "<svg><g><p>X</p><span>Y</span>", "<svg:svg><svg:g></g></svg><p>X</p><span>Y</span>"},
    {"end br breakout", "<svg><g></br><p>X", "<svg:svg><svg:g></g></svg><br></br><p>X</p>"},
    {"end p breakout", "<svg><g></p><p>X", "<svg:svg><svg:g></g></svg><p></p><p>X</p>"},
    {"SVG integration points",
        "<svg><foreignObject><a>X</a></foreignObject><title><b>T</b></title><desc><span>D</span></desc></svg>",
        "<svg:svg><svg:foreignObject><a>X</a></foreignObject><svg:title><b>T</b></title><svg:desc><span>D</span></desc></svg>"},
    {"HTML ancestor end tag", "<div><svg><g></div><p>X", "<div><svg:svg><svg:g></g></svg></div><p>X</p>"},
    {"unknown HTML ancestor end tag", "<foo><svg><g></foo><p>X", "<foo><svg:svg><svg:g></g></svg></foo><p>X</p>"},
    {"foreignObject special boundary", "<foo><svg><foreignObject></foo><p>X</p></foreignObject></svg>Y",
        "<foo><svg:svg><svg:foreignObject><p>X</p></foreignObject></svg>Y</foo>"},
    {"foreign form-named elements", "<form><svg><input/><select/><button/><textarea/></svg></form>",
        "<form><svg:svg><svg:input></input><svg:select></select><svg:button></button><svg:textarea></textarea></svg></form>"},
    {"foreign font", "<svg><font><path/></font><font face=x>X</font>",
        "<svg:svg><svg:font><svg:path></path></font></svg><font>X</font>"},
    {"CDATA transitions",
        "<svg><![CDATA[A]]><foreignObject><![CDATA[B]]><div><![CDATA[C]]></div><![CDATA[D]]></foreignObject></svg><![CDATA[E]]>",
        "<svg:svg>A<svg:foreignObject>B<div><!--[CDATA[C]]--></div>D</foreignObject></svg><!--[CDATA[E]]-->"},
    {"CDATA brackets and empty text", "<svg><![CDATA[]]><![CDATA[A]]]>B<![CDATA[C]]]]>D</svg>",
        "<svg:svg>A]BC]]D</svg>"},
    {"CDATA newlines", "<svg><![CDATA[A\r\nB\rC]]></svg>", "<svg:svg>A\nB\nC</svg>"},
    {"MathML text integration", "<math><mrow><mi><b>X</b><mglyph/><malignmark/></mi><mn>1</mn></mrow></math>",
        "<math:math><math:mrow><math:mi><b>X</b><math:mglyph></mglyph><math:malignmark></malignmark></mi><math:mn>1</mn></mrow></math>"},
    {"annotation-xml HTML encoding",
        "<math><annotation-xml encoding='TEXT/HTML'><a>X</a></annotation-xml><mi>Y</mi></math>",
        "<math:math><math:annotation-xml><a>X</a></annotation-xml><math:mi>Y</mi></math>"},
    {"annotation-xml SVG transition", "<math><annotation-xml><svg><path/></svg><a>X</a></annotation-xml></math>",
        "<math:math><math:annotation-xml><svg:svg><svg:path></path></svg><math:a>X</a></annotation-xml></math>"},
    {"foreign names are not HTML special elements", "<b><svg><input></b><p>X",
        "<b><svg:svg><svg:input></input></svg></b><p>X</p>"},
    {"formatting inside an integration point", "<svg><foreignObject><b><p>A</b>B</p></foreignObject><path/></svg>",
        "<svg:svg><svg:foreignObject><b></b><p><b>A</b>B</p></foreignObject><svg:path></path></svg>"},
    {"restore table mode",
        "<table><tr><td><svg><foreignObject><table><tr><td>X</table></foreignObject></svg><td>Y</table>",
        "<table><tbody><tr><td><svg:svg><svg:foreignObject><table><tbody><tr><td>X</td></tr></tbody></table></foreignObject></svg></td><td>Y</td></tr></tbody></table>"},
};

START_TEST(test_foreign_document_and_fragment)
{
    const size_t count = sizeof(foreign_document_cases) / sizeof(foreign_document_cases[0]);
    size_t v = _i % count, mode = _i / count;
    dom_document *parsed = NULL;
    dom_node *root = parse_body(foreign_document_cases[v].input, mode, &parsed);
    used = 0;
    tree[0] = 0;
    describe_namespaces = true;
    dom_node *child = NULL;
    ck_assert_int_eq(dom_node_get_first_child(root, &child), DOM_NO_ERR);
    while (child != NULL) {
        describe(child);
        dom_node *next = NULL;
        ck_assert_int_eq(dom_node_get_next_sibling(child, &next), DOM_NO_ERR);
        dom_node_unref(child);
        child = next;
    }
    ck_assert_msg(strcmp(tree, foreign_document_cases[v].expected) == 0, "%s, %s, chunk %u: expected %s, got %s",
        foreign_document_cases[v].label, mode < 2 ? "fragment" : "document", mode & 1 ? 1 : 4096,
        foreign_document_cases[v].expected, tree);
    describe_namespaces = false;
    dom_node_unref(root);
    dom_node_unref(parsed);
}
END_TEST

static void check_attribute(dom_node *node, const char *uri, const char *local, const char *prefix, const char *value)
{
    dom_string *ns = uri == NULL ? NULL : string(uri), *name = string(local);
    dom_attr *attr = NULL;
    ck_assert_int_eq(dom_element_get_attribute_node_ns(node, ns, name, &attr), DOM_NO_ERR);
    ck_assert_msg(attr != NULL, "Missing attribute {%s}%s", uri == NULL ? "" : uri, local);
    dom_string *actual = NULL;
    ck_assert_int_eq(dom_node_get_local_name(attr, &actual), DOM_NO_ERR);
    ck_assert_str_eq(dom_string_data(actual), local);
    dom_string_unref(actual);
    ck_assert_int_eq(dom_node_get_prefix(attr, &actual), DOM_NO_ERR);
    if (prefix == NULL)
        ck_assert_ptr_null(actual);
    else
        ck_assert_str_eq(dom_string_data(actual), prefix);
    dom_string_unref(actual);
    ck_assert_int_eq(dom_attr_get_value(attr, &actual), DOM_NO_ERR);
    ck_assert_str_eq(dom_string_data(actual), value);
    dom_string_unref(actual);
    dom_node_unref(attr);
    dom_string_unref(ns);
    dom_string_unref(name);
}

START_TEST(test_foreign_names_and_attributes)
{
    dom_document *parsed = NULL;
    dom_node *root = parse_body(
        "<svg viewbox='0 0 1 1' xmlns='http://www.w3.org/2000/svg' "
        "xmlns:xlink='http://www.w3.org/1999/xlink' xlink:href='#x' xml:space='preserve' custom:x='v' =odd='x'>"
        "<foo:bar/></svg><math definitionurl='url'></math><div xlink:href='plain'></div>",
        _i, &parsed);
    dom_node *svg = NULL, *child = NULL, *math = NULL, *div = NULL;
    ck_assert_int_eq(dom_node_get_first_child(root, &svg), DOM_NO_ERR);
    check_attribute(svg, NULL, "viewBox", NULL, "0 0 1 1");
    check_attribute(svg, "http://www.w3.org/2000/xmlns/", "xmlns", NULL, "http://www.w3.org/2000/svg");
    check_attribute(svg, "http://www.w3.org/2000/xmlns/", "xlink", "xmlns", "http://www.w3.org/1999/xlink");
    check_attribute(svg, "http://www.w3.org/1999/xlink", "href", "xlink", "#x");
    check_attribute(svg, "http://www.w3.org/XML/1998/namespace", "space", "xml", "preserve");
    check_attribute(svg, NULL, "custom:x", NULL, "v");
    check_attribute(svg, NULL, "=odd", NULL, "x");
    ck_assert_int_eq(dom_node_get_first_child(svg, &child), DOM_NO_ERR);
    dom_string *name = NULL;
    ck_assert_int_eq(dom_node_get_local_name(child, &name), DOM_NO_ERR);
    ck_assert_str_eq(dom_string_data(name), "foo:bar");
    dom_string_unref(name);
    ck_assert_int_eq(dom_node_get_prefix(child, &name), DOM_NO_ERR);
    ck_assert_ptr_null(name);
    ck_assert_int_eq(dom_node_get_next_sibling(svg, &math), DOM_NO_ERR);
    check_attribute(math, NULL, "definitionURL", NULL, "url");
    ck_assert_int_eq(dom_node_get_next_sibling(math, &div), DOM_NO_ERR);
    check_attribute(div, NULL, "xlink:href", NULL, "plain");
    dom_node_unref(child);
    dom_node_unref(svg);
    dom_node_unref(math);
    dom_node_unref(div);
    dom_node_unref(root);
    dom_node_unref(parsed);
}
END_TEST

/* The parser's resolved-name constructors must not relax the public DOM APIs. */
START_TEST(test_public_namespace_validation)
{
    dom_string *ns = string("http://www.w3.org/2000/svg"), *qname = string("a:b:c");
    dom_element *node = NULL;
    ck_assert_int_eq(dom_document_create_element_ns(document, ns, qname, &node), DOM_NAMESPACE_ERR);
    dom_string_unref(qname);
    qname = string("s:svg");
    ck_assert_int_eq(dom_document_create_element_ns(document, ns, qname, &node), DOM_NO_ERR);
    dom_string *prefix = NULL;
    ck_assert_int_eq(dom_node_get_prefix(node, &prefix), DOM_NO_ERR);
    ck_assert_str_eq(dom_string_data(prefix), "s");
    dom_string_unref(prefix);
    dom_string_unref(qname);
    qname = string("=odd");
    dom_string *value = string("v");
    ck_assert_int_eq(dom_element_set_attribute(node, qname, value), DOM_INVALID_CHARACTER_ERR);
    dom_string_unref(qname);
    dom_string_unref(ns);
    ns = string("http://www.w3.org/1999/xlink");
    qname = string("xlink:href");
    ck_assert_int_eq(dom_element_set_attribute_ns(node, ns, qname, value), DOM_NO_ERR);
    check_attribute((dom_node *)node, dom_string_data(ns), "href", "xlink", "v");
    dom_string_unref(qname);
    qname = string("other:href");
    ck_assert_int_eq(dom_element_set_attribute_ns(node, ns, qname, value), DOM_NO_ERR);
    /* Setting an existing attribute value preserves its original prefix. */
    check_attribute((dom_node *)node, dom_string_data(ns), "href", "xlink", "v");
    dom_string_unref(qname);
    dom_string_unref(value);
    dom_string_unref(ns);
    dom_node_unref(node);
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
    ck_assert_int_eq(
        dom_hubbub_fragment_parser_create(&params, (dom_element *)context, &parser, &fragment), DOM_HUBBUB_OK);
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

static unsigned html_script_count, svg_count;

static dom_hubbub_error count_html_script(void *ctx, dom_node *node)
{
    dom_string *ns = NULL;
    ck_assert_int_eq(dom_node_get_namespace(node, &ns), DOM_NO_ERR);
    ck_assert_str_eq(dom_string_data(ns), "http://www.w3.org/1999/xhtml");
    dom_string_unref(ns);
    html_script_count++;
    return DOM_HUBBUB_OK;
}

static dom_hubbub_error count_svg(void *ctx, dom_node *node)
{
    svg_count++;
    return DOM_HUBBUB_OK;
}

START_TEST(test_foreign_callbacks_and_lifetime)
{
    bool fragment_mode = _i < 2, complete = (_i & 1) == 0;
    dom_hubbub_parser_params params = {
        .enc = "UTF-8", .fix_enc = true, .enable_script = true, .script = count_html_script, .svg = count_svg};
    dom_hubbub_parser *parser = NULL;
    dom_document *parsed = NULL;
    dom_document_fragment *fragment = NULL;
    dom_element *context = NULL;
    html_script_count = svg_count = 0;
    if (fragment_mode) {
        dom_string *ns = string(namespace_uri("svg")), *name = string("svg");
        ck_assert_int_eq(dom_document_create_element_ns(document, ns, name, &context), DOM_NO_ERR);
        dom_string_unref(ns);
        dom_string_unref(name);
        ck_assert_int_eq(dom_hubbub_fragment_parser_create(&params, context, &parser, &fragment), DOM_HUBBUB_OK);
        /* No later token processing may rely on this context node remaining alive. */
        dom_node_unref(context);
    } else {
        ck_assert_int_eq(dom_hubbub_parser_create(&params, &parser, &parsed), DOM_HUBBUB_OK);
    }
    const char *input = fragment_mode
        ? "<svg><script>x</script><script/><foreignObject><script>y</script></foreignObject></svg><g>open"
        : "<!doctype html><svg><script>x</script><script/></svg><script>y</script><svg><g>open";
    for (size_t i = 0; input[i]; i++)
        ck_assert_int_eq(dom_hubbub_parser_parse_chunk(parser, (const uint8_t *)input + i, 1), DOM_HUBBUB_OK);
    if (complete)
        ck_assert_int_eq(dom_hubbub_parser_completed(parser), DOM_HUBBUB_OK);
    ck_assert_uint_eq(html_script_count, fragment_mode ? 0 : 1);
    ck_assert_uint_eq(svg_count, fragment_mode ? 0 : 1);
    dom_node_unref(fragment);
    dom_node_unref(parsed);
    dom_hubbub_parser_destroy(parser);
}
END_TEST

START_TEST(test_foreign_cdata_null)
{
    dom_string *ns = string(namespace_uri("svg")), *name = string("svg");
    dom_element *context = NULL;
    ck_assert_int_eq(dom_document_create_element_ns(document, ns, name, &context), DOM_NO_ERR);
    dom_hubbub_parser *parser = NULL;
    dom_document_fragment *fragment = NULL;
    dom_hubbub_parser_params params = {0};
    ck_assert_int_eq(dom_hubbub_fragment_parser_create(&params, context, &parser, &fragment), DOM_HUBBUB_OK);
    const char input[] = "<![CDATA[A\0B]]><g/>";
    size_t chunk = _i ? 1 : sizeof(input) - 1;
    for (size_t offset = 0; offset < sizeof(input) - 1;) {
        size_t take = sizeof(input) - 1 - offset < chunk ? sizeof(input) - 1 - offset : chunk;
        ck_assert_int_eq(dom_hubbub_parser_parse_chunk(parser, (const uint8_t *)input + offset, take), DOM_HUBBUB_OK);
        offset += take;
    }
    ck_assert_int_eq(dom_hubbub_parser_completed(parser), DOM_HUBBUB_OK);
    used = 0;
    tree[0] = 0;
    describe_namespaces = true;
    describe((dom_node *)fragment);
    ck_assert_str_eq(tree,
        "A\xEF\xBF\xBD"
        "B<svg:g></g>");
    describe_namespaces = false;
    dom_hubbub_parser_destroy(parser);
    dom_node_unref(fragment);
    dom_node_unref(context);
    dom_string_unref(ns);
    dom_string_unref(name);
}
END_TEST

START_TEST(test_foreign_descendant)
{
    dom_node *context = element("div");
    dom_node *fragment = parse(
        context, "<svg><linearGradient viewBox='0 0 1 1'></linearGradient></svg><p>end", 1, true);
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
    dom_string *ns = string("urn:unsupported"), *name = string("svg");
    dom_element *context = NULL;
    if (_i == 0)
        ck_assert_int_eq(dom_document_create_element_ns(document, ns, name, &context), DOM_NO_ERR);
    else
        context = (dom_element *)element("template");
    dom_hubbub_parser_params params = {0};
    dom_hubbub_parser *parser = NULL;
    dom_document_fragment *fragment = NULL;
    ck_assert_int_eq(
        dom_hubbub_fragment_parser_create(&params, context, &parser, &fragment), DOM_HUBBUB_HUBBUB_ERR_INVALID);
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
    tcase_add_loop_test(tc, test_foreign_callbacks_and_lifetime, 0, 4);
    tcase_add_loop_test(tc, test_foreign_cdata_null, 0, 2);
    tcase_add_loop_test(tc, test_foreign_document_and_fragment, 0,
        4 * (sizeof(foreign_document_cases) / sizeof(foreign_document_cases[0])));
    tcase_add_loop_test(tc, test_foreign_names_and_attributes, 0, 4);
    tcase_add_test(tc, test_public_namespace_validation);
    tcase_add_loop_test(tc, test_foreign_contexts, 0, 2 * (sizeof(foreign_cases) / sizeof(foreign_cases[0])));
    tcase_add_loop_test(tc, test_unsupported_context, 0, 2);
    tcase_add_loop_test(tc, test_select_document_and_fragment, 0, 4 * (sizeof(select_cases) / sizeof(select_cases[0])));
    suite_add_tcase(suite, tc);
    SRunner *runner = srunner_create(suite);
    srunner_run_all(runner, CK_NORMAL);
    int failed = srunner_ntests_failed(runner);
    srunner_free(runner);
    return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

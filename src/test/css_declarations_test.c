/* Accepted inline declarations must retain parser output without changing the
 * compiled style. This exercises the real generated and manual property parsers. */
#include <check.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libcss/font_face.h>

#include "parse/declarations.h"
#include "parse/declaration_order.h"
#include "parse/declaration_value.h"
#include "parse/language.h"
#include "parse/property.h"
#include "stylesheet.h"
#include "bytecode/opcodes.h"
#include "utils/css_utils.h"

#ifdef WISP_TEST_ALLOC_FAILURE
#include "test/malloc_fig.h"
#endif

static css_error resolve_url(void *pw, const char *base, lwc_string *rel, lwc_string **abs)
{
    (void)pw;
    (void)base;
    *abs = lwc_string_ref(rel);
    return CSS_OK;
}

static const css_stylesheet_params params = {.params_version = CSS_STYLESHEET_PARAMS_VERSION_1,
    .level = CSS_LEVEL_DEFAULT,
    .charset = "UTF-8",
    .url = "https://example.org/styles/",
    .inline_style = true,
    .resolve = resolve_url};

static const struct {
    const char *css;
    const char *names[8];
    unsigned important;
} cases[] = {
    {"", {NULL}, 0},
    {" /* nothing */ ; ; ", {NULL}, 0},
    {"opacity:.5; color:red", {"opacity", "color"}, 0},
    {"CoLoR: red !IMPORTANT; WIDTH: 3em", {"color", "width"}, 1},
    {"width:1px; width:2px!important; width:3px", {"width", "width", "width"}, 2},
    {"width:nope; opacity:; unknown-property:1; color:red; height:2px junk; width:7%", {"color", "width"}, 0},
    {"margin:1px 2em 3% 0; margin-left:auto; padding:0", {"margin", "margin-left", "padding"}, 0},
    {"border:1px solid red!important; border-left-width:2px", {"border", "border-left-width"}, 1},
    {"margin-inline:2px; aspect-ratio:16/9; display:flex", {"margin-inline", "aspect-ratio", "display"}, 0},
    {"--Tone:red; --tone:blue; color:var(--Tone, green)", {"--Tone", "--tone", "color"}, 0},
    {"--empty:; --space: /**/ ; --v:var(--missing,)!important", {"--empty", "--space", "--v"}, 4},
    {"--a:'!important'; --b:fn(!important); --c:important; --d:red ! important", {"--a", "--b", "--c", "--d"}, 8},
    {"color:red !bogus; color:blue!important trailing; opacity:0", {"opacity"}, 0},
    {"width:calc(100% - 2em); height:calc(10px + 3em)", {"width", "height"}, 0},
    {"content:'a;:b\\\"c' attr(title); font-family:'A;B',sans-serif", {"content", "font-family"}, 0},
    {"background-image:url('tile;a:b.png'); color:rgb(1,2,3)", {"background-image", "color"}, 0},
    {"\\63 olor: red; --\\54 one: blue; --é: 2px", {"color", "--Tone", "--é"}, 0},
    {"color: inherit; width: initial; display: unset", {"color", "width", "display"}, 0},
    {"color:var(--bad, red) !important; margin:var(--space, 1px 2px)", {"color", "margin"}, 1},
    {"\\2d -Tone:Red; -\\2d tone:Blue; --9:7px; ---:; --:bad; color:var(--9)",
        {"--Tone", "--tone", "--9", "---", "color"}, 0},
    {"opacity:.5; @import 'no.css'; height:2px", {"opacity", "height"}, 0},
    {"font-family:ubuntu,urban; --u:U+400-4FF; --fn:u(red)", {"font-family", "--u", "--fn"}, 0},
    /* High-bit masking used to turn these valid Unicode identifiers into
     * grid-row and margin-left. Neither is a supported CSS property name. */
    {"griĭrow:2; margiέleft:1px; margiέleft:var(--x); color:red", {"color"}, 0},
};
enum { CASE_COUNT = sizeof(cases) / sizeof(cases[0]) };

static void check_append(css_error error)
{
    ck_assert_msg(error == CSS_OK || error == CSS_NEEDDATA, "append failed: %d", error);
}

static css_declaration_list *parse(const char *css, bool chunks)
{
    css_declaration_list *list;
    ck_assert_int_eq(css__declaration_list_create(&params, &list), CSS_OK);
    /* The collector must own its tokens even after this buffer is overwritten,
     * the input stream grows, and completion destroys the lexer/parser. */
    char *input = strdup(css);
    ck_assert_ptr_nonnull(input);
    size_t len = strlen(input);
    if (chunks) {
        for (size_t i = 0; i < len; i++) {
            check_append(css__declaration_list_append(list, (const uint8_t *)input + i, 1));
            input[i] = '?';
        }
    } else {
        check_append(css__declaration_list_append(list, (const uint8_t *)input, len));
        memset(input, '?', len);
    }
    free(input);
    ck_assert_uint_eq(css__declaration_list_count(list), 0);
    ck_assert_ptr_null(css__declaration_list_get(list, 0));
    ck_assert_ptr_null(css__declaration_list_sheet(list));
    ck_assert_int_eq(css__declaration_list_complete(list), CSS_OK);
    return list;
}

static void compare_strings(lwc_string *a, lwc_string *b)
{
    if (a == NULL || b == NULL) {
        ck_assert_ptr_eq(a, b);
    } else {
        ck_assert_uint_eq(lwc_string_length(a), lwc_string_length(b));
        ck_assert_int_eq(memcmp(lwc_string_data(a), lwc_string_data(b), lwc_string_length(a)), 0);
    }
}

static const css_style *inline_style(const css_stylesheet *sheet)
{
    ck_assert_ptr_nonnull(sheet->rule_list);
    ck_assert_int_eq(sheet->rule_list->type, CSS_RULE_SELECTOR);
    ck_assert_ptr_null(sheet->rule_list->next);
    return ((const css_rule_selector *)sheet->rule_list)->style;
}

static void compare_sheets(const css_stylesheet *a, const css_stylesheet *b)
{
    const css_style *style_a = inline_style(a);
    const css_style *style_b = inline_style(b);
    if (style_a == NULL || style_b == NULL) {
        ck_assert_ptr_eq(style_a, style_b);
    } else {
        ck_assert_uint_eq(style_a->used, style_b->used);
        ck_assert_int_eq(memcmp(style_a->bytecode, style_b->bytecode, style_a->used * sizeof(css_code_t)), 0);
    }
    ck_assert_uint_eq(a->string_vector_c, b->string_vector_c);
    for (uint32_t i = 0; i < a->string_vector_c; i++)
        compare_strings(a->string_vector[i], b->string_vector[i]);
    ck_assert_uint_eq(a->var_values_count, b->var_values_count);
}

START_TEST(test_declarations)
{
    size_t index = (size_t)_i;
    css_declaration_list *list = parse(cases[index].css, false);
    css_declaration_list *chunks = parse(cases[index].css, true);
    size_t expected = 0;
    while (expected < 8 && cases[index].names[expected] != NULL)
        expected++;
    if (css__declaration_list_count(list) != expected || css__declaration_list_count(chunks) != expected) {
        fprintf(stderr, "case %zu: whole:", index);
        for (size_t i = 0; i < css__declaration_list_count(list); i++)
            fprintf(stderr, " %s", lwc_string_data(css__declaration_list_get(list, i)->name));
        fprintf(stderr, "; chunks:");
        for (size_t i = 0; i < css__declaration_list_count(chunks); i++)
            fprintf(stderr, " %s", lwc_string_data(css__declaration_list_get(chunks, i)->name));
        fputc('\n', stderr);
    }
    ck_assert_msg(css__declaration_list_count(list) == expected, "case %zu: %s: expected %zu, got %zu", index,
        cases[index].css, expected, css__declaration_list_count(list));
    ck_assert_uint_eq(css__declaration_list_count(chunks), expected);
    uint32_t end = 0;
    for (size_t i = 0; i < expected; i++) {
        const css_declaration *a = css__declaration_list_get(list, i);
        const css_declaration *b = css__declaration_list_get(chunks, i);
        ck_assert_str_eq(lwc_string_data(a->name), cases[index].names[i]);
        ck_assert_msg(
            a->important == !!(cases[index].important & (1u << i)), "case %zu declaration %zu priority", index, i);
        compare_strings(a->name, b->name);
        compare_strings(a->source_value, b->source_value);
        ck_assert_ptr_eq(a->property, b->property);
        ck_assert_int_eq(a->deferred, b->deferred);
        ck_assert_uint_eq(a->value_start, b->value_start);
        ck_assert_uint_eq(a->value_end, b->value_end);
        if (strncmp(lwc_string_data(a->name), "--", 2) == 0) {
            ck_assert_ptr_null(a->property);
        } else {
            ck_assert_ptr_nonnull(a->property);
            ck_assert_str_eq(a->property->name, lwc_string_data(a->name));
        }
        ck_assert_int_eq(a->important, b->important);
        ck_assert_uint_eq(a->token_count, b->token_count);
        ck_assert_uint_eq(a->bytecode_offset, end);
        ck_assert_uint_gt(a->bytecode_length, 0);
        ck_assert_uint_eq(a->bytecode_offset, b->bytecode_offset);
        ck_assert_uint_eq(a->bytecode_length, b->bytecode_length);
        end += a->bytecode_length;
        for (size_t j = 0; j < a->token_count; j++) {
            ck_assert_int_eq(a->tokens[j].type, b->tokens[j].type);
            compare_strings(a->tokens[j].text, b->tokens[j].text);
        }
    }
    ck_assert_ptr_null(css__declaration_list_get(list, expected));
    const css_stylesheet *sheet = css__declaration_list_sheet(list);
    const css_style *style = inline_style(sheet);
    ck_assert_uint_eq(end, style != NULL ? style->used : 0);
    compare_sheets(sheet, css__declaration_list_sheet(chunks));

    css_stylesheet *ordinary;
    ck_assert_int_eq(css_stylesheet_create(&params, &ordinary), CSS_OK);
    ck_assert_ptr_null(((css_language *)ordinary->parser_frontend)->declarations);
    check_append(css_stylesheet_append_data(ordinary, (const uint8_t *)cases[index].css, strlen(cases[index].css)));
    ck_assert_int_eq(css_stylesheet_data_done(ordinary), CSS_OK);
    compare_sheets(sheet, ordinary);
    css_stylesheet_destroy(ordinary);
    css__declaration_list_destroy(list);
    css__declaration_list_destroy(chunks);
}
END_TEST

static const struct {
    const char *input;
    css_token_type type;
    const char *text;
} lexer_cases[] = {
    {"url('tile;a:b.png')", CSS_TOKEN_URI, "tile;a:b.png"},
    {"URL(tile.png)", CSS_TOKEN_URI, "tile.png"},
    {"url()", CSS_TOKEN_URI, ""},
    {"url(  )", CSS_TOKEN_URI, ""},
    {"url( '' )", CSS_TOKEN_URI, ""},
    {"url(' foo ')", CSS_TOKEN_URI, " foo "},
    {"url(\\ foo)", CSS_TOKEN_URI, " foo"},
    {"url(foo\\ )", CSS_TOKEN_URI, "foo "},
    {"url(foo\\' )", CSS_TOKEN_URI, "foo'"},
    {"url(\\\"foo)", CSS_TOKEN_URI, "\"foo"},
    {"url(\\22 foo\\22 )", CSS_TOKEN_URI, "\"foo\""},
    {"url(\\20 foo\\20 )", CSS_TOKEN_URI, " foo "},
    {"url('a\\\"b\\\\c')", CSS_TOKEN_URI, "a\"b\\c"},
    {"url(Știri💡.png)", CSS_TOKEN_URI, "Știri💡.png"},
    {"'\\0 \\d800 \\110000 '", CSS_TOKEN_STRING, "\xef\xbf\xbd\xef\xbf\xbd\xef\xbf\xbd"},
    {"'\\1 \\b \\d \\1f \\7f '", CSS_TOKEN_STRING, "\x01\x0b\x0d\x1f\x7f"},
    {"'\\80 \\9f \\fdd0 \\fffe \\1ffff '", CSS_TOKEN_STRING,
        "\xc2\x80\xc2\x9f\xef\xb7\x90\xef\xbf\xbe\xf0\x9f\xbf\xbf"},
    {"u", CSS_TOKEN_IDENT, "u"},
    {"ubuntu", CSS_TOKEN_IDENT, "ubuntu"},
    {"urban", CSS_TOKEN_IDENT, "urban"},
    {"u(", CSS_TOKEN_FUNCTION, "u"},
    {"U+400-4FF", CSS_TOKEN_UNICODE_RANGE, "400-4FF"},
    {"--", CSS_TOKEN_IDENT, "--"},
    {"--9", CSS_TOKEN_IDENT, "--9"},
    {"---", CSS_TOKEN_IDENT, "---"},
    {"\\2d -Tone", CSS_TOKEN_IDENT, "--Tone"},
    {"-\\2d Tone", CSS_TOKEN_IDENT, "--Tone"},
    {"--fn(", CSS_TOKEN_FUNCTION, "--fn"},
    {"-->", CSS_TOKEN_CDC, "-->"},
    {"--\\54 one", CSS_TOKEN_IDENT, "--Tone"},
    {"--\\Theme", CSS_TOKEN_IDENT, "--Theme"},
    {"'", CSS_TOKEN_STRING, ""},
    {"'unfinished", CSS_TOKEN_STRING, "unfinished"},
    {"'literal\"", CSS_TOKEN_STRING, "literal\""},
    {"\"literal'", CSS_TOKEN_STRING, "literal'"},
    {"'escaped\\'", CSS_TOKEN_STRING, "escaped'"},
    {"'escaped\\27", CSS_TOKEN_STRING, "escaped'"},
    {"'trailing\\", CSS_TOKEN_STRING, "trailing"},
    {"'\\", CSS_TOKEN_STRING, ""},
    {"'a\\\nb'", CSS_TOKEN_STRING, "ab"},
    {"'a\\\r\nb'", CSS_TOKEN_STRING, "ab"},
    {"'a\\\rb'", CSS_TOKEN_STRING, "ab"},
    {"'a\\\fb'", CSS_TOKEN_STRING, "ab"},
    {"'a\\\r", CSS_TOKEN_STRING, "a"},
    {"'a\\\r\n", CSS_TOKEN_STRING, "a"},
    {"'a\\\n\\\rb'", CSS_TOKEN_STRING, "ab"},
};
enum { LEXER_CASE_COUNT = sizeof(lexer_cases) / sizeof(lexer_cases[0]) };

START_TEST(test_lexer_resume)
{
    const char *input = lexer_cases[_i].input;
    size_t length = strlen(input);
    /* Every two-chunk split, including immediately after 'u' and in escapes.
     * Check the lexer directly too, independently of declaration capture. */
    for (size_t split = 0; split < length; split++) {
        parserutils_inputstream *stream;
        css_lexer *lexer;
        css_token *token;
        ck_assert_int_eq(parserutils_inputstream_create("UTF-8", 0, NULL, &stream), PARSERUTILS_OK);
        ck_assert_int_eq(css__lexer_create(stream, &lexer), CSS_OK);
        ck_assert_int_eq(parserutils_inputstream_append(stream, (const uint8_t *)input, split), PARSERUTILS_OK);
        ck_assert_int_eq(css__lexer_get_token(lexer, &token), CSS_NEEDDATA);
        ck_assert_int_eq(
            parserutils_inputstream_append(stream, (const uint8_t *)input + split, length - split), PARSERUTILS_OK);
        ck_assert_int_eq(parserutils_inputstream_append(stream, NULL, 0), PARSERUTILS_OK);
        ck_assert_int_eq(css__lexer_get_token(lexer, &token), CSS_OK);
        ck_assert_msg(token->type == lexer_cases[_i].type, "token %s split %zu: expected %d, got %d", input, split,
            lexer_cases[_i].type, token->type);
        ck_assert_uint_eq(token->data.len, strlen(lexer_cases[_i].text));
        ck_assert_int_eq(memcmp(token->data.data, lexer_cases[_i].text, token->data.len), 0);
        ck_assert_int_eq(css__lexer_get_token(lexer, &token), CSS_OK);
        ck_assert_int_eq(token->type, CSS_TOKEN_EOF);
        css__lexer_destroy(lexer);
        parserutils_inputstream_destroy(stream);
    }
}
END_TEST

static const struct {
    const char *css;
    const char *name;
    const char *value;
    bool important;
} source_cases[] = {
    {"--x:  a\t b  ;", "--x", "  a\t b  ", false},
    {"--x: /* leading */ a/**/b /* trailing */ ;", "--x", " /* leading */ a/**/b /* trailing */ ", false},
    {"--x:\\41  \\000042 \\; \\!important;", "--x", "\\41  \\000042 \\; \\!important", false},
    {"--x: 'a;:b\\\"c' \"!important\";", "--x", " 'a;:b\\\"c' \"!important\"", false},
    {"--x: red /* value */ !/**/IMPORTANT /* priority */;", "--x", " red /* value */ ", true},
    {"--x: /* !important; */;", "--x", " /* !important; */", false},
    {"--x:;", "--x", "", false},
    {"--x: \t ! important;", "--x", " \t ", true},
    {"--x:fn(!important);", "--x", "fn(!important)", false},
    {"--x: url( 'data:a;b:c' ) ;", "--x", " url( 'data:a;b:c' ) ", false},
    {"--x: 12345678-12e3-8d9b-a456-426614174000;", "--x", " 12345678-12e3-8d9b-a456-426614174000", false},
    {"--é: 'Știri 💡' /**/ ;", "--é", " 'Știri 💡' /**/ ", false},
    {"--\\54 one: BLUE !important;", "--Tone", " BLUE ", true},
    {"--x: red /* unfinished  ", "--x", " red /* unfinished  ", false},
    {"--x: /*", "--x", " /*", false},
    {"--x: 'unfinished", "--x", " 'unfinished", false},
    {"content: 'a\\\r\nb'", "content", " 'a\\\r\nb'", false},
    {"--x: 'escaped\\'", "--x", " 'escaped\\'", false},
    {"content: 'unfinished", "content", " 'unfinished", false},
    {"unknown:a; color:invalid; color: RGB(1, 2, 3) !important; color:wrong;", "color", " RGB(1, 2, 3) ", true},
    {"margin: var(--space, 1px  2px) /* end */;", "margin", " var(--space, 1px  2px) /* end */", false},
    {"\xef\xbb\xbf" "color:red; --x:blue", "--x", "blue", false},
};
enum { SOURCE_CASE_COUNT = sizeof(source_cases) / sizeof(source_cases[0]) };

START_TEST(test_source_value)
{
    for (int chunks = 0; chunks < 2; chunks++) {
        css_declaration_list *list = parse(source_cases[_i].css, chunks);
        ck_assert_msg(css__declaration_list_count(list) == 1, "source case %d, chunks %d: got %zu declarations",
            _i, chunks, css__declaration_list_count(list));
        const css_declaration *entry = css__declaration_list_get(list, 0);
        ck_assert_str_eq(lwc_string_data(entry->name), source_cases[_i].name);
        ck_assert_ptr_nonnull(entry->source_value);
        ck_assert_uint_eq(lwc_string_length(entry->source_value), strlen(source_cases[_i].value));
        ck_assert_str_eq(lwc_string_data(entry->source_value), source_cases[_i].value);
        ck_assert_int_eq(entry->important, source_cases[_i].important);
        css__declaration_list_destroy(list);
    }
}
END_TEST

START_TEST(test_source_growth)
{
    /* Force both the input stream and captured-source buffers to move within
     * a single comment and a single string, then check following offsets. */
    enum { RUN = 8192 };
    char css[2 * RUN + 128];
    size_t pos = 0;
    pos += (size_t)sprintf(css + pos, "--long: /*");
    memset(css + pos, 'c', RUN);
    pos += RUN;
    pos += (size_t)sprintf(css + pos, "*/ '\\41 ");
    memset(css + pos, 's', RUN);
    pos += RUN;
    pos += (size_t)sprintf(css + pos, "'; color:blue");
    for (int chunks = 0; chunks < 2; chunks++) {
        css_declaration_list *list = parse(css, chunks);
        ck_assert_uint_eq(css__declaration_list_count(list), 2);
        const css_declaration *long_value = css__declaration_list_get(list, 0);
        const size_t start = strlen("--long:");
        const size_t end = pos - strlen("; color:blue");
        ck_assert_uint_eq(lwc_string_length(long_value->source_value), end - start);
        ck_assert_int_eq(memcmp(lwc_string_data(long_value->source_value), css + start, end - start), 0);
        ck_assert_uint_eq(long_value->token_count, 1);
        ck_assert_uint_eq(lwc_string_length(long_value->tokens[0].text), RUN + 1);
        ck_assert_int_eq(lwc_string_data(long_value->tokens[0].text)[0], 'A');
        ck_assert_str_eq(lwc_string_data(css__declaration_list_get(list, 1)->source_value), "blue");
        css__declaration_list_destroy(list);
    }
}
END_TEST

START_TEST(test_owned_tokens)
{
    css_declaration_list *list = parse("--Name: F(\"a;b:c\", 2px) ! IMPORTANT; --empty:; opacity:.5", true);
    const css_declaration *entry = css__declaration_list_get(list, 0);
    ck_assert_ptr_nonnull(entry);
    ck_assert(entry->important);
    const css_token_type types[] = {
        CSS_TOKEN_FUNCTION, CSS_TOKEN_STRING, CSS_TOKEN_CHAR, CSS_TOKEN_S, CSS_TOKEN_DIMENSION, CSS_TOKEN_CHAR};
    const char *texts[] = {"F", "a;b:c", ",", NULL, "2px", ")"};
    ck_assert_uint_eq(entry->token_count, sizeof(types) / sizeof(types[0]));
    for (size_t i = 0; i < entry->token_count; i++) {
        ck_assert_int_eq(entry->tokens[i].type, types[i]);
        if (texts[i] == NULL)
            ck_assert_ptr_null(entry->tokens[i].text);
        else
            ck_assert_str_eq(lwc_string_data(entry->tokens[i].text), texts[i]);
    }
    entry = css__declaration_list_get(list, 1);
    ck_assert_uint_eq(entry->token_count, 0);
    ck_assert(!entry->important);
    entry = css__declaration_list_get(list, 2);
    ck_assert_uint_eq(entry->token_count, 1);
    ck_assert_int_eq(entry->tokens[0].type, CSS_TOKEN_NUMBER);
    ck_assert_str_eq(lwc_string_data(entry->tokens[0].text), ".5");
    css__declaration_list_destroy(list);
}
END_TEST

START_TEST(test_lifecycle)
{
    css_declaration_list *list = (void *)1;
    ck_assert_int_eq(css__declaration_list_create(NULL, &list), CSS_BADPARM);
    ck_assert_ptr_null(list);
    ck_assert_int_eq(css__declaration_list_create(&params, NULL), CSS_BADPARM);
    ck_assert_int_eq(css__declaration_list_append(NULL, (const uint8_t *)"", 0), CSS_BADPARM);
    ck_assert_int_eq(css__declaration_list_complete(NULL), CSS_BADPARM);
    ck_assert_ptr_null(css__declaration_list_get(NULL, 0));
    ck_assert_ptr_null(css__declaration_list_sheet(NULL));
    css__declaration_list_destroy(NULL);

    css_stylesheet_params overridden = params;
    overridden.inline_style = false;
    overridden.charset = "invalid-charset";
    ck_assert_int_eq(css__declaration_list_create(&overridden, &list), CSS_OK);
    ck_assert_int_eq(css__declaration_list_append(list, NULL, 0), CSS_BADPARM);
    /* Bad API arguments do not poison an otherwise usable parser. */
    ck_assert_int_eq(css__declaration_list_complete(list), CSS_OK);
    ck_assert(css__declaration_list_sheet(list)->inline_style);
    ck_assert_int_eq(css__declaration_list_complete(list), CSS_INVALID);
    ck_assert_int_eq(css__declaration_list_append(list, (const uint8_t *)"color:red", 9), CSS_INVALID);
    ck_assert_uint_eq(css__declaration_list_count(list), 0);
    css__declaration_list_destroy(list);

    ck_assert_int_eq(css__declaration_list_create(&params, &list), CSS_OK);
    const char *partial = "color:red; --p:var(";
    check_append(css__declaration_list_append(list, (const uint8_t *)partial, strlen(partial)));
    css__declaration_list_destroy(list); /* Abandon a partially parsed declaration. */
}
END_TEST

START_TEST(test_growth)
{
    css_declaration_list *list;
    ck_assert_int_eq(css__declaration_list_create(&params, &list), CSS_OK);
    const char *declaration = "margin:1px 2px!important;";
    for (size_t i = 0; i < 80; i++)
        check_append(css__declaration_list_append(list, (const uint8_t *)declaration, strlen(declaration)));
    ck_assert_int_eq(css__declaration_list_complete(list), CSS_OK);
    ck_assert_uint_eq(css__declaration_list_count(list), 80);
    for (size_t i = 0; i < 80; i++) {
        const css_declaration *entry = css__declaration_list_get(list, i);
        ck_assert_str_eq(lwc_string_data(entry->name), "margin");
        ck_assert_str_eq(lwc_string_data(entry->source_value), "1px 2px");
        ck_assert(entry->important);
        ck_assert_uint_eq(entry->token_count, 3);
    }
    css__declaration_list_destroy(list);
}
END_TEST


static const struct {
    const char *css;
    const char *names[16];
    size_t sources[16];
    size_t components[16];
} order_cases[] = {
    {"", {NULL}, {0}, {0}},
    {"width:1px; height:2px; width:3px", {"height", "width"}, {1, 2}, {0, 0}},
    {"width:1px!important; height:2px; width:3px", {"width", "height"}, {0, 1}, {0, 0}},
    {"width:1px!important; height:2px; width:3px!important", {"height", "width"}, {1, 2}, {0, 0}},
    {"margin:1px 2px; color:red; margin-left:9px!important",
        {"margin-top", "margin-right", "margin-bottom", "color", "margin-left"}, {0, 0, 0, 1, 2}, {0, 1, 2, 0, 0}},
    {"margin-left:9px!important; margin:1px 2px", {"margin-left", "margin-top", "margin-right", "margin-bottom"},
        {0, 1, 1, 1}, {0, 0, 1, 2}},
    {"margin-left:9px; margin:1px 2px!important", {"margin-top", "margin-right", "margin-bottom", "margin-left"},
        {1, 1, 1, 1}, {0, 1, 2, 3}},
    {"margin:var(--space); margin-left:9px", {"margin-top", "margin-right", "margin-bottom", "margin-left"},
        {0, 0, 0, 1}, {0, 1, 2, 0}},
    {"--Tone:red!important; --tone:blue; --Tone:green; color:var(--Tone)", {"--Tone", "--tone", "color"}, {0, 1, 3},
        {0, 0, 0}},
    {"--Tone:red; --tone:blue; --Tone:green!important", {"--tone", "--Tone"}, {1, 2}, {0, 0}},
    {"width:1px; inline-size:2px; height:3px; block-size:4px; width:5px",
        {"inline-size", "height", "block-size", "width"}, {1, 2, 3, 4}, {0, 0, 0, 0}},
    {"margin-inline:1px 2px; margin-left:3px; margin-right:4px",
        {"margin-inline-start", "margin-inline-end", "margin-left", "margin-right"}, {0, 0, 1, 2}, {0, 1, 0, 0}},
    {"margin-block:1px 2px; margin-block-start:3px; margin-top:4px",
        {"margin-block-end", "margin-block-start", "margin-top"}, {0, 1, 2}, {1, 0, 0}},
    {"padding:initial; padding-inline:2px; padding-block-end:3px",
        {"padding-top", "padding-right", "padding-bottom", "padding-left", "padding-inline-start", "padding-inline-end",
            "padding-block-end"},
        {0, 0, 0, 0, 1, 1, 2}, {0, 1, 2, 3, 0, 1, 0}},
    {"gap:3px 4px; row-gap:5px; overflow:hidden", {"column-gap", "row-gap", "overflow-x", "overflow-y"}, {0, 1, 2, 2},
        {1, 0, 0, 1}},
    {"border-top:1px solid red; border-top-color:blue", {"border-top-width", "border-top-style", "border-top-color"},
        {0, 0, 1}, {0, 1, 0}},
    {"border:inherit; border-left-color:red",
        {"border-top-width", "border-right-width", "border-bottom-width", "border-left-width", "border-top-style",
            "border-right-style", "border-bottom-style", "border-left-style", "border-top-color", "border-right-color",
            "border-bottom-color", "border-left-color"},
        {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1}, {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 0}},
    {"grid-area:1/2/3/4; grid-row:5/6", {"grid-column-start", "grid-column-end", "grid-row-start", "grid-row-end"},
        {0, 0, 1, 1}, {1, 3, 0, 1}},
};
enum { ORDER_CASE_COUNT = sizeof(order_cases) / sizeof(order_cases[0]) };

START_TEST(test_declaration_order)
{
    css_declaration_list *list = parse(order_cases[_i].css, true);
    css_declaration_order *order;
    ck_assert_int_eq(css__declaration_order_create(list, &order), CSS_OK);
    size_t expected = 0;
    while (expected < 16 && order_cases[_i].names[expected] != NULL)
        expected++;
    ck_assert_msg(css__declaration_order_count(order) == expected, "order case %d: expected %zu, got %zu", _i, expected,
        css__declaration_order_count(order));
    for (size_t i = 0; i < expected; i++) {
        const css_declaration_contribution *entry = css__declaration_order_get(order, i);
        ck_assert_msg(strcmp(entry->name, order_cases[_i].names[i]) == 0,
            "order case %d, position %zu: expected %s, got %s", _i, i, order_cases[_i].names[i], entry->name);
        ck_assert_ptr_eq(entry->source, css__declaration_list_get(list, order_cases[_i].sources[i]));
        ck_assert_uint_eq(entry->component, order_cases[_i].components[i]);
    }
    ck_assert_ptr_null(css__declaration_order_get(order, expected));
    css__declaration_order_destroy(order);
    css__declaration_list_destroy(list);
}
END_TEST

START_TEST(test_order_many_custom_properties)
{
    enum { COUNT = 512 };
    char *css = malloc(COUNT * 100);
    ck_assert_ptr_nonnull(css);
    size_t used = 0;
    for (size_t i = 0; i < COUNT; i++)
        used += (size_t)sprintf(css + used, "--v%zu:old%s;", i, i % 2 == 0 ? "!important" : "");
    for (size_t i = COUNT; i-- > 0;)
        used += (size_t)sprintf(css + used, "--v%zu:new;", i);
    css_declaration_list *list = parse(css, false);
    free(css);
    css_declaration_order *order;
    ck_assert_int_eq(css__declaration_order_create(list, &order), CSS_OK);
    ck_assert_uint_eq(css__declaration_order_count(order), COUNT);
    for (size_t i = 0; i < COUNT; i++) {
        const css_declaration_contribution *entry = css__declaration_order_get(order, i);
        char name[32];
        size_t id = i < COUNT / 2 ? 2 * i : COUNT - 1 - 2 * (i - COUNT / 2);
        snprintf(name, sizeof(name), "--v%zu", id);
        ck_assert_str_eq(entry->name, name);
        ck_assert_str_eq(lwc_string_data(entry->source->source_value), i < COUNT / 2 ? "old" : "new");
        ck_assert_int_eq(entry->source->important, i < COUNT / 2);
    }
    css__declaration_order_destroy(order);
    css__declaration_list_destroy(list);
}
END_TEST

START_TEST(test_property_metadata)
{
    const struct css_prop_entry *width = css_prop_lookup("width", 5);
    const struct css_prop_entry *logical = css_prop_lookup("INLINE-SIZE", 11);
    ck_assert_ptr_nonnull(width);
    ck_assert_ptr_nonnull(logical);
    ck_assert_int_eq(width->id, CSS_DECL_WIDTH);
    ck_assert_int_eq(logical->id, CSS_DECL_INLINE_SIZE);
    ck_assert_int_eq(width->logical_group, CSS_LOGICAL_GROUP_SIZE);
    ck_assert_int_eq(width->logical_group, logical->logical_group);
    ck_assert(!width->logical && logical->logical);
    ck_assert(!width->shorthand && !logical->shorthand);
    const struct css_prop_entry *side = css_prop_lookup("border-top", 10);
    const struct css_prop_entry *color = css_prop_lookup("border-top-color", 16);
    ck_assert(side->shorthand && !color->shorthand); /* Both use generated WRAP parsers. */
    ck_assert_uint_eq(side->longhand_count, 3);
    ck_assert_str_eq(side->longhands[0], "border-top-width");
    ck_assert_str_eq(side->longhands[1], "border-top-style");
    ck_assert_str_eq(side->longhands[2], "border-top-color");
    ck_assert_ptr_null(color->longhands);
    ck_assert_uint_eq(color->longhand_count, 0);
    ck_assert_ptr_null(css_prop_lookup(NULL, 0));
    ck_assert_ptr_null(css_prop_lookup("", 0));
    ck_assert_ptr_null(css_prop_lookup("width-junk", 10));
    ck_assert_ptr_null(css_prop_lookup("width", SIZE_MAX));
    ck_assert_ptr_null(css_prop_lookup("griĭrow", strlen("griĭrow")));
    ck_assert_ptr_null(css_prop_lookup("margiέleft", strlen("margiέleft")));
    ck_assert_ptr_null(css_prop_lookup("--width", 7));
    /* The length API accepts a slice, without requiring a terminator. */
    const char slice[] = {'w', 'i', 'd', 't', 'h'};
    ck_assert_ptr_eq(css_prop_lookup(slice, sizeof(slice)), width);
}
END_TEST

START_TEST(test_order_lifecycle)
{
    css_declaration_order *order = (void *)1;
    ck_assert_int_eq(css__declaration_order_create(NULL, &order), CSS_BADPARM);
    ck_assert_ptr_null(order);
    ck_assert_int_eq(css__declaration_order_create(NULL, NULL), CSS_BADPARM);
    ck_assert_uint_eq(css__declaration_order_count(NULL), 0);
    ck_assert_ptr_null(css__declaration_order_get(NULL, 0));
    css__declaration_order_destroy(NULL);
    css_declaration_list *list;
    ck_assert_int_eq(css__declaration_list_create(&params, &list), CSS_OK);
    ck_assert_int_eq(css__declaration_order_create(list, &order), CSS_INVALID);
    ck_assert_ptr_null(order);
    ck_assert_int_eq(css__declaration_list_complete(list), CSS_OK);
    ck_assert_int_eq(css__declaration_order_create(list, &order), CSS_OK);
    ck_assert_uint_eq(css__declaration_order_count(order), 0);
    css__declaration_order_destroy(order);
    css__declaration_list_destroy(list);
}
END_TEST

#ifdef WISP_TEST_ALLOC_FAILURE
START_TEST(test_order_allocation_failure)
{
    css_declaration_list *list = parse("margin:1px; margin-left:2px; --a:red", true);
    css_declaration_order *order = (void *)1;
    malloc_limit(0);
    css_error error = css__declaration_order_create(list, &order);
    malloc_limit(UINT_MAX);
    ck_assert_int_eq(error, CSS_NOMEM);
    ck_assert_ptr_null(order);
    ck_assert_uint_eq(css__declaration_list_count(list), 3);
    ck_assert_int_eq(css__declaration_order_create(list, &order), CSS_OK);
    ck_assert_uint_eq(css__declaration_order_count(order), 5);
    css__declaration_order_destroy(order);
    css__declaration_list_destroy(list);
}
END_TEST
#endif

#ifdef WISP_TEST_ALLOC_FAILURE
START_TEST(test_allocation_failure)
{
    void (*limit)(unsigned int) = _i == 0 ? malloc_limit : _i == 1 ? calloc_limit : realloc_limit;
    const char *declarations = "color:red; width:2px!important; --Tone:green; margin:var(--space, 1px 2px);"
                               "padding:1em; display:flex; height:3px; opacity:.5; color:blue; --extra:;"
                               "flex:auto 2 3;flex-flow:wrap column;flex:initial;flex-flow:inherit;"
                               "list-style:none inside url(Marker.png);columns:auto 12em;"
                               "list-style:initial;columns:inherit;"
                               "border:.1234567px solid hsl(12 34% 56%);border:initial;"
                               "outline:invert dashed 1em;outline:unset;"
                               "column-rule:thin solid;column-rule:revert;"
                               "grid-area:2/3/span 2/2 span;grid-column:span 2/4;grid-row:initial;"
                               "grid-column-start:3 span;grid-column-end:unset;grid-row-end:-3;"
                               "color:rgb(1.2 3.4 5.6 / 50%);border-color:#1234 red rgb(3,2,1) currentcolor;"
                               /* Force the parser's token vector to grow too. */
                               "--large:red red red red red red red red red red red red red red red red "
                               "red red red red red red red red red red red red red red red red red red;"
                               "content:'a\\\r\nb' attr(Title) counters(Chapter, '.', UPPER-ROMAN);"
                               "counter-reset:Chapter 2 Section; counter-increment:Chapter; quotes:'«' '»';"
                               "overflow:hidden auto!important; font-family:One,Two/**/Words,Three\\20 ;"
                               "background-image:url(\\20 file\\22 ),url('other\\1 image');";
    /* Grow the captured-source buffer after a retained declaration exists. */
    char css[16384];
    size_t pos = (size_t)sprintf(css, "opacity:1; /*");
    memset(css + pos, 'c', 8192);
    pos += 8192;
    snprintf(css + pos, sizeof(css) - pos, "*/%s", declarations);
    const size_t expected = 41;
    /* Keep shared property strings alive, so each iteration tests the same
     * allocation sequence instead of global string-cache initialization. */
    css_stylesheet *keeper;
    ck_assert_int_eq(css_stylesheet_create(&params, &keeper), CSS_OK);
    bool succeeded = false;
    unsigned failures = 0;
    for (unsigned attempt = 0; attempt < 1000; attempt++) {
        css_declaration_list *list = NULL;
        limit(attempt);
        css_error error = css__declaration_list_create(&params, &list);
        if (error == CSS_OK) {
            error = css__declaration_list_append(list, (const uint8_t *)css, strlen(css));
            if (error == CSS_OK || error == CSS_NEEDDATA)
                error = css__declaration_list_complete(list);
        }
        limit(UINT_MAX);
        if (error == CSS_OK) {
            ck_assert_uint_eq(css__declaration_list_count(list), expected);
            succeeded = true;
        } else {
            ck_assert_msg(error == CSS_NOMEM, "allocator %d attempt %u returned %d", _i, attempt, error);
            failures++;
            ck_assert_ptr_null(css__declaration_list_sheet(list));
            ck_assert_ptr_null(css__declaration_list_get(list, 0));
            if (list != NULL) {
                ck_assert_int_eq(css__declaration_list_complete(list), CSS_NOMEM);
                ck_assert_int_eq(css__declaration_list_append(list, (const uint8_t *)"", 0), CSS_NOMEM);
            }
        }
        css__declaration_list_destroy(list);
        if (succeeded)
            break;
    }
    ck_assert_msg(succeeded && failures > 0, "allocator %d: failure sweep did not reach success", _i);
    css_declaration_list *retry = parse(css, true);
    ck_assert_uint_eq(css__declaration_list_count(retry), expected);
    css__declaration_list_destroy(retry);
    css_stylesheet_destroy(keeper);
}
END_TEST
#endif


static const css_declaration_contribution *find_value(const css_declaration_order *order, const char *name)
{
    for (size_t i = 0; i < css__declaration_order_count(order); i++) {
        const css_declaration_contribution *value = css__declaration_order_get(order, i);
        if (strcmp(value->name, name) == 0)
            return value;
    }
    return NULL;
}

static css_error serialize_property(const css_declaration_order *order, const char *name, lwc_string **out)
{
    const struct css_prop_entry *property = css_prop_lookup(name, strlen(name));
    if (property == NULL || !property->shorthand)
        return css__declaration_value_serialize(find_value(order, name), out);
    const css_declaration_contribution *members[16];
    /* This helper also runs with allocation failures enabled. Check's assert
     * bookkeeping allocates, so assertions belong after restoring the limit. */
    if (property->longhand_count > sizeof(members) / sizeof(members[0]))
        return CSS_BADPARM;
    for (size_t i = 0; i < property->longhand_count; i++)
        members[i] = find_value(order, property->longhands[i]);
    return css__declaration_shorthand_serialize(property, members, property->longhand_count, out);
}

static const struct {
    const char *css, *property, *expected;
} value_cases[] = {
    {"--x:  001.2300PX  /**/ 'MiXeD'  ;", "--x", "001.2300PX  /**/ 'MiXeD'"},
    {"--x: \t /* leading */ a /* trailing */ \r\n", "--x", "/* leading */ a /* trailing */"},
    {"--x: \t /* one */ \t /* two */ \t;", "--x", "/* one */ \t /* two */"},
    {"--x:12345678-12e3-8d9b-a456-426614174000;", "--x", "12345678-12e3-8d9b-a456-426614174000"},
    {"--x:;", "--x", " "},
    {"--x: \t\r\n !important;", "--x", " "},
    {"--x: /**/ ;", "--x", "/**/"},
    {"--x: foo\\ ;", "--x", "foo\\ "},
    {"--x: foo\\  ;", "--x", "foo\\ "},
    {"--x: \\61 ;", "--x", "\\61 "},
    {"--x: \\61  ;", "--x", "\\61 "},
    {"--x: \\61 /**/ ;", "--x", "\\61 /**/"},
    {"--x: 'ends in spaces  ", "--x", "'ends in spaces  "},
    {"--x: 'escaped\\'  ", "--x", "'escaped\\'  "},
    {"--x: /* unterminated  ", "--x", "/* unterminated  "},
    {"--x: x /* unterminated  ", "--x", "x /* unterminated  "},
    {"--x: x \t/**/ \t/* unterminated  ", "--x", "x \t/**/ \t/* unterminated  "},
    {"--x: UnSeT;", "--x", "UnSeT"},
    {"--é: 'Știri 💡' /* comment */ !IMPORTANT", "--é", "'Știri 💡' /* comment */"},
    {"width: VaR(--x, 0002PX) /**/ !important", "width", "VaR(--x, 0002PX) /**/"},
    {"color:var(--x, rgb(1, 2, 3))", "color", "var(--x, rgb(1, 2, 3))"},
    {"margin:var(--x)", "margin-left", ""},
    {"border:var(--x)", "border-left-color", ""},
    {"margin: InHeRiT", "margin-left", "inherit"},
    {"border: INITIAL", "border-left-color", "initial"},
    {"width: UnSeT", "width", "unset"},
    {"width: \\72 evert", "width", "revert"},
    {"margin:1PX", "margin-left", "1px"},
    {"margin:1px 2EM", "margin-bottom", "1px"},
    {"margin:1px 2EM 3%", "margin-left", "2em"},
    {"margin:1px 2em 3% -0", "margin-left", "0px"},
    {"margin:1px 2em 3% -4px", "margin-bottom", "3%"},
    {"padding:1px 2px 3px 4px", "padding-right", "2px"},
    {"inset: AuTo -0 2em", "left", "0px"},
    {"margin-inline:1px 2em", "margin-inline-end", "2em"},
    {"padding-block:3%", "padding-block-end", "3%"},
    {"inset-block: AuTo -1px", "inset-block-start", "auto"},
    {"border-width: tHiN medium 0 1em", "border-bottom-width", "0px"},
    {"gap: NoRmAl 2px", "row-gap", "normal"},
    {"width: 0001.2300000PX", "width", "1.23px"},
    {"width: 0.0000004em", "width", "0em"},
    {"width: .9999995px", "width", "1px"},
    {"width: 9999999999999999999.9999999px", "width", "10000000000000000000px"},
    {"width: .0000005PX", "width", "0.000001px"},
    {"margin-top: -.0000004px", "margin-top", "0px"},
    {"margin-top: -.0000005px", "margin-top", "-0.000001px"},
    {"margin-top: -999.9999999px", "margin-top", "-1000px"},
    {"width: +0002.5000p\\78 ", "width", "2.5px"},
    {"height: 0", "height", "0px"},
    {"height: 0em", "height", "0em"},
    {"width: 01.250000%", "width", "1.25%"},
    {"font-size: LaRgEr", "font-size", "larger"},
    {"min-width: MiN-CoNtEnT", "min-width", "min-content"},
    {"opacity: .123456789", "opacity", "0.123457"},
    /* Specified opacity is not the renderer's clamped computed value. */
    {"opacity: 2", "opacity", "2"},
    {"opacity: -0.25", "opacity", "-0.25"},
    {"line-height: 0001.2500", "line-height", "1.25"},
    {"line-height: 0001.2500EM", "line-height", "1.25em"},
    {"line-height: 0", "line-height", "0"},
    {"z-index: +002", "z-index", "2"},
    {"z-index: AUTO", "z-index", "auto"},
    {"order:-0002", "order", "-2"},
    {"margin: 0px 1em 0px 1em", "margin", "0px 1em"},
    {"margin: 0 1px 1px 1px", "margin", "0px 1px 1px"},
    {"margin: 1px 2px 3px 4px", "margin", "1px 2px 3px 4px"},
    {"padding: 0 0px 0PX -0", "padding", "0px"},
    {"inset: auto 1em auto 1em", "inset", "auto 1em"},
    {"margin:1px; margin-left:2px", "margin", "1px 1px 1px 2px"},
    {"margin:1px!important; margin-left:2px", "margin", "1px"},
    {"margin:1px; margin-left:2px!important", "margin", ""},
    {"margin-top:1px; margin-left:2px", "margin", ""},
    {"margin-inline: 2px 2px", "margin-inline", "2px"},
    {"padding-block: 2px 3px", "padding-block", "2px 3px"},
    {"gap: 1em; column-gap:2em", "gap", "1em 2em"},
    {"margin:INITIAL", "margin", "initial"},
    {"border:UNSET", "border-top", "unset"},
    {"margin:inherit; margin-left:initial", "margin", ""},
    {"margin:inherit; margin-left:1px", "margin", ""},
    {"margin:1px; margin-left:inherit", "margin", ""},
    {"margin:var(--x, 1px  2px) /**/ ", "margin", "var(--x, 1px  2px) /**/"},
    {"margin:var(--x); margin-left:1px", "margin", ""},
    {"margin:var(--x); margin-left:var(--x)", "margin", ""},
    {"margin-top:var(--x); margin-right:var(--x); margin-bottom:var(--x); margin-left:var(--x)", "margin", ""},
    {"border:var(--x)", "border-top", ""},
    {"border:var(--x)", "border", "var(--x)"},
    {"margin:var(--x)!important; margin-left:1px", "margin", "var(--x)"},
    {"width:calc(1px + 2em)", "width", NULL},
    {"margin:1px calc(2px + 1em)", "margin", NULL},
    {"margin:1px calc(2px + 1em)", "margin-top", NULL},
    {"color:rgb(1, 2, 3)", "color", "rgb(1, 2, 3)"},
    {"font-family:'Example'", "font-family", "\"Example\""},
    {"display: InLiNe-BlOcK", "display", "inline-block"},
    {"display: \\66 lex", "display", "flex"},
    {"visibility: HiDdEn !important; visibility:visible", "visibility", "hidden"},
    {"position: ReLaTiVe", "position", "relative"},
    {"float: LeFt", "float", "left"},
    {"clear: BoTh", "clear", "both"},
    {"box-sizing: BORDER-BOX", "box-sizing", "border-box"},
    {"flex-direction: COLUMN-REVERSE", "flex-direction", "column-reverse"},
    {"align-items: FLEX-START", "align-items", "flex-start"},
    {"justify-content: SPACE-BETWEEN", "justify-content", "space-between"},
    {"object-fit: CoVeR", "object-fit", "cover"},
    {"font-weight: 700", "font-weight", "700"},
    {"font-weight: BOLDER", "font-weight", "bolder"},
    {"column-rule-width: 0002.5PX", "column-rule-width", "2.5px"},
    {"border-style: SOLID dashed", "border-bottom-style", "solid"},
    {"border-style: SOLID dashed dotted", "border-left-style", "dashed"},
    {"border-style: SOLID dashed dotted double", "border-left-style", "double"},
    {"border-style: SOLID dashed solid dashed", "border-style", "solid dashed"},
    {"border-style: solid; border-left-style:dotted", "border-style", "solid solid solid dotted"},
    {"border-style: solid!important; border-left-style:dotted", "border-style", "solid"},
    {"border-style: solid; border-left-style:dotted!important", "border-style", ""},
    {"border-style: INITIAL", "border-style", "initial"},
    {"overflow: HIDDEN", "overflow", "hidden"},
    {"overflow: HIDDEN AUTO", "overflow", "hidden auto"},
    {"overflow: hidden auto !important", "overflow-y", "auto"},
    {"overflow: hidden auto; overflow-x:SCROLL", "overflow", "scroll auto"},
    {"overflow: hidden auto; overflow-y:HIDDEN", "overflow", "hidden"},
    {"overflow: hidden; overflow-y:visible!important", "overflow", ""},
    {"overflow: INITIAL", "overflow", "initial"},
    {"overflow:var(--o, hidden auto)", "overflow", "var(--o, hidden auto)"},
    {"overflow:var(--o)", "overflow-x", ""},
    {"overflow: hidden; overflow:hidden initial", "overflow", "hidden"},
    {"overflow: hidden; overflow:initial hidden", "overflow", "hidden"},
    {"overflow: hidden; overflow:scroll auto visible", "overflow", "hidden"},
    {"overflow: hidden; overflow:scroll bogus", "overflow", "hidden"},
    {"list-style-image: NoNe", "list-style-image", "none"},
    {"list-style-image: URL(../Icons/Mark.png)", "list-style-image", "url(\"../Icons/Mark.png\")"},
    {"list-style-image:url(  '#marker'  )", "list-style-image", "url(\"#marker\")"},
    {"list-style-image:url('')", "list-style-image", "url(\"\")"},
    {"list-style-image:url()", "list-style-image", "url(\"\")"},
    {"list-style-image:url('foo bar')", "list-style-image", "url(\"foo bar\")"},
    {"list-style-image:url(\\ foo\\ )", "list-style-image", "url(\" foo \")"},
    {"list-style-image:url(\\22 foo\\22 )", "list-style-image", "url(\"\\\"foo\\\"\")"},
    {"list-style-image:url(foo\\')", "list-style-image", "url(\"foo'\")"},
    {"list-style-image:url('a\\\\b\\\"c')", "list-style-image", "url(\"a\\\\b\\\"c\")"},
    {"list-style-image:url(Știri💡.png)", "list-style-image", "url(\"Știri💡.png\")"},
    {"list-style-image:url('a\\a b\\9 c')", "list-style-image", "url(\"a\\a b\\9 c\")"},
    {"list-style-image:url('a\\01 b\\0b c\\0d d\\1f e\\7f f')", "list-style-image",
        "url(\"a\\1 b\\b c\\d d\\1f e\\7f f\")"},
    {"list-style-image:url('\\80 \\fdd0 ')", "list-style-image", "url(\"\xc2\x80\xef\xb7\x90\")"},
    {"cue-before:URL(sound.ogg)", "cue-before", "url(\"sound.ogg\")"},
    {"cue-after:NoNe", "cue-after", "none"},
    {"background-image:URL(a.png) , NoNe, url('B,C.png')", "background-image",
        "url(\"a.png\"), none, url(\"B,C.png\")"},
    {"background-image: url('data:image/svg+xml;a,b;c')", "background-image",
        "url(\"data:image/svg+xml;a,b;c\")"},
    {"background-image: url(a.png)!important; background-image:none", "background-image", "url(\"a.png\")"},
    {"background-attachment: FiXeD , SCROLL , FiXeD", "background-attachment", "fixed, scroll, fixed"},
    {"background-image:linear-gradient(red, blue)", "background-image", NULL},
    {"background-image:url(a.png), linear-gradient(red, blue)", "background-image", NULL},
    {"content:NoRmAl", "content", "normal"},
    {"content:NoNe", "content", "none"},
    {"content:InHeRiT", "content", "inherit"},
    {"content:var(--Caption, 'Text')", "content", "var(--Caption, 'Text')"},
    {"content:'Hello'\"World\"", "content", "\"Hello\" \"World\""},
    {"content:''", "content", "\"\""},
    {"content:'Știri 💡'", "content", "\"Știri 💡\""},
    {"content:'a\\a b\\9 c\\01 d\\7f e'", "content", "\"a\\a b\\9 c\\1 d\\7f e\""},
    {"content:'a\\\"b\\\\c'", "content", "\"a\\\"b\\\\c\""},
    {"content:'ends in spaces  ", "content", "\"ends in spaces  \""},
    {"content:'escaped\\'  ", "content", "\"escaped'  \""},
    {"content:OpEn-QuOtE 'Hello' CLOSE-QUOTE NO-OPEN-QUOTE No-Close-Quote", "content",
        "open-quote \"Hello\" close-quote no-open-quote no-close-quote"},
    {"content:URL(../Icons/Mark.png) 'Caption'", "content", "url(\"../Icons/Mark.png\") \"Caption\""},
    {"content:ATTR( Title )", "content", "attr(Title)"},
    {"content:\\61 ttr( \\54 itle )", "content", "attr(Title)"},
    {"content:attr(inherit) attr(none)", "content", "attr(inherit) attr(none)"},
    {"content:attr(data\\:Title) attr(a\\ b)", "content", "attr(data\\:Title) attr(a\\ b)"},
    {"content:attr(\\31 2) attr(-\\32 x)", "content", "attr(\\31 2) attr(-\\32 x)"},
    {"content:attr(\\-) attr(--x)", "content", "attr(\\-) attr(--x)"},
    {"content:attr(a\\1 b\\7f c) attr(Știri)", "content", "attr(a\\1 b\\7f c) attr(Știri)"},
    {"content:attr(a\\\\b) attr(\\22 )", "content", "attr(a\\\\b) attr(\\\")"},
    {"content:COUNTER( Chapter )", "content", "counter(Chapter)"},
    {"content:counter( Chapter , DECIMAL )", "content", "counter(Chapter)"},
    {"content:counter( Chapter , UPPER-ROMAN )", "content", "counter(Chapter, upper-roman)"},
    {"content:COUNTERS( Chapter , '.' )", "content", "counters(Chapter, \".\")"},
    {"content:counters( Chapter , '.' , DeCiMaL )", "content", "counters(Chapter, \".\")"},
    {"content:counters( Chapter , 'a\\a \\22 \\\\' , LOWER-ALPHA )", "content",
        "counters(Chapter, \"a\\a \\\"\\\\\", lower-alpha)"},
    {"content:counter(\\31 2) counters(-\\32 x,'')", "content", "counter(\\31 2) counters(-\\32 x, \"\")"},
    {"content:attr(Title) counter(Chapter) '.' counters(Section,'.',lower-roman)", "content",
        "attr(Title) counter(Chapter) \".\" counters(Section, \".\", lower-roman)"},
    {"content:'kept'; content:'rejected' bogus", "content", "\"kept\""},
    {"content:'kept'; content:attr()", "content", "\"kept\""},
    {"content:'kept'; content:counter(A,bogus)", "content", "\"kept\""},
    {"content:'kept'; content:counters(A)", "content", "\"kept\""},
    {"quotes:NoNe", "quotes", "none"},
    {"quotes:UnSeT", "quotes", "unset"},
    {"quotes:'«' '»' '‹' '›'", "quotes", "\"«\" \"»\" \"‹\" \"›\""},
    {"quotes:'' ''", "quotes", "\"\" \"\""},
    {"quotes:'\\22 ''\\22 '\"'\"\"'\"", "quotes", "\"\\\"\" \"\\\"\" \"'\" \"'\""},
    {"quotes:'a\\a ' 'b\\9 '", "quotes", "\"a\\a \" \"b\\9 \""},
    {"quotes:'left' 'right  ", "quotes", "\"left\" \"right  \""},
    {"quotes:none; quotes:'odd'", "quotes", "none"},
    {"counter-reset:NoNe", "counter-reset", "none"},
    {"counter-increment:INITIAL", "counter-increment", "initial"},
    {"counter-reset:Chapter Section", "counter-reset", "Chapter Section"},
    {"counter-increment:Chapter Section", "counter-increment", "Chapter Section"},
    {"counter-reset:A +001 B 0 C -0000 D -0002", "counter-reset", "A 1 B C D -2"},
    {"counter-increment:A +001 B 0 C -0000 D -0002", "counter-increment", "A B 0 C 0 D -2"},
    {"counter-reset:A 12345678 B -12345678", "counter-reset", "A 12345678 B -12345678"},
    {"counter-increment:A 12345678 B -12345678", "counter-increment", "A 12345678 B -12345678"},
    {"counter-reset:A 1 A 2 a -1", "counter-reset", "A 1 A 2 a -1"},
    {"counter-increment:A 1 A 2 a -1", "counter-increment", "A A 2 a -1"},
    {"counter-reset:\\31 2 0 -\\32 x 1 \\- -1 Știri 2", "counter-reset", "\\31 2 -\\32 x 1 \\- -1 Știri 2"},
    {"counter-increment:keep; counter-increment:A 1.5", "counter-increment", "keep"},
    {"counter-reset:keep; counter-reset:A 1px", "counter-reset", "keep"},
    {"counter-reset:var(--Counters)", "counter-reset", "var(--Counters)"},

    {"content:attr(--9) counter(--Chapter) counters(--, '.')", "content",
        "attr(--9) counter(--Chapter) counters(--, \".\")"},
    {"counter-increment:--Chapter 1 --9 -2", "counter-increment", "--Chapter --9 -2"},
    {"counter-reset:\\2d -Chapter +0 --9 2", "counter-reset", "--Chapter --9 2"},
    {"\\2d -Tone:ReD", "--Tone", "ReD"},

    {"aspect-ratio:AuTo", "aspect-ratio", "auto"},
    {"aspect-ratio:INITIAL", "aspect-ratio", "initial"},
    {"aspect-ratio:var(--Ratio, 16/9)", "aspect-ratio", "var(--Ratio, 16/9)"},
    {"aspect-ratio:16/9", "aspect-ratio", "16 / 9"},
    {"aspect-ratio:4/2", "aspect-ratio", "4 / 2"},
    {"aspect-ratio:3", "aspect-ratio", "3 / 1"},
    {"aspect-ratio:003.00/+01.000", "aspect-ratio", "3 / 1"},
    {"aspect-ratio:003.00 AuTo", "aspect-ratio", "auto 3 / 1"},
    {"aspect-ratio:4/3 AuTo", "aspect-ratio", "auto 4 / 3"},
    {"aspect-ratio:AuTo 4/3", "aspect-ratio", "auto 4 / 3"},
    {"aspect-ratio:0", "aspect-ratio", "0 / 1"},
    {"aspect-ratio:-0.0000/-0", "aspect-ratio", "0 / 0"},
    {"aspect-ratio:auto 0/0", "aspect-ratio", "auto 0 / 0"},
    {"aspect-ratio:1/0", "aspect-ratio", "1 / 0"},
    {"aspect-ratio:.1234567/.9999995", "aspect-ratio", "0.123457 / 1"},
    {"aspect-ratio:12345678/98765432", "aspect-ratio", "12345678 / 98765432"},
    {"aspect-ratio:3;aspect-ratio:-.00000001", "aspect-ratio", "3 / 1"},
    {"aspect-ratio:3;aspect-ratio:1/-.00000001", "aspect-ratio", "3 / 1"},
    {"aspect-ratio:3;aspect-ratio:auto auto", "aspect-ratio", "3 / 1"},
    {"aspect-ratio:3;aspect-ratio:auto 1 2", "aspect-ratio", "3 / 1"},
    {"aspect-ratio:3;aspect-ratio:1/", "aspect-ratio", "3 / 1"},
    {"aspect-ratio:3;aspect-ratio:1px/2", "aspect-ratio", "3 / 1"},
    {"border-spacing:INHERIT", "border-spacing", "inherit"},
    {"border-spacing:var(--Spacing)", "border-spacing", "var(--Spacing)"},
    {"border-spacing:0", "border-spacing", "0px"},
    {"border-spacing:-0.0 00PX", "border-spacing", "0px"},
    {"border-spacing:2EM", "border-spacing", "2em"},
    {"border-spacing:2EM 2em", "border-spacing", "2em"},
    {"border-spacing:.1234567EM .9999995PX", "border-spacing", "0.123457em 1px"},
    {"border-spacing:12345678px 23456789em", "border-spacing", "12345678px 23456789em"},
    {"border-spacing:0px 0em", "border-spacing", "0px 0em"},
    {"border-spacing:3px;border-spacing:-.00000001px", "border-spacing", "3px"},
    {"border-spacing:3px;border-spacing:0 -.00000001em", "border-spacing", "3px"},
    {"border-spacing:3px;border-spacing:.00000001", "border-spacing", "3px"},
    {"border-spacing:3px;border-spacing:1px .00000001", "border-spacing", "3px"},
    {"border-spacing:3px;border-spacing:1px 2px 3px", "border-spacing", "3px"},
    {"clip:AuTo", "clip", "auto"},
    {"clip:INITIAL", "clip", "initial"},
    {"clip:var(--Clip)", "clip", "var(--Clip)"},
    {"clip:RECT(AUTO,AUTO,AUTO,AUTO)", "clip", "rect(auto, auto, auto, auto)"},
    {"clip:rect(0 00PX -0 AuTo)", "clip", "rect(0px, 0px, 0px, auto)"},
    {"clip:rect(1px,2em,-3vw,auto)", "clip", "rect(1px, 2em, -3vw, auto)"},
    {"clip:rect(-.000001em .1234567px -.9999995px auto)", "clip",
        "rect(-0.000001em, 0.123457px, -1px, auto)"},
    {"clip:rect(12345678px,23456789px,0,0)", "clip", "rect(12345678px, 23456789px, 0px, 0px)"},
    {"clip:auto; clip:rect(.00000001,0,0,0)", "clip", "auto"},
    {"clip:auto; clip:rect(0,0,0,-.00000001)", "clip", "auto"},
    {"clip:auto; clip:rect(0,0,0)", "clip", "auto"},
    {"clip:auto; clip:rect(0,0,0,0,0)", "clip", "auto"},
    {"clip:auto; clip:rect(0,0,0,0,)", "clip", "auto"},
    {"clip:auto; clip:rect(0,initial,0,0)", "clip", "auto"},
    {"margin-left:2px;margin-left:.00000001", "margin-left", "2px"},
    {"margin-left:2px;margin-left:-.00000001", "margin-left", "2px"},
    {"margin-left:-.00000001px", "margin-left", "0px"},

    /* CSSOM named-family strings retain case and decoded spaces. */
    {"font-family:Arial", "font-family", "\"Arial\""},
    {"font-family:Open Sans", "font-family", "\"Open Sans\""},
    {"font-family:Open/**/Sans", "font-family", "\"Open Sans\""},
    {"font-family:Open\t/**/\nSans", "font-family", "\"Open Sans\""},
    {"font-family:Open\\ Sans", "font-family", "\"Open Sans\""},
    {"font-family:Open\\20 ", "font-family", "\"Open \""},
    {"font-family:Open\\20 /**/Sans\\20 ", "font-family", "\"Open  Sans \""},
    {"font-family:\\20 Open\\20 ", "font-family", "\" Open \""},
    {"font-family:\"A  B\"", "font-family", "\"A  B\""},
    {"font-family:''", "font-family", "\"\""},
    {"font-family:a\\22 b\\5c c\\a d", "font-family", "\"a\\\"b\\\\c\\a d\""},
    {"font-family:Știri 字体", "font-family", "\"Știri 字体\""},
    {"font-family:\\31 2 \\2d -Name", "font-family", "\"12 --Name\""},
    {"font-family:none", "font-family", "\"none\""},
    {"font-family:normal", "font-family", "\"normal\""},
    {"font-family:SERIF,SaNs-SeRiF,CURSIVE,Fantasy,MONOSPACE", "font-family",
        "serif, sans-serif, cursive, fantasy, monospace"},
    {"font-family:\"serif\",serif,Example,\"Arial\",Example", "font-family",
        "\"serif\", serif, \"Example\", \"Arial\", \"Example\""},
    {"font-family:\"default\", \"inherit\", \"initial\", \"unset\", \"revert-layer\"", "font-family",
        "\"default\", \"inherit\", \"initial\", \"unset\", \"revert-layer\""},
    {"font-family:\\73 erif, \\41 rial", "font-family", "serif, \"Arial\""},
    {"font-family:InHeRiT", "font-family", "inherit"},
    {"font-family:INITIAL", "font-family", "initial"},
    {"font-family:UnSeT", "font-family", "unset"},
    {"font-family:ReVeRt", "font-family", "revert"},
    {"font-family:var(--Fonts, Open Sans)", "font-family", "var(--Fonts, Open Sans)"},
    {"font-family:\"system-ui\"", "font-family", "\"system-ui\""},
    {"font-family:Kept; font-family:A B C,", "font-family", "\"Kept\""},
    {"font-family:Kept; font-family:A, ,B", "font-family", "\"Kept\""},
    {"font-family:Kept; font-family:\"A\" B", "font-family", "\"Kept\""},
    {"font-family:Kept; font-family:A \"B\"", "font-family", "\"Kept\""},
    {"font-family:Kept; font-family:A 2", "font-family", "\"Kept\""},
    {"font-family:Kept; font-family:A,inherit", "font-family", "\"Kept\""},
    {"font-family:Kept; font-family:default", "font-family", "\"Kept\""},
    {"font-family:Kept; font-family:Foo default", "font-family", "\"Kept\""},
    {"font-family:Kept; font-family:Foo serif", "font-family", "\"Kept\""},
    {"font-family:system-ui", "font-family", NULL},
    {"font-family:ui-serif", "font-family", NULL},
    {"font-family:ui-sans-serif", "font-family", NULL},
    {"font-family:ui-monospace", "font-family", NULL},
    {"font-family:ui-rounded", "font-family", NULL},
    {"font-family:math", "font-family", NULL},
    {"font-family:emoji", "font-family", NULL},
    {"font-family:fangsong", "font-family", NULL},

    {"flex:100px", "flex", "1 1 100px"},
    {"flex:100px", "flex-grow", "1"},
    {"flex:100px", "flex-shrink", "1"},
    {"flex:100px", "flex-basis", "100px"},
    {"flex:2", "flex", "2 1 0px"},
    {"flex:2", "flex-basis", "0px"},
    {"flex:0", "flex", "0 1 0px"},
    {"flex:0 0", "flex", "0 0 0px"},
    {"flex:0 0 0", "flex", "0 0 0px"},
    {"flex:0px", "flex", "1 1 0px"},
    {"flex:20%", "flex", "1 1 20%"},
    {"flex:none", "flex", "0 0 auto"},
    {"flex:AUTO", "flex", "1 1 auto"},
    {"flex:CONTENT", "flex", "1 1 content"},
    {"flex:auto 2", "flex", "2 1 auto"},
    {"flex:auto 2 3", "flex", "2 3 auto"},
    {"flex:2 auto", "flex", "2 1 auto"},
    {"flex:10px 2 3", "flex", "2 3 10px"},
    {"flex:2 3 10px", "flex", "2 3 10px"},
    {"flex:+02.1250000 +03.000 .1234567EM", "flex", "2.125 3 0.123457em"},
    {"flex:-0 -0 -0px", "flex", "0 0 0px"},
    {"flex:2/**/3/**/AUTO", "flex", "2 3 auto"},
    {"flex:2 3 10px;flex-basis:20%", "flex", "2 3 20%"},
    {"flex-grow:2;flex-basis:10px;flex-shrink:3", "flex", "2 3 10px"},
    {"flex-grow:2", "flex", ""},
    {"flex:2;flex-grow:3!important", "flex", ""},
    {"flex:2!important;flex-grow:3", "flex", "2 1 0px"},
    {"flex:INITIAL", "flex", "initial"},
    {"flex:inherit;flex-grow:inherit", "flex", "inherit"},
    {"flex:inherit;flex-shrink:2", "flex", ""},
    {"flex:var(--f, 2 3 auto)", "flex", "var(--f, 2 3 auto)"},
    {"flex:var(--f)", "flex-grow", ""},
    {"flex:var(--f);flex-grow:2", "flex", ""},
    {"flex:2 calc(1 + 1) auto", "flex", NULL},
    {"flex:2 1 calc(10px + 2px)", "flex", NULL},
    {"flex-flow:ROW NOWRAP", "flex-flow", "row"},
    {"flex-flow:wrap", "flex-flow", "wrap"},
    {"flex-flow:wrap", "flex-direction", "row"},
    {"flex-flow:column", "flex-wrap", "nowrap"},
    {"flex-flow:WRAP-REVERSE COLUMN", "flex-flow", "column wrap-reverse"},
    {"flex-flow:column nowrap", "flex-flow", "column"},
    {"flex-flow:wrap;flex-direction:column", "flex-flow", "column wrap"},
    {"flex-flow:wrap;flex-wrap:nowrap", "flex-flow", "row"},
    {"flex-flow:unset", "flex-flow", "unset"},
    {"flex-flow:row!important;flex-wrap:wrap", "flex-flow", "row"},
    {"flex-flow:row;flex-wrap:wrap!important", "flex-flow", ""},
    {"flex-flow:var(--flow)", "flex-flow", "var(--flow)"},
    {"flex-flow:var(--flow)", "flex-wrap", ""},

    {"color:ReD", "color", "red"},
    {"color:ButtonText", "color", "buttontext"},
    {"color:WindowText", "color", "windowtext"},
    {"background-color:Canvas", "background-color", "canvas"},
    {"color:ReBeCcApUrPlE", "color", "rebeccapurple"},
    {"color:TRANSPARENT", "color", "transparent"},
    {"color:CURRENTcolor", "color", "currentcolor"},
    {"color:#123", "color", "rgb(17, 34, 51)"},
    {"color:#1234", "color", "rgba(17, 34, 51, 0.267)"},
    {"color:#c0FFee80", "color", "rgba(192, 255, 238, 0.5)"},
    {"color:#ff00ffed", "color", "rgba(255, 0, 255, 0.93)"},
    {"color:#ff00ffec", "color", "rgba(255, 0, 255, 0.925)"},
    {"color:#0001", "color", "rgba(0, 0, 0, 0.067)"},
    {"color:#0000", "color", "rgba(0, 0, 0, 0)"},
    {"color:#123f", "color", "rgb(17, 34, 51)"},
    {"color:#FF0000FF", "color", "rgb(255, 0, 0)"},
    {"color:RGB(01, +2, 003)", "color", "rgb(1, 2, 3)"},
    {"color:rgba(1, 2, 3)", "color", "rgb(1, 2, 3)"},
    {"color:rgb(1,2,3,.5)", "color", "rgba(1, 2, 3, 0.5)"},
    {"color:rgba(50%, 20%, 0%, 50%)", "color", "rgba(127.5, 51, 0, 0.5)"},
    {"color:rgb(1.25 50% 2.5 / 20%)", "color", "rgba(1.25, 127.5, 2.5, 0.2)"},
    {"color:rgb(1/**/2/**/3 / .1234567)", "color", "rgba(1, 2, 3, 0.123457)"},
    {"color:rgb(-1 999 0 / 2)", "color", "rgb(0, 255, 0)"},
    {"color:rgba(1, 2, 3, -1)", "color", "rgba(1, 2, 3, 0)"},
    {"color:rgb(1 2 3 / 0.99999999)", "color", "rgb(1, 2, 3)"},
    {"color:rgb(1 2 3 / 0.00000001)", "color", "rgba(1, 2, 3, 0)"},
    {"color:rgb(999999999999999999 0 0 / 999999999999999999)", "color", "rgb(255, 0, 0)"},
    {"color:rgb(-999999999999999999% 999999999999999999% 0%)", "color", "rgb(0, 255, 0)"},
    {"color:rgb(none 0 0)", "color", NULL},
    {"color:rgb(1 2 3 / none)", "color", NULL},
    {"color:hsl(0,100%,50%)", "color", "rgb(255, 0, 0)"},
    {"color:hwb(0 0% 0%)", "color", "rgb(255, 0, 0)"},
    {"color:HSL(120,30%,50%)", "color", "rgb(89.25, 165.75, 89.25)"},
    {"color:HSLA(120 30 50 / .1234567)", "color", "rgba(89.25, 165.75, 89.25, 0.123457)"},
    {"color:hsl(120 30% 50)", "color", "rgb(89.25, 165.75, 89.25)"},
    {"color:hsl(120 30 50%)", "color", "rgb(89.25, 165.75, 89.25)"},
    {"color:hsl(-.5TuRn,100%,50%,50%)", "color", "rgba(0, 255, 255, 0.5)"},
    {"color:hsl(100GRAD 100% 50%)", "color", "rgb(127.5, 255, 0)"},
    {"color:hsl(3.141592653589793rad 100% 50%)", "color", "rgb(0, 255, 255)"},
    {"color:hsl(1000000080deg 100% 50%)", "color", "rgb(255, 0, 0)"},
    {"color:hsl(1000000000.25turn 100% 50%)", "color", "rgb(127.5, 255, 0)"},
    {"color:hsl(30 -50% 60)", "color", "rgb(153, 153, 153)"},
    {"color:hsl(0 100% 50% / 10000000)", "color", "rgb(255, 0, 0)"},
    {"color:hsl(0 100% 50% / -1)", "color", "rgba(255, 0, 0, 0)"},
    {"color:hsl(0/**/100%/**/50%)", "color", "rgb(255, 0, 0)"},
    {"color:hsl(30 200% 50%)", "color", NULL},
    {"color:hsl(0 0% 110%)", "color", NULL},
    {"color:hsl(0 0% -10%)", "color", NULL},
    {"color:hsl(none 100% 50%)", "color", NULL},
    {"color:hsla(120 none 50% / none)", "color", NULL},
    {"color:hwb(none none none)", "color", NULL},
    {"color:hwb(0 20% 10% / none)", "color", NULL},
    {"color:HWB(740deg 20% 30% / 50%)", "color", "rgba(178.5, 93.5, 51, 0.5)"},
    {"color:hwb(150 20% 10%)", "color", "rgb(51, 229.5, 140.25)"},
    {"color:hwb(267 19% 60%)", "color", "rgb(72.5475, 48.45, 102)"},
    {"color:hwb(0 40 80%)", "color", "rgb(85, 85, 85)"},
    {"color:hwb(0 400% 200%)", "color", "rgb(170, 170, 170)"},
    {"color:hwb(0 100000000 50000000)", "color", "rgb(170, 170, 170)"},
    {"color:hwb(0 0% 0% / 100000000%)", "color", "rgb(255, 0, 0)"},
    {"color:hwb(0 0% 0% / -100%)", "color", "rgba(255, 0, 0, 0)"},
    {"color:hwb(0 -10% 10%)", "color", NULL},
    {"border-color:hsl(0,100%,50%) hwb(120 0 0)", "border-color", "rgb(255, 0, 0) rgb(0, 255, 0)"},
    {"border-color:hsl(0,100%,50%);border-left-color:rgb(255 0 0)", "border-color", "rgb(255, 0, 0)"},
    {"border-color:hwb(0 0 0)!important;border-right-color:blue", "border-color", "rgb(255, 0, 0)"},
    {"color:lab(50% 0 0)", "color", NULL},
    {"color:oklab(50% 0 0)", "color", NULL},
    {"color:lch(50% 10 0)", "color", NULL},
    {"color:oklch(50% 0.1 0)", "color", NULL},
    {"color:red; color:color(srgb 0 0 1)", "color", "red"},
    {"background-color:#123", "background-color", "rgb(17, 34, 51)"},
    {"outline-color:INVERT", "outline-color", "invert"},
    {"column-rule-color:#abc", "column-rule-color", "rgb(170, 187, 204)"},
    {"border-color:RED", "border-color", "red"},
    {"border-color:RED #123", "border-color", "red rgb(17, 34, 51)"},
    {"border-color:RED #123", "border-bottom-color", "red"},
    {"border-color:RED #123", "border-left-color", "rgb(17, 34, 51)"},
    {"border-color:RED rgb(1,2,3) TRANSPARENT", "border-color", "red rgb(1, 2, 3) transparent"},
    {"border-color:red rgb(1,2,3) transparent", "border-left-color", "rgb(1, 2, 3)"},
    {"border-color:red green blue black", "border-color", "red green blue black"},
    {"border-color:red green blue black", "border-bottom-color", "blue"},
    {"border-color:red green blue black", "border-left-color", "black"},
    {"border-color:red green red green", "border-color", "red green"},
    {"border-color:red green; border-left-color:#123", "border-color", "red green red rgb(17, 34, 51)"},
    {"border-color:rgba(1,2,3,.5);border-right-color:rgba(1,2,3,50%)", "border-color", "rgba(1, 2, 3, 0.5)"},
    {"border-color:red!important;border-left-color:blue", "border-color", "red"},
    {"border-color:red;border-left-color:blue!important", "border-color", ""},
    {"border-color:initial", "border-color", "initial"},
    {"border-color:inherit;border-left-color:initial", "border-color", ""},
    {"border-color:var(--border)", "border-color", "var(--border)"},
    {"border-color:var(--border)", "border-top-color", ""},
    {"border-color:var(--border);border-left-color:red", "border-color", ""},
    {"border-color:red hsl(0,100%,50%)", "border-left-color", "rgb(255, 0, 0)"},
    {"border-color:red hsl(0,100%,50%) blue", "border-bottom-color", "blue"},
    {"border-color:red hsl(0,100%,50%) blue", "border-color", "red rgb(255, 0, 0) blue"},

    /* CSS Lists 3 §3.6 and Multicol 1 §3.3: unordered shorthand components. */
    {"list-style:none", "list-style", "none"},
    {"list-style:none none", "list-style", "none"},
    {"list-style:NONE DISC", "list-style", "disc"},
    {"list-style:NONE DISC", "list-style-type", "disc"},
    {"list-style:NONE DISC", "list-style-image", "none"},
    {"list-style:DISC NONE", "list-style-position", "outside"},
    {"list-style:NONE INSIDE", "list-style", "inside none"},
    {"list-style:none url(Marker.png)", "list-style", "url(\"Marker.png\") none"},
    {"list-style:url(Marker.png) none", "list-style-type", "none"},
    {"list-style:url(Marker.png) none", "list-style-image", "url(\"Marker.png\")"},
    {"list-style:circle url(Marker.png) inside", "list-style", "inside url(\"Marker.png\") circle"},
    {"list-style:circle url(Marker.png) inside", "list-style-position", "inside"},
    {"list-style:outside disc none", "list-style", "disc"},
    {"list-style:inside", "list-style-type", "disc"},
    {"list-style:inside", "list-style-image", "none"},
    {"list-style:inside", "list-style", "inside"},
    {"list-style:url(Marker.png)", "list-style", "url(\"Marker.png\")"},
    {"list-style:outside", "list-style", "disc"},
    {"list-style:circle inside;list-style-type:square", "list-style", "inside square"},
    {"list-style:none;list-style-image:url(New.png)", "list-style", "url(\"New.png\") none"},
    {"list-style:none!important;list-style-type:disc", "list-style", "none"},
    {"list-style:none;list-style-type:disc!important", "list-style", ""},
    {"list-style-type:disc;list-style-position:inside", "list-style", ""},
    {"list-style:inherit", "list-style", "inherit"},
    {"list-style:initial", "list-style-image", "initial"},
    {"list-style:unset", "list-style", "unset"},
    {"list-style:revert", "list-style-type", "revert"},
    {"list-style:inherit;list-style-type:initial", "list-style", ""},
    {"list-style:var(--Marker, none)", "list-style", "var(--Marker, none)"},
    {"list-style:var(--Marker)", "list-style-image", ""},
    {"list-style:var(--Marker);list-style-position:inside", "list-style", ""},
    {"columns:auto 12EM", "columns", "12em"},
    {"columns:12em auto", "column-width", "12em"},
    {"columns:auto 12em", "column-count", "auto"},
    {"columns:3 auto", "columns", "3"},
    {"columns:auto 3", "column-width", "auto"},
    {"columns:3 auto", "column-count", "3"},
    {"columns:3 12em", "columns", "12em 3"},
    {"columns:12em 3", "columns", "12em 3"},
    {"columns:auto auto", "columns", "auto"},
    {"columns:0", "columns", "0px"},
    {"columns:-0 auto", "columns", "0px"},
    {"columns:2 0", "columns", "0px 2"},
    {"columns:0 2", "column-count", "2"},
    {"columns:0 2", "column-width", "0px"},
    {"column-width:0px", "column-width", "0px"},
    {"columns:+0003 .1234567EM", "columns", "0.123457em 3"},
    {"columns:12em 2;column-count:3", "columns", "12em 3"},
    {"columns:12em 2!important;column-count:3", "columns", "12em 2"},
    {"columns:12em 2;column-count:3!important", "columns", ""},
    {"column-width:12em", "columns", ""},
    {"columns:initial", "columns", "initial"},
    {"columns:inherit", "column-count", "inherit"},
    {"columns:revert", "columns", "revert"},
    {"columns:unset", "column-width", "unset"},
    {"columns:inherit;column-count:initial", "columns", ""},
    {"columns:var(--Columns, 12em 2)", "columns", "var(--Columns, 12em 2)"},
    {"columns:var(--Columns)", "column-count", ""},
    {"columns:var(--Columns);column-width:auto", "columns", ""},
    {"columns:calc(12px + 2px) 2", "columns", NULL},

    /* Border/outline/rule specified components, including resets and side overrides. */
    {"border:none", "border", "none"},
    {"border:solid", "border", "solid"},
    {"border:THICK", "border", "thick"},
    {"border:RED", "border", "red"},
    {"border:dashed red 2PX", "border", "2px dashed red"},
    {"border:0", "border", "0px"},
    {"border:none", "border-left-width", "medium"},
    {"border:none", "border-left-color", "currentcolor"},
    {"border:2px", "border-left-style", "none"},
    {"border:solid", "border-left-width", "medium"},
    {"border:solid rgb(1.2 3.4 5.6 / .25) .1234567EM", "border-left-color", "rgba(1.2, 3.4, 5.6, 0.25)"},
    {"border:solid rgb(1.2 3.4 5.6 / .25) .1234567EM", "border-left-width", "0.123457em"},
    {"border:initial", "border", "initial"},
    {"border:unset", "border-left-style", "unset"},
    {"border:revert", "border-left-width", "revert"},
    {"border:inherit", "border-left-color", "inherit"},
    {"border:var(--Edge, thin solid)", "border", "var(--Edge, thin solid)"},
    {"border:var(--Edge)", "border-left-color", ""},
    {"border:var(--Edge);border-left-width:1px", "border", ""},
    {"border:solid!important;border-left-width:3px", "border", "solid"},
    {"border:solid;border-left-width:3px!important", "border", ""},
    {"border:solid;border-left-color:inherit", "border", ""},
    {"border:calc(1px + 2px) solid red", "border", NULL},
    {"border:calc(1px + 2px) solid red", "border-left-color", "red"},
    {"border:solid oklch(50% .1 0)", "border-left-width", "medium"},
    {"border:solid oklch(50% .1 0)", "border-left-color", NULL},
    {"border-top:none", "border-top", "none"},
    {"border-top:solid", "border-top", "solid"},
    {"border-top:THICK", "border-top", "thick"},
    {"border-top:RED", "border-top", "red"},
    {"border-top:dashed red 2PX", "border-top", "2px dashed red"},
    {"border-top:0", "border-top", "0px"},
    {"border-top:none", "border-top-width", "medium"},
    {"border-top:none", "border-top-color", "currentcolor"},
    {"border-top:2px", "border-top-style", "none"},
    {"border-top:solid", "border-top-width", "medium"},
    {"border-top:solid rgb(1.2 3.4 5.6 / .25) .1234567EM", "border-top-color", "rgba(1.2, 3.4, 5.6, 0.25)"},
    {"border-top:solid rgb(1.2 3.4 5.6 / .25) .1234567EM", "border-top-width", "0.123457em"},
    {"border-top:initial", "border-top", "initial"},
    {"border-top:unset", "border-top-style", "unset"},
    {"border-top:revert", "border-top-width", "revert"},
    {"border-top:inherit", "border-top-color", "inherit"},
    {"border-top:var(--Edge, thin solid)", "border-top", "var(--Edge, thin solid)"},
    {"border-top:var(--Edge)", "border-top-color", ""},
    {"border-top:var(--Edge);border-top-width:1px", "border-top", ""},
    {"border-top:solid!important;border-top-width:3px", "border-top", "solid"},
    {"border-top:solid;border-top-width:3px!important", "border-top", ""},
    {"border-top:solid;border-top-color:inherit", "border-top", ""},
    {"border-top:calc(1px + 2px) solid red", "border-top", NULL},
    {"border-top:calc(1px + 2px) solid red", "border-top-color", "red"},
    {"border-top:solid oklch(50% .1 0)", "border-top-width", "medium"},
    {"border-top:solid oklch(50% .1 0)", "border-top-color", NULL},
    {"border-right:none", "border-right", "none"},
    {"border-right:solid", "border-right", "solid"},
    {"border-right:THICK", "border-right", "thick"},
    {"border-right:RED", "border-right", "red"},
    {"border-right:dashed red 2PX", "border-right", "2px dashed red"},
    {"border-right:0", "border-right", "0px"},
    {"border-right:none", "border-right-width", "medium"},
    {"border-right:none", "border-right-color", "currentcolor"},
    {"border-right:2px", "border-right-style", "none"},
    {"border-right:solid", "border-right-width", "medium"},
    {"border-right:solid rgb(1.2 3.4 5.6 / .25) .1234567EM", "border-right-color", "rgba(1.2, 3.4, 5.6, 0.25)"},
    {"border-right:solid rgb(1.2 3.4 5.6 / .25) .1234567EM", "border-right-width", "0.123457em"},
    {"border-right:initial", "border-right", "initial"},
    {"border-right:unset", "border-right-style", "unset"},
    {"border-right:revert", "border-right-width", "revert"},
    {"border-right:inherit", "border-right-color", "inherit"},
    {"border-right:var(--Edge, thin solid)", "border-right", "var(--Edge, thin solid)"},
    {"border-right:var(--Edge)", "border-right-color", ""},
    {"border-right:var(--Edge);border-right-width:1px", "border-right", ""},
    {"border-right:solid!important;border-right-width:3px", "border-right", "solid"},
    {"border-right:solid;border-right-width:3px!important", "border-right", ""},
    {"border-right:solid;border-right-color:inherit", "border-right", ""},
    {"border-right:calc(1px + 2px) solid red", "border-right", NULL},
    {"border-right:calc(1px + 2px) solid red", "border-right-color", "red"},
    {"border-right:solid oklch(50% .1 0)", "border-right-width", "medium"},
    {"border-right:solid oklch(50% .1 0)", "border-right-color", NULL},
    {"border-bottom:none", "border-bottom", "none"},
    {"border-bottom:solid", "border-bottom", "solid"},
    {"border-bottom:THICK", "border-bottom", "thick"},
    {"border-bottom:RED", "border-bottom", "red"},
    {"border-bottom:dashed red 2PX", "border-bottom", "2px dashed red"},
    {"border-bottom:0", "border-bottom", "0px"},
    {"border-bottom:none", "border-bottom-width", "medium"},
    {"border-bottom:none", "border-bottom-color", "currentcolor"},
    {"border-bottom:2px", "border-bottom-style", "none"},
    {"border-bottom:solid", "border-bottom-width", "medium"},
    {"border-bottom:solid rgb(1.2 3.4 5.6 / .25) .1234567EM", "border-bottom-color", "rgba(1.2, 3.4, 5.6, 0.25)"},
    {"border-bottom:solid rgb(1.2 3.4 5.6 / .25) .1234567EM", "border-bottom-width", "0.123457em"},
    {"border-bottom:initial", "border-bottom", "initial"},
    {"border-bottom:unset", "border-bottom-style", "unset"},
    {"border-bottom:revert", "border-bottom-width", "revert"},
    {"border-bottom:inherit", "border-bottom-color", "inherit"},
    {"border-bottom:var(--Edge, thin solid)", "border-bottom", "var(--Edge, thin solid)"},
    {"border-bottom:var(--Edge)", "border-bottom-color", ""},
    {"border-bottom:var(--Edge);border-bottom-width:1px", "border-bottom", ""},
    {"border-bottom:solid!important;border-bottom-width:3px", "border-bottom", "solid"},
    {"border-bottom:solid;border-bottom-width:3px!important", "border-bottom", ""},
    {"border-bottom:solid;border-bottom-color:inherit", "border-bottom", ""},
    {"border-bottom:calc(1px + 2px) solid red", "border-bottom", NULL},
    {"border-bottom:calc(1px + 2px) solid red", "border-bottom-color", "red"},
    {"border-bottom:solid oklch(50% .1 0)", "border-bottom-width", "medium"},
    {"border-bottom:solid oklch(50% .1 0)", "border-bottom-color", NULL},
    {"border-left:none", "border-left", "none"},
    {"border-left:solid", "border-left", "solid"},
    {"border-left:THICK", "border-left", "thick"},
    {"border-left:RED", "border-left", "red"},
    {"border-left:dashed red 2PX", "border-left", "2px dashed red"},
    {"border-left:0", "border-left", "0px"},
    {"border-left:none", "border-left-width", "medium"},
    {"border-left:none", "border-left-color", "currentcolor"},
    {"border-left:2px", "border-left-style", "none"},
    {"border-left:solid", "border-left-width", "medium"},
    {"border-left:solid rgb(1.2 3.4 5.6 / .25) .1234567EM", "border-left-color", "rgba(1.2, 3.4, 5.6, 0.25)"},
    {"border-left:solid rgb(1.2 3.4 5.6 / .25) .1234567EM", "border-left-width", "0.123457em"},
    {"border-left:initial", "border-left", "initial"},
    {"border-left:unset", "border-left-style", "unset"},
    {"border-left:revert", "border-left-width", "revert"},
    {"border-left:inherit", "border-left-color", "inherit"},
    {"border-left:var(--Edge, thin solid)", "border-left", "var(--Edge, thin solid)"},
    {"border-left:var(--Edge)", "border-left-color", ""},
    {"border-left:var(--Edge);border-left-width:1px", "border-left", ""},
    {"border-left:solid!important;border-left-width:3px", "border-left", "solid"},
    {"border-left:solid;border-left-width:3px!important", "border-left", ""},
    {"border-left:solid;border-left-color:inherit", "border-left", ""},
    {"border-left:calc(1px + 2px) solid red", "border-left", NULL},
    {"border-left:calc(1px + 2px) solid red", "border-left-color", "red"},
    {"border-left:solid oklch(50% .1 0)", "border-left-width", "medium"},
    {"border-left:solid oklch(50% .1 0)", "border-left-color", NULL},
    {"outline:none", "outline", "none"},
    {"outline:solid", "outline", "solid"},
    {"outline:THICK", "outline", "thick"},
    {"outline:RED", "outline", "red"},
    {"outline:dashed red 2PX", "outline", "red dashed 2px"},
    {"outline:0", "outline", "0px"},
    {"outline:none", "outline-width", "medium"},
    {"outline:none", "outline-color", "invert"},
    {"outline:2px", "outline-style", "none"},
    {"outline:solid", "outline-width", "medium"},
    {"outline:solid rgb(1.2 3.4 5.6 / .25) .1234567EM", "outline-color", "rgba(1.2, 3.4, 5.6, 0.25)"},
    {"outline:solid rgb(1.2 3.4 5.6 / .25) .1234567EM", "outline-width", "0.123457em"},
    {"outline:initial", "outline", "initial"},
    {"outline:unset", "outline-style", "unset"},
    {"outline:revert", "outline-width", "revert"},
    {"outline:inherit", "outline-color", "inherit"},
    {"outline:var(--Edge, thin solid)", "outline", "var(--Edge, thin solid)"},
    {"outline:var(--Edge)", "outline-color", ""},
    {"outline:var(--Edge);outline-width:1px", "outline", ""},
    {"outline:solid!important;outline-width:3px", "outline", "solid"},
    {"outline:solid;outline-width:3px!important", "outline", ""},
    {"outline:solid;outline-color:inherit", "outline", ""},
    {"outline:calc(1px + 2px) solid red", "outline", NULL},
    {"outline:calc(1px + 2px) solid red", "outline-color", "red"},
    {"outline:solid oklch(50% .1 0)", "outline-width", "medium"},
    {"outline:solid oklch(50% .1 0)", "outline-color", NULL},
    {"column-rule:none", "column-rule", "none"},
    {"column-rule:solid", "column-rule", "solid"},
    {"column-rule:THICK", "column-rule", "thick"},
    {"column-rule:RED", "column-rule", "red"},
    {"column-rule:dashed red 2PX", "column-rule", "2px dashed red"},
    {"column-rule:0", "column-rule", "0px"},
    {"column-rule:none", "column-rule-width", "medium"},
    {"column-rule:none", "column-rule-color", "currentcolor"},
    {"column-rule:2px", "column-rule-style", "none"},
    {"column-rule:solid", "column-rule-width", "medium"},
    {"column-rule:solid rgb(1.2 3.4 5.6 / .25) .1234567EM", "column-rule-color", "rgba(1.2, 3.4, 5.6, 0.25)"},
    {"column-rule:solid rgb(1.2 3.4 5.6 / .25) .1234567EM", "column-rule-width", "0.123457em"},
    {"column-rule:initial", "column-rule", "initial"},
    {"column-rule:unset", "column-rule-style", "unset"},
    {"column-rule:revert", "column-rule-width", "revert"},
    {"column-rule:inherit", "column-rule-color", "inherit"},
    {"column-rule:var(--Edge, thin solid)", "column-rule", "var(--Edge, thin solid)"},
    {"column-rule:var(--Edge)", "column-rule-color", ""},
    {"column-rule:var(--Edge);column-rule-width:1px", "column-rule", ""},
    {"column-rule:solid!important;column-rule-width:3px", "column-rule", "solid"},
    {"column-rule:solid;column-rule-width:3px!important", "column-rule", ""},
    {"column-rule:solid;column-rule-color:inherit", "column-rule", ""},
    {"column-rule:calc(1px + 2px) solid red", "column-rule", NULL},
    {"column-rule:calc(1px + 2px) solid red", "column-rule-color", "red"},
    {"column-rule:solid oklch(50% .1 0)", "column-rule-width", "medium"},
    {"column-rule:solid oklch(50% .1 0)", "column-rule-color", NULL},
    {"border:2px solid red;border-left-color:blue", "border", ""},
    {"border:2px solid red;border-left-color:blue", "border-left", "2px solid blue"},
    {"border:2px solid red;border-left-width:3px", "border-width", "2px 2px 2px 3px"},
    {"border:2px solid red;border-bottom-style:dashed", "border-style", "solid solid dashed"},
    {"border:2px solid red;border-left-color:blue", "border-color", "red red red blue"},
    {"border:2px solid red", "border-right-width", "2px"},
    {"border:2px solid red;border-width:3px", "border", "3px solid red"},
    {"border:2px solid red;border-color:blue", "border", "2px solid blue"},
    {"border:2px solid red;border-style:dashed", "border", "2px dashed red"},
    {"border-width:2px;border-style:solid;border-color:red", "border", "2px solid red"},
    {"border-top:solid;border-right:solid;border-bottom:solid;border-left:solid", "border", "solid"},
    {"border:0 red;border-color:currentcolor", "border", "0px"},
    {"border:000 solid", "border", "0px solid"},
    {"border:2px solid red;border-left-color:#ff0000", "border", ""},
    {"outline:currentcolor", "outline", "currentcolor"},
    {"outline:invert", "outline", "none"},
    {"border-top-width:2px;border-top-style:solid", "border-top", ""},

    /* CSS Grid 2 numeric line placement and shorthand defaults. */
    {"grid-row-start:AUTO", "grid-row-start", "auto"},
    {"grid-row-start:+0002", "grid-row-start", "2"},
    {"grid-row-start:-003", "grid-row-start", "-3"},
    {"grid-row-start:SPAN +002", "grid-row-start", "span 2"},
    {"grid-row-start:003 span", "grid-row-start", "span 3"},
    {"grid-row-start:inherit", "grid-row-start", "inherit"},
    {"grid-row-start:var(--line)", "grid-row-start", "var(--line)"},
    {"grid-row-start:2;grid-row-start:0", "grid-row-start", "2"},
    {"grid-row-start:2;grid-row-start:span", "grid-row-start", "2"},
    {"grid-row-start:2;grid-row-start:span -1", "grid-row-start", "2"},
    {"grid-row-start:2;grid-row-start:span 0", "grid-row-start", "2"},
    {"grid-row-start:2;grid-row-start:1.0", "grid-row-start", "2"},
    {"grid-row-start:2;grid-row-start:1px", "grid-row-start", "2"},
    {"grid-row-start:2;grid-row-start:span initial", "grid-row-start", "2"},
    {"grid-row-end:AUTO", "grid-row-end", "auto"},
    {"grid-row-end:+0002", "grid-row-end", "2"},
    {"grid-row-end:-003", "grid-row-end", "-3"},
    {"grid-row-end:SPAN +002", "grid-row-end", "span 2"},
    {"grid-row-end:003 span", "grid-row-end", "span 3"},
    {"grid-row-end:inherit", "grid-row-end", "inherit"},
    {"grid-row-end:var(--line)", "grid-row-end", "var(--line)"},
    {"grid-row-end:2;grid-row-end:0", "grid-row-end", "2"},
    {"grid-row-end:2;grid-row-end:span", "grid-row-end", "2"},
    {"grid-row-end:2;grid-row-end:span -1", "grid-row-end", "2"},
    {"grid-row-end:2;grid-row-end:span 0", "grid-row-end", "2"},
    {"grid-row-end:2;grid-row-end:1.0", "grid-row-end", "2"},
    {"grid-row-end:2;grid-row-end:1px", "grid-row-end", "2"},
    {"grid-row-end:2;grid-row-end:span initial", "grid-row-end", "2"},
    {"grid-column-start:AUTO", "grid-column-start", "auto"},
    {"grid-column-start:+0002", "grid-column-start", "2"},
    {"grid-column-start:-003", "grid-column-start", "-3"},
    {"grid-column-start:SPAN +002", "grid-column-start", "span 2"},
    {"grid-column-start:003 span", "grid-column-start", "span 3"},
    {"grid-column-start:inherit", "grid-column-start", "inherit"},
    {"grid-column-start:var(--line)", "grid-column-start", "var(--line)"},
    {"grid-column-start:2;grid-column-start:0", "grid-column-start", "2"},
    {"grid-column-start:2;grid-column-start:span", "grid-column-start", "2"},
    {"grid-column-start:2;grid-column-start:span -1", "grid-column-start", "2"},
    {"grid-column-start:2;grid-column-start:span 0", "grid-column-start", "2"},
    {"grid-column-start:2;grid-column-start:1.0", "grid-column-start", "2"},
    {"grid-column-start:2;grid-column-start:1px", "grid-column-start", "2"},
    {"grid-column-start:2;grid-column-start:span initial", "grid-column-start", "2"},
    {"grid-column-end:AUTO", "grid-column-end", "auto"},
    {"grid-column-end:+0002", "grid-column-end", "2"},
    {"grid-column-end:-003", "grid-column-end", "-3"},
    {"grid-column-end:SPAN +002", "grid-column-end", "span 2"},
    {"grid-column-end:003 span", "grid-column-end", "span 3"},
    {"grid-column-end:inherit", "grid-column-end", "inherit"},
    {"grid-column-end:var(--line)", "grid-column-end", "var(--line)"},
    {"grid-column-end:2;grid-column-end:0", "grid-column-end", "2"},
    {"grid-column-end:2;grid-column-end:span", "grid-column-end", "2"},
    {"grid-column-end:2;grid-column-end:span -1", "grid-column-end", "2"},
    {"grid-column-end:2;grid-column-end:span 0", "grid-column-end", "2"},
    {"grid-column-end:2;grid-column-end:1.0", "grid-column-end", "2"},
    {"grid-column-end:2;grid-column-end:1px", "grid-column-end", "2"},
    {"grid-column-end:2;grid-column-end:span initial", "grid-column-end", "2"},
    {"grid-row:AUTO", "grid-row", "auto"},
    {"grid-row:2 / auto", "grid-row", "2"},
    {"grid-row:auto / 3", "grid-row", "auto / 3"},
    {"grid-row:2 / 2", "grid-row", "2 / 2"},
    {"grid-row:SPAN +002 / -003", "grid-row", "span 2 / -3"},
    {"grid-row:2 span / 3 span", "grid-row", "span 2 / span 3"},
    {"grid-row:initial", "grid-row", "initial"},
    {"grid-row:var(--line, 2 / 3)", "grid-row", "var(--line, 2 / 3)"},
    {"grid-row:span 2", "grid-row-end", "auto"},
    {"grid-row:2 / span 3", "grid-row-end", "span 3"},
    {"grid-row:2 / 3;grid-row-end:4", "grid-row", "2 / 4"},
    {"grid-row:2 / 3!important;grid-row-end:4", "grid-row", "2 / 3"},
    {"grid-row:2 / 3;grid-row-end:4!important", "grid-row", ""},
    {"grid-column:AUTO", "grid-column", "auto"},
    {"grid-column:2 / auto", "grid-column", "2"},
    {"grid-column:auto / 3", "grid-column", "auto / 3"},
    {"grid-column:2 / 2", "grid-column", "2 / 2"},
    {"grid-column:SPAN +002 / -003", "grid-column", "span 2 / -3"},
    {"grid-column:2 span / 3 span", "grid-column", "span 2 / span 3"},
    {"grid-column:initial", "grid-column", "initial"},
    {"grid-column:var(--line, 2 / 3)", "grid-column", "var(--line, 2 / 3)"},
    {"grid-column:span 2", "grid-column-end", "auto"},
    {"grid-column:2 / span 3", "grid-column-end", "span 3"},
    {"grid-column:2 / 3;grid-column-end:4", "grid-column", "2 / 4"},
    {"grid-column:2 / 3!important;grid-column-end:4", "grid-column", "2 / 3"},
    {"grid-column:2 / 3;grid-column-end:4!important", "grid-column", ""},
    {"grid-area:auto / auto / auto / auto", "grid-area", "auto"},
    {"grid-area:2 / auto / auto / auto", "grid-area", "2"},
    {"grid-area:2 / 3 / auto / auto", "grid-area", "2 / 3"},
    {"grid-area:2 / auto / 4 / auto", "grid-area", "2 / auto / 4"},
    {"grid-area:2 / auto / auto / 5", "grid-area", "2 / auto / auto / 5"},
    {"grid-area:2 / 3 / 2 / 3", "grid-area", "2 / 3 / 2 / 3"},
    {"grid-area:2 / span 2 / 3 span / -1", "grid-area", "2 / span 2 / span 3 / -1"},
    {"grid-area:initial", "grid-area", "initial"},
    {"grid-area:var(--area)", "grid-area", "var(--area)"},
    {"grid-area:2", "grid-column-start", "auto"},
    {"grid-area:2", "grid-row-end", "auto"},
    {"grid-area:2", "grid-column-end", "auto"},
    {"grid-area:2 / 3 / span 4 / 5", "grid-row", "2 / span 4"},
    {"grid-area:2 / 3 / 4 / span 5", "grid-column", "3 / span 5"},
    {"grid-area:2 / 3 / 4 / 5;grid-row:6", "grid-area", "6 / 3 / auto / 5"},
    {"grid-row:2 / 4;grid-column:3 / 5", "grid-area", "2 / 3 / 4 / 5"},
    {"grid-area:var(--a)", "grid-row", ""},
    {"grid-area:initial;grid-column-end:unset", "grid-area", ""},
    {"grid-area:1;grid-row-end:2!important", "grid-area", ""},
    {"grid-column-start:span 12345678", "grid-column-start", "span 12345678"},
    {"grid-row:-12345678 / 23456789", "grid-row", "-12345678 / 23456789"},

};
enum { VALUE_CASE_COUNT = sizeof(value_cases) / sizeof(value_cases[0]) };

START_TEST(test_specified_values)
{
    for (unsigned chunks = 0; chunks < 2; chunks++) {
        css_declaration_list *list = parse(value_cases[_i].css, chunks != 0);
        css_declaration_order *order;
        ck_assert_int_eq(css__declaration_order_create(list, &order), CSS_OK);
        lwc_string *value = NULL;
        css_error error = serialize_property(order, value_cases[_i].property, &value);
        ck_assert_msg(error == (value_cases[_i].expected == NULL ? CSS_INVALID : CSS_OK),
            "case %d chunks %u: %s / %s: error %d", _i, chunks, value_cases[_i].css,
            value_cases[_i].property, error);
        if (value_cases[_i].expected == NULL) {
            ck_assert_ptr_null(value);
        } else {
            ck_assert_msg(strcmp(lwc_string_data(value), value_cases[_i].expected) == 0,
                "case %d chunks %u: %s / %s: expected <%s>, got <%s>", _i, chunks, value_cases[_i].css,
                value_cases[_i].property, value_cases[_i].expected, lwc_string_data(value));
        }
        /* Specified values must survive serialize/reparse, including values
         * more precise or larger than the renderer's fixed-point storage.
         * Some logical members currently only have shorthand parse handlers. */
        const char *name = value_cases[_i].property;
        if (value != NULL && lwc_string_length(value) != 0 &&
            (strncmp(name, "--", 2) == 0 || css_prop_lookup(name, strlen(name)) != NULL)) {
            size_t length = strlen(name) + 1 + lwc_string_length(value);
            char *css = malloc(length + 1);
            ck_assert_ptr_nonnull(css);
            snprintf(css, length + 1, "%s:%s", name, lwc_string_data(value));
            css_declaration_list *roundtrip = parse(css, chunks != 0);
            free(css);
            css_declaration_order *roundtrip_order;
            ck_assert_int_eq(css__declaration_order_create(roundtrip, &roundtrip_order), CSS_OK);
            lwc_string *roundtrip_value;
            ck_assert_int_eq(serialize_property(roundtrip_order, name, &roundtrip_value), CSS_OK);
            compare_strings(value, roundtrip_value);
            lwc_string_unref(roundtrip_value);
            css__declaration_order_destroy(roundtrip_order);
            css__declaration_list_destroy(roundtrip);
        }
        css__declaration_order_destroy(order);
        css__declaration_list_destroy(list);
        /* The returned string is owned, independent of both borrowed views. */
        if (value != NULL)
            lwc_string_unref(value);
    }
}
END_TEST

/* CSS Lists <counter-name> excludes CSS-wide keywords, default and none.
 * Exercise both counter functions and every position in the generated parsers.
 * A rejected declaration must leave the earlier valid declaration in place. */
START_TEST(test_reserved_counter_names)
{
    static const char *const names[] = {
        "none", "INHERIT", "Initial", "uNsEt", "REVERT", "revert-layer", "Default", "\\6e one", "\\64 efault"};
    static const struct {
        const char *property, *format, *expected;
    } forms[] = {
        {"content", "counter(%s)", "\"kept\""},
        {"content", "counters(%s, '.')", "\"kept\""},
        {"counter-reset", "%s 2", "kept 2"},
        {"counter-reset", "A 1 %s", "kept 2"},
        {"counter-increment", "%s 2", "kept 2"},
        {"counter-increment", "A 1 %s", "kept 2"},
    };
    for (size_t n = 0; n < sizeof(names) / sizeof(names[0]); n++) {
        for (size_t f = 0; f < sizeof(forms) / sizeof(forms[0]); f++) {
            char invalid[100], css[256];
            snprintf(invalid, sizeof(invalid), forms[f].format, names[n]);
            snprintf(css, sizeof(css), "%s:%s; %s:%s", forms[f].property, forms[f].expected,
                forms[f].property, invalid);
            css_declaration_list *list = parse(css, _i != 0);
            ck_assert_msg(css__declaration_list_count(list) == 1, "accepted reserved counter name: %s", css);
            css_declaration_order *order;
            ck_assert_int_eq(css__declaration_order_create(list, &order), CSS_OK);
            lwc_string *value;
            ck_assert_int_eq(serialize_property(order, forms[f].property, &value), CSS_OK);
            ck_assert_str_eq(lwc_string_data(value), forms[f].expected);
            lwc_string_unref(value);
            css__declaration_order_destroy(order);
            css__declaration_list_destroy(list);
        }
    }
}
END_TEST


/* Inspect rendering strings too: a successful serializer alone must not hide a
 * disagreement between the native font name and the CSSOM name. */
START_TEST(test_font_name_storage)
{
    static const struct { const char *value, *name; } forms[] = {
        {"Open/**/Sans", "Open Sans"}, {"Open\tSans", "Open Sans"},
        {"Open\\20 ", "Open "}, {"Open\\20 /**/Sans\\20 ", "Open  Sans "},
        {"\\20 Open\\20 ", " Open "}, {"''", ""}, {"'A  B'", "A  B"},
        {"\\31 2 \\2d -Name", "12 --Name"}, {"Știri 字体", "Știri 字体"},
    };
    static const char *const prefixes[] = {"font-family:", "font:italic 16px/1.2 "};
    for (size_t f = 0; f < N_ELEMENTS(forms); f++) {
        for (size_t p = 0; p < N_ELEMENTS(prefixes); p++) {
            char css[160];
            snprintf(css, sizeof(css), "%s%s", prefixes[p], forms[f].value);
            css_declaration_list *list = parse(css, _i != 0);
            const css_stylesheet *sheet = css__declaration_list_sheet(list);
            ck_assert_msg(css__declaration_list_count(list) == 1 && sheet->string_vector_c == 1,
                "font-name storage: %s", css);
            ck_assert_str_eq(lwc_string_data(sheet->string_vector[0]), forms[f].name);
            css__declaration_list_destroy(list);
        }
    }
}
END_TEST

static css_stylesheet *font_sheet(const char *css, bool chunks)
{
    css_stylesheet_params full = params;
    full.inline_style = false;
    css_stylesheet *sheet;
    ck_assert_int_eq(css_stylesheet_create(&full, &sheet), CSS_OK);
    size_t length = strlen(css);
    if (chunks) {
        for (size_t i = 0; i < length; i++)
            check_append(css_stylesheet_append_data(sheet, (const uint8_t *)css + i, 1));
    } else {
        check_append(css_stylesheet_append_data(sheet, (const uint8_t *)css, length));
    }
    ck_assert_int_eq(css_stylesheet_data_done(sheet), CSS_OK);
    return sheet;
}

static const css_font_face *only_font_face(const css_stylesheet *sheet)
{
    ck_assert_ptr_nonnull(sheet->rule_list);
    ck_assert_int_eq(sheet->rule_list->type, CSS_RULE_FONT_FACE);
    ck_assert_ptr_null(sheet->rule_list->next);
    return ((const css_rule_font_face *)sheet->rule_list)->font_face;
}

static void check_font_face_names(const css_font_face *face, const char *family, const char *local)
{
    lwc_string *name;
    ck_assert_int_eq(css_font_face_get_font_family(face, &name), CSS_OK);
    ck_assert_ptr_nonnull(name);
    ck_assert_str_eq(lwc_string_data(name), family);
    uint32_t count;
    ck_assert_int_eq(css_font_face_count_srcs(face, &count), CSS_OK);
    ck_assert_uint_eq(count, 1);
    const css_font_face_src *src;
    ck_assert_int_eq(css_font_face_get_src(face, 0, &src), CSS_OK);
    ck_assert_int_eq(css_font_face_src_location_type(src), CSS_FONT_FACE_LOCATION_TYPE_LOCAL);
    ck_assert_int_eq(css_font_face_src_get_location(src, &name), CSS_OK);
    ck_assert_str_eq(lwc_string_data(name), local);
}

START_TEST(test_font_face_names)
{
    css_stylesheet *sheet = font_sheet(
        "@font-face{font-family:Open/**/Sans\\20 ;src:local(\\20 Open/**/Sans\\20 )}", _i != 0);
    check_font_face_names(only_font_face(sheet), "Open Sans ", " Open Sans ");
    css_stylesheet_destroy(sheet);
    /* Quoting distinguishes a named font from a keyword. */
    sheet = font_sheet("@font-face{font-family:'serif';src:local('inherit')}", _i != 0);
    check_font_face_names(only_font_face(sheet), "serif", "inherit");
    css_stylesheet_destroy(sheet);
    /* A later invalid source must release already collected source strings. */
    sheet = font_sheet("@font-face{font-family:Kept;src:local(Kept);"
        "src:local(Temporary),url(temporary.woff),local(inherit)}", _i != 0);
    css_stylesheet_destroy(sheet);
}
END_TEST

START_TEST(test_reserved_font_names)
{
    static const char *const names[] = {"inherit", "INITIAL", "unset", "revert", "revert-layer", "DEFAULT",
        "\\69 nherit", "de\\66 ault"};
    static const char *const forms[] = {"A %s", "%s A", "A,%s", "%s,A"};
    for (size_t n = 0; n < N_ELEMENTS(names); n++) {
        for (size_t f = 0; f < N_ELEMENTS(forms); f++) {
            char value[80], css[240];
            snprintf(value, sizeof(value), forms[f], names[n]);
            snprintf(css, sizeof(css), "font-family:Kept; font-family:%s; font:16px %s", value, value);
            css_declaration_list *list = parse(css, _i != 0);
            ck_assert_msg(css__declaration_list_count(list) == 1, "accepted invalid family: %s", css);
            css__declaration_list_destroy(list);
        }
        /* An invalid descriptor must not replace the previous valid value. */
        char css[240];
        snprintf(css, sizeof(css), "@font-face{font-family:Kept;src:local(Kept);"
            "font-family:%s;src:local(%s)}", names[n], names[n]);
        css_stylesheet *sheet = font_sheet(css, _i != 0);
        check_font_face_names(only_font_face(sheet), "Kept", "Kept");
        css_stylesheet_destroy(sheet);
    }
    static const char *const generics[] = {"serif", "sans-serif", "cursive", "fantasy", "monospace"};
    for (size_t g = 0; g < N_ELEMENTS(generics); g++) {
        char css[240];
        snprintf(css, sizeof(css), "@font-face{font-family:Kept;src:local(Kept);"
            "font-family:%s;src:local(%s)}", generics[g], generics[g]);
        css_stylesheet *sheet = font_sheet(css, _i != 0);
        check_font_face_names(only_font_face(sheet), "Kept", "Kept");
        css_stylesheet_destroy(sheet);
    }
}
END_TEST

START_TEST(test_long_font_name)
{
    enum { WORDS = 2048 };
    const char *prefix = "font-family:";
    char *css = malloc(strlen(prefix) + WORDS * 7 + 1);
    ck_assert_ptr_nonnull(css);
    char *p = stpcpy(css, prefix);
    for (size_t i = 0; i < WORDS; i++)
        p = stpcpy(p, "Abc/**/");
    css_declaration_list *list = parse(css, _i != 0);
    free(css);
    css_declaration_order *order;
    ck_assert_int_eq(css__declaration_order_create(list, &order), CSS_OK);
    lwc_string *value;
    ck_assert_int_eq(serialize_property(order, "font-family", &value), CSS_OK);
    ck_assert_uint_eq(lwc_string_length(value), 4 * WORDS + 1);
    const char *text = lwc_string_data(value);
    ck_assert_int_eq(text[0], '"');
    for (size_t i = 0; i < WORDS; i++) {
        ck_assert_int_eq(memcmp(text + 1 + 4 * i, "Abc", 3), 0);
        ck_assert_int_eq(text[4 + 4 * i], i + 1 == WORDS ? '"' : ' ');
    }
    lwc_string_unref(value);
    css__declaration_order_destroy(order);
    css__declaration_list_destroy(list);
}
END_TEST

START_TEST(test_clip_separators)
{
    for (unsigned mask = 0; mask < 8; mask++) {
        char css[100];
        snprintf(css, sizeof(css), "clip:auto;clip:rect(1px%s2px%s3px%s4px)",
            mask & 1 ? "," : " ", mask & 2 ? "," : " ", mask & 4 ? "," : " ");
        css_declaration_list *list = parse(css, _i != 0);
        bool accepted = mask == 0 || mask == 7;
        ck_assert_msg(css__declaration_list_count(list) == (accepted ? 2u : 1u),
            "clip separator mask %u: %s", mask, css);
        css_declaration_order *order;
        ck_assert_int_eq(css__declaration_order_create(list, &order), CSS_OK);
        lwc_string *value;
        ck_assert_int_eq(serialize_property(order, "clip", &value), CSS_OK);
        ck_assert_str_eq(lwc_string_data(value), accepted ? "rect(1px, 2px, 3px, 4px)" : "auto");
        lwc_string_unref(value);
        css__declaration_order_destroy(order);
        css__declaration_list_destroy(list);
    }
}
END_TEST

START_TEST(test_geometry_invalid_units)
{
    static const char *const units[] = {"%", "fr", "dpi", "dpcm", "dppx", "deg", "rad", "grad", "turn", "s", "ms",
        "Hz", "kHz"};
    static const char *const formats[] = {"border-spacing:1%s", "border-spacing:2px 1%s",
        "clip:rect(1%s,auto,auto,auto)", "clip:rect(auto,1%s,auto,auto)",
        "clip:rect(auto,auto,1%s,auto)", "clip:rect(auto,auto,auto,1%s)"};
    for (size_t u = 0; u < sizeof(units) / sizeof(units[0]); u++) {
        for (size_t f = 0; f < sizeof(formats) / sizeof(formats[0]); f++) {
            char css[100];
            snprintf(css, sizeof(css), formats[f], units[u]);
            css_declaration_list *list = parse(css, _i != 0);
            ck_assert_msg(css__declaration_list_count(list) == 0, "accepted invalid length: %s", css);
            css__declaration_list_destroy(list);
        }
    }
}
END_TEST

START_TEST(test_quirky_colour_values)
{
    static const char *const forms[] = {"f00", "123456", "12ab56"};
    static const char *const expected[] = {"rgb(255, 0, 0)", "rgb(18, 52, 86)", "rgb(18, 171, 86)"};
    for (size_t i = 0; i < N_ELEMENTS(forms); i++) {
        char css[32];
        snprintf(css, sizeof(css), "color:%s", forms[i]);
        css_stylesheet_params quirky = params;
        quirky.allow_quirks = true;
        css_declaration_list *list;
        ck_assert_int_eq(css__declaration_list_create(&quirky, &list), CSS_OK);
        check_append(css__declaration_list_append(list, (const uint8_t *)css, strlen(css)));
        ck_assert_int_eq(css__declaration_list_complete(list), CSS_OK);
        ck_assert_uint_eq(css__declaration_list_count(list), 1);
        css_declaration_order *order;
        ck_assert_int_eq(css__declaration_order_create(list, &order), CSS_OK);
        lwc_string *value;
        ck_assert_int_eq(serialize_property(order, "color", &value), CSS_OK);
        ck_assert_str_eq(lwc_string_data(value), expected[i]);
        lwc_string_unref(value);
        css__declaration_order_destroy(order);
        css__declaration_list_destroy(list);
    }
}
END_TEST

/* The specified serializer must not quietly change the colour on reparse.
 * Exhaust all hex alpha bytes, including ones without an integer-percent form. */
START_TEST(test_colour_alpha_roundtrip)
{
    for (unsigned alpha = 0; alpha < 256; alpha++) {
        char css[128];
        snprintf(css, sizeof(css), "color:#123456%02x", alpha);
        css_declaration_list *list = parse(css, _i != 0);
        css_declaration_order *order;
        ck_assert_int_eq(css__declaration_order_create(list, &order), CSS_OK);
        lwc_string *value;
        ck_assert_int_eq(serialize_property(order, "color", &value), CSS_OK);
        snprintf(css, sizeof(css), "color:%s", lwc_string_data(value));
        css_declaration_list *again = parse(css, _i != 0);
        ck_assert_msg(css__declaration_list_count(again) == 1, "rejected alpha %u: %s", alpha, css);
        const css_style *style = inline_style(css__declaration_list_sheet(again));
        ck_assert_msg(style->used == 2 && style->bytecode[1] == (alpha << 24 | 0x123456),
            "alpha %u changed after %s", alpha, css);
        lwc_string_unref(value);
        css__declaration_order_destroy(order);
        css__declaration_list_destroy(list);
        css__declaration_list_destroy(again);
    }
}
END_TEST

START_TEST(test_colour_rendering_values)
{
    static const struct { const char *value; uint32_t packed; } cases[] = {
        {"rgb(50%,0%,100%)", 0xff8000ff},
        {"rgb(1.5,2.5,3.5)", 0xff020304},
        {"rgb(1 50% 2.5 / .5)", 0x80018003},
        {"rgb(1 2 3 / 50%)", 0x80010203},
        {"rgba(1,2,3,.1)", 0x1a010203},
        {"rgba(1,2,3,1000000)", 0xff010203},
        {"rgb(999999999999999999% -999999999999999999% 50%)", 0xffff0080},
        {"rgb(none 50% none / none)", 0x00008000},
        {"rgba(none none none)", 0xff000000},
        {"rgb(0 0 0 / .000001)", 0x00000000},
        {"rgb(0 0 0 / .999999)", 0xff000000},
        {"hsl(120,30%,50%)", 0xff59a659},
        {"hsl(-90deg 100% 50%)", 0xff8000ff},
        {"hsl(1000000080 100% 50%)", 0xffff0000},
        {"hsl(1000000000.25turn 100% 50%)", 0xff80ff00},
        {"hsl(100grad 100% 50%)", 0xff80ff00},
        {"hsl(3.141592653589793rad 100% 50%)", 0xff00ffff},
        {"hsla(0,100%,50%,.5)", 0x80ff0000},
        {"hsla(0 100% 50% / .1)", 0x1aff0000},
        {"hsl(30 -50 60)", 0xff999999},
        {"hsl(none 100% 50%)", 0xffff0000},
        {"hsl(120 none 50% / none)", 0x00808080},
        {"hwb(none none none)", 0xffff0000},
        {"hwb(120 none 50% / none)", 0x00008000},
        {"hwb(740deg 20% 30% / 50%)", 0x80b35e33},
        {"hwb(150 20% 10%)", 0xff33e68c},
        {"hwb(267 19% 60%)", 0xff493066},
        {"hwb(0 400% 200%)", 0xffaaaaaa},
        {"hwb(0 100000000 50000000)", 0xffaaaaaa},
        {"hsl(0 100% 50% / 10000000)", 0xffff0000},

    };
    for (size_t i = 0; i < N_ELEMENTS(cases); i++) {
        char css[128];
        snprintf(css, sizeof(css), "color:%s", cases[i].value);
        css_declaration_list *list = parse(css, _i != 0);
        ck_assert_msg(css__declaration_list_count(list) == 1, "rejected %s", css);
        const css_style *style = inline_style(css__declaration_list_sheet(list));
        ck_assert_msg(style->used == 2 && style->bytecode[1] == cases[i].packed,
            "%s: expected ARGB %08x, got %08x", css, cases[i].packed, style->bytecode[1]);
        css__declaration_list_destroy(list);
    }
}
END_TEST

/* Unsupported colours must discard the entire declaration, including partial
 * shorthand/gradient output, and must not consume the following declaration.
 * Compare rendering bytecode directly, including SVG paint properties whose
 * CSSOM readback is not implemented yet. */
START_TEST(test_unsupported_colour_fallbacks)
{
    static const struct { const char *fallback; const char *unsupported; } cases[] = {
        {"color:red", "color:color(srgb 0 0 1)"},
        {"background-color:#123", "background-color:color(display-p3 1 0 0)"},
        {"border-top-color:red", "border-top-color:color(srgb 0 0 1)"},
        {"border-right-color:red", "border-right-color:color(srgb 0 0 1)"},
        {"border-bottom-color:red", "border-bottom-color:color(srgb 0 0 1)"},
        {"border-left-color:red", "border-left-color:color(srgb 0 0 1)"},
        {"outline-color:red", "outline-color:color(srgb 0 0 1)"},
        {"column-rule-color:red", "column-rule-color:color(srgb 0 0 1)"},
        {"fill:red", "fill:color(srgb 0 0 1)"},
        {"stroke:red", "stroke:color(srgb 0 0 1)"},
        {"border-color:red green blue", "border-color:red color(srgb 0 0 1) blue"},
        {"border:4px solid red", "border:8px dashed color(srgb 0 0 1)"},
        {"border-top:4px solid red", "border-top:8px dashed color(srgb 0 0 1)"},
        {"outline:4px solid red", "outline:8px dashed color(srgb 0 0 1)"},
        {"column-rule:4px solid red", "column-rule:8px dashed color(srgb 0 0 1)"},
        {"background:blue", "background:fixed color(srgb 0 0 1)"},
        {"background:red", "background:linear-gradient(blue,white) color(srgb 0 0 1)"},
        {"background-image:linear-gradient(red,blue)",
            "background-image:linear-gradient(color(srgb 0 0 1),white)"},
        {"background-image:linear-gradient(red,blue)",
            "background-image:linear-gradient(white,color(display-p3 1 0 0))"},
        {"background-image:linear-gradient(red,blue)",
            "background-image:radial-gradient(white,color(srgb 0 0 1))"},
        {"background-image:linear-gradient(red,blue)",
            "background-image:linear-gradient(white,black),linear-gradient(white,color(srgb 0 0 1))"},
    };
    for (size_t i = 0; i < N_ELEMENTS(cases); i++) {
        char baseline[256], attempted[512];
        snprintf(baseline, sizeof(baseline), "%s;opacity:.25", cases[i].fallback);
        snprintf(attempted, sizeof(attempted), "%s;%s!important;opacity:.25",
            cases[i].fallback, cases[i].unsupported);
        css_declaration_list *expected = parse(baseline, _i != 0);
        css_declaration_list *actual = parse(attempted, _i != 0);
        ck_assert_msg(css__declaration_list_count(expected) == 2, "invalid test fallback: %s", baseline);
        ck_assert_msg(css__declaration_list_count(actual) == 2, "incorrect fallback/recovery: %s", attempted);
        const css_style *want = inline_style(css__declaration_list_sheet(expected));
        const css_style *got = inline_style(css__declaration_list_sheet(actual));
        ck_assert_msg(want->used == got->used &&
                memcmp(want->bytecode, got->bytecode, want->used * sizeof(css_code_t)) == 0,
            "unsupported colour changed rendering bytecode: %s", attempted);
        css__declaration_list_destroy(actual);
        css__declaration_list_destroy(expected);
    }
}
END_TEST

START_TEST(test_colour_invalid)
{
    static const char *const values[] = {
        "rgb(1%,2,3)", "rgb(none,0,0)", "rgba(0,0,0,none)", "rgb(1 2 3,.5)", "rgb(1,2,3/.5)",
        "rgb(1 2 3 4)", "rgb(1 2)", "rgba(1,2,3,4,)", "rgb(1 2 3 / .5 /)", "rgb(1 2 3 /)",
        "rgb(1 2 3 / .5 extra)", "rgb(1,2,3,)", "rgb(1,2,3,.5,)", "rgb(1,2,3,.5,.6)",
        "rgb(1,2,3,.5 /)", "rgb(1,2,3,.5 / junk)", "rgb(1px 2 3)", "rgb(1 2 3 / 1px)",
        "hsl(0,50,50%)", "hsl(0,50%,50)", "hsl(0 50%,50%)", "hsl(0,50% 50%)",
        "hsl(0,50%,50% / .5)", "hsl(0 50% 50%,.5)", "hsl(0,50%,50%,)", "hsla(0,50%,50%,.5,)",
        "hsl(0 50% 50% .5)", "hsl(0 50% 50% /)", "hsl(0 50%)", "hsl(0,50%,50%,.5,.6)",
        "hsl(none,50%,50%)", "hsl(0,none,50%)", "hsl(0,50%,50%,none)", "hsl(0% 50% 50%)",
        "hsl(0px 50% 50%)", "hsl(0 50px 50%)", "hsl(0 50% 50% / 1px)",
        "hwb(0,50%,50%)", "hwb(0 50% 50%,.5)", "hwb(0 50% 50% / .5,)", "hwb(0 50% 50% / .5 /)",
        "hwb(0 50% 50% / .5 extra)", "hwb(0 50% 50% /)", "hwb(0 50%)", "hwb(0 50% 50% .5)",
        "hwba(0 50% 50% / .5)", "hwb(0px 50% 50%)", "hwb(0 50px 50%)", "hwb(0 50% 50% / 1px)",
        "color(srgb 0 0 1)", "color(display-p3 1 0 0)", "color(unknown 1 0 0)", "color(srgb)",
        "color(srgb 1 2)", "unknown(srgb 0 0 1)", "unknown(srgb 0 0 1 / .5)", "#12", "#12345",
    };
    static const char *const properties[] = {"color", "background-color", "border-color", "border-top-color",
        "outline-color", "column-rule-color"};
    for (size_t p = 0; p < N_ELEMENTS(properties); p++) {
        for (size_t i = 0; i < N_ELEMENTS(values); i++) {
            char css[256];
            snprintf(css, sizeof(css), "%s:red;%s:%s;opacity:.5", properties[p], properties[p], values[i]);
            css_declaration_list *list = parse(css, _i != 0);
            ck_assert_msg(css__declaration_list_count(list) == 2, "accepted invalid/unsupported colour: %s", css);
            css_declaration_order *order;
            ck_assert_int_eq(css__declaration_order_create(list, &order), CSS_OK);
            lwc_string *value;
            ck_assert_int_eq(serialize_property(order, properties[p], &value), CSS_OK);
            ck_assert_str_eq(lwc_string_data(value), "red");
            lwc_string_unref(value);
            css__declaration_order_destroy(order);
            css__declaration_list_destroy(list);
        }
    }
}
END_TEST

/* Flexbox §7.1: shorthand defaults are 1 1 0px, and the grow/shrink
 * pair is indivisible. These comparisons check actual rendering bytecode too. */
START_TEST(test_flex_bytecode)
{
    static const char *const forms[][2] = {
        {"flex:100px", "flex-grow:1;flex-shrink:1;flex-basis:100px"},
        {"flex:0px", "flex-grow:1;flex-shrink:1;flex-basis:0px"},
        {"flex:0", "flex-grow:0;flex-shrink:1;flex-basis:0px"},
        {"flex:2 3", "flex-grow:2;flex-shrink:3;flex-basis:0px"},
        {"flex:auto", "flex-grow:1;flex-shrink:1;flex-basis:auto"},
        {"flex:auto 2", "flex-grow:2;flex-shrink:1;flex-basis:auto"},
        {"flex:auto 2 3", "flex-grow:2;flex-shrink:3;flex-basis:auto"},
        {"flex:10px 2 3", "flex-grow:2;flex-shrink:3;flex-basis:10px"},
        {"flex:none", "flex-grow:0;flex-shrink:0;flex-basis:auto"},
        {"flex:content", "flex-grow:1;flex-shrink:1;flex-basis:content"},
        {"flex:-0 -0 -0", "flex-grow:0;flex-shrink:0;flex-basis:0px"},
        {"flex:inherit", "flex-grow:inherit;flex-shrink:inherit;flex-basis:inherit"},
        {"flex-flow:wrap column", "flex-direction:column;flex-wrap:wrap"},
        {"flex-flow:wrap", "flex-direction:row;flex-wrap:wrap"},
        {"flex-flow:column", "flex-direction:column;flex-wrap:nowrap"},
        {"flex-flow:initial", "flex-direction:initial;flex-wrap:initial"},
    };
    for (size_t i = 0; i < N_ELEMENTS(forms); i++) {
        css_declaration_list *a = parse(forms[i][0], _i != 0);
        css_declaration_list *b = parse(forms[i][1], _i != 0);
        ck_assert_msg(css__declaration_list_count(a) == 1, "rejected %s", forms[i][0]);
        compare_sheets(css__declaration_list_sheet(a), css__declaration_list_sheet(b));
        css__declaration_list_destroy(a);
        css__declaration_list_destroy(b);
    }
}
END_TEST

START_TEST(test_flex_invalid)
{
    static const char *const invalid[] = {
        "flex:1 10px 2", "flex:0 10px 0", "flex:auto auto", "flex:none 1", "flex:1 none",
        "flex:1 2 3", "flex:0 0 0 0", "flex:10px 1 2 0", "flex:1 initial", "flex:initial 1",
        "flex:1 1 revert", "flex:1 unset", "flex:auto inherit", "flex:auto 1 initial",
        "flex:auto 1 1 unset", "flex:1 1 1fr", "flex:1 1 1deg", "flex:1 1 1s",
        "flex:-.00000001", "flex:1 -.00000001", "flex:1 1 -.00000001px",
        "flex:1 1 -.00000001%", "flex-grow:-.00000001", "flex-shrink:-.00000001",
        "flex-basis:-.00000001px", "flex-basis:1fr", "flex:invalid",
        "flex-flow:row row", "flex-flow:wrap wrap", "flex-flow:row wrap column",
        "flex-flow:row initial", "flex-flow:wrap unset", "flex-flow:revert wrap", "flex-flow:invalid",
    };
    for (size_t i = 0; i < N_ELEMENTS(invalid); i++) {
        char css[160];
        snprintf(css, sizeof(css), "flex:2 3 20px;flex-flow:column wrap;%s;color:red", invalid[i]);
        css_declaration_list *list = parse(css, _i != 0);
        ck_assert_msg(css__declaration_list_count(list) == 3, "accepted invalid value: %s", invalid[i]);
        css_declaration_order *order;
        ck_assert_int_eq(css__declaration_order_create(list, &order), CSS_OK);
        lwc_string *value;
        ck_assert_int_eq(serialize_property(order, "flex", &value), CSS_OK);
        ck_assert_str_eq(lwc_string_data(value), "2 3 20px");
        lwc_string_unref(value);
        ck_assert_int_eq(serialize_property(order, "flex-flow", &value), CSS_OK);
        ck_assert_str_eq(lwc_string_data(value), "column wrap");
        lwc_string_unref(value);
        css__declaration_order_destroy(order);
        css__declaration_list_destroy(list);
    }
}
END_TEST

START_TEST(test_list_columns_bytecode)
{
    static const char *const forms[][2] = {
        {"list-style:circle inside url(Marker.png)", "list-style-image:url(Marker.png);list-style-position:inside;list-style-type:circle"},
        {"list-style:circle url(Marker.png) inside", "list-style-image:url(Marker.png);list-style-position:inside;list-style-type:circle"},
        {"list-style:inside circle url(Marker.png)", "list-style-image:url(Marker.png);list-style-position:inside;list-style-type:circle"},
        {"list-style:inside url(Marker.png) circle", "list-style-image:url(Marker.png);list-style-position:inside;list-style-type:circle"},
        {"list-style:url(Marker.png) circle inside", "list-style-image:url(Marker.png);list-style-position:inside;list-style-type:circle"},
        {"list-style:url(Marker.png) inside circle", "list-style-image:url(Marker.png);list-style-position:inside;list-style-type:circle"},
        {"list-style:disc none", "list-style-image:none;list-style-position:outside;list-style-type:disc"},
        {"list-style:none disc", "list-style-image:none;list-style-position:outside;list-style-type:disc"},
        {"list-style:none url(Marker.png)", "list-style-image:url(Marker.png);list-style-position:outside;list-style-type:none"},
        {"list-style:url(Marker.png) none", "list-style-image:url(Marker.png);list-style-position:outside;list-style-type:none"},
        {"list-style:inside none none", "list-style-image:none;list-style-position:inside;list-style-type:none"},
        {"list-style:none inside none", "list-style-image:none;list-style-position:inside;list-style-type:none"},
        {"list-style:none none inside", "list-style-image:none;list-style-position:inside;list-style-type:none"},
        {"list-style:none", "list-style-image:none;list-style-position:outside;list-style-type:none"},
        {"list-style:inside", "list-style-image:none;list-style-position:inside;list-style-type:disc"},
        {"list-style:inherit", "list-style-image:inherit;list-style-position:inherit;list-style-type:inherit"},
        {"columns:12em auto", "column-width:12em;column-count:auto"},
        {"columns:auto 12em", "column-width:12em;column-count:auto"},
        {"columns:3 auto", "column-width:auto;column-count:3"},
        {"columns:auto 3", "column-width:auto;column-count:3"},
        {"columns:0 3", "column-width:0px;column-count:3"},
        {"columns:3 0", "column-width:0px;column-count:3"},
        {"columns:12em 3", "column-width:12em;column-count:3"},
        {"columns:3 12em", "column-width:12em;column-count:3"},
        {"columns:auto auto", "column-width:auto;column-count:auto"},
        {"columns:0", "column-width:0px;column-count:auto"},
        {"columns:unset", "column-width:unset;column-count:unset"},
    };
    for (size_t i = 0; i < N_ELEMENTS(forms); i++) {
        css_declaration_list *a = parse(forms[i][0], _i != 0);
        css_declaration_list *b = parse(forms[i][1], _i != 0);
        ck_assert_msg(css__declaration_list_count(a) == 1, "rejected %s", forms[i][0]);
        compare_sheets(css__declaration_list_sheet(a), css__declaration_list_sheet(b));
        css__declaration_list_destroy(a);
        css__declaration_list_destroy(b);
    }
}
END_TEST

START_TEST(test_list_columns_invalid)
{
    static const char *const invalid[] = {
        "list-style:none none none", "list-style:none none disc", "list-style:none disc url(Marker.png)",
        "list-style:none url(Marker.png) disc", "list-style:disc url(Marker.png) none",
        "list-style:inside outside", "list-style:disc circle", "list-style:url(A.png) url(B.png)",
        "list-style:initial inside", "list-style:inside initial", "list-style:none unset",
        "list-style:url(Marker.png) revert", "list-style:none inside inherit", "list-style:none,inside",
        "list-style:1", "list-style:\"disc\"", "list-style:no-such-style",
        "columns:auto auto auto", "columns:auto 12em 3", "columns:3 auto 12em", "columns:12em 3 auto",
        "columns:12em 13em", "columns:2 3", "columns:0 0", "columns:12em 0", "columns:0 12em",
        "columns:-1px", "columns:-.00000001px", "columns:1fr", "columns:2%", "columns:1deg",
        "columns:-1", "columns:1.5", "columns:.00000001", "columns:12em initial",
        "columns:auto unset", "columns:initial 3", "columns:revert auto", "columns:3 inherit",
        "column-count:0", "column-count:-0", "column-count:1.5", "column-count:-1",
        "column-width:-1px", "column-width:-.00000001px", "column-width:1%", "column-width:1fr",
    };
    const char *baseline = "list-style:inside square;columns:12em 3;color:red";
    css_declaration_list *base = parse(baseline, false);
    const css_style *base_style = inline_style(css__declaration_list_sheet(base));
    for (size_t i = 0; i < N_ELEMENTS(invalid); i++) {
        char css[256];
        snprintf(css, sizeof(css), "%s;%s", baseline, invalid[i]);
        css_declaration_list *list = parse(css, _i != 0);
        ck_assert_msg(css__declaration_list_count(list) == 3, "accepted invalid value: %s", invalid[i]);
        const css_style *style = inline_style(css__declaration_list_sheet(list));
        ck_assert_uint_eq(style->used, base_style->used);
        ck_assert_msg(memcmp(style->bytecode, base_style->bytecode, style->used * sizeof(css_code_t)) == 0,
            "invalid value changed rendering bytecode: %s", invalid[i]);
        css__declaration_list_destroy(list);
    }
    css__declaration_list_destroy(base);
}
END_TEST

START_TEST(test_border_shorthand_bytecode)
{
    static const char *const properties[] = {
        "border", "border-top", "border-right", "border-bottom", "border-left", "outline", "column-rule"};
    static const char *const forms[][4] = {
        {"solid red 2px", "red", "solid", "2px"},
        {"solid 2px red", "red", "solid", "2px"},
        {"red solid 2px", "red", "solid", "2px"},
        {"red 2px solid", "red", "solid", "2px"},
        {"2px red solid", "red", "solid", "2px"},
        {"2px solid red", "red", "solid", "2px"},
        {"none", NULL, "none", "medium"},
        {"thin", NULL, "none", "thin"},
        {"solid", NULL, "solid", "medium"},
        {"rgb(1,2,3)", "rgb(1,2,3)", "none", "medium"},
        {"solid calc(1px + 2px)", NULL, "solid", "calc(1px + 2px)"},
        {"inherit", "inherit", "inherit", "inherit"},
        {"initial", "initial", "initial", "initial"},
        {"unset", "unset", "unset", "unset"},
        {"revert", "revert", "revert", "revert"},
    };
    static const char *const sides[] = {"border-top", "border-right", "border-bottom", "border-left"};
    for (size_t p = 0; p < N_ELEMENTS(properties); p++) {
        for (size_t f = 0; f < N_ELEMENTS(forms); f++) {
            char shorthand[128], longhands[1024];
            snprintf(shorthand, sizeof(shorthand), "%s:%s", properties[p], forms[f][0]);
            size_t pos = 0;
            unsigned count = p == 0 ? 4 : 1;
            for (unsigned side = 0; side < count; side++) {
                const char *name = p == 0 ? sides[side] : properties[p];
                const char *colour = forms[f][1] != NULL ? forms[f][1] :
                    strcmp(name, "outline") == 0 ? "invert" : "currentcolor";
                pos += snprintf(longhands + pos, sizeof(longhands) - pos, "%s-color:%s;%s-style:%s;%s-width:%s;",
                    name, colour, name, forms[f][2], name, forms[f][3]);
            }
            css_declaration_list *a = parse(shorthand, _i != 0);
            css_declaration_list *b = parse(longhands, _i != 0);
            ck_assert_msg(css__declaration_list_count(a) == 1, "rejected %s", shorthand);
            compare_sheets(css__declaration_list_sheet(a), css__declaration_list_sheet(b));
            css__declaration_list_destroy(a);
            css__declaration_list_destroy(b);
        }
    }
}
END_TEST

START_TEST(test_border_shorthand_invalid)
{
    static const char *const properties[] = {"border", "border-top", "border-left", "outline", "column-rule"};
    static const char *const invalid[] = {
        "solid dashed", "red blue", "1px 2px", "solid 1px red blue", "initial solid", "solid initial",
        "red unset", "1px revert", "solid red inherit", "solid,red", "solid unknown", "solid color(srgb 1 0 0)",
        "solid rgb(1 2)", "-1px solid", "-.00000001px solid", "1fr solid", "1% solid", "1deg solid",
        "1s solid", "1dpi solid", "solid 1", "'solid'", "url(edge.png)",
    };
    for (size_t p = 0; p < N_ELEMENTS(properties); p++) {
        char baseline[64];
        snprintf(baseline, sizeof(baseline), "%s:3px dotted green", properties[p]);
        css_declaration_list *base = parse(baseline, false);
        const css_style *base_style = inline_style(css__declaration_list_sheet(base));
        for (size_t i = 0; i < N_ELEMENTS(invalid); i++) {
            char css[192];
            snprintf(css, sizeof(css), "%s;%s:%s", baseline, properties[p], invalid[i]);
            css_declaration_list *list = parse(css, _i != 0);
            ck_assert_msg(css__declaration_list_count(list) == 1, "accepted invalid declaration: %s", css);
            const css_style *style = inline_style(css__declaration_list_sheet(list));
            ck_assert_uint_eq(style->used, base_style->used);
            ck_assert_int_eq(memcmp(style->bytecode, base_style->bytecode, style->used * sizeof(css_code_t)), 0);
            css__declaration_list_destroy(list);
        }
        css__declaration_list_destroy(base);
    }
}
END_TEST

START_TEST(test_grid_placement_bytecode)
{
    static const char *const properties[] = {
        "grid-row-start", "grid-row-end", "grid-column-start", "grid-column-end"};
    static const enum css_properties_e opcodes[] = {
        CSS_PROP_GRID_ROW_START, CSS_PROP_GRID_ROW_END, CSS_PROP_GRID_COLUMN_START, CSS_PROP_GRID_COLUMN_END};
    static const struct { const char *text; uint16_t kind; int number; } forms[] = {
        {"auto", CSS_GRID_LINE_AUTO, 0}, {"2", CSS_GRID_LINE_SET, 2}, {"-3", CSS_GRID_LINE_SET, -3},
        {"span 2", CSS_GRID_LINE_SPAN, 2}, {"3 SPAN", CSS_GRID_LINE_SPAN, 3},
        {"32768", CSS_GRID_LINE_SET, 32768},
    };
    for (size_t p = 0; p < N_ELEMENTS(properties); p++) {
        for (size_t f = 0; f < N_ELEMENTS(forms); f++) {
            for (unsigned important = 0; important < 2; important++) {
                char css[128];
                snprintf(css, sizeof(css), "%s:%s%s", properties[p], forms[f].text, important ? "!important" : "");
                css_declaration_list *list = parse(css, _i != 0);
                ck_assert_msg(css__declaration_list_count(list) == 1, "rejected %s", css);
                const css_style *style = inline_style(css__declaration_list_sheet(list));
                ck_assert_uint_eq(style->used, forms[f].kind == CSS_GRID_LINE_AUTO ? 1 : 2);
                uint8_t flags = important ? FLAG_IMPORTANT : 0;
                ck_assert_msg(style->bytecode[0] == buildOPV(opcodes[p], flags, forms[f].kind), "wrong opcode: %s", css);
                if (style->used == 2)
                    ck_assert_msg((css_fixed)style->bytecode[1] == INTTOFIX(forms[f].number), "wrong integer: %s", css);
                css__declaration_list_destroy(list);
            }
        }
    }
    static const char *const pairs[][2] = {
        {"grid-row:2", "grid-row-start:2;grid-row-end:auto"},
        {"grid-column:2 span / 4", "grid-column-start:span 2;grid-column-end:4"},
        {"grid-area:2 / 3 / span 2 / 4 span",
            "grid-row-start:2;grid-column-start:3;grid-row-end:span 2;grid-column-end:span 4"},
        {"grid-area:2 / span 3", "grid-row-start:2;grid-column-start:span 3;grid-row-end:auto;grid-column-end:auto"},
        {"grid-area:2", "grid-row-start:2;grid-column-start:auto;grid-row-end:auto;grid-column-end:auto"},
        {"grid-row:inherit", "grid-row-start:inherit;grid-row-end:inherit"},
        {"grid-column:initial", "grid-column-start:initial;grid-column-end:initial"},
        {"grid-area:unset", "grid-row-start:unset;grid-column-start:unset;grid-row-end:unset;grid-column-end:unset"},
        {"grid-area:revert", "grid-row-start:revert;grid-column-start:revert;grid-row-end:revert;grid-column-end:revert"},
    };
    for (size_t i = 0; i < N_ELEMENTS(pairs); i++) {
        css_declaration_list *a = parse(pairs[i][0], _i != 0), *b = parse(pairs[i][1], _i != 0);
        ck_assert_msg(css__declaration_list_count(a) == 1, "rejected %s", pairs[i][0]);
        compare_sheets(css__declaration_list_sheet(a), css__declaration_list_sheet(b));
        css__declaration_list_destroy(a);
        css__declaration_list_destroy(b);
    }
}
END_TEST

START_TEST(test_grid_placement_invalid)
{
    static const char *const properties[] = {"grid-row", "grid-column", "grid-area",
        "grid-row-start", "grid-row-end", "grid-column-start", "grid-column-end"};
    static const char *const invalid[] = {"0", "-0", "span", "span 0", "span -1", "0 span", "-1 span",
        "2.0", "1.5", "2px", "2%", "span auto", "auto span", "span 2 span", "2 3", "2 /", "2 / / 3",
        "initial 2", "span initial", "2 unset", "inherit / 2", "2 / initial", "2 / 3 / 4 / 5 /",
        "2 / 3 / 4 / 5 / 6", "2 / 0", "2 / 3 / 0", "2 / 3 / 4 / 0", "2 / 3 / span",
        /* Named lines and math remain unsupported, rather than guessed. */
        "Named", "2 Named", "span Named", "calc(1 + 1)"};
    for (size_t p = 0; p < N_ELEMENTS(properties); p++) {
        char baseline[64];
        snprintf(baseline, sizeof(baseline), "%s:2", properties[p]);
        css_declaration_list *base = parse(baseline, false);
        for (size_t i = 0; i < N_ELEMENTS(invalid); i++) {
            char css[192];
            snprintf(css, sizeof(css), "%s;%s:%s", baseline, properties[p], invalid[i]);
            css_declaration_list *list = parse(css, _i != 0);
            ck_assert_msg(css__declaration_list_count(list) == 1, "accepted unsupported declaration: %s", css);
            compare_sheets(css__declaration_list_sheet(base), css__declaration_list_sheet(list));
            css__declaration_list_destroy(list);
        }
        css__declaration_list_destroy(base);
    }
}
END_TEST

START_TEST(test_quirky_border_values)
{
    static const char *const forms[][3] = {
        {"border:000 solid", "border-left-color", "rgb(0, 0, 0)"},
        {"border:000 solid", "border-left-width", "medium"},
        {"border:123456 solid", "border", "solid rgb(18, 52, 86)"},
        {"border:12ab56 solid", "border-left-color", "rgb(18, 171, 86)"},
        {"border:f00 solid", "border-left-color", "rgb(255, 0, 0)"},
        {"border:red solid 000", "border-left-width", "0px"},
        {"border:123456 solid 2", "border", "2px solid rgb(18, 52, 86)"},
    };
    for (size_t i = 0; i < N_ELEMENTS(forms); i++) {
        css_stylesheet_params quirky = params;
        quirky.allow_quirks = true;
        css_declaration_list *list;
        ck_assert_int_eq(css__declaration_list_create(&quirky, &list), CSS_OK);
        check_append(css__declaration_list_append(list, (const uint8_t *)forms[i][0], strlen(forms[i][0])));
        ck_assert_int_eq(css__declaration_list_complete(list), CSS_OK);
        css_declaration_order *order;
        ck_assert_int_eq(css__declaration_order_create(list, &order), CSS_OK);
        lwc_string *value;
        ck_assert_int_eq(serialize_property(order, forms[i][1], &value), CSS_OK);
        ck_assert_str_eq(lwc_string_data(value), forms[i][2]);
        lwc_string_unref(value);
        css__declaration_order_destroy(order);
        css__declaration_list_destroy(list);
    }
}
END_TEST

START_TEST(test_geometry_bytecode)
{
    static const char *const forms[][2] = {
        {"aspect-ratio:3", "aspect-ratio:3 / 1"},
        {"aspect-ratio:4/3 auto", "aspect-ratio:auto 4 / 3"},
        {"aspect-ratio:-0 / -0", "aspect-ratio:0 / 0"},
        {"border-spacing:2em", "border-spacing:2em 2em"},
        {"border-spacing:0 -0", "border-spacing:0px 0px"},
        {"clip:RECT(0 2em -3px AUTO)", "clip:rect(0px, 2em, -3px, auto)"},
    };
    for (size_t i = 0; i < sizeof(forms) / sizeof(forms[0]); i++) {
        css_declaration_list *a = parse(forms[i][0], _i != 0);
        css_declaration_list *b = parse(forms[i][1], _i != 0);
        ck_assert_uint_eq(css__declaration_list_count(a), 1);
        ck_assert_uint_eq(css__declaration_list_count(b), 1);
        compare_sheets(css__declaration_list_sheet(a), css__declaration_list_sheet(b));
        css__declaration_list_destroy(a);
        css__declaration_list_destroy(b);
    }
}
END_TEST

/* The renderer resolves URLs while parsing; specified CSSOM values must retain
 * their declared spelling. Use a distinct result to detect accidental reads
 * from the compiled stylesheet's resolved string table. */
static css_error resolve_absolute_url(void *pw, const char *base, lwc_string *rel, lwc_string **abs)
{
    unsigned *calls = pw;
    (*calls)++;
    ck_assert_str_eq(base, params.url);
    ck_assert_str_eq(lwc_string_data(rel), "../Icons/Mark.png");
    const char *url = "https://example.org/Icons/Mark.png";
    return lwc_intern_string(url, strlen(url), abs) == lwc_error_ok ? CSS_OK : CSS_NOMEM;
}

START_TEST(test_specified_url_resolution)
{
    css_stylesheet_params resolving = params;
    unsigned calls = 0;
    resolving.resolve = resolve_absolute_url;
    resolving.resolve_pw = &calls;
    css_declaration_list *list;
    const char *css = "background-image:url(../Icons/Mark.png)";
    ck_assert_int_eq(css__declaration_list_create(&resolving, &list), CSS_OK);
    check_append(css__declaration_list_append(list, (const uint8_t *)css, strlen(css)));
    ck_assert_int_eq(css__declaration_list_complete(list), CSS_OK);
    ck_assert_uint_eq(calls, 1);
    const css_stylesheet *sheet = css__declaration_list_sheet(list);
    ck_assert_uint_eq(sheet->string_vector_c, 1);
    ck_assert_str_eq(lwc_string_data(sheet->string_vector[0]), "https://example.org/Icons/Mark.png");
    css_declaration_order *order;
    ck_assert_int_eq(css__declaration_order_create(list, &order), CSS_OK);
    lwc_string *value;
    ck_assert_int_eq(serialize_property(order, "background-image", &value), CSS_OK);
    ck_assert_str_eq(lwc_string_data(value), "url(\"../Icons/Mark.png\")");
    lwc_string_unref(value);
    css__declaration_order_destroy(order);
    css__declaration_list_destroy(list);
}
END_TEST

START_TEST(test_long_url)
{
    const char *prefix = "background-image:url('";
    const char *chunk = "a\\\"b\\\\c\\1 d";
    const char *canonical = "a\\\"b\\\\c\\1 d";
    enum { REPEATS = 2048 };
    size_t length = strlen(prefix) + REPEATS * strlen(chunk) + 3;
    char *css = malloc(length);
    ck_assert_ptr_nonnull(css);
    char *p = stpcpy(css, prefix);
    for (size_t i = 0; i < REPEATS; i++)
        p = stpcpy(p, chunk);
    strcpy(p, "')");
    css_declaration_list *list = parse(css, _i != 0);
    free(css);
    css_declaration_order *order;
    ck_assert_int_eq(css__declaration_order_create(list, &order), CSS_OK);
    lwc_string *value;
    ck_assert_int_eq(serialize_property(order, "background-image", &value), CSS_OK);
    ck_assert_uint_eq(lwc_string_length(value), 7 + REPEATS * strlen(canonical));
    const char *result = lwc_string_data(value);
    ck_assert_int_eq(memcmp(result, "url(\"", 5), 0);
    for (size_t i = 0; i < REPEATS; i++) {
        ck_assert_int_eq(memcmp(result + 5 + i * strlen(canonical), canonical, strlen(canonical)), 0);
    }
    ck_assert_str_eq(result + lwc_string_length(value) - 2, "\")");
    lwc_string_unref(value);
    css__declaration_order_destroy(order);
    css__declaration_list_destroy(list);
}
END_TEST

START_TEST(test_overflow_axes)
{
    static const char *const keywords[] = {"visible", "hidden", "scroll", "auto"};
    /* Compare actual parser bytecode for every implemented two-axis pair,
     * independently of serialization (including priority handling). */
    for (size_t x = 0; x < 4; x++) {
        for (size_t y = 0; y < 4; y++) {
            char shorthand[80], longhands[100];
            snprintf(shorthand, sizeof(shorthand), "overflow:%s %s!important", keywords[x], keywords[y]);
            snprintf(longhands, sizeof(longhands), "overflow-x:%s!important;overflow-y:%s!important",
                keywords[x], keywords[y]);
            css_declaration_list *a = parse(shorthand, _i != 0);
            css_declaration_list *b = parse(longhands, _i != 0);
            compare_sheets(css__declaration_list_sheet(a), css__declaration_list_sheet(b));
            css__declaration_list_destroy(a);
            css__declaration_list_destroy(b);
        }
    }
}
END_TEST

START_TEST(test_value_arguments)
{
    lwc_string *value = (void *)1;
    ck_assert_int_eq(css__declaration_value_serialize(NULL, &value), CSS_BADPARM);
    ck_assert_ptr_null(value);
    ck_assert_int_eq(css__declaration_value_serialize(NULL, NULL), CSS_BADPARM);
    ck_assert_int_eq(css__declaration_shorthand_serialize(NULL, NULL, 0, &value), CSS_BADPARM);
    ck_assert_ptr_null(value);
    css_declaration_list *list = parse("margin:1px; width:1px", false);
    css_declaration_order *order;
    ck_assert_int_eq(css__declaration_order_create(list, &order), CSS_OK);
    css_declaration_contribution bad = *find_value(order, "margin-top");
    bad.component = 4;
    ck_assert_int_eq(css__declaration_value_serialize(&bad, &value), CSS_BADPARM);
    ck_assert_ptr_null(value);
    const struct css_prop_entry *property = css_prop_lookup("margin", 6);
    const css_declaration_contribution *members[4] = {find_value(order, "width")};
    ck_assert_int_eq(css__declaration_shorthand_serialize(property, members, 4, &value), CSS_BADPARM);
    ck_assert_ptr_null(value);
    ck_assert_int_eq(css__declaration_shorthand_serialize(property, members, 3, &value), CSS_BADPARM);
    ck_assert_ptr_null(value);
    css__declaration_order_destroy(order);
    css__declaration_list_destroy(list);
}
END_TEST

#ifdef WISP_TEST_ALLOC_FAILURE
/* Every grid parser must restore its complete output span if any append fails,
 * including CSS-wide expansion and the later defaulted shorthand members. */
START_TEST(test_grid_placement_rollback)
{
    static const char *const names[] = {"grid-area", "grid-row", "grid-column",
        "grid-row-start", "grid-row-end", "grid-column-start", "grid-column-end"};
    const struct css_prop_entry *property = css_prop_lookup(names[_i], strlen(names[_i]));
    css_stylesheet *sheet;
    ck_assert_int_eq(css_stylesheet_create(&params, &sheet), CSS_OK);
    css_language *language = sheet->parser_frontend;
    lwc_string *number;
    ck_assert_int_eq(lwc_intern_string("2", 1, &number), lwc_error_ok);
    for (unsigned wide = 0; wide < 2; wide++) {
        parserutils_vector *tokens;
        ck_assert_int_eq(parserutils_vector_create(sizeof(css_token), 4, &tokens), PARSERUTILS_OK);
        css_token parts[] = {{.type = CSS_TOKEN_IDENT, .idata = language->strings[SPAN]},
            {.type = CSS_TOKEN_S}, {.type = CSS_TOKEN_NUMBER, .idata = number}};
        css_token initial = {.type = CSS_TOKEN_IDENT, .idata = language->strings[INITIAL]};
        if (wide) {
            ck_assert_int_eq(parserutils_vector_append(tokens, &initial), PARSERUTILS_OK);
        } else {
            for (size_t i = 0; i < N_ELEMENTS(parts); i++)
                ck_assert_int_eq(parserutils_vector_append(tokens, &parts[i]), PARSERUTILS_OK);
        }
        size_t words = (property->shorthand ? property->longhand_count : 1) + (wide ? 0 : 1);
        for (size_t spare = 0; spare < words; spare++) {
            css_style *style;
            ck_assert_int_eq(css__stylesheet_style_create(sheet, &style), CSS_OK);
            uint32_t before = style->allocated - spare;
            for (uint32_t i = 0; i < before; i++)
                ck_assert_int_eq(css__stylesheet_style_append(style, 0x12345678), CSS_OK);
            int32_t ctx = 0;
            realloc_limit(0);
            css_error error = property->handler(language, tokens, &ctx, style);
            realloc_limit(UINT_MAX);
            ck_assert_msg(error == CSS_NOMEM, "%s wide %u spare %zu: %d", names[_i], wide, spare, error);
            ck_assert_int_eq(ctx, 0);
            ck_assert_uint_eq(style->used, before);
            for (uint32_t i = 0; i < before; i++)
                ck_assert_uint_eq(style->bytecode[i], 0x12345678);
            ck_assert_int_eq(property->handler(language, tokens, &ctx, style), CSS_OK);
            ck_assert_uint_eq(style->used, before + words);
            css__stylesheet_style_destroy(style);
        }
        parserutils_vector_destroy(tokens);
    }
    lwc_string_unref(number);
    css_stylesheet_destroy(sheet);
}
END_TEST

/* Fill the style exactly so the final CONTENT_NORMAL terminator is the only
 * append that needs allocation. Losing that failure used to publish truncated
 * bytecode as a successfully parsed declaration. */
START_TEST(test_content_terminator_allocation_failure)
{
    css_stylesheet *sheet;
    css_style *style;
    parserutils_vector *tokens;
    ck_assert_int_eq(css_stylesheet_create(&params, &sheet), CSS_OK);
    ck_assert_int_eq(css__stylesheet_style_create(sheet, &style), CSS_OK);
    css_language *language = sheet->parser_frontend;
    ck_assert_int_eq(parserutils_vector_create(sizeof(css_token), 16, &tokens), PARSERUTILS_OK);
    css_token quote = {.type = CSS_TOKEN_IDENT, .idata = language->strings[OPEN_QUOTE]};
    for (unsigned i = 0; i < style->allocated; i++)
        ck_assert_int_eq(parserutils_vector_append(tokens, &quote), PARSERUTILS_OK);
    int32_t ctx = 0;
    realloc_limit(0);
    css_error error = css__parse_content(language, tokens, &ctx, style);
    realloc_limit(UINT_MAX);
    ck_assert_int_eq(error, CSS_NOMEM);
    ck_assert_int_eq(ctx, 0);
    style->used = 0; /* The caller discards a failed parse's temporary bytecode. */
    ck_assert_int_eq(css__parse_content(language, tokens, &ctx, style), CSS_OK);
    ck_assert_int_eq(style->bytecode[style->used - 1], CONTENT_NORMAL);
    parserutils_vector_destroy(tokens);
    css__stylesheet_style_destroy(style);
    css_stylesheet_destroy(sheet);
}
END_TEST

START_TEST(test_font_face_allocation_failure)
{
    void (*limit)(unsigned int) = _i == 0 ? malloc_limit : _i == 1 ? calloc_limit : realloc_limit;
    const char *css = "@font-face{font-family:Open/**/Sans\\20 ;"
        "src:local(First\\20 ),local(Second/**/Face),url(font.woff)}";
    css_stylesheet_params full = params;
    full.inline_style = false;
    css_stylesheet *keeper;
    ck_assert_int_eq(css_stylesheet_create(&full, &keeper), CSS_OK);
    bool succeeded = false;
    unsigned failures = 0;
    for (unsigned attempt = 0; attempt < 500; attempt++) {
        css_stylesheet *sheet = NULL;
        limit(attempt);
        css_error error = css_stylesheet_create(&full, &sheet);
        if (error == CSS_OK) {
            error = css_stylesheet_append_data(sheet, (const uint8_t *)css, strlen(css));
            if (error == CSS_OK || error == CSS_NEEDDATA)
                error = css_stylesheet_data_done(sheet);
        }
        limit(UINT_MAX);
        if (error == CSS_OK) {
            uint32_t count;
            ck_assert_int_eq(css_font_face_count_srcs(only_font_face(sheet), &count), CSS_OK);
            ck_assert_uint_eq(count, 3);
            succeeded = true;
        } else {
            ck_assert_msg(error == CSS_NOMEM, "font-face allocator %d attempt %u: %d", _i, attempt, error);
            failures++;
        }
        if (sheet != NULL)
            css_stylesheet_destroy(sheet);
        if (succeeded)
            break;
    }
    ck_assert_msg(succeeded && failures > 0, "font-face allocator %d did not reach success", _i);
    css_stylesheet *retry = font_sheet(css, true);
    css_stylesheet_destroy(retry);
    css_stylesheet_destroy(keeper);
}
END_TEST

START_TEST(test_value_allocation_failure)
{
    const char *css = "margin:.1234567px .3456789em .5678912% -.6789123px; --x:  original value  ;"
                      "background-image:url('a\\\"b\\\\c\\1 d'), none, url(Other.png);"
                      "background-attachment:FIXED, SCROLL; border-style:SOLID DASHED DOTTED DOUBLE;"
                      "content:attr(\\31 2) counters(Chapter, 'a\\a b', upper-roman) url(Other.png);"
                      "quotes:'«' '»'; counter-reset:Chapter 0 Section -0002;"
                      "aspect-ratio:auto .1234567; border-spacing:.3456789em .5678912px;"
                      "flex:.1234567 .3456789 1.2345678px;flex-flow:wrap-reverse column;"
                      "color:rgb(1.2 3.4 5.6 / .234567);border-color:#1234 hsl(12 34% 56%) hwb(78 9% 10%);"
                      "clip:rect(.1234567px, auto, -1.2345678em, 1px);"
                      "list-style:none inside url(Marker.png);columns:.1234567em 3;"
                      "grid-area:2/3/span 4/5 span;grid-column:2 span/3;"
                      "font-family:One/**/Two\\20 , 'a\\22 b\\a c',SERIF;";
    static const char *const names[] = {"margin", "--x", "background-image", "background-attachment", "border-style",
        "content", "quotes", "counter-reset", "aspect-ratio", "border-spacing", "clip", "font-family", "flex",
        "flex-flow", "color", "border-color", "list-style", "columns", "border", "border-left", "outline", "column-rule",
        "grid-area", "grid-column", "grid-row", "grid-row-end"};
    const char *name = names[_i];
    /* Keep full-border inputs separate: they reset the quad style/colour cases. */
    if (strcmp(name, "border") == 0 || strcmp(name, "border-left") == 0)
        css = "border:.1234567px solid rgb(1.2 3.4 5.6)";
    else if (strcmp(name, "outline") == 0)
        css = "outline:1.2345678em dotted #1234";
    else if (strcmp(name, "column-rule") == 0)
        css = "column-rule:.1234567px dashed hsl(12 34% 56%)";
    css_declaration_list *list = parse(css, false);
    css_declaration_order *order;
    ck_assert_int_eq(css__declaration_order_create(list, &order), CSS_OK);
    bool succeeded = false;
    unsigned failures = 0;
    for (unsigned attempt = 0; attempt < 100; attempt++) {
        lwc_string *value = (void *)1;
        malloc_limit(attempt);
        css_error error = serialize_property(order, name, &value);
        malloc_limit(UINT_MAX);
        if (error == CSS_OK) {
            ck_assert_ptr_nonnull(value);
            lwc_string_unref(value);
            succeeded = true;
            break;
        }
        ck_assert_int_eq(error, CSS_NOMEM);
        ck_assert_ptr_null(value);
        failures++;
    }
    ck_assert_msg(succeeded && failures > 0, "serializer %s did not test failure and recovery", name);
    /* The borrowed declarations remain usable after every failed serialization. */
    lwc_string *value;
    ck_assert_int_eq(serialize_property(order, name, &value), CSS_OK);
    lwc_string_unref(value);
    css__declaration_order_destroy(order);
    css__declaration_list_destroy(list);
}
END_TEST
#endif

static void unexpected_string(lwc_string *string, void *pw)
{
    unsigned *count = pw;
    (*count)++;
    fprintf(stderr, "Leaked interned string: %.*s\n", (int)lwc_string_length(string), lwc_string_data(string));
}

int main(void)
{
    Suite *suite = suite_create("CSS declaration capture");
    TCase *tc = tcase_create("Parser output");
    tcase_add_loop_test(tc, test_declarations, 0, CASE_COUNT);
    tcase_add_loop_test(tc, test_lexer_resume, 0, LEXER_CASE_COUNT);
    tcase_add_loop_test(tc, test_source_value, 0, SOURCE_CASE_COUNT);
    tcase_add_test(tc, test_source_growth);
    tcase_add_loop_test(tc, test_specified_values, 0, VALUE_CASE_COUNT);
    tcase_add_test(tc, test_specified_url_resolution);
    tcase_add_loop_test(tc, test_reserved_counter_names, 0, 2);
    tcase_add_loop_test(tc, test_font_name_storage, 0, 2);
    tcase_add_loop_test(tc, test_font_face_names, 0, 2);
    tcase_add_loop_test(tc, test_reserved_font_names, 0, 2);
    tcase_add_loop_test(tc, test_long_font_name, 0, 2);
    tcase_add_loop_test(tc, test_clip_separators, 0, 2);
    tcase_add_loop_test(tc, test_geometry_invalid_units, 0, 2);
    tcase_add_loop_test(tc, test_geometry_bytecode, 0, 2);
    tcase_add_loop_test(tc, test_flex_bytecode, 0, 2);
    tcase_add_loop_test(tc, test_list_columns_bytecode, 0, 2);
    tcase_add_loop_test(tc, test_list_columns_invalid, 0, 2);
    tcase_add_loop_test(tc, test_border_shorthand_bytecode, 0, 2);
    tcase_add_loop_test(tc, test_border_shorthand_invalid, 0, 2);
    tcase_add_loop_test(tc, test_grid_placement_bytecode, 0, 2);
    tcase_add_loop_test(tc, test_grid_placement_invalid, 0, 2);
    tcase_add_test(tc, test_quirky_border_values);
    tcase_add_loop_test(tc, test_colour_alpha_roundtrip, 0, 2);
    tcase_add_test(tc, test_quirky_colour_values);
    tcase_add_loop_test(tc, test_colour_rendering_values, 0, 2);
    tcase_add_loop_test(tc, test_colour_invalid, 0, 2);
    tcase_add_loop_test(tc, test_unsupported_colour_fallbacks, 0, 2);
    tcase_add_loop_test(tc, test_flex_invalid, 0, 2);
    tcase_add_loop_test(tc, test_long_url, 0, 2);
    tcase_add_loop_test(tc, test_overflow_axes, 0, 2);
    tcase_add_test(tc, test_value_arguments);
#ifdef WISP_TEST_ALLOC_FAILURE
    tcase_add_loop_test(tc, test_value_allocation_failure, 0, 26);
    tcase_add_loop_test(tc, test_grid_placement_rollback, 0, 7);
    tcase_add_loop_test(tc, test_font_face_allocation_failure, 0, 3);
    tcase_add_test(tc, test_content_terminator_allocation_failure);
#endif
    tcase_add_loop_test(tc, test_declaration_order, 0, ORDER_CASE_COUNT);
    tcase_add_test(tc, test_property_metadata);
    tcase_add_test(tc, test_order_many_custom_properties);
    tcase_add_test(tc, test_order_lifecycle);
    tcase_add_test(tc, test_owned_tokens);
    tcase_add_test(tc, test_lifecycle);
    tcase_add_test(tc, test_growth);
#ifdef WISP_TEST_ALLOC_FAILURE
    tcase_add_loop_test(tc, test_allocation_failure, 0, 3);
    tcase_add_test(tc, test_order_allocation_failure);
#endif
    suite_add_tcase(suite, tc);
    SRunner *runner = srunner_create(suite);
    srunner_set_fork_status(runner, CK_NOFORK);
    srunner_run_all(runner, CK_NORMAL);
    int failed = srunner_ntests_failed(runner);
    srunner_free(runner);
    unsigned remaining = 0;
    lwc_iterate_strings(unexpected_string, &remaining);
    return failed != 0 || remaining != 0;
}

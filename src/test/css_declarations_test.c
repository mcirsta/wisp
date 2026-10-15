/* Accepted inline declarations must retain parser output without changing the
 * compiled style. This exercises the real generated and manual property parsers. */
#include <check.h>
#include <float.h>
#include <limits.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libcss/font_face.h>
#include <libcss/select.h>

#include "parse/declarations.h"
#include "parse/declaration_order.h"
#include "parse/declaration_value.h"
#include "parse/declaration_store.h"
#include "parse/declaration_block.h"
#include "parse/calc.h"
#include "select/calc.h"
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
    {"grid-gap:3px 4px; GRID-ROW-GAP:5px; grid-column-gap:6px!important",
        {"gap", "row-gap", "column-gap"}, 4},
    {"grid-gap:var(--Gaps, 3px 4px);grid-row-gap:var(--Row);grid-column-gap:var(--Column)",
        {"gap", "row-gap", "column-gap"}, 0},
    {"\\67 rid-gap:3px; --grid-gap:9px;--Grid-gap:10px", {"gap", "--grid-gap", "--Grid-gap"}, 0},

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
    {"inset-block:1px 2px;inset-block-start:3px;top:4px",
        {"inset-block-end", "inset-block-start", "top"}, {0, 1, 2}, {1, 0, 0}},
    {"margin-block:1px 2px; margin-block-start:3px; margin-top:4px",
        {"margin-block-end", "margin-block-start", "margin-top"}, {0, 1, 2}, {1, 0, 0}},
    {"padding:initial; padding-inline:2px; padding-block-end:3px",
        {"padding-top", "padding-right", "padding-bottom", "padding-left", "padding-inline-start", "padding-inline-end",
            "padding-block-end"},
        {0, 0, 0, 0, 1, 1, 2}, {0, 1, 2, 3, 0, 1, 0}},
    {"grid-gap:3px 4px; row-gap:5px", {"column-gap", "row-gap"}, {0, 1}, {1, 0}},
    {"row-gap:5px; grid-gap:3px 4px", {"row-gap", "column-gap"}, {1, 1}, {0, 1}},
    {"grid-row-gap:5px!important; gap:3px 4px", {"row-gap", "column-gap"}, {0, 1}, {0, 1}},
    {"grid-gap:3px 4px;gap:5px 6px;grid-column-gap:7px", {"row-gap", "column-gap"}, {1, 2}, {0, 0}},
    {"grid-gap:var(--g);row-gap:3px", {"column-gap", "row-gap"}, {0, 1}, {1, 0}},
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

START_TEST(test_alias_bytecode)
{
    static const char *const names[][2] = {{"grid-gap", "gap"}, {"grid-row-gap", "row-gap"},
        {"grid-column-gap", "column-gap"}};
    static const char *const values[] = {"3px", "normal", "1.2345678em", "12%", "calc(2px + 3%)",
        "inherit", "initial", "unset", "revert", "var(--Gaps, 1px)", "var(--missing, initial)"};
    for (size_t n = 0; n < N_ELEMENTS(names); n++) {
        for (size_t v = 0; v < N_ELEMENTS(values); v++) {
            for (unsigned important = 0; important < 2; important++) {
                char alias[128], canonical[128];
                snprintf(alias, sizeof(alias), "%s:%s%s", names[n][0], values[v], important ? "!important" : "");
                snprintf(canonical, sizeof(canonical), "%s:%s%s", names[n][1], values[v], important ? "!important" : "");
                css_declaration_list *a = parse(alias, _i != 0), *b = parse(canonical, _i != 0);
                ck_assert_msg(css__declaration_list_count(a) == 1, "rejected %s", alias);
                compare_sheets(css__declaration_list_sheet(a), css__declaration_list_sheet(b));
                ck_assert_ptr_eq(css__declaration_list_get(a, 0)->property, css__declaration_list_get(b, 0)->property);
                css__declaration_list_destroy(a);
                css__declaration_list_destroy(b);
            }
        }
    }
}
END_TEST

START_TEST(test_property_metadata)
{
    const struct css_prop_entry *width = css_prop_lookup("width", 5);
    const struct css_prop_entry *logical = css_prop_lookup("INLINE-SIZE", 11);
    ck_assert_ptr_nonnull(css_prop_lookup("grid-gap", 8));
    ck_assert_ptr_eq(css_prop_lookup("grid-gap", 8), css_prop_lookup("gap", 3));
    ck_assert_ptr_eq(css_prop_lookup("GRID-ROW-GAP", 12), css_prop_lookup("row-gap", 7));
    ck_assert_ptr_eq(css_prop_lookup("grid-column-gap", 15), css_prop_lookup("column-gap", 10));
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
    ck_assert_ptr_null(css_prop_lookup("grid-gap-junk", 13));
    ck_assert_ptr_null(css_prop_lookup("grid-ģap", strlen("grid-ģap")));
    ck_assert_ptr_null(css_prop_lookup("--grid-gap", 10));
    const char gap_slice[] = {'G', 'R', 'I', 'D', '-', 'G', 'A', 'P'};
    ck_assert_ptr_eq(css_prop_lookup(gap_slice, sizeof(gap_slice)), css_prop_lookup("gap", 3));
    /* The length API accepts a slice, without requiring a terminator. */
    const char slice[] = {'w', 'i', 'd', 't', 'h'};
    ck_assert_ptr_eq(css_prop_lookup(slice, sizeof(slice)), width);
}
END_TEST

START_TEST(test_aggregate_longhand_parsers)
{
    static const struct {
        const char *aggregate, *longhand;
        const char *valid[8];
        const char *invalid[8];
    } families[] = {
        {"font-variant", "font-variant-caps", {"normal", "SMALL-CAPS", "inherit", "initial", "unset", "revert"},
            {"small-caps normal", "small-caps small-caps", "1", "all-small-caps", "none"}},
        {"text-decoration", "text-decoration-line",
            {"none", "UNDERLINE", "overline line-through underline blink", "inherit", "initial", "unset", "revert"},
            {"none underline", "underline underline", "underline solid", "red", "2px", "underline,overline"}},
    };
    for (size_t f = 0; f < sizeof(families) / sizeof(families[0]); f++) {
        const struct css_prop_entry *aggregate = css_prop_lookup(families[f].aggregate, strlen(families[f].aggregate));
        const struct css_prop_entry *longhand = css_prop_lookup(families[f].longhand, strlen(families[f].longhand));
        ck_assert_ptr_nonnull(aggregate);
        ck_assert_ptr_nonnull(longhand);
        ck_assert(aggregate != longhand && aggregate->id != longhand->id);
        ck_assert(aggregate->shorthand && !longhand->shorthand);
        ck_assert_uint_eq(aggregate->longhand_count, 1);
        ck_assert_str_eq(aggregate->longhands[0], longhand->name);
        for (size_t i = 0; i < 8 && families[f].valid[i] != NULL; i++) {
            for (unsigned important = 0; important < 2; important++) {
                char a[192], b[192];
                snprintf(a, sizeof(a), "%s:%s%s", families[f].aggregate, families[f].valid[i],
                    important ? "!important" : "");
                snprintf(b, sizeof(b), "%s:%s%s", families[f].longhand, families[f].valid[i],
                    important ? "!important" : "");
                css_declaration_list *first = parse(a, _i != 0), *second = parse(b, _i != 0);
                ck_assert_msg(css__declaration_list_count(second) == 1, "rejected %s", b);
                /* Identical native bytecode: rendering, priority and CSS-wide
                 * cascade semantics use the same implementation. */
                compare_sheets(css__declaration_list_sheet(first), css__declaration_list_sheet(second));
                css__declaration_list_destroy(first);
                css__declaration_list_destroy(second);
            }
        }
        for (size_t i = 0; i < 8 && families[f].invalid[i] != NULL; i++) {
            char css[192];
            snprintf(css, sizeof(css), "color:red;%s:%s;opacity:.5", families[f].longhand, families[f].invalid[i]);
            css_declaration_list *list = parse(css, _i != 0);
            ck_assert_msg(css__declaration_list_count(list) == 2, "incorrectly accepted %s", css);
            ck_assert_str_eq(lwc_string_data(css__declaration_list_get(list, 0)->name), "color");
            ck_assert_str_eq(lwc_string_data(css__declaration_list_get(list, 1)->name), "opacity");
            css__declaration_list_destroy(list);
        }
    }
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
                               "place-content:center flex-end;place-content:normal!important;place-content:unset;"
                               "background-position:top left,2px 3%,center;object-position:center .5em!important;"
                               "background-repeat:repeat no-repeat,no-repeat repeat!important;"
                               "text-decoration:blink underline overline; text-decoration:initial;"
                               "color:rgb(1.2 3.4 5.6 / 50%);border-color:#1234 red rgb(3,2,1) currentcolor;"
                               /* Force the parser's token vector to grow too. */
                               "--large:red red red red red red red red red red red red red red red red "
                               "red red red red red red red red red red red red red red red red red red;"
                               "content:'a\\\r\nb' attr(Title) counters(Chapter, '.', UPPER-ROMAN);"
                               "counter-reset:Chapter 2 Section; counter-increment:Chapter; quotes:'«' '»';"
                               "overflow:hidden auto!important; font-family:One,Two/**/Words,Three\\20 ;"
                               "background-image:url(\\20 file\\22 ),url('other\\1 image');"
                               "font:normal italic 1.2345em/1.25 One Two,serif; font:initial;"
                               "grid-gap:1px 2px;grid-row-gap:3px;grid-gap:var(--Gap);"
                               "GRID-COLUMN-GAP:var(--Column, 4px);--grid-gap:5px;"
                               "grid-template-columns:repeat(32,minmax(0,1fr));grid-template-rows:repeat(2,1px 2em);"
                               "grid-auto-flow:dense column;"
                               "fill:rgb(1 2 3);stroke:none;fill-opacity:.5;stroke-opacity:.25;stroke-width:2;"
                               "cursor:url(Cursor.cur),url(Other.png),pointer;"
                               "azimuth:behind left;elevation:0;pause-before:123.456789ms;pause-after:1s;"
                               "pitch-range:23;pitch:2khz;play-during:url(A.ogg) repeat mix;richness:50;"
                               "speak-header:once;speak-numeral:digits;speak-punctuation:code;speak:none;"
                               "speech-rate:90;stress:60;voice-family:One Two,male;volume:55%;"
                               "cue:url(A.ogg) none;pause:1000ms 1s;";
    /* Grow the captured-source buffer after a retained declaration exists. */
    char css[16384];
    size_t pos = (size_t)sprintf(css, "opacity:1; /*");
    memset(css + pos, 'c', 8192);
    pos += 8192;
    snprintf(css + pos, sizeof(css) - pos, "*/%s", declarations);
    const size_t expected = 83;
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
        return css__declaration_value_serialize(find_value(order, property != NULL ? property->name : name), out);
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
    {"margin-inline:1px 2px;margin-inline-start:3px", "margin-inline", "3px 2px"},
    {"margin-inline:1px 2px;margin-inline-end:3px", "margin-inline", "1px 3px"},
    {"margin-inline:1px!important;margin-inline-end:3px", "margin-inline", "1px"},
    {"margin-inline-start:1px!important;margin-inline-end:3px", "margin-inline", ""},
    {"margin-inline:var(--Edges)", "margin-inline-start", ""},
    {"margin-inline-end:calc(2px + 5%)", "margin-inline-end", "calc(5% + 2px)"},
    {"margin-inline-start:inherit;margin-inline-end:inherit", "margin-inline", "inherit"},
    {"padding-inline:1px 2px;padding-inline-start:3px", "padding-inline", "3px 2px"},
    {"padding-inline:1px 2px;padding-inline-end:3px", "padding-inline", "1px 3px"},
    {"padding-inline:1px!important;padding-inline-end:3px", "padding-inline", "1px"},
    {"padding-inline-start:1px!important;padding-inline-end:3px", "padding-inline", ""},
    {"padding-inline:var(--Edges)", "padding-inline-start", ""},
    {"padding-inline-end:calc(2px + 5%)", "padding-inline-end", "calc(5% + 2px)"},
    {"padding-inline-start:inherit;padding-inline-end:inherit", "padding-inline", "inherit"},
    {"inset-inline:1px 2px;inset-inline-start:3px", "inset-inline", "3px 2px"},
    {"inset-inline:1px 2px;inset-inline-end:3px", "inset-inline", "1px 3px"},
    {"inset-inline:1px!important;inset-inline-end:3px", "inset-inline", "1px"},
    {"inset-inline-start:1px!important;inset-inline-end:3px", "inset-inline", ""},
    {"inset-inline:var(--Edges)", "inset-inline-start", ""},
    {"inset-inline-end:calc(2px + 5%)", "inset-inline-end", "calc(5% + 2px)"},
    {"inset-inline-start:inherit;inset-inline-end:inherit", "inset-inline", "inherit"},

    {"inset-block:1px 2em", "inset-block-start", "1px"},
    {"inset-block:1px 2em", "inset-block-end", "2em"},
    {"inset-block:auto", "inset-block-end", "auto"},
    {"inset-block-start:+2.5PX", "inset-block-start", "2.5px"},
    {"inset-block-end:-10%", "inset-block-end", "-10%"},
    {"inset-block-end:calc(2px + 5%)", "inset-block-end", "calc(5% + 2px)"},
    {"inset-block:1px 2px;inset-block-start:3px", "inset-block", "3px 2px"},
    {"inset-block:1px 2px;top:3px", "inset-block", "1px 2px"},
    {"inset-block-start:inherit;inset-block-end:inherit", "inset-block", "inherit"},
    {"inset-block-start:initial;inset-block-end:inherit", "inset-block", ""},
    {"inset-block:1px!important;inset-block-end:2px", "inset-block", "1px"},
    {"inset-block-start:1px!important;inset-block-end:2px", "inset-block", ""},
    {"inset-block:var(--I)", "inset-block-start", ""},
    {"inset-block:var(--I);inset-block-end:auto", "inset-block", ""},
    {"inset-block-end:var(--I, 2px)", "inset-block-end", "var(--I, 2px)"},
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
    /* Legacy aural declarations: specified values, not audio rendering. */
    {"azimuth:BEHIND LEFT", "azimuth", "220deg"},
    {"azimuth:LEFT behind", "azimuth", "220deg"},
    {"azimuth:CENTER BEHIND", "azimuth", "180deg"},
    {"azimuth:BEHIND", "azimuth", "180deg"},
    {"azimuth:left-side", "azimuth", "270deg"},
    {"azimuth:left-side behind", "azimuth", "270deg"},
    {"azimuth:far-left", "azimuth", "300deg"},
    {"azimuth:far-left behind", "azimuth", "240deg"},
    {"azimuth:left", "azimuth", "320deg"},
    {"azimuth:left behind", "azimuth", "220deg"},
    {"azimuth:center-left", "azimuth", "340deg"},
    {"azimuth:center-left behind", "azimuth", "200deg"},
    {"azimuth:center", "azimuth", "0deg"},
    {"azimuth:center behind", "azimuth", "180deg"},
    {"azimuth:center-right", "azimuth", "20deg"},
    {"azimuth:center-right behind", "azimuth", "160deg"},
    {"azimuth:right", "azimuth", "40deg"},
    {"azimuth:right behind", "azimuth", "140deg"},
    {"azimuth:far-right", "azimuth", "60deg"},
    {"azimuth:far-right behind", "azimuth", "120deg"},
    {"azimuth:right-side", "azimuth", "90deg"},
    {"azimuth:right-side behind", "azimuth", "90deg"},
    {"azimuth:LEFTWARDS", "azimuth", "leftwards"},
    {"azimuth:RIGHTWARDS", "azimuth", "rightwards"},
    {"azimuth:0", "azimuth", "0deg"},
    {"azimuth:-000.123456789DEG", "azimuth", "-0.123457deg"},
    {"azimuth:1.23456789RAD", "azimuth", "1.234568rad"},
    {"azimuth:10GRAD", "azimuth", "10grad"},
    {"azimuth:var(--Direction,LEFT)", "azimuth", "var(--Direction,LEFT)"},
    {"elevation:BELOW", "elevation", "below"},
    {"elevation:LEVEL", "elevation", "level"},
    {"elevation:ABOVE", "elevation", "above"},
    {"elevation:HIGHER", "elevation", "higher"},
    {"elevation:LOWER", "elevation", "lower"},
    {"elevation:0", "elevation", "0deg"},
    {"elevation:-90DEG", "elevation", "-90deg"},
    {"elevation:1.23456789RAD", "elevation", "1.234568rad"},
    {"elevation:99GRAD", "elevation", "99grad"},
    {"pause-before:0", "pause-before", "0s"},
    {"pause-before:-0ms", "pause-before", "0s"},
    {"pause-before:1.23456789S", "pause-before", "1.234568s"},
    {"pause-before:1234.56789MS", "pause-before", "1.234568s"},
    {"pause-before:.000499999ms", "pause-before", "0s"},
    {"pause-before:.0005MS", "pause-before", "0.000001s"},
    {"pause-before:999.9999999ms", "pause-before", "1s"},
    {"pause-before:1234567890123.456789MS", "pause-before", "1234567890.123457s"},
    {"pause-before:20.123456789%", "pause-before", "20.123457%"},
    {"pause-after:0", "pause-after", "0s"},
    {"pause-after:-0ms", "pause-after", "0s"},
    {"pause-after:1.23456789S", "pause-after", "1.234568s"},
    {"pause-after:1234.56789MS", "pause-after", "1.234568s"},
    {"pause-after:.000499999ms", "pause-after", "0s"},
    {"pause-after:.0005MS", "pause-after", "0.000001s"},
    {"pause-after:999.9999999ms", "pause-after", "1s"},
    {"pause-after:1234567890123.456789MS", "pause-after", "1234567890.123457s"},
    {"pause-after:20.123456789%", "pause-after", "20.123457%"},
    {"pitch-range:0", "pitch-range", "0"},
    {"pitch-range:+099.123456789", "pitch-range", "99.123457"},
    {"pitch-range:100", "pitch-range", "100"},
    {"richness:0", "richness", "0"},
    {"richness:+099.123456789", "richness", "99.123457"},
    {"richness:100", "richness", "100"},
    {"stress:0", "stress", "0"},
    {"stress:+099.123456789", "stress", "99.123457"},
    {"stress:100", "stress", "100"},
    {"pitch:X-LOW", "pitch", "x-low"},
    {"pitch:LOW", "pitch", "low"},
    {"pitch:MEDIUM", "pitch", "medium"},
    {"pitch:HIGH", "pitch", "high"},
    {"pitch:X-HIGH", "pitch", "x-high"},
    {"pitch:0", "pitch", "0hz"},
    {"pitch:220.123456789HZ", "pitch", "220.123457hz"},
    {"pitch:1.23456789KHZ", "pitch", "1.234568khz"},
    {"play-during:NONE", "play-during", "none"},
    {"play-during:AUTO", "play-during", "auto"},
    {"play-during:URL(A.ogg)", "play-during", "url(\"A.ogg\")"},
    {"play-during:url(A.ogg) REPEAT MIX", "play-during", "url(\"A.ogg\") mix repeat"},
    {"play-during:url(A.ogg) MIX REPEAT", "play-during", "url(\"A.ogg\") mix repeat"},
    {"play-during:url(A.ogg) REPEAT", "play-during", "url(\"A.ogg\") repeat"},
    {"play-during:url(A.ogg) MIX", "play-during", "url(\"A.ogg\") mix"},
    {"speak-header:ONCE", "speak-header", "once"},
    {"speak-header:ALWAYS", "speak-header", "always"},
    {"speak-numeral:DIGITS", "speak-numeral", "digits"},
    {"speak-numeral:CONTINUOUS", "speak-numeral", "continuous"},
    {"speak-punctuation:CODE", "speak-punctuation", "code"},
    {"speak-punctuation:NONE", "speak-punctuation", "none"},
    {"speak:NORMAL", "speak", "normal"},
    {"speak:NONE", "speak", "none"},
    {"speak:SPELL-OUT", "speak", "spell-out"},
    {"speech-rate:X-SLOW", "speech-rate", "x-slow"},
    {"speech-rate:SLOW", "speech-rate", "slow"},
    {"speech-rate:MEDIUM", "speech-rate", "medium"},
    {"speech-rate:FAST", "speech-rate", "fast"},
    {"speech-rate:X-FAST", "speech-rate", "x-fast"},
    {"speech-rate:FASTER", "speech-rate", "faster"},
    {"speech-rate:SLOWER", "speech-rate", "slower"},
    {"volume:SILENT", "volume", "silent"},
    {"volume:X-SOFT", "volume", "x-soft"},
    {"volume:SOFT", "volume", "soft"},
    {"volume:MEDIUM", "volume", "medium"},
    {"volume:LOUD", "volume", "loud"},
    {"volume:X-LOUD", "volume", "x-loud"},
    {"speech-rate:+123456789.123456789", "speech-rate", "123456789.123457"},
    {"volume:0", "volume", "0"},
    {"volume:100", "volume", "100"},
    {"volume:00012.123456789%", "volume", "12.123457%"},
    {"volume:10000%", "volume", "10000%"},
    {"voice-family:MALE, FEMALE, CHILD", "voice-family", "male, female, child"},
    {"voice-family:One/**/Two,MALE", "voice-family", "\"One Two\", male"},
    {"voice-family:\"Male\", female", "voice-family", "\"Male\", female"},
    {"voice-family:System-UI,serif,Child", "voice-family", "\"System-UI\", \"serif\", child"},
    {"voice-family:One\\20 Two, female", "voice-family", "\"One Two\", female"},
    {"voice-family:\"a\\22 b\\a c\",CHILD", "voice-family", "\"a\\\"b\\a c\", child"},
    {"cue:none", "cue", "none"},
    {"cue:URL(A.ogg)", "cue", "url(\"A.ogg\")"},
    {"cue:url(A.ogg) url(A.ogg)", "cue", "url(\"A.ogg\")"},
    {"cue:none URL(B.ogg)", "cue", "none url(\"B.ogg\")"},
    {"pause:0", "pause", "0s"},
    {"pause:1000ms 1s", "pause", "1s"},
    {"pause:20MS 30%", "pause", "0.02s 30%"},
    {"cue:url(A.ogg) none", "cue-before", "url(\"A.ogg\")"},
    {"cue:url(A.ogg) none", "cue-after", "none"},
    {"pause:20ms 30%", "pause-before", "0.02s"},
    {"pause:20ms 30%", "pause-after", "30%"},
    {"cue:none;cue-after:url(A.ogg)", "cue", "none url(\"A.ogg\")"},
    {"pause:1s;pause-before:2s", "pause", "2s 1s"},
    {"cue:none!important;cue-after:url(A.ogg)", "cue", "none"},
    {"pause:1s;pause-after:2s!important", "pause", ""},
    {"cue:var(--Cue, url(A.ogg))", "cue", "var(--Cue, url(A.ogg))"},
    {"cue:var(--Cue)", "cue-before", ""},
    {"pause:var(--Pause, 20ms)", "pause", "var(--Pause, 20ms)"},
    {"pause:var(--Pause);pause-after:1s", "pause", ""},
    {"azimuth:INITIAL", "azimuth", "initial"},
    {"azimuth:INHERIT", "azimuth", "inherit"},
    {"azimuth:UNSET", "azimuth", "unset"},
    {"azimuth:REVERT", "azimuth", "revert"},
    {"elevation:INITIAL", "elevation", "initial"},
    {"elevation:INHERIT", "elevation", "inherit"},
    {"elevation:UNSET", "elevation", "unset"},
    {"elevation:REVERT", "elevation", "revert"},
    {"pause:INITIAL", "pause", "initial"},
    {"pause:INHERIT", "pause", "inherit"},
    {"pause:UNSET", "pause", "unset"},
    {"pause:REVERT", "pause", "revert"},
    {"pause-before:INITIAL", "pause-before", "initial"},
    {"pause-before:INHERIT", "pause-before", "inherit"},
    {"pause-before:UNSET", "pause-before", "unset"},
    {"pause-before:REVERT", "pause-before", "revert"},
    {"pause-after:INITIAL", "pause-after", "initial"},
    {"pause-after:INHERIT", "pause-after", "inherit"},
    {"pause-after:UNSET", "pause-after", "unset"},
    {"pause-after:REVERT", "pause-after", "revert"},
    {"pitch-range:INITIAL", "pitch-range", "initial"},
    {"pitch-range:INHERIT", "pitch-range", "inherit"},
    {"pitch-range:UNSET", "pitch-range", "unset"},
    {"pitch-range:REVERT", "pitch-range", "revert"},
    {"pitch:INITIAL", "pitch", "initial"},
    {"pitch:INHERIT", "pitch", "inherit"},
    {"pitch:UNSET", "pitch", "unset"},
    {"pitch:REVERT", "pitch", "revert"},
    {"play-during:INITIAL", "play-during", "initial"},
    {"play-during:INHERIT", "play-during", "inherit"},
    {"play-during:UNSET", "play-during", "unset"},
    {"play-during:REVERT", "play-during", "revert"},
    {"richness:INITIAL", "richness", "initial"},
    {"richness:INHERIT", "richness", "inherit"},
    {"richness:UNSET", "richness", "unset"},
    {"richness:REVERT", "richness", "revert"},
    {"speak-header:INITIAL", "speak-header", "initial"},
    {"speak-header:INHERIT", "speak-header", "inherit"},
    {"speak-header:UNSET", "speak-header", "unset"},
    {"speak-header:REVERT", "speak-header", "revert"},
    {"speak-numeral:INITIAL", "speak-numeral", "initial"},
    {"speak-numeral:INHERIT", "speak-numeral", "inherit"},
    {"speak-numeral:UNSET", "speak-numeral", "unset"},
    {"speak-numeral:REVERT", "speak-numeral", "revert"},
    {"speak-punctuation:INITIAL", "speak-punctuation", "initial"},
    {"speak-punctuation:INHERIT", "speak-punctuation", "inherit"},
    {"speak-punctuation:UNSET", "speak-punctuation", "unset"},
    {"speak-punctuation:REVERT", "speak-punctuation", "revert"},
    {"speak:INITIAL", "speak", "initial"},
    {"speak:INHERIT", "speak", "inherit"},
    {"speak:UNSET", "speak", "unset"},
    {"speak:REVERT", "speak", "revert"},
    {"speech-rate:INITIAL", "speech-rate", "initial"},
    {"speech-rate:INHERIT", "speech-rate", "inherit"},
    {"speech-rate:UNSET", "speech-rate", "unset"},
    {"speech-rate:REVERT", "speech-rate", "revert"},
    {"stress:INITIAL", "stress", "initial"},
    {"stress:INHERIT", "stress", "inherit"},
    {"stress:UNSET", "stress", "unset"},
    {"stress:REVERT", "stress", "revert"},
    {"voice-family:INITIAL", "voice-family", "initial"},
    {"voice-family:INHERIT", "voice-family", "inherit"},
    {"voice-family:UNSET", "voice-family", "unset"},
    {"voice-family:REVERT", "voice-family", "revert"},
    {"volume:INITIAL", "volume", "initial"},
    {"volume:INHERIT", "volume", "inherit"},
    {"volume:UNSET", "volume", "unset"},
    {"volume:REVERT", "volume", "revert"},
    {"cue:INITIAL", "cue", "initial"},
    {"cue:INHERIT", "cue", "inherit"},
    {"cue:UNSET", "cue", "unset"},
    {"cue:REVERT", "cue", "revert"},
    {"pause:calc(1s + 2s)", "pause", "calc(3s)"},
    {"pitch:calc(1hz + 2hz)", "pitch", "calc(3hz)"},
    {"speech-rate:calc(1 + 2)", "speech-rate", "calc(3)"},

    {"fill:NONE", "fill", "none"},
    {"fill:CURRENTcolor", "fill", "currentcolor"},
    {"fill:ReD", "fill", "red"},
    {"fill:#1234", "fill", "rgba(17, 34, 51, 0.267)"},
    {"fill:hsl(none 100 50 / 50%)", "fill", "hsl(none 100% 50% / 0.5)"},
    {"fill:var(--Paint, #ABc)", "fill", "var(--Paint, #ABc)"},
    {"stroke:NoNe", "stroke", "none"},
    {"stroke:currentColor", "stroke", "currentcolor"},
    {"stroke:rgb(1.2 3.4 5.6 / 50%)", "stroke", "rgba(1.2, 3.4, 5.6, 0.5)"},
    {"stroke:transparent", "stroke", "transparent"},
    {"stroke:#123!important;stroke:none", "stroke", "rgb(17, 34, 51)"},
    {"stroke:INITIAL", "stroke", "initial"},
    {"fill:UNSET", "fill", "unset"},
    {"stroke:INHERIT", "stroke", "inherit"},
    {"fill:REVERT", "fill", "revert"},
    {"fill-opacity:+.123456789", "fill-opacity", "0.123457"},
    {"fill-opacity:123456789.123456789", "fill-opacity", "123456789.123457"},
    {"fill-opacity:-.25", "fill-opacity", "-0.25"},
    {"fill-opacity:2!important;fill-opacity:.5", "fill-opacity", "2"},
    {"stroke-opacity:1.23456789", "stroke-opacity", "1.234568"},
    {"stroke-opacity:-0.0000004", "stroke-opacity", "0"},
    {"stroke-opacity:-1", "stroke-opacity", "-1"},
    {"stroke-opacity:var(--Alpha)", "stroke-opacity", "var(--Alpha)"},
    {"fill-opacity:initial", "fill-opacity", "initial"},
    {"stroke-opacity:inherit", "stroke-opacity", "inherit"},
    {"stroke-width:+001.23456789", "stroke-width", "1.234568"},
    {"stroke-width:0", "stroke-width", "0"},
    {"stroke-width:-0.000000000000", "stroke-width", "0"},
    {"stroke-width:0PX", "stroke-width", "0px"},
    {"stroke-width:-0%", "stroke-width", "0%"},
    {"stroke-width:.123456789EM", "stroke-width", "0.123457em"},
    {"stroke-width:123456789.123456789%", "stroke-width", "123456789.123457%"},
    {"stroke-width:2p\\78 !important;stroke-width:3", "stroke-width", "2px"},
    {"stroke-width:var(--Width, 2PX)", "stroke-width", "var(--Width, 2PX)"},
    {"stroke-width:INITIAL", "stroke-width", "initial"},
    {"stroke-width:UNSET", "stroke-width", "unset"},
    {"cursor:AUTO", "cursor", "auto"},
    {"cursor:CrOsShAiR", "cursor", "crosshair"},
    {"cursor:DEFAULT", "cursor", "default"},
    {"cursor:POINTER", "cursor", "pointer"},
    {"cursor:MOVE", "cursor", "move"},
    {"cursor:E-RESIZE", "cursor", "e-resize"},
    {"cursor:NE-RESIZE", "cursor", "ne-resize"},
    {"cursor:NW-RESIZE", "cursor", "nw-resize"},
    {"cursor:N-RESIZE", "cursor", "n-resize"},
    {"cursor:SE-RESIZE", "cursor", "se-resize"},
    {"cursor:SW-RESIZE", "cursor", "sw-resize"},
    {"cursor:S-RESIZE", "cursor", "s-resize"},
    {"cursor:W-RESIZE", "cursor", "w-resize"},
    {"cursor:TEXT", "cursor", "text"},
    {"cursor:WAIT", "cursor", "wait"},
    {"cursor:HELP", "cursor", "help"},
    {"cursor:PROGRESS", "cursor", "progress"},
    {"cursor:URL(Cur.cur), p\\6f inter", "cursor", "url(\"Cur.cur\"), pointer"},
    {"cursor:url('a b\\22 c\\a d'),url(Other.png),MOVE", "cursor", "url(\"a b\\\"c\\a d\"), url(\"Other.png\"), move"},
    {"cursor:url(),url(Cur.cur),url(Cur.cur),AUTO", "cursor", "url(\"\"), url(\"Cur.cur\"), url(\"Cur.cur\"), auto"},
    {"cursor:INHERIT", "cursor", "inherit"},
    {"cursor:INITIAL", "cursor", "initial"},
    {"cursor:REVERT", "cursor", "revert"},
    {"cursor:UNSET", "cursor", "unset"},
    {"cursor:var(--Cursor, url(Cur.cur), AUTO)", "cursor", "var(--Cursor, url(Cur.cur), AUTO)"},
    {"grid-template-columns:NONE", "grid-template-columns", "none"},
    {"grid-template-rows:INITIAL", "grid-template-rows", "initial"},
    {"grid-template-columns:inherit", "grid-template-columns", "inherit"},
    {"grid-template-rows:revert", "grid-template-rows", "revert"},
    {"grid-template-columns:unset", "grid-template-columns", "unset"},
    {"grid-template-columns:+001.23456789EM 0002FR .5678912% -0", "grid-template-columns",
        "1.234568em 2fr 0.567891% 0px"},
    {"grid-template-rows:AUTO min-CONTENT MAX-content 0FR", "grid-template-rows", "auto min-content max-content 0fr"},
    {"grid-template-columns:MINMAX(0,1FR) minmax(MIN-content,MAX-content)", "grid-template-columns",
        "minmax(0px, 1fr) minmax(min-content, max-content)"},
    {"grid-template-rows:minmax(AUTO,20%) minmax(2EM,1PX)", "grid-template-rows", "minmax(auto, 20%) minmax(2em, 1px)"},
    {"grid-template-columns:REPEAT(+0002,MINMAX(0,1FR) 2PX) 3%", "grid-template-columns",
        "repeat(2, minmax(0px, 1fr) 2px) 3%"},
    {"grid-template-rows:repeat(1,auto)repeat(2,0)minmax(0,MAX-CONTENT)", "grid-template-rows",
        "repeat(1, auto) repeat(2, 0px) minmax(0px, max-content)"},
    {"grid-template-rows:repeat(32,MINMAX(1PX,2FR))", "grid-template-rows", "repeat(32, minmax(1px, 2fr))"},
    {"grid-template-columns:repeat(16,1px 2px)", "grid-template-columns", "repeat(16, 1px 2px)"},
    {"grid-template-rows:1px repeat(31,2em)", "grid-template-rows", "1px repeat(31, 2em)"},
    {"grid-template-columns:123456789.12345678em .00000049%", "grid-template-columns", "123456789.123457em 0%"},
    {"grid-template-rows:repeat(2,1px)!important;grid-template-rows:none", "grid-template-rows", "repeat(2, 1px)"},
    {"grid-template-columns:r\\65 peat(2,m\\69 nmax(0,1f\\72 ))", "grid-template-columns", "repeat(2, minmax(0px, 1fr))"},
    {"grid-template-rows:var(--Rows, 1PX  2px)", "grid-template-rows", "var(--Rows, 1PX  2px)"},
    {"grid-auto-flow:ROW", "grid-auto-flow", "row"},
    {"grid-auto-flow:COLUMN", "grid-auto-flow", "column"},
    {"grid-auto-flow:DENSE", "grid-auto-flow", "dense"},
    {"grid-auto-flow:ROW dense", "grid-auto-flow", "dense"},
    {"grid-auto-flow:DENSE ROW", "grid-auto-flow", "dense"},
    {"grid-auto-flow:dense/**/CoLuMn", "grid-auto-flow", "column dense"},
    {"grid-auto-flow:column dense", "grid-auto-flow", "column dense"},
    {"grid-auto-flow:d\\65 nse c\\6f lumn", "grid-auto-flow", "column dense"},
    {"grid-auto-flow:column!important;grid-auto-flow:dense", "grid-auto-flow", "column"},
    {"grid-auto-flow:INITIAL", "grid-auto-flow", "initial"},
    {"grid-auto-flow:UNSET", "grid-auto-flow", "unset"},
    {"grid-auto-flow:var(--flow)", "grid-auto-flow", "var(--flow)"},
    {"transform:NONE", "transform", "none"},
    {"transform:INITIAL", "transform", "initial"},
    {"transform:var(--move,  translateX(1px))", "transform", "var(--move,  translateX(1px))"},
    {"transform:TRANSLATEX( +01.23456789EM )", "transform", "translateX(1.234568em)"},
    {"transform:translatey(-0010.2000%)", "transform", "translateY(-10.2%)"},
    {"transform:translate(0)", "transform", "translate(0px)"},
    {"transform:translate(2px,-0%)", "transform", "translate(2px)"},
    {"transform:translate(2px,.00000001em)", "transform", "translate(2px)"},
    {"transform:translate(2px,3%)", "transform", "translate(2px, 3%)"},
    {"transform:scale(2,2)", "transform", "scale(2)"},
    {"transform:scale(2,0)", "transform", "scale(2, 0)"},
    {"transform:SCALE(50%,.50)", "transform", "scale(0.5)"},
    {"transform:scale(50%,150%)", "transform", "scale(0.5, 1.5)"},
    {"transform:scaleX(-012.3456789%)", "transform", "scaleX(-0.123457)"},
    {"transform:scaleY(.00005%)", "transform", "scaleY(0.000001)"},
    {"transform:scaleY(.00004999%)", "transform", "scaleY(0)"},
    {"transform:scaleX(-.5%)", "transform", "scaleX(-0.005)"},
    {"transform:scaleY(+5%)", "transform", "scaleY(0.05)"},
    {"transform:scaleY(-0%)", "transform", "scaleY(0)"},
    {"transform:scale(12345678912345678912345%)", "transform", "scale(123456789123456789123.45)"},
    {"transform:scale(.12345678,-1.23456789)", "transform", "scale(0.123457, -1.234568)"},
    {"transform:rotate(0)", "transform", "rotate(0deg)"},
    {"transform:rotate(-0)", "transform", "rotate(0deg)"},
    {"transform:rotate(740DEG)", "transform", "rotate(740deg)"},
    {"transform:rotate(-1.25TURN)", "transform", "rotate(-1.25turn)"},
    {"transform:rotate(100GRAD)", "transform", "rotate(100grad)"},
    {"transform:rotate(3.14159265rad)", "transform", "rotate(3.141593rad)"},
    {"transform:translateX(1px)scaleY(2)rotate(10deg)", "transform",
        "translateX(1px) scaleY(2) rotate(10deg)"},
    {"transform:t\\72 anslatex(1p\\78 )/**/sCaLeY(100%)", "transform", "translateX(1px) scaleY(1)"},
    {"transform:scale(1)!important;transform:none", "transform", "scale(1)"},
    {"transform:translate(123456789.12345678px, -123456789.7654321em)", "transform",
        "translate(123456789.123457px, -123456789.765432em)"},
    /* CSS Values 4 10.10.1 / 10.13: specified simplification, without layout
     * metrics, property range clamping or dropping zero-valued unit terms. */
    {"width:calc(1px + 2em)", "width", "calc(2em + 1px)"},
    {"width:calc(20px + 30px)", "width", "calc(50px)"},
    {"width:calc(20px + 0%)", "width", "calc(0% + 20px)"},
    {"width:calc(100% - 100% + 1px)", "width", "calc(0% + 1px)"},
    {"width:calc(1em - 1em + 1px)", "width", "calc(0em + 1px)"},
    {"width:calc(0 * (10% + 1em + 5px))", "width", "calc(0% + 0em + 0px)"},
    {"width:calc(2 * (50% - 5px))", "width", "calc(100% - 10px)"},
    {"width:calc((50% - 5px) * -2)", "width", "calc(-100% + 10px)"},
    {"width:calc(50% - (2px - 1em))", "width", "calc(50% + 1em - 2px)"},
    {"width:calc(calc(1px + 2em) * calc(2 + 1))", "width", "calc(6em + 3px)"},
    {"width:calc(12px / (1 + 2))", "width", "calc(4px)"},
    {"width:calc(12px / calc(1 + 2) + 1px)", "width", "calc(5px)"},
    {"width:calc(1px / 3)", "width", "calc(0.333333px)"},
    {"width:calc(1px / -3)", "width", "calc(-0.333333px)"},
    {"width:calc(1px / 3 * 3)", "width", "calc(1px)"},
    {"width:calc(.0000004px * 10)", "width", "calc(0.000004px)"},
    {"width:calc(.0000005px * 3)", "width", "calc(0.000002px)"},
    {"width:calc(-.0000005px * 3)", "width", "calc(-0.000002px)"},
    {"width:calc(.1234567px + .1234567px)", "width", "calc(0.246913px)"},
    {"width:calc(999999999px + .25px)", "width", "calc(999999999.25px)"},
    {"width:calc(1in + 2.54cm + 25.4mm + 72pt + 6pc + 101.6Q)", "width", "calc(576px)"},
    {"width:calc(1q)", "width", "calc(0.944882px)"},
    {"width:calc(1rem + 1vw + 1px + 1em + 1ch + 1%)", "width", "calc(1% + 1ch + 1em + 1px + 1rem + 1vw)"},
    {"width:CALC(001.250PX /* unit */ + 2EM)", "width", "calc(2em + 1.25px)"},
    {"width:calc(-0px)", "width", "calc(0px)"},
    {"width:calc(-2px)", "width", "calc(-2px)"},
    {"z-index:calc(1.5)", "z-index", "calc(1.5)"},
    {"line-height:calc(.5 + .75)", "line-height", "calc(1.25)"},
    {"line-height:calc(100% + 1px)", "line-height", "calc(100% + 1px)"},
    {"pause:calc(1000ms + 50%) calc(1s / 2)", "pause", "calc(50% + 1s) calc(0.5s)"},
    {"pitch:calc(1khz + 20hz)", "pitch", "calc(1020hz)"},
    {"volume:calc(20% + 5%)", "volume", "calc(25%)"},
    {"gap:calc(1px + 2%) calc(2em * 2)", "gap", "calc(2% + 1px) calc(4em)"},
    {"margin-inline:calc(1px + 2%) calc(2px)", "margin-inline", "calc(2% + 1px) calc(2px)"},
    {"margin:calc(1px) calc(2px) calc(3px)", "margin-left", "calc(2px)"},
    {"padding:calc(-2px)", "padding", "calc(-2px)"},
    {"border:calc(1px / 0) solid red", "border-left-color", "red"},
    {"flex:calc(12px / 2) calc(1 + 1) calc(3)", "flex", "calc(2) calc(3) calc(6px)"},
    {"flex:calc(1px / 0) 2 3", "flex-grow", "2"},
    {"columns:calc(1 + 1) calc(10px * 2)", "columns", "calc(20px) calc(2)"},
    {"columns:calc(1px / 0) 2", "column-count", "2"},
    /* Values 4 10.9.2 / 10.13: keywords round-trip without prematurely
     * censoring non-finite results or nested signed zero. */
    {"width:calc(1px / 0)", "width", "calc(infinity * 1px)"},
    {"width:calc(-1px / 0)", "width", "calc(-infinity * 1px)"},
    {"width:calc(0px / 0)", "width", "calc(NaN * 1px)"},
    {"width:calc(1px * nAn)", "width", "calc(NaN * 1px)"},
    {"width:calc(1in * iNfInItY)", "width", "calc(infinity * 1px)"},
    {"width:calc(1% * -infinity)", "width", "calc(-infinity * 1%)"},
    {"width:calc(1px / -0)", "width", "calc(infinity * 1px)"},
    {"width:calc(1px / calc(-5 * 0))", "width", "calc(-infinity * 1px)"},
    {"width:calc(1px / (1 / -infinity))", "width", "calc(-infinity * 1px)"},
    {"width:calc(1px * infinity / infinity)", "width", "calc(NaN * 1px)"},
    {"width:calc(0px * infinity)", "width", "calc(NaN * 1px)"},
    {"width:calc(1px * (infinity - infinity))", "width", "calc(NaN * 1px)"},
    {"width:calc(1px / -infinity)", "width", "calc(0px)"},
    {"width:calc((1em - 1px) / 0)", "width", "calc((infinity * 1em) - (infinity * 1px))"},
    {"width:calc((1% - 1px) / 0)", "width", "calc((infinity * 1%) - (infinity * 1px))"},
    {"width:calc((0% + 1px) / 0)", "width", "calc((NaN * 1%) + (infinity * 1px))"},
    {"line-height:calc(NaN)", "line-height", "calc(NaN)"},
    {"line-height:calc(infinity)", "line-height", "calc(infinity)"},
    {"pitch:calc(1khz / 0)", "pitch", "calc(infinity * 1hz)"},
    {"pause-before:calc(1ms / 0)", "pause-before", "calc(infinity * 1s)"},
    {"flex:calc(infinity) 1 calc(NaN * 1px)", "flex", "calc(infinity) 1 calc(NaN * 1px)"},
    {"margin:1px calc(2px + 1em)", "margin", "1px calc(1em + 2px)"},
    {"margin:1px calc(2px + 1em)", "margin-top", "1px"},
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
    {"background-image:linear-gradient(red, blue)", "background-image", "linear-gradient(red, blue)"},
    {"background-image:url(a.png), linear-gradient(red, blue)", "background-image",
        "url(\"a.png\"), linear-gradient(red, blue)"},
    /* CSS Images 3 section 7, including its normative serialization example. */
    {"background-image:Linear-Gradient( to bottom, red 0%,yellow,black 100px)", "background-image",
        "linear-gradient(red, yellow, black 100px)"},
    {"background-image:linear-gradient(TO TOP, RED,BLUE)", "background-image", "linear-gradient(to top, red, blue)"},
    {"background-image:linear-gradient(to LEFT, red,blue)", "background-image", "linear-gradient(to left, red, blue)"},
    {"background-image:linear-gradient(to right, red,blue)", "background-image", "linear-gradient(to right, red, blue)"},
    {"background-image:linear-gradient(0180.000DEG, red,blue)", "background-image", "linear-gradient(red, blue)"},
    {"background-image:linear-gradient(-045.1234567deg,red -.1234567em, blue 12345678px)", "background-image",
        "linear-gradient(-45.123457deg, red -0.123457em, blue 12345678px)"},
    {"background-image:linear-gradient(+000deg,red -0em, blue 100.000%)", "background-image",
        "linear-gradient(0deg, red, blue)"},
    {"background-image:linear-gradient(450deg,red,blue)", "background-image", "linear-gradient(450deg, red, blue)"},
    {"background-image:linear-gradient(red 10%,green 0%,blue 100px)", "background-image",
        "linear-gradient(red 10%, green 0%, blue 100px)"},
    {"background-image:linear-gradient(red 120%,blue 100%)", "background-image", "linear-gradient(red 120%, blue)"},
    {"background-image:radial-gradient( ELLIPSE, red,blue)", "background-image", "radial-gradient(red, blue)"},
    {"background-image:radial-gradient( CIRCLE, red 0%,blue 100%)", "background-image", "radial-gradient(circle, red, blue)"},
    {"background-image:linear-gradient(/**/to/**/bottom,red/**/0%,blue/**/100%)", "background-image",
        "linear-gradient(red, blue)"},
    {"background-image:l\\69near-gradient(t\\6f  right,r\\65 d 0,BLUE)", "background-image",
        "linear-gradient(to right, red, blue)"},
    {"background-image:linear-gradient(RGB(255 0 0 / .5) 0%,#00f 100%)", "background-image",
        "linear-gradient(rgba(255, 0, 0, 0.5), rgb(0, 0, 255))"},
    {"background-image:radial-gradient(hsl(0 100% 50%),transparent)", "background-image",
        "radial-gradient(rgb(255, 0, 0), transparent)"},
    {"background-image:none,linear-gradient(red,blue),url('a,b.png'),radial-gradient(circle,white,black)",
        "background-image", "none, linear-gradient(red, blue), url(\"a,b.png\"), radial-gradient(circle, white, black)"},
    {"background:radial-gradient(ellipse,red 0%,blue 100%) no-repeat left fixed,linear-gradient(to bottom,white,black) red",
        "background", "radial-gradient(red, blue) left center no-repeat fixed, linear-gradient(white, black) red"},
    {"background-image:linear-gradient(red,blue)!important;background-image:none", "background-image",
        "linear-gradient(red, blue)"},
    {"background-image:linear-gradient(var(--Stop),BLUE)", "background-image", "linear-gradient(var(--Stop),BLUE)"},
    /* Missing and non-legacy color components survive gradient serialization. */
    {"background-image:linear-gradient(lab(50% 0 0),blue)", "background-image", "linear-gradient(lab(50 0 0), blue)"},
    {"background-image:linear-gradient(rgb(none 0 0),blue)", "background-image",
        "linear-gradient(color(srgb none 0 0), blue)"},
    {"background:none", "background", "none"},
    {"background:transparent", "background", "none"},
    {"background:repeat scroll none 0% 0% transparent", "background", "none"},
    {"background:red", "background", "red"},
    {"background:CURRENTCOLOR", "background", "currentcolor"},
    {"background:red", "background-image", "none"},
    {"background:red", "background-position", "0% 0%"},
    {"background:red", "background-repeat", "repeat"},
    {"background:red", "background-attachment", "scroll"},
    {"background:red", "background-color", "red"},
    {"background:NONE, NONE, NONE", "background", "none, none, none"},
    {"background:NONE, NONE, NONE", "background-image", "none, none, none"},
    {"background:none,none", "background-position", "0% 0%, 0% 0%"},
    {"background:none,none", "background-repeat", "repeat, repeat"},
    {"background:none,none", "background-attachment", "scroll, scroll"},
    {"background:none,none", "background-color", "transparent"},
    {"background:fixed top left no-repeat url(A.png) #1234", "background",
        "url(\"A.png\") left top no-repeat fixed rgba(17, 34, 51, 0.267)"},
    {"background:repeat no-repeat url(A.png)", "background", "url(\"A.png\") repeat-x"},
    {"background:url(A.png) no-repeat repeat", "background", "url(\"A.png\") repeat-y"},
    {"background:TOP", "background", "center top"},
    {"background:-.1234567em +00012.3456789%", "background-position", "-0.123457em 12.345679%"},
    {"background:0px 0px", "background", "0px 0px"},
    {"background:left top", "background", "left top"},
    {"background:url('data:image/svg+xml;a,b;c') fixed", "background",
        "url(\"data:image/svg+xml;a,b;c\") fixed"},
    {"background:url('a\\\"b\\\\c')", "background", "url(\"a\\\"b\\\\c\")"},
    {"background:url(Știri.png) top,none fixed rgb(1 2 3 / .4)", "background",
        "url(\"Știri.png\") center top, fixed rgba(1, 2, 3, 0.4)"},
    {"background:url(A.png) top,no-repeat url(B.png) bottom blue", "background-image",
        "url(\"A.png\"), url(\"B.png\")"},
    {"background:url(A.png) top,no-repeat url(B.png) bottom blue", "background-position", "center top, center bottom"},
    {"background:url(A.png) top,no-repeat url(B.png) bottom blue", "background-repeat", "repeat, no-repeat"},
    {"background:url(A.png) top,no-repeat url(B.png) bottom blue", "background-color", "blue"},
    {"background:url(A.png),url(B.png);background-repeat:repeat no-repeat,no-repeat repeat", "background",
        "url(\"A.png\") repeat-x, url(\"B.png\") repeat-y"},
    {"background:url(A.png),url(B.png);background-position:right top,bottom left", "background",
        "url(\"A.png\") right top, url(\"B.png\") left bottom"},
    {"background:url(A.png),url(B.png);background-attachment:fixed,scroll", "background",
        "url(\"A.png\") fixed, url(\"B.png\")"},
    {"background:url(A.png),url(B.png);background-color:#fff", "background",
        "url(\"A.png\"), url(\"B.png\") rgb(255, 255, 255)"},
    {"background:url(A.png),url(B.png);background-image:none,none", "background", "none, none"},
    {"background:url(A.png),url(B.png);background-repeat:no-repeat", "background", ""},
    {"background:none;background-image:url(A.png),url(B.png)", "background", ""},
    {"background:none;background-position:left,top", "background", ""},
    {"background:none;background-attachment:fixed,scroll", "background", ""},
    {"background:none;background-repeat:repeat,repeat", "background", ""},
    {"background-image:none;background-position:top;background-repeat:no-repeat;background-attachment:fixed;"
        "background-color:red", "background", "center top no-repeat fixed red"},
    {"background:red!important;background-color:blue", "background", "red"},
    {"background:red;background-color:blue!important", "background", ""},
    {"background:red!important;background-color:blue!important", "background", "blue"},
    {"background-image:none;background-color:red", "background", ""},
    {"background:inherit", "background", "inherit"},
    {"background:INITIAL", "background-position", "initial"},
    {"background:unset", "background", "unset"},
    {"background:REVERT", "background-color", "revert"},
    {"background:inherit;background-image:initial", "background", ""},
    {"background:var(--Layers, url(A.png),none)", "background", "var(--Layers, url(A.png),none)"},
    {"background:var(--Layers)", "background-repeat", ""},
    {"background:var(--Layers);background-color:red", "background", ""},
    {"background:none;background-image:var(--Image)", "background", ""},
    {"background:linear-gradient(red,blue) fixed", "background", "linear-gradient(red, blue) fixed"},
    {"background:linear-gradient(red,blue),none fixed rgb(1,2,3)", "background-color", "rgb(1, 2, 3)"},
    {"background:linear-gradient(red,blue),none fixed rgb(1,2,3)", "background-attachment", "scroll, fixed"},
    {"background:radial-gradient(red,blue) no-repeat", "background-image", "radial-gradient(red, blue)"},
    {"background:red;background:red,none", "background", "red"},
    {"background:blue;background:none,none,", "background", "blue"},
    {"background:red;background:repeat-x repeat", "background", "red"},
    {"background:blue;background:initial red", "background", "blue"},
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
    {"flex:2 calc(1 + 1) auto", "flex", "2 calc(2) auto"},
    {"flex:2 1 calc(10px + 2px)", "flex", "2 1 calc(12px)"},
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
    {"color:rgb(none 0 0)", "color", "color(srgb none 0 0)"},
    {"color:rgb(1 2 3 / none)", "color", "color(srgb 0.003922 0.007843 0.011765 / none)"},
    {"color:rgba(none none none)", "color", "color(srgb none none none)"},
    {"color:rgb(255 none 50% / .1234567)", "color", "color(srgb 1 none 0.5 / 0.123457)"},
    {"color:rgb(-10 none 999% / 2)", "color", "color(srgb 0 none 1)"},
    {"color:rgb(none 0 0 / -2)", "color", "color(srgb none 0 0 / 0)"},
    {"color:COLOR(SRGB .5 25% +0.0 / 50%)", "color", "color(srgb 0.5 0.25 0 / 0.5)"},
    {"color:color(srgb 1 0 0)", "color", "color(srgb 1 0 0)"},
    {"color:color(srgb -1 200% .4)", "color", "color(srgb -1 2 0.4)"},
    {"color:color(srgb none none none / none)", "color", "color(srgb none none none / none)"},
    {"color:color(srgb .3 .4 .5 / 999%)", "color", "color(srgb 0.3 0.4 0.5)"},
    {"color:color(srgb .3 .4 .5 / -10)", "color", "color(srgb 0.3 0.4 0.5 / 0)"},
    {"color:color(srgb -.0000005 .0000005 -.0000015 / .99999999)", "color", "color(srgb 0 0.000001 -0.000001)"},
    {"color:color(srgb -.00005% .00005% -.00015%)", "color", "color(srgb 0 0.000001 -0.000001)"},
    {"color:color(srgb 123456789012345678901234567890.1234567% -12345678901234567890.1234567 0)", "color",
        "color(srgb 1234567890123456789012345678.901235 -12345678901234567890.123457 0)"},
    {"border:solid color(srgb 0 50% 0 / none)", "border", "solid color(srgb 0 0.5 0 / none)"},
    {"fill:color(srgb none .25 1)", "fill", "color(srgb none 0.25 1)"},
    {"background:linear-gradient(color(srgb 1 0 0),rgb(none 255 0)) color(srgb 0 0 1)", "background",
        "linear-gradient(color(srgb 1 0 0), color(srgb none 1 0)) color(srgb 0 0 1)"},
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
    {"color:hsl(30 200% 50%)", "color", "rgb(255, 127.5, 0)"},
    {"color:hsl(30,200%,40%)", "color", "rgb(255, 102, 0)"},
    {"color:hsla(210 200 60 / 50%)", "color", "rgba(0, 153, 255, 0.5)"},
    {"color:hsl(0 200% 110%)", "color", "rgb(229.5, 255, 255)"},
    {"color:hsl(0 200% -10%)", "color", "rgb(0, 25.5, 25.5)"},
    {"color:hsl(30 100.000001% 50% / .1234567)", "color", "rgba(255, 127.5, 0, 0.123457)"},
    {"color:hwb(0 10% -10%)", "color", "rgb(255, 25.5, 25.5)"},
    {"color:hwb(0 200% -100%)", "color", "rgb(255, 255, 255)"},
    {"color:hwb(0 -100% 200%)", "color", "rgb(0, 0, 0)"},
    {"color:hwb(0 -.000001% 0)", "color", "rgb(255, 0, 0)"},
    {"color:hwb(240 -10 -10 / .5)", "color", "rgba(0, 0, 255, 0.5)"},
    {"border-color:hsl(30 200 40) hwb(0 -10 10)", "border-color", "rgb(255, 102, 0) rgb(229.5, 0, 0)"},
    {"outline:2px solid hsl(30 200 40)", "outline", "rgb(255, 102, 0) solid 2px"},
    {"fill:hsl(30 200 40)", "fill", "rgb(255, 102, 0)"},
    {"stroke:hwb(0 -10 10)", "stroke", "rgb(229.5, 0, 0)"},
    {"background:linear-gradient(hsl(30 200 40),hwb(0 -10 10)) hsl(30 200 40)", "background",
        "linear-gradient(rgb(255, 102, 0), rgb(229.5, 0, 0)) rgb(255, 102, 0)"},
    {"color:hsl(0 0% 110%)", "color", "rgb(255, 255, 255)"},
    {"color:hsl(0 0% -10%)", "color", "rgb(0, 0, 0)"},
    {"color:hsl(none 100% 50%)", "color", "hsl(none 100% 50%)"},
    {"color:hsla(120 none 50% / none)", "color", "hsl(120 none 50% / none)"},
    {"color:hwb(none none none)", "color", "hwb(none none none)"},
    {"color:hwb(0 20% 10% / none)", "color", "hwb(0 20% 10% / none)"},
    /* CSS Color 4 section 16.2.2: missing polar components keep their space. */
    {"color:HSLA(NONE +0100 050 / 100%)", "color", "hsl(none 100% 50%)"},
    {"color:hsl(740deg none .1234567%)", "color", "hsl(20 none 0.123457%)"},
    {"color:hwb(740deg 20% none / .5000000)", "color", "hwb(20 20% none / 0.5)"},
    {"color:hwb(20 none 30% / none)", "color", "hwb(20 none 30% / none)"},
    {"color:hsl(-.5turn none 50%)", "color", "hsl(180 none 50%)"},
    {"color:hwb(100grad 30 none)", "color", "hwb(90 30% none)"},
    {"color:hsl(3.141592653589793rad 100% none)", "color", "hsl(180 100% none)"},
    {"color:hsl(1000000000.25turn none 50%)", "color", "hsl(90 none 50%)"},
    {"color:hsl(-.00000001deg none 50%)", "color", "hsl(0 none 50%)"},
    {"color:hwb(359.9999999 20% none)", "color", "hwb(0 20% none)"},
    {"color:hsl(none -.00000001 50%)", "color", "hsl(none 0% 50%)"},
    {"color:hsl(none -0 -0)", "color", "hsl(none 0% 0%)"},
    {"color:hsl(none 200% 110%)", "color", "hsl(none 200% 110%)"},
    {"color:hsl(none 20% -10%)", "color", "hsl(none 20% -10%)"},
    {"color:hwb(none -10% 10%)", "color", "hwb(none -10% 10%)"},
    {"color:hwb(none 400% 200%)", "color", "hwb(none 400% 200%)"},
    {"color:hwb(none 100000000000000000000 50000000000000000000)", "color",
        "hwb(none 100000000000000000000% 50000000000000000000%)"},
    {"color:hsl(none 30.1234567% 123456789123456789.7654321%)", "color",
        "hsl(none 30.123457% 123456789123456789.765432%)"},
    {"color:hwb(none 0 0 / -1)", "color", "hwb(none 0% 0% / 0)"},
    {"color:hsl(none 100 50 / 1000000)", "color", "hsl(none 100% 50%)"},
    {"color:hwb(none 20 30 / .1234567)", "color", "hwb(none 20% 30% / 0.123457)"},
    {"color:hsl(none 100 50 / .99999999)", "color", "hsl(none 100% 50%)"},
    {"color:hsl(none 100 50 / .00000001)", "color", "hsl(none 100% 50% / 0)"},
    {"color:h\\73la(n\\6fne/**/100/**/50/NoNe)", "color", "hsl(none 100% 50% / none)"},
    {"color:hsl(none 100% 50%)!important;color:blue", "color", "hsl(none 100% 50%)"},
    {"border-color:hsl(none 100 50) hwb(120 none 10)", "border-color",
        "hsl(none 100% 50%) hwb(120 none 10%)"},
    {"border:solid hsl(none 100 50)", "border", "solid hsl(none 100% 50%)"},
    {"outline:1px hwb(none 0 0) dashed", "outline", "hwb(none 0% 0%) dashed 1px"},
    {"background:linear-gradient(hsl(none 100 50),hwb(120 none 0)) fixed hsl(0 none 50)", "background",
        "linear-gradient(hsl(none 100% 50%), hwb(120 none 0%)) fixed hsl(0 none 50%)"},
    {"background-image:radial-gradient(circle,hwb(none none none),hsl(0 none 50 / none))", "background-image",
        "radial-gradient(circle, hwb(none none none), hsl(0 none 50% / none))"},
    {"color:HWB(740deg 20% 30% / 50%)", "color", "rgba(178.5, 93.5, 51, 0.5)"},
    {"color:hwb(150 20% 10%)", "color", "rgb(51, 229.5, 140.25)"},
    {"color:hwb(267 19% 60%)", "color", "rgb(72.5475, 48.45, 102)"},
    {"color:hwb(0 40 80%)", "color", "rgb(85, 85, 85)"},
    {"color:hwb(0 400% 200%)", "color", "rgb(170, 170, 170)"},
    {"color:hwb(0 100000000 50000000)", "color", "rgb(170, 170, 170)"},
    {"color:hwb(0 0% 0% / 100000000%)", "color", "rgb(255, 0, 0)"},
    {"color:hwb(0 0% 0% / -100%)", "color", "rgba(255, 0, 0, 0)"},
    {"color:hwb(0 -10% 10%)", "color", "rgb(229.5, 0, 0)"},
    {"border-color:hsl(0,100%,50%) hwb(120 0 0)", "border-color", "rgb(255, 0, 0) rgb(0, 255, 0)"},
    {"border-color:hsl(0,100%,50%);border-left-color:rgb(255 0 0)", "border-color", "rgb(255, 0, 0)"},
    {"border-color:hwb(0 0 0)!important;border-right-color:blue", "border-color", "rgb(255, 0, 0)"},
    {"color:lab(50 -0.0000005 0)", "color", "lab(50 0 0)"},
    {"color:lab(50 -0.00000050001 0)", "color", "lab(50 -0.000001 0)"},
    {"color:lab(50 -0.00000049999 0)", "color", "lab(50 0 0)"},
    {"color:lab(50 0.99999999999% -0.99999999999%)", "color", "lab(50 1.25 -1.25)"},
    {"color:lch(50 2% 359.9999999)", "color", "lch(50 3 0)"},
    {"color:lab(50 -999999999999999999.9999999% 0)", "color", "lab(50 -1250000000000000000 0)"},
    {"color:lab(50 10000000000000000000000000000000000000000000000000"
        "00000000000000000000000000000000000000000000000000000000000 0)", "color",
        "lab(50 10000000000000000000000000000000000000000000000000"
        "00000000000000000000000000000000000000000000000000000000000 0)"},
    {"color:LAB(56.200% 0.000 66.88%)", "color", "lab(56.2 0 83.6)"},
    {"color:lab(50 -100% 100%)", "color", "lab(50 -125 125)"},
    {"color:lab(120 -200 180 / 150%)", "color", "lab(100 -200 180)"},
    {"color:lab(-20 -1.23456789 2 / -1)", "color", "lab(0 -1.234568 2 / 0)"},
    {"color:lab(none 10% none / none)", "color", "lab(none 12.5 none / none)"},
    {"color:lch(37% 105.0 305.00)", "color", "lch(37 105 305)"},
    {"color:lch(50% 100% -90deg / 93%)", "color", "lch(50 150 270 / 0.93)"},
    {"color:lch(50 -1.234567 1.25turn)", "color", "lch(50 0 90)"},
    {"color:lch(none none none)", "color", "lch(none none none)"},
    {"color:lch(60 20 100grad)", "color", "lch(60 20 90)"},
    {"color:oklab(54% -25% -5%)", "color", "oklab(0.54 -0.1 -0.02)"},
    {"color:oklab(200% -1 1 / .5)", "color", "oklab(1 -1 1 / 0.5)"},
    {"color:oklab(-.5 none none)", "color", "oklab(0 none none)"},
    {"color:oklch(56.43% 0.0900 123.40)", "color", "oklch(0.5643 0.09 123.4)"},
    {"color:oklch(.5 100% -450deg / none)", "color", "oklch(0.5 0.4 270 / none)"},
    {"color:oklch(50% -25% none)", "color", "oklch(0.5 0 none)"},
    {"color:oklch(50% 20% 1000000000.25turn)", "color", "oklch(0.5 0.08 90)"},
    {"color:lab(50 .0000004% -.0000004%)", "color", "lab(50 0.000001 0)"},
    {"color:lch(50 .000001% 0)", "color", "lch(50 0.000002 0)"},
    {"color:oklab(50% .000125% -.000125%)", "color", "oklab(0.5 0.000001 0)"},
    {"color:lab(50 12345678901234567890.1234567 0)", "color", "lab(50 12345678901234567890.123457 0)"},
    {"color:lab(50 12345678901234567890.1234567% 0)", "color", "lab(50 15432098626543209862.654321 0)"},
    {"color:lab(50% 0 0)", "color", "lab(50 0 0)"},
    {"color:oklab(50% 0 0)", "color", "oklab(0.5 0 0)"},
    {"color:lch(50% 10 0)", "color", "lch(50 10 0)"},
    {"color:oklch(50% 0.1 0)", "color", "oklch(0.5 0.1 0)"},
    {"color:red; color:color(display-p3 0 0 1)", "color", "red"},
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
    {"columns:calc(12px + 2px) 2", "columns", "calc(14px) 2"},

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
    {"border:calc(1px + 2px) solid red", "border", "calc(3px) solid red"},
    {"border:calc(1px + 2px) solid red", "border-left-color", "red"},
    {"border:solid oklch(50% .1 0)", "border-left-width", "medium"},
    {"border:solid oklch(50% .1 0)", "border-left-color", "oklch(0.5 0.1 0)"},
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
    {"border-top:calc(1px + 2px) solid red", "border-top", "calc(3px) solid red"},
    {"border-top:calc(1px + 2px) solid red", "border-top-color", "red"},
    {"border-top:solid oklch(50% .1 0)", "border-top-width", "medium"},
    {"border-top:solid oklch(50% .1 0)", "border-top-color", "oklch(0.5 0.1 0)"},
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
    {"border-right:calc(1px + 2px) solid red", "border-right", "calc(3px) solid red"},
    {"border-right:calc(1px + 2px) solid red", "border-right-color", "red"},
    {"border-right:solid oklch(50% .1 0)", "border-right-width", "medium"},
    {"border-right:solid oklch(50% .1 0)", "border-right-color", "oklch(0.5 0.1 0)"},
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
    {"border-bottom:calc(1px + 2px) solid red", "border-bottom", "calc(3px) solid red"},
    {"border-bottom:calc(1px + 2px) solid red", "border-bottom-color", "red"},
    {"border-bottom:solid oklch(50% .1 0)", "border-bottom-width", "medium"},
    {"border-bottom:solid oklch(50% .1 0)", "border-bottom-color", "oklch(0.5 0.1 0)"},
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
    {"border-left:calc(1px + 2px) solid red", "border-left", "calc(3px) solid red"},
    {"border-left:calc(1px + 2px) solid red", "border-left-color", "red"},
    {"border-left:solid oklch(50% .1 0)", "border-left-width", "medium"},
    {"border-left:solid oklch(50% .1 0)", "border-left-color", "oklch(0.5 0.1 0)"},
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
    {"outline:calc(1px + 2px) solid red", "outline", "red solid calc(3px)"},
    {"outline:calc(1px + 2px) solid red", "outline-color", "red"},
    {"outline:solid oklch(50% .1 0)", "outline-width", "medium"},
    {"outline:solid oklch(50% .1 0)", "outline-color", "oklch(0.5 0.1 0)"},
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
    {"column-rule:calc(1px + 2px) solid red", "column-rule", "calc(3px) solid red"},
    {"column-rule:calc(1px + 2px) solid red", "column-rule-color", "red"},
    {"column-rule:solid oklch(50% .1 0)", "column-rule-width", "medium"},
    {"column-rule:solid oklch(50% .1 0)", "column-rule-color", "oklch(0.5 0.1 0)"},
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
    /* CSS Values 4 position serialization retains keywords and both axes. */
    {"background-position:CENTER", "background-position", "center center"},
    {"background-position:left", "background-position", "left center"},
    {"background-position:RIGHT", "background-position", "right center"},
    {"background-position:top", "background-position", "center top"},
    {"background-position:bottom", "background-position", "center bottom"},
    {"background-position:top left", "background-position", "left top"},
    {"background-position:bottom right", "background-position", "right bottom"},
    {"background-position:center left", "background-position", "left center"},
    {"background-position:center right", "background-position", "right center"},
    {"background-position:top center", "background-position", "center top"},
    {"background-position:bottom center", "background-position", "center bottom"},
    {"background-position:center top", "background-position", "center top"},
    {"background-position:left bottom", "background-position", "left bottom"},
    {"background-position:right top", "background-position", "right top"},
    {"background-position:0", "background-position", "0px center"},
    {"background-position:-0 .125%", "background-position", "0px 0.125%"},
    {"background-position:.1234567EM", "background-position", "0.123457em center"},
    {"background-position:-0002.50px +075.00%", "background-position", "-2.5px 75%"},
    {"background-position:CENTER .1234567PX", "background-position", "center 0.123457px"},
    {"background-position:12px bottom", "background-position", "12px bottom"},
    {"background-position:right -2.5em", "background-position", "right -2.5em"},
    {"background-position:\\74 op \\6c eft", "background-position", "left top"},
    {"background-position:inherit", "background-position", "inherit"},
    {"background-position:initial", "background-position", "initial"},
    {"background-position:unset", "background-position", "unset"},
    {"background-position:revert", "background-position", "revert"},
    {"background-position:var(--Pos, top left)", "background-position", "var(--Pos, top left)"},
    {"background-position:left!important;background-position:right", "background-position", "left center"},
    {"background-position:left;background-position:right!important", "background-position", "right center"},
    {"background-position:left;background-position:1fr", "background-position", "left center"},
    {"background-position:left;background-position:1dppx", "background-position", "left center"},
    {"object-position:CENTER", "object-position", "center center"},
    {"object-position:left", "object-position", "left center"},
    {"object-position:RIGHT", "object-position", "right center"},
    {"object-position:top", "object-position", "center top"},
    {"object-position:bottom", "object-position", "center bottom"},
    {"object-position:top left", "object-position", "left top"},
    {"object-position:bottom right", "object-position", "right bottom"},
    {"object-position:center left", "object-position", "left center"},
    {"object-position:center right", "object-position", "right center"},
    {"object-position:top center", "object-position", "center top"},
    {"object-position:bottom center", "object-position", "center bottom"},
    {"object-position:center top", "object-position", "center top"},
    {"object-position:left bottom", "object-position", "left bottom"},
    {"object-position:right top", "object-position", "right top"},
    {"object-position:0", "object-position", "0px center"},
    {"object-position:-0 .125%", "object-position", "0px 0.125%"},
    {"object-position:.1234567EM", "object-position", "0.123457em center"},
    {"object-position:-0002.50px +075.00%", "object-position", "-2.5px 75%"},
    {"object-position:CENTER .1234567PX", "object-position", "center 0.123457px"},
    {"object-position:12px bottom", "object-position", "12px bottom"},
    {"object-position:right -2.5em", "object-position", "right -2.5em"},
    {"object-position:\\74 op \\6c eft", "object-position", "left top"},
    {"object-position:inherit", "object-position", "inherit"},
    {"object-position:initial", "object-position", "initial"},
    {"object-position:unset", "object-position", "unset"},
    {"object-position:revert", "object-position", "revert"},
    {"object-position:var(--Pos, top left)", "object-position", "var(--Pos, top left)"},
    {"object-position:left!important;object-position:right", "object-position", "left center"},
    {"object-position:left;object-position:right!important", "object-position", "right center"},
    {"object-position:left;object-position:1fr", "object-position", "left center"},
    {"object-position:left;object-position:1dppx", "object-position", "left center"},
    {"background-position:TOP LEFT, 0, center -2.5EM", "background-position", "left top, 0px center, center -2.5em"},
    {"background-position:top,top,top", "background-position", "center top, center top, center top"},
    {"background-position:var(--Pos, top, bottom)", "background-position", "var(--Pos, top, bottom)"},
    {"object-position:top;object-position:top,bottom", "object-position", "center top"},

    {"background-position:top left,2px,center -3em", "background-position-x", "left, 2px, center"},
    {"background-position:top left,2px,center -3em", "background-position-y", "top, center, -3em"},
    {"background:top url(a),url(b) blue", "background-position-x", "center, 0%"},
    {"background:top url(a),url(b) blue", "background-position-y", "top, 0%"},
    {"background-position-x:RIGHT, -.1234567EM, 150%", "background-position-x", "right, -0.123457em, 150%"},
    {"background-position-y:TOP, +025.50%, -0", "background-position-y", "top, 25.5%, 0px"},
    {"background-position:10px 20px,30px 40px;background-position-x:5px", "background-position", ""},
    {"background-position:10px 20px,30px 40px;background-position-x:5px", "background-position-y", "20px, 40px"},
    {"background-position-x:left,right;background-position-y:top,bottom", "background-position", "left top, right bottom"},
    {"background-position-x:inherit;background-position-y:inherit", "background-position", "inherit"},
    {"background-position-x:initial;background-position-y:inherit", "background-position", ""},
    {"background-position-x:left!important;background-position-y:top", "background-position", ""},
    {"background-position:var(--P)", "background-position-x", ""},
    {"background-position:var(--P);background-position-x:left", "background-position", ""},
    {"background-position-x:var(--P)", "background-position-x", "var(--P)"},
    {"white-space:NORMAL", "white-space-collapse", "collapse"},
    {"white-space:PRE", "text-wrap-mode", "nowrap"},
    {"white-space:pre-wrap", "white-space-collapse", "preserve"},
    {"white-space:PRE-LINE", "white-space-collapse", "preserve-breaks"},
    {"white-space:pre-wrap;text-wrap-mode:nowrap", "white-space", "pre"},
    {"white-space:pre;white-space-collapse:collapse", "white-space", "nowrap"},
    {"white-space:nowrap PRESERVE", "white-space", "pre"},
    {"white-space:wrap collapse", "white-space", "normal"},
    {"white-space:preserve-breaks nowrap", "white-space", "preserve-breaks nowrap"},
    {"white-space:nowrap preserve-breaks", "white-space", "preserve-breaks nowrap"},
    {"white-space:wrap", "white-space", "normal"},
    {"white-space:preserve", "white-space", "pre-wrap"},
    {"white-space:preserve-breaks", "white-space", "pre-line"},
    {"white-space:var(--WS)", "text-wrap-mode", ""},
    {"white-space:var(--WS);text-wrap-mode:nowrap", "white-space", ""},
    {"text-wrap-mode:var(--W)", "text-wrap-mode", "var(--W)"},
    {"white-space:pre!important;text-wrap-mode:wrap", "white-space", "pre"},
    {"white-space-collapse:preserve!important;text-wrap-mode:wrap", "white-space", ""},
    {"white-space-collapse:inherit;text-wrap-mode:inherit", "white-space", "inherit"},
    {"white-space-collapse:initial;text-wrap-mode:inherit", "white-space", ""},
    {"background-repeat:repeat-x,repeat-y", "background-repeat-x", "repeat, no-repeat"},
    {"background-repeat:repeat-x,repeat-y", "background-repeat-y", "no-repeat, repeat"},
    {"background:repeat-y,repeat-x red", "background-repeat-x", "no-repeat, repeat"},
    {"background:repeat-y,repeat-x red", "background-repeat-y", "repeat, no-repeat"},
    {"background-repeat:repeat-x,repeat-y;background-repeat-x:no-repeat", "background-repeat", ""},
    {"background-repeat:repeat-x,repeat-y;background-repeat-x:no-repeat", "background-repeat-y", "no-repeat, repeat"},
    {"background-repeat-x:repeat,repeat;background-repeat-y:no-repeat,repeat", "background-repeat", "repeat-x, repeat"},
    {"background-repeat-x:inherit;background-repeat-y:inherit", "background-repeat", "inherit"},
    {"background-repeat-x:inherit;background-repeat-y:initial", "background-repeat", ""},
    {"background-repeat-x:repeat!important;background-repeat-y:no-repeat", "background-repeat", ""},
    {"background-repeat:var(--R)", "background-repeat-x", ""},
    {"background-repeat:var(--R);background-repeat-x:no-repeat", "background-repeat", ""},
    {"background-repeat-x:VAR(--R)", "background-repeat-x", "VAR(--R)"},
    {"background-repeat-x:repeat,NO-REPEAT, repeat", "background-repeat-x", "repeat, no-repeat, repeat"},
    {"background-repeat:REPEAT", "background-repeat", "repeat"},
    {"background-repeat:NO-REPEAT", "background-repeat", "no-repeat"},
    {"background-repeat:REPEAT-X", "background-repeat", "repeat-x"},
    {"background-repeat:REPEAT-Y", "background-repeat", "repeat-y"},
    {"background-repeat:repeat repeat", "background-repeat", "repeat"},
    {"background-repeat:no-repeat no-repeat", "background-repeat", "no-repeat"},
    {"background-repeat:repeat no-repeat", "background-repeat", "repeat-x"},
    {"background-repeat:no-repeat repeat", "background-repeat", "repeat-y"},
    {"background-repeat:REPEAT/**/NO-REPEAT, no-repeat REPEAT, repeat repeat", "background-repeat", "repeat-x, repeat-y, repeat"},
    {"background-repeat:repeat,repeat,repeat", "background-repeat", "repeat, repeat, repeat"},
    {"background-repeat:r\\65 peat n\\6f-repeat", "background-repeat", "repeat-x"},
    {"background-repeat:repeat no-repeat!important;background-repeat:repeat", "background-repeat", "repeat-x"},
    {"background-repeat:no-repeat repeat;background-repeat:repeat", "background-repeat", "repeat"},
    {"background-repeat:repeat; background-repeat:repeat-x repeat-y", "background-repeat", "repeat"},
    {"background-repeat:repeat; background-repeat:repeat no-repeat repeat", "background-repeat", "repeat"},
    {"background-repeat:inherit", "background-repeat", "inherit"},
    {"background-repeat:INITIAL", "background-repeat", "initial"},
    {"background-repeat:unset", "background-repeat", "unset"},
    {"background-repeat:revert", "background-repeat", "revert"},
    {"background-repeat:var(--Repeat, repeat no-repeat)", "background-repeat", "var(--Repeat, repeat no-repeat)"},
    {"text-decoration:NONE", "text-decoration", "none"},
    {"text-decoration:UNDERLINE", "text-decoration", "underline"},
    {"text-decoration:OVERLINE", "text-decoration", "overline"},
    {"text-decoration:LINE-THROUGH", "text-decoration", "line-through"},
    {"text-decoration:BLINK", "text-decoration", "blink"},
    {"text-decoration:blink line-through overline underline", "text-decoration", "underline overline line-through blink"},
    {"text-decoration:line-through/**/underline", "text-decoration", "underline line-through"},
    {"text-decoration:u\\6e derline", "text-decoration", "underline"},
    {"text-decoration:underline!important;text-decoration:none", "text-decoration", "underline"},
    {"text-decoration:underline;text-decoration:none", "text-decoration", "none"},
    {"text-decoration:underline;text-decoration:underline underline", "text-decoration", "underline"},
    {"text-decoration:underline;text-decoration:blink none", "text-decoration", "underline"},
    {"text-decoration:INHERIT", "text-decoration", "inherit"},
    {"text-decoration:underline;text-decoration:INITIAL", "text-decoration", "initial"},
    {"text-decoration:underline;text-decoration:UNSET", "text-decoration", "unset"},
    {"text-decoration:underline;text-decoration:REVERT", "text-decoration", "revert"},
    {"text-decoration:var(--Lines, overline underline)", "text-decoration", "var(--Lines, overline underline)"},
    {"font-variant:NORMAL", "font-variant", "normal"},
    {"font-variant:SMALL-CAPS", "font-variant", "small-caps"},
    {"font-variant:small-caps!important;font-variant:normal", "font-variant", "small-caps"},
    {"font-variant:small-caps;font-variant:initial", "font-variant", "initial"},
    {"font-variant:var(--Caps, small-caps)", "font-variant", "var(--Caps, small-caps)"},
    {"font-variant:SMALL-CAPS", "font-variant-caps", "small-caps"},
    {"font-variant-caps:SMALL-CAPS", "font-variant", "small-caps"},
    {"font-variant:var(--Caps, small-caps)", "font-variant-caps", ""},
    {"font-variant-caps:var(--Caps, small-caps)", "font-variant", ""},
    {"font-variant-caps:var(--Caps, small-caps)", "font-variant-caps", "var(--Caps, small-caps)"},
    {"font-variant:small-caps;font-variant-caps:normal", "font-variant", "normal"},
    {"font-variant-caps:small-caps!important;font-variant:normal", "font-variant", "small-caps"},
    {"font:italic small-caps 16px serif", "font-variant-caps", "small-caps"},
    {"font:italic small-caps 16px serif;font-variant-caps:normal", "font", "italic 16px serif"},
    {"font-variant-caps:small-caps;font:16px serif", "font-variant-caps", "normal"},
    {"font:var(--Font)", "font-variant-caps", ""},
    {"font-variant-caps:initial", "font-variant", "initial"},
    {"font-variant:inherit", "font-variant-caps", "inherit"},
    {"text-decoration:BLINK OVERLINE UNDERLINE", "text-decoration-line", "underline overline blink"},
    {"text-decoration-line:LINE-THROUGH UNDERLINE", "text-decoration", "underline line-through"},
    {"text-decoration:var(--Lines)", "text-decoration-line", ""},
    {"text-decoration-line:var(--Lines)", "text-decoration", ""},
    {"text-decoration-line:var(--Lines)", "text-decoration-line", "var(--Lines)"},
    {"text-decoration-line:overline!important;text-decoration:underline", "text-decoration", "overline"},
    {"text-decoration:underline;text-decoration-line:none", "text-decoration", "none"},
    {"text-decoration-line:unset", "text-decoration", "unset"},
    {"text-decoration:revert", "text-decoration-line", "revert"},
    {"white-space:NORMAL", "white-space", "normal"},
    {"white-space:PRE", "white-space", "pre"},
    {"white-space:NOWRAP", "white-space", "nowrap"},
    {"white-space:PRE-WRAP", "white-space", "pre-wrap"},
    {"white-space:PRE-LINE", "white-space", "pre-line"},
    {"white-space:pre!important;white-space:nowrap", "white-space", "pre"},
    {"white-space:pre;white-space:unset", "white-space", "unset"},
    {"white-space:var(--Space, pre)", "white-space", "var(--Space, pre)"},
    {"vertical-align:BASELINE", "vertical-align", "baseline"},
    {"vertical-align:SUB", "vertical-align", "sub"},
    {"vertical-align:SUPER", "vertical-align", "super"},
    {"vertical-align:TOP", "vertical-align", "top"},
    {"vertical-align:TEXT-TOP", "vertical-align", "text-top"},
    {"vertical-align:MIDDLE", "vertical-align", "middle"},
    {"vertical-align:BOTTOM", "vertical-align", "bottom"},
    {"vertical-align:TEXT-BOTTOM", "vertical-align", "text-bottom"},
    {"vertical-align:-0", "vertical-align", "0px"},
    {"vertical-align:.1234567PX", "vertical-align", "0.123457px"},
    {"vertical-align:-.2345678EM", "vertical-align", "-0.234568em"},
    {"vertical-align:+00012.3456789%", "vertical-align", "12.345679%"},
    {"vertical-align:5000000px", "vertical-align", "5000000px"},
    {"vertical-align:2px!important;vertical-align:0", "vertical-align", "2px"},
    {"vertical-align:2px;vertical-align:revert", "vertical-align", "revert"},
    {"vertical-align:var(--Offset, 2px)", "vertical-align", "var(--Offset, 2px)"},
    {"vertical-align:calc(2px + 3px)", "vertical-align", "calc(5px)"},
    {"page-break-before:ALWAYS", "page-break-before", "always"},
    {"page-break-before:LEFT", "page-break-before", "left"},
    {"page-break-before:RIGHT", "page-break-before", "right"},
    {"page-break-after:AUTO", "page-break-after", "auto"},
    {"page-break-after:AVOID", "page-break-after", "avoid"},
    {"page-break-inside:AVOID", "page-break-inside", "avoid"},
    {"page-break-inside:AUTO", "page-break-inside", "auto"},
    {"page-break-after:initial", "page-break-after", "initial"},

    /* CSS Cascade name aliases normalize before ordering and readback. */
    {"grid-gap:1px 2px", "gap", "1px 2px"},
    {"gap:1px 2px", "grid-gap", "1px 2px"},
    {"GRID-GAP:1px", "grid-gap", "1px"},
    {"grid-gap:1px 2px", "row-gap", "1px"},
    {"grid-gap:1px 2px", "grid-row-gap", "1px"},
    {"grid-gap:1px 2px", "grid-column-gap", "2px"},
    {"grid-row-gap:3px;grid-column-gap:4px", "gap", "3px 4px"},
    {"grid-column-gap:4px;row-gap:3px", "grid-gap", "3px 4px"},
    {"grid-gap:1px 2px;gap:3px", "grid-gap", "3px"},
    {"gap:3px;grid-gap:1px 2px", "gap", "1px 2px"},
    {"grid-gap:1px 2px;row-gap:3px", "grid-gap", "3px 2px"},
    {"gap:1px 2px;grid-column-gap:4px", "grid-gap", "1px 4px"},
    {"grid-gap:1px 2px!important;gap:3px", "grid-gap", "1px 2px"},
    {"gap:1px 2px!important;grid-gap:3px!important", "gap", "3px"},
    {"gap:1px 2px;grid-column-gap:3px!important", "grid-gap", ""},
    {"grid-gap:INHERIT", "gap", "inherit"},
    {"grid-gap:INITIAL", "grid-row-gap", "initial"},
    {"grid-row-gap:UNSET", "row-gap", "unset"},
    {"grid-column-gap:REVERT", "column-gap", "revert"},
    {"grid-gap:var(--Gap, 1px 2px)", "gap", "var(--Gap, 1px 2px)"},
    {"gap:var(--Gap, 1px 2px)", "grid-gap", "var(--Gap, 1px 2px)"},
    {"grid-gap:var(--Gap)", "grid-column-gap", ""},
    {"grid-gap:var(--Gap);row-gap:2px", "grid-gap", ""},
    {"grid-row-gap:var(--Row, 2px)", "row-gap", "var(--Row, 2px)"},
    {"row-gap:var(--Row, 2px)", "grid-row-gap", "var(--Row, 2px)"},
    {"\\67 rid-gap:1px", "gap", "1px"},
    {"grid-gap:3px;grid-gap:1px garbage", "grid-gap", "3px"},
    {"grid-gap:1px;--grid-gap:Authored;--Grid-gap:Other", "--grid-gap", "Authored"},
    {"grid-gap:1px;--grid-gap:Authored;--Grid-gap:Other", "--Grid-gap", "Other"},

    /* Font readback retains specified precision, units and family token boundaries. */
    {"font:16px serif", "font", "16px serif"},
    {"font:16px serif", "font-style", "normal"},
    {"font:16px serif", "font-variant", "normal"},
    {"font:16px serif", "font-weight", "normal"},
    {"font:16px serif", "font-size", "16px"},
    {"font:16px serif", "line-height", "normal"},
    {"font:16px serif", "font-family", "serif"},
    {"font:normal italic 16px serif", "font", "italic 16px serif"},
    {"font:normal normal italic 16px serif", "font-style", "italic"},
    {"font:normal bold normal 16px serif", "font", "bold 16px serif"},
    {"font:normal normal normal 16px/normal serif", "font", "16px serif"},
    {"font:700 small-caps oblique 1.2345678EM / 120% One/**/Two,SERIF", "font",
        "oblique small-caps 700 1.234568em / 120% \"One Two\", serif"},
    {"font:ITALIC SMALL-CAPS BOLDER MEDIUM/1.2345678 SANS-SERIF", "font",
        "italic small-caps bolder medium / 1.234568 sans-serif"},
    {"font:16px/120% serif", "line-height", "120%"},
    {"font:16px/1.2345678em serif", "line-height", "1.234568em"},
    {"font:italic 1.2345678rem/normal serif", "font-size", "1.234568rem"},
    {"font:SMALLER/0 serif", "font", "smaller / 0 serif"},
    {"font:0 serif", "font", "0px serif"},
    {"font:0/0px serif", "font", "0px / 0px serif"},
    {"font:+000400 16px serif", "font", "400 16px serif"},
    {"font:italic 16px 'initial', One\\20 Two,SERIF", "font-family", "\"initial\", \"One Two\", serif"},
    {"font:italic 16px 'initial', One\\20 Two,SERIF", "font",
        "italic 16px \"initial\", \"One Two\", serif"},
    {"font:16px 'A,B/C', normal, bold", "font", "16px \"A,B/C\", \"normal\", \"bold\""},
    {"font:16px serif; font-weight:bold", "font", "bold 16px serif"},
    {"font:italic 16px serif; font-style:normal", "font", "16px serif"},
    {"font:16px serif; font-size:2em;line-height:1.5", "font", "2em / 1.5 serif"},
    {"font-style:italic;font-variant:small-caps;font-weight:700;font-size:18px;"
        "line-height:24px;font-family:One Two,serif", "font", "italic small-caps 700 18px / 24px \"One Two\", serif"},
    {"font-size:16px;font-family:serif", "font", ""},
    {"font:16px serif!important;font-weight:bold", "font", "16px serif"},
    {"font:16px serif;font-weight:bold!important", "font", ""},
    {"font:INHERIT", "font", "inherit"},
    {"font:INITIAL", "font-size", "initial"},
    {"font:UNSET", "font", "unset"},
    {"font:REVERT", "font", "revert"},
    {"font:initial;line-height:inherit", "font", ""},
    {"font:var(--Font, 16px serif)", "font", "var(--Font, 16px serif)"},
    {"font:var(--Font)", "font-size", ""},
    {"font:var(--Font);font-size:16px", "font", ""},
    {"font:16px serif; font-weight:var(--Weight)", "font", ""},
    {"font:italic calc(1em + 2px)/calc(1 + .25) serif", "font", "italic calc(1em + 2px) / calc(1.25) serif"},
    {"font:italic calc(1em + 2px)/calc(1 + .25) serif", "font-size", "calc(1em + 2px)"},
    {"font:italic calc(1em + 2px)/calc(1 + .25) serif", "line-height", "calc(1.25)"},
    {"font:italic calc(1em + 2px)/calc(1 + .25) serif", "font-style", "italic"},
    {"font:italic calc(1em + 2px)/calc(1 + .25) serif", "font-family", "serif"},

    /* CSS Align 3 section 5.2: axis order and omitted second value. */
    {"place-content:CENTER", "place-content", "center"},
    {"place-content:center", "align-content", "center"},
    {"place-content:center", "justify-content", "center"},
    {"place-content:CENTER center", "place-content", "center"},
    {"place-content:NORMAL", "place-content", "normal"},
    {"place-content:normal", "align-content", "normal"},
    {"place-content:normal", "justify-content", "normal"},
    {"place-content:stretch", "place-content", "stretch"},
    {"place-content:flex-start flex-end", "place-content", "flex-start flex-end"},
    {"place-content:space-around space-between", "align-content", "space-around"},
    {"place-content:space-around space-between", "justify-content", "space-between"},
    {"place-content:SPACE-EVENLY", "place-content", "space-evenly"},
    {"place-content:\\63 enter", "place-content", "center"},
    {"place-content:start center", "place-content", "start center"},
    {"place-content:end flex-start", "place-content", "end flex-start"},
    {"place-content:center;justify-content:flex-end", "place-content", "center flex-end"},
    {"place-content:center;align-content:flex-end", "place-content", "flex-end center"},
    {"align-content:center;justify-content:center", "place-content", "center"},
    {"align-content:stretch", "place-content", ""},
    {"place-content:center!important;justify-content:flex-end", "place-content", "center"},
    {"place-content:center;justify-content:flex-end!important", "place-content", ""},
    {"place-content:inherit", "place-content", "inherit"},
    {"place-content:initial", "justify-content", "initial"},
    {"place-content:unset", "align-content", "unset"},
    {"place-content:revert", "place-content", "revert"},
    {"place-content:initial;justify-content:inherit", "place-content", ""},
    {"place-content:var(--Align, center flex-end)", "place-content", "var(--Align, center flex-end)"},
    {"place-content:var(--Align)", "align-content", ""},
    {"place-content:var(--Align);justify-content:center", "place-content", ""},
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
         * more precise or larger than the renderer's fixed-point storage. */
        const char *name = value_cases[_i].property;
        if (value != NULL && lwc_string_length(value) != 0) {
            ck_assert(strncmp(name, "--", 2) == 0 || css_prop_lookup(name, strlen(name)) != NULL);
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
        css_declaration_store *store;
        css_stylesheet *compiled = NULL;
        ck_assert_int_eq(css__declaration_store_create(list, &store), CSS_OK);
        error = css__declaration_store_compile(store, &params, &compiled);
        ck_assert_msg(error == CSS_OK, "compile case %d chunks %u: %s: error %d", _i, chunks,
            value_cases[_i].css, error);
        css__declaration_store_destroy(store);
        css_stylesheet_destroy(compiled);
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
/* CSS2 Fonts §15.8: normal can fill any omitted prefix component. Compare
 * native output with explicit longhands, independently of CSSOM readback. */
START_TEST(test_font_prefix_bytecode)
{
    static const char *const styles[] = {"normal", "italic", "oblique"};
    static const char *const variants[] = {"normal", "small-caps"};
    static const char *const weights[] = {"normal", "bold", "700", "lighter"};
    static const unsigned permutations[][3] = {{0, 1, 2}, {0, 2, 1}, {1, 0, 2}, {1, 2, 0}, {2, 0, 1}, {2, 1, 0}};
    for (size_t st = 0; st < N_ELEMENTS(styles); st++) {
        for (size_t va = 0; va < N_ELEMENTS(variants); va++) {
            for (size_t we = 0; we < N_ELEMENTS(weights); we++) {
                const char *parts[] = {styles[st], variants[va], weights[we]};
                char longhands[256];
                snprintf(longhands, sizeof(longhands),
                    "font-style:%s;font-variant:%s;font-weight:%s;font-size:16px;line-height:1.25;"
                    "font-family:'Example',serif", parts[0], parts[1], parts[2]);
                css_declaration_list *reference = parse(longhands, _i != 0);
                for (size_t p = 0; p < N_ELEMENTS(permutations); p++) {
                    char input[256];
                    snprintf(input, sizeof(input), "font:%s %s %s 16px/1.25 'Example',serif",
                        parts[permutations[p][0]], parts[permutations[p][1]], parts[permutations[p][2]]);
                    css_declaration_list *list = parse(input, _i != 0);
                    ck_assert_msg(css__declaration_list_count(list) == 1, "rejected %s", input);
                    compare_sheets(css__declaration_list_sheet(list), css__declaration_list_sheet(reference));
                    css_declaration_order *order;
                    ck_assert_int_eq(css__declaration_order_create(list, &order), CSS_OK);
                    lwc_string *value;
                    ck_assert_int_eq(serialize_property(order, "font", &value), CSS_OK);
                    snprintf(input, sizeof(input), "font:%s", lwc_string_data(value));
                    css_declaration_list *again = parse(input, _i != 0);
                    compare_sheets(css__declaration_list_sheet(again), css__declaration_list_sheet(reference));
                    css__declaration_list_destroy(again);
                    lwc_string_unref(value);
                    css__declaration_order_destroy(order);
                    css__declaration_list_destroy(list);
                }
                css__declaration_list_destroy(reference);
            }
        }
    }
}
END_TEST

START_TEST(test_font_invalid_components)
{
    const char *baseline = "font:italic small-caps bold 16px/1.25 serif;";
    css_declaration_list *base = parse(baseline, false);
    static const char *const wide[] = {"inherit", "initial", "unset", "revert"};
    static const char *const templates[] = {"%s 18px serif", "italic %s 18px serif",
        "italic small-caps %s 18px serif", "italic small-caps bold %s serif",
        "18px/%s serif", "18px %s", "18px serif, %s"};
    for (size_t i = 0; i < N_ELEMENTS(wide); i++) {
        for (size_t j = 0; j < N_ELEMENTS(templates); j++) {
            char value[128], input[256];
            snprintf(value, sizeof(value), templates[j], wide[i]);
            snprintf(input, sizeof(input), "%sfont:%s", baseline, value);
            css_declaration_list *list = parse(input, _i != 0);
            ck_assert_msg(css__declaration_list_count(list) == 1, "accepted embedded CSS-wide keyword: %s", value);
            compare_sheets(css__declaration_list_sheet(base), css__declaration_list_sheet(list));
            css__declaration_list_destroy(list);
        }
    }
    static const char *const invalid[] = {"italic oblique 18px serif", "bold 700 18px serif",
        "small-caps small-caps 18px serif", "italic 18px", "18px/ serif", "18px/2", "18px serif/2"};
    for (size_t i = 0; i < N_ELEMENTS(invalid); i++) {
        char input[256];
        snprintf(input, sizeof(input), "%sfont:%s", baseline, invalid[i]);
        css_declaration_list *list = parse(input, _i != 0);
        ck_assert_msg(css__declaration_list_count(list) == 1, "accepted %s", invalid[i]);
        compare_sheets(css__declaration_list_sheet(base), css__declaration_list_sheet(list));
        css__declaration_list_destroy(list);
    }
    css__declaration_list_destroy(base);
}
END_TEST

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

/* Every missing-component combination must survive CSSOM without turning none
 * into zero, inventing a missing component, or changing direct-rendering bytes. */
START_TEST(test_missing_colour_roundtrip)
{
    static const struct {
        const char *input, *output, *authored[4], *normalized[4];
    } forms[] = {
        {"HSLA(", "hsl(", {"-240deg", "025", "050%", "050%"}, {"120", "25%", "50%", "0.5"}},
        {"HWB(", "hwb(", {"-240deg", "025", "050%", "050%"}, {"120", "25%", "50%", "0.5"}},
        {"RGBA(", "color(srgb ", {"255", "050%", "0", "050%"}, {"1", "0.5", "0", "0.5"}},
        {"COLOR(SRGB ", "color(srgb ", {"100%", ".500", "0%", "050%"}, {"1", "0.5", "0", "0.5"}},
    };
    for (size_t f = 0; f < N_ELEMENTS(forms); f++) {
        for (unsigned mask = 1; mask < 16; mask++) {
            char input[160], expected[160];
            const char *parts[4], *values[4];
            for (unsigned i = 0; i < 4; i++) {
                parts[i] = mask & (1u << i) ? "none" : forms[f].authored[i];
                values[i] = mask & (1u << i) ? "none" : forms[f].normalized[i];
            }
            snprintf(input, sizeof(input), "color:%s%s %s %s / %s)", forms[f].input,
                parts[0], parts[1], parts[2], parts[3]);
            snprintf(expected, sizeof(expected), "%s%s %s %s / %s)", forms[f].output,
                values[0], values[1], values[2], values[3]);
            css_declaration_list *list = parse(input, _i != 0);
            ck_assert_uint_eq(css__declaration_list_count(list), 1);
            css_declaration_order *order;
            ck_assert_int_eq(css__declaration_order_create(list, &order), CSS_OK);
            lwc_string *value;
            ck_assert_int_eq(serialize_property(order, "color", &value), CSS_OK);
            ck_assert_msg(strcmp(lwc_string_data(value), expected) == 0,
                "%s expected %s, got %s", input, expected, lwc_string_data(value));
            snprintf(input, sizeof(input), "color:%s", lwc_string_data(value));
            css_declaration_list *again = parse(input, _i != 0);
            compare_sheets(css__declaration_list_sheet(list), css__declaration_list_sheet(again));
            lwc_string_unref(value);
            css__declaration_order_destroy(order);
            css__declaration_list_destroy(list);
            css__declaration_list_destroy(again);
        }
    }
}
END_TEST

/* Extremely large accepted coordinates must not overflow the bounded legacy
 * serializer, including infinite intermediates from finite HSL inputs. */
START_TEST(test_extreme_polar_readback)
{
    char huge[351];
    memset(huge, '9', sizeof(huge) - 1);
    huge[sizeof(huge) - 1] = 0;
    static const struct { const char *format, *expected; } cases[] = {
        {"color:hsl(30 %s %s)", "rgb(0, 255, 255)"},
        {"color:hsl(30 %s -%s)", "rgb(0, 0, 255)"},
        {"color:hsl(30 -%s %s)", "rgb(255, 255, 255)"},
        {"color:hwb(0 %s %s)", "rgb(127.5, 127.5, 127.5)"},
        {"color:hwb(0 -%s -%s)", "rgb(255, 0, 0)"},
    };
    for (size_t i = 0; i < N_ELEMENTS(cases); i++) {
        char css[768];
        snprintf(css, sizeof(css), cases[i].format, huge, huge);
        css_declaration_list *list = parse(css, _i != 0);
        css_declaration_order *order;
        ck_assert_int_eq(css__declaration_order_create(list, &order), CSS_OK);
        lwc_string *value;
        css_error error = serialize_property(order, "color", &value);
        ck_assert_msg(error == CSS_OK, "extreme color %zu chunks %d: %d", i, _i, error);
        ck_assert_msg(strcmp(lwc_string_data(value), cases[i].expected) == 0,
            "extreme color %zu: expected %s, got %s", i, cases[i].expected, lwc_string_data(value));
        snprintf(css, sizeof(css), "color:%s", lwc_string_data(value));
        css_declaration_list *again = parse(css, _i != 0);
        compare_sheets(css__declaration_list_sheet(list), css__declaration_list_sheet(again));
        lwc_string_unref(value);
        css__declaration_order_destroy(order);
        css__declaration_list_destroy(again);
        css__declaration_list_destroy(list);
    }
}
END_TEST

/* CSS Color 4 requires normalized sRGB readback to retain at least eight-bit
 * channel precision. Exercise every byte through RGB -> color(srgb) -> paint. */
START_TEST(test_missing_rgb_precision)
{
    for (unsigned channel = 0; channel < 256; channel++) {
        char css[160];
        snprintf(css, sizeof(css), "color:rgb(%u none %u)", channel, 255 - channel);
        css_declaration_list *list = parse(css, _i != 0);
        css_declaration_order *order;
        ck_assert_int_eq(css__declaration_order_create(list, &order), CSS_OK);
        lwc_string *value;
        ck_assert_int_eq(serialize_property(order, "color", &value), CSS_OK);
        snprintf(css, sizeof(css), "color:%s", lwc_string_data(value));
        css_declaration_list *again = parse(css, _i != 0);
        ck_assert_msg(css__declaration_list_count(again) == 1, "rejected channel %u: %s", channel, css);
        const css_style *style = inline_style(css__declaration_list_sheet(again));
        uint32_t packed = 0xff000000 | channel << 16 | (255 - channel);
        ck_assert_msg(style->used == 2 && style->bytecode[1] == packed,
            "channel %u changed after %s", channel, css);
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
        {"color(srgb 1 0 0)", 0xffff0000},
        {"color(SRGB 0 100% 0)", 0xff00ff00},
        {"color(srgb 0 0 1)", 0xff0000ff},
        {"color(srgb .5 .25 0 / 50%)", 0x80804000},
        {"color(srgb none .5 none)", 0xff008000},
        {"color(srgb 1 none none / none)", 0x00ff0000},
        {"color(srgb .2 .4 .6 / .1)", 0x1a336699},
        {"color(srgb .2 .4 .6 / 10)", 0xff336699},
        {"color(srgb .2 .4 .6 / -1)", 0x00336699},
        {"lab(50% 0 0)", 0xff777777},
        {"lch(50% 0 270deg)", 0xff777777},
        {"lab(100 0 0)", 0xffffffff},
        {"lab(-50 80 80)", 0xff000000},
        {"lab(150 80 80)", 0xffffffff},
        {"lab(50 0 0 / .5)", 0x80777777},
        {"lab(50 0 0 / .1)", 0x1a777777},
        {"lab(50 none none / none)", 0x00777777},
        {"lab(none 20 20)", 0xff000000},
        {"oklab(50% 0 0)", 0xff636363},
        {"oklch(.5 0 0 / 50%)", 0x80636363},
        {"oklab(80% none none)", 0xffbebebe},
        {"oklch(50% -20% 30)", 0xff636363},
        {"oklab(0 1 1)", 0xff000000},
        {"oklab(10 1 1)", 0xffffffff},
        {"lab(29.567 68.298 -112.0294)", 0xff0000ff},
        {"lab(54.290543 80.80492 69.890988)", 0xffff0000},
        {"oklab(.62795536 .22486306 .12584630)", 0xffff0000},
        {"oklch(62.795536% .25768331 29.233885)", 0xffff0000},
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
 * Compare rendering bytecode directly, including SVG paint properties. */
START_TEST(test_unsupported_colour_fallbacks)
{
    static const struct { const char *fallback; const char *unsupported; } cases[] = {
        {"color:red", "color:color(display-p3 0 0 1)"},
        {"background-color:#123", "background-color:color(display-p3 1 0 0)"},
        {"border-top-color:red", "border-top-color:color(display-p3 0 0 1)"},
        {"border-right-color:red", "border-right-color:color(display-p3 0 0 1)"},
        {"border-bottom-color:red", "border-bottom-color:color(display-p3 0 0 1)"},
        {"border-left-color:red", "border-left-color:color(display-p3 0 0 1)"},
        {"outline-color:red", "outline-color:color(display-p3 0 0 1)"},
        {"column-rule-color:red", "column-rule-color:color(display-p3 0 0 1)"},
        {"fill:red", "fill:color(display-p3 0 0 1)"},
        {"stroke:red", "stroke:color(display-p3 0 0 1)"},
        {"border-color:red green blue", "border-color:red color(display-p3 0 0 1) blue"},
        {"border:4px solid red", "border:8px dashed color(display-p3 0 0 1)"},
        {"border-top:4px solid red", "border-top:8px dashed color(display-p3 0 0 1)"},
        {"outline:4px solid red", "outline:8px dashed color(display-p3 0 0 1)"},
        {"column-rule:4px solid red", "column-rule:8px dashed color(display-p3 0 0 1)"},
        {"background:blue", "background:fixed color(display-p3 0 0 1)"},
        {"background:red", "background:linear-gradient(blue,white) color(display-p3 0 0 1)"},
        {"background-image:linear-gradient(red,blue)",
            "background-image:linear-gradient(color(display-p3 0 0 1),white)"},
        {"background-image:linear-gradient(red,blue)",
            "background-image:linear-gradient(white,color(display-p3 1 0 0))"},
        {"background-image:linear-gradient(red,blue)",
            "background-image:radial-gradient(white,color(display-p3 0 0 1))"},
        {"background-image:linear-gradient(red,blue)",
            "background-image:linear-gradient(white,black),linear-gradient(white,color(display-p3 0 0 1))"},
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
        "color()", "color(1 0 0)", "color(srgb,1,0,0)", "color(srgb 1,0,0)",
        "color(srgb 1 0 0,.5)", "color(srgb 1 0 0 .5)", "color(srgb 1 0 0 /)",
        "color(srgb 1 0 0 / .5 /)", "color(srgb 1 0 0 / .5 extra)", "color(srgb 1px 0 0)",
        "color(srgb 1 0deg 0)", "color(srgb 1 0 0 / 1px)", "color(srgb none none)",
        "color(from red srgb r g b)", "color(srgb calc(1) 0 0)", "color(srgb-linear 1 0 0)",
        "lab(1px 0 0)",
        "lab(50 1px 0)",
        "lab(50 0 10deg)",
        "lab(50,0,0)",
        "lab(50 0 0 .5)",
        "lab(50 0 0 /)",
        "lab(50 0 0 / 1px)",
        "lab(50 0 0 / none extra)",
        "oklab(50% 0 0, .5)",
        "lch(50 20 30px)",
        "lch(50 20 20%)",
        "lch(50px 20 0)",
        "lch(50 20px 0)",
        "lch(50 20 0 / .5,)",
        "oklch(50% 20% 1hz)",
        "oklch(50% 20% 20%)",
        "lch(50 20)",
        "lch(50 20 0 1)",
        "lab(from red l a b)",
        "lab(calc(50) 0 0)",
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
        "color(display-p3 0 0 1)", "color(display-p3 1 0 0)", "color(unknown 1 0 0)", "color(srgb)",
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
        "red unset", "1px revert", "solid red inherit", "solid,red", "solid unknown", "solid color(display-p3 1 0 0)",
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

START_TEST(test_position_bytecode)
{
    const struct {
        const char *value;
        uint16_t code;
        css_fixed offsets[2];
        uint32_t units[2];
    } forms[] = {
        {"center", 0, {0}, {0}}, {"top", BACKGROUND_POSITION_VERT_TOP, {0}, {0}},
        {"bottom right", BACKGROUND_POSITION_HORZ_RIGHT | BACKGROUND_POSITION_VERT_BOTTOM, {0}, {0}},
        {"top left", BACKGROUND_POSITION_HORZ_LEFT | BACKGROUND_POSITION_VERT_TOP, {0}, {0}},
        {"center left", BACKGROUND_POSITION_HORZ_LEFT, {0}, {0}},
        {"17px", BACKGROUND_POSITION_HORZ_SET, {INTTOFIX(17), 0}, {UNIT_PX, 0}},
        {"center 19%", BACKGROUND_POSITION_VERT_SET, {0, INTTOFIX(19)}, {0, UNIT_PCT}},
        {"-2.5px 75%", BACKGROUND_POSITION_HORZ_SET | BACKGROUND_POSITION_VERT_SET,
            {-2560, INTTOFIX(75)}, {UNIT_PX, UNIT_PCT}},
        {"32768px 32768%", BACKGROUND_POSITION_HORZ_SET | BACKGROUND_POSITION_VERT_SET,
            {INTTOFIX(32768), INTTOFIX(32768)}, {UNIT_PX, UNIT_PCT}},
    };
    static const char *const properties[] = {"background-position", "object-position"};
    for (size_t p = 0; p < N_ELEMENTS(properties); p++) {
        for (size_t f = 0; f < N_ELEMENTS(forms); f++) {
            for (unsigned important = 0; important < 2; important++) {
                char css[128];
                snprintf(css, sizeof(css), "%s:%s%s", properties[p], forms[f].value, important ? "!important" : "");
                css_declaration_list *list = parse(css, _i != 0);
                ck_assert_msg(css__declaration_list_count(list) == 1, "rejected %s", css);
                const css_style *style = inline_style(css__declaration_list_sheet(list));
                size_t pos = 0;
                if (p == 1) {
                    ck_assert_uint_eq(style->bytecode[pos++],
                        buildOPV(CSS_PROP_OBJECT_POSITION, important ? FLAG_IMPORTANT : 0, forms[f].code));
                }
                for (size_t axis = 0; axis < 2; axis++) {
                    if (p == 0) {
                        uint16_t code = axis == 0 ? forms[f].code >> 4 : forms[f].code & 0xf;
                        if (code == 8)
                            code = BACKGROUND_POSITION_AXIS_SET;
                        ck_assert_uint_eq(style->bytecode[pos++], buildOPV(
                            axis == 0 ? CSS_PROP_BACKGROUND_POSITION_X : CSS_PROP_BACKGROUND_POSITION_Y,
                            important ? FLAG_IMPORTANT : 0, code));
                    }
                    if (forms[f].units[axis] == 0)
                        continue;
                    ck_assert_msg(pos + 2 <= style->used, "truncated %s", css);
                    ck_assert_msg((css_fixed)style->bytecode[pos++] == forms[f].offsets[axis], "wrong offset: %s", css);
                    ck_assert_msg(style->bytecode[pos++] == forms[f].units[axis], "wrong unit: %s", css);
                }
                ck_assert_uint_eq(style->used, pos);
                css__declaration_list_destroy(list);
            }
        }
    }
}
END_TEST

START_TEST(test_repeat_bytecode)
{
    static const char *const forms[] = {"no-repeat no-repeat", "repeat no-repeat", "no-repeat repeat", "repeat repeat"};
    static const char *const aliases[] = {"no-repeat", "repeat-x", "repeat-y", "repeat"};
    static const uint16_t codes[] = {BACKGROUND_REPEAT_NO_REPEAT, BACKGROUND_REPEAT_REPEAT_X,
        BACKGROUND_REPEAT_REPEAT_Y, BACKGROUND_REPEAT_REPEAT};
    for (size_t i = 0; i < N_ELEMENTS(forms); i++) {
        for (unsigned important = 0; important < 2; important++) {
            char css[256], equivalent[256];
            const char *priority = important ? "!important" : "";
            snprintf(css, sizeof(css), "background-repeat:%s%s", forms[i], priority);
            css_declaration_list *list = parse(css, _i != 0);
            const css_style *style = inline_style(css__declaration_list_sheet(list));
            ck_assert_uint_eq(style->used, 2);
            static const opcode_t axes[] = {CSS_PROP_BACKGROUND_REPEAT_X, CSS_PROP_BACKGROUND_REPEAT_Y};
            for (size_t axis = 0; axis < N_ELEMENTS(axes); axis++) {
                uint16_t code = codes[i] & (1u << axis) ? BACKGROUND_REPEAT_REPEAT : BACKGROUND_REPEAT_NO_REPEAT;
                ck_assert_msg(style->bytecode[axis] == buildOPV(axes[axis], important ? FLAG_IMPORTANT : 0, code),
                    "wrong repeat bytecode, axis %zu: %s", axis, css);
            }
            css__declaration_list_destroy(list);
            /* Try both sides of other unordered background components, and
             * multiple layers, to catch an over-consuming component parser. */
            snprintf(css, sizeof(css), "background:%s red url(A.png) 2px 3px%s", forms[i], priority);
            snprintf(equivalent, sizeof(equivalent), "background:%s red url(A.png) 2px 3px%s", aliases[i], priority);
            list = parse(css, _i != 0);
            css_declaration_list *reference = parse(equivalent, false);
            compare_sheets(css__declaration_list_sheet(reference), css__declaration_list_sheet(list));
            css__declaration_list_destroy(list);
            css__declaration_list_destroy(reference);
            snprintf(css, sizeof(css), "background:left top %s,blue none fixed %s%s", forms[i], forms[3 - i], priority);
            snprintf(equivalent, sizeof(equivalent), "background:left top %s,blue none fixed %s%s",
                aliases[i], aliases[3 - i], priority);
            list = parse(css, _i != 0);
            reference = parse(equivalent, false);
            compare_sheets(css__declaration_list_sheet(reference), css__declaration_list_sheet(list));
            css__declaration_list_destroy(list);
            css__declaration_list_destroy(reference);
        }
    }
}
END_TEST

START_TEST(test_background_readback)
{
    static const char *const tokens[] = {"url(A.png)", "top right", "repeat no-repeat", "fixed", "#123"};
    static const char *const properties[] = {"background-image", "background-position", "background-repeat",
        "background-attachment", "background-color"};
    static const char *const expected[] = {"url(\"A.png\")", "right top", "repeat-x", "fixed", "rgb(17, 34, 51)"};
    const char *canonical = "url(\"A.png\") right top repeat-x fixed rgb(17, 34, 51)";
    /* Every ordering of the five disjoint groups must yield the same specified
     * components and rendering bytecode, including after shorthand readback. */
    for (unsigned permutation = 0; permutation < 3125; permutation++) {
        char css[256] = "background:";
        unsigned mask = 0, order = permutation;
        for (size_t i = 0; i < N_ELEMENTS(tokens); i++, order /= 5) {
            unsigned part = order % 5;
            if (mask & (1u << part)) {
                mask = 0;
                break;
            }
            mask |= 1u << part;
            strcat(css, tokens[part]);
            strcat(css, " ");
        }
        if (mask == 0)
            continue;
        strcat(css, "!important");
        css_declaration_list *list = parse(css, _i != 0);
        ck_assert_msg(css__declaration_list_count(list) == 1, "rejected permutation: %s", css);
        css_declaration_order *view;
        ck_assert_int_eq(css__declaration_order_create(list, &view), CSS_OK);
        for (size_t i = 0; i <= N_ELEMENTS(properties); i++) {
            const char *name = i == N_ELEMENTS(properties) ? "background" : properties[i];
            lwc_string *value;
            ck_assert_msg(serialize_property(view, name, &value) == CSS_OK, "%s / %s", css, name);
            ck_assert_msg(strcmp(lwc_string_data(value), i == N_ELEMENTS(properties) ? canonical : expected[i]) == 0,
                "%s / %s -> %s", css, name, lwc_string_data(value));
            lwc_string_unref(value);
        }
        snprintf(css, sizeof(css), "background:%s!important", canonical);
        css_declaration_list *again = parse(css, _i != 0);
        compare_sheets(css__declaration_list_sheet(list), css__declaration_list_sheet(again));
        css__declaration_list_destroy(again);
        css__declaration_order_destroy(view);
        css__declaration_list_destroy(list);
    }
}
END_TEST

START_TEST(test_background_many_layers)
{
    enum { LAYERS = 128 };
    char css[16384] = "background:", expected[16384] = "";
    for (unsigned i = 0; i < LAYERS; i++) {
        size_t used = strlen(css), written = strlen(expected);
        snprintf(css + used, sizeof(css) - used, "%sno-repeat url('tile,%u.png') %upx %upx", i ? "," : "", i, i, i + 1);
        snprintf(expected + written, sizeof(expected) - written, "%surl(\"tile,%u.png\") %upx %upx no-repeat",
            i ? ", " : "", i, i, i + 1);
    }
    css_declaration_list *list = parse(css, _i != 0);
    ck_assert_uint_eq(css__declaration_list_count(list), 1);
    css_declaration_order *order;
    ck_assert_int_eq(css__declaration_order_create(list, &order), CSS_OK);
    lwc_string *value;
    ck_assert_int_eq(serialize_property(order, "background", &value), CSS_OK);
    ck_assert_str_eq(lwc_string_data(value), expected);
    snprintf(css, sizeof(css), "background:%s", lwc_string_data(value));
    css_declaration_list *again = parse(css, _i != 0);
    compare_sheets(css__declaration_list_sheet(list), css__declaration_list_sheet(again));
    css__declaration_list_destroy(again);
    lwc_string_unref(value);
    css__declaration_order_destroy(order);
    css__declaration_list_destroy(list);
}
END_TEST

START_TEST(test_background_quirks)
{
    static const struct { const char *input, *output; } forms[] = {
        {"background:000", "rgb(0, 0, 0)"},
        {"background:123456 fixed", "fixed rgb(18, 52, 86)"},
        {"background:000 1 2", "1px 2px rgb(0, 0, 0)"},
        {"background:top fixed 000", "center top fixed rgb(0, 0, 0)"},
        {"background:0 000", "0px 0px"},
    };
    for (size_t i = 0; i < N_ELEMENTS(forms); i++) {
        css_stylesheet_params quirky = params;
        quirky.allow_quirks = true;
        css_declaration_list *list;
        ck_assert_int_eq(css__declaration_list_create(&quirky, &list), CSS_OK);
        check_append(css__declaration_list_append(list, (const uint8_t *)forms[i].input, strlen(forms[i].input)));
        ck_assert_int_eq(css__declaration_list_complete(list), CSS_OK);
        ck_assert_msg(css__declaration_list_count(list) == 1, "quirks rejected %s", forms[i].input);
        css_declaration_order *order;
        ck_assert_int_eq(css__declaration_order_create(list, &order), CSS_OK);
        lwc_string *value;
        ck_assert_msg(serialize_property(order, "background", &value) == CSS_OK, "quirks %s", forms[i].input);
        ck_assert_msg(strcmp(lwc_string_data(value), forms[i].output) == 0, "%s -> %s, expected %s",
            forms[i].input, lwc_string_data(value), forms[i].output);
        lwc_string_unref(value);
        css__declaration_order_destroy(order);
        css__declaration_list_destroy(list);
    }
}
END_TEST

START_TEST(test_decoration_bytecode)
{
    static const char *const names[] = {"underline", "overline", "line-through", "blink"};
    /* Every nonempty permutation of the four supported decorations. Bytecode
     * is a set, while CSSOM orders its specified keywords per the grammar. */
    for (size_t count = 1; count <= N_ELEMENTS(names); count++) {
        for (unsigned sequence = 0; sequence < (1u << (count * 2)); sequence++) {
            char css[128] = "text-decoration:";
            unsigned mask = 0;
            for (size_t i = 0; i < count; i++) {
                unsigned part = (sequence >> (i * 2)) & 3;
                if (mask & (1u << part)) {
                    mask = 0;
                    break;
                }
                mask |= 1u << part;
                strcat(css, names[part]);
                strcat(css, " ");
            }
            if (!mask)
                continue;
            strcat(css, "!important");
            css_declaration_list *list = parse(css, _i != 0);
            const css_style *style = inline_style(css__declaration_list_sheet(list));
            ck_assert_uint_eq(style->used, 1);
            ck_assert_msg(style->bytecode[0] == buildOPV(CSS_PROP_TEXT_DECORATION, FLAG_IMPORTANT, mask),
                "wrong decoration bytecode: %s", css);
            char expected[64] = "";
            for (size_t i = 0; i < N_ELEMENTS(names); i++) {
                if (mask & (1u << i)) {
                    if (expected[0])
                        strcat(expected, " ");
                    strcat(expected, names[i]);
                }
            }
            css_declaration_order *order;
            ck_assert_int_eq(css__declaration_order_create(list, &order), CSS_OK);
            lwc_string *value;
            ck_assert_int_eq(serialize_property(order, "text-decoration", &value), CSS_OK);
            ck_assert_msg(strcmp(lwc_string_data(value), expected) == 0, "%s -> %s, expected %s",
                css, lwc_string_data(value), expected);
            lwc_string_unref(value);
            css__declaration_order_destroy(order);
            css__declaration_list_destroy(list);
        }
    }
}
END_TEST

START_TEST(test_repeat_decoration_invalid)
{
    static const struct { const char *property, *value; } cases[] = {
        {"background-repeat-x", "repeat-x"}, {"background-repeat-y", "repeat-y"},
        {"background-repeat-x", "repeat no-repeat"}, {"background-repeat-y", "repeat repeat"},
        {"background-repeat-x", "repeat,initial"}, {"background-repeat-y", "initial,repeat"},
        {"background-repeat-x", "repeat,"}, {"background-repeat-y", ",repeat"},
        {"background-repeat-x", "space"}, {"background-repeat-y", "round"},
        {"background-repeat", "repeat-x repeat"}, {"background-repeat", "repeat repeat-x"},
        {"background-repeat", "no-repeat repeat-y"}, {"background-repeat", "repeat-y no-repeat"},
        {"background-repeat", "repeat repeat repeat"}, {"background-repeat", "none"},
        {"background-repeat", "initial repeat"}, {"background-repeat", "repeat initial"},
        {"background-repeat", "repeat,initial"}, {"background-repeat", "initial,repeat"},
        {"background-repeat", "repeat,"}, {"background-repeat", ",repeat"},
        {"background-repeat", "repeat,,repeat"}, {"background-repeat", "space"}, {"background-repeat", "round"},
        {"background", "repeat-x repeat"}, {"background", "repeat repeat-x"},
        {"background", "repeat no-repeat repeat"}, {"background", "repeat initial"},
        {"text-decoration", "underline underline"}, {"text-decoration", "none underline"},
        {"text-decoration", "underline none"}, {"text-decoration", "initial underline"},
        {"text-decoration", "underline unset"}, {"text-decoration", "underline,overline"},
        {"text-decoration", "underline solid"}, {"text-decoration", "red"}, {"text-decoration", "2px"}
    };
    for (size_t i = 0; i < N_ELEMENTS(cases); i++) {
        char css[192];
        snprintf(css, sizeof(css), "%s:inherit", cases[i].property);
        css_declaration_list *reference = parse(css, false);
        snprintf(css, sizeof(css), "%s:inherit;%s:%s", cases[i].property, cases[i].property, cases[i].value);
        css_declaration_list *list = parse(css, _i != 0);
        ck_assert_msg(css__declaration_list_count(list) == 1, "accepted invalid/unsupported value: %s", css);
        compare_sheets(css__declaration_list_sheet(reference), css__declaration_list_sheet(list));
        css__declaration_list_destroy(list);
        css__declaration_list_destroy(reference);
    }
}
END_TEST

/* CSS-wide keywords cannot be mixed with another component. These six
 * shorthands use the same atomic pair parser, including deferred values. */
static const char *const logical_pairs[] = {
    "margin-block", "padding-block", "inset-block", "margin-inline", "padding-inline", "inset-inline"
};

START_TEST(test_logical_pair_invalid)
{
    const char *property = logical_pairs[_i / 2];
    static const char *const invalid[] = {"1px inherit", "1px initial", "1px unset", "1px revert",
        "inherit 1px", "initial auto", "unset initial", "1px 2px 3px", "1px, 2px", "1px junk"};
    char css[192];
    snprintf(css, sizeof(css), "%s:3px 4px", property);
    css_declaration_list *base = parse(css, false);
    for (size_t i = 0; i < N_ELEMENTS(invalid); i++) {
        snprintf(css, sizeof(css), "%s:3px 4px;%s:%s", property, property, invalid[i]);
        css_declaration_list *list = parse(css, _i % 2 != 0);
        ck_assert_msg(css__declaration_list_count(list) == 1, "accepted invalid pair: %s", css);
        compare_sheets(css__declaration_list_sheet(base), css__declaration_list_sheet(list));
        css__declaration_list_destroy(list);
    }
    css__declaration_list_destroy(base);
}
END_TEST

START_TEST(test_white_space_invalid)
{
    static const struct { const char *property, *value; } cases[] = {
        {"white-space", "normal nowrap"}, {"white-space", "pre wrap"}, {"white-space", "pre-line preserve"},
        {"white-space", "collapse collapse"}, {"white-space", "wrap nowrap"},
        {"white-space", "preserve collapse"}, {"white-space", "preserve wrap wrap"},
        {"white-space", "wrap, preserve"}, {"white-space", "inherit wrap"}, {"white-space", "wrap initial"},
        {"white-space", "break-spaces"}, {"white-space", "preserve-spaces"}, {"white-space", "discard-before"},
        {"white-space-collapse", "normal"}, {"white-space-collapse", "nowrap"},
        {"white-space-collapse", "preserve preserve"}, {"white-space-collapse", "break-spaces"},
        {"text-wrap-mode", "pre"}, {"text-wrap-mode", "balance"}, {"text-wrap-mode", "wrap nowrap"},
    };
    for (size_t i = 0; i < N_ELEMENTS(cases); i++) {
        char css[192];
        snprintf(css, sizeof(css), "%s:inherit", cases[i].property);
        css_declaration_list *base = parse(css, false);
        snprintf(css, sizeof(css), "%s:inherit;%s:%s", cases[i].property, cases[i].property, cases[i].value);
        css_declaration_list *list = parse(css, _i != 0);
        ck_assert_msg(css__declaration_list_count(list) == 1, "accepted invalid/unsupported white-space: %s", css);
        compare_sheets(css__declaration_list_sheet(base), css__declaration_list_sheet(list));
        css__declaration_list_destroy(list);
        css__declaration_list_destroy(base);
    }
}
END_TEST

START_TEST(test_position_invalid)
{
    static const struct { const char *property, *value; } axis_cases[] = {
        {"background-position-x", "top"}, {"background-position-x", "bottom"},
        {"background-position-y", "left"}, {"background-position-y", "right"},
        {"background-position-x", "1px 2px"}, {"background-position-y", "top 2px"},
        {"background-position-x", "right 2px"}, {"background-position-y", "calc(2px)"},
        {"background-position-x", "center,initial"}, {"background-position-y", "initial,center"},
        {"background-position-x", "left,"}, {"background-position-y", ",top"},
        {"background-position-x", "left,,right"}, {"background-position-y", "2fr"},
        {"background-position-x", "3dppx"}, {"background-position-y", "2"},
    };
    for (size_t i = 0; i < N_ELEMENTS(axis_cases); i++) {
        char css[192];
        snprintf(css, sizeof(css), "%s:inherit", axis_cases[i].property);
        css_declaration_list *base = parse(css, false);
        snprintf(css, sizeof(css), "%s:inherit;%s:%s", axis_cases[i].property,
            axis_cases[i].property, axis_cases[i].value);
        css_declaration_list *list = parse(css, _i != 0);
        ck_assert_msg(css__declaration_list_count(list) == 1, "accepted invalid/unsupported axis: %s", css);
        compare_sheets(css__declaration_list_sheet(base), css__declaration_list_sheet(list));
        css__declaration_list_destroy(list);
        css__declaration_list_destroy(base);
    }
    static const char *const properties[] = {"background-position", "object-position", "background"};
    static const char *const values[] = {"left left", "left right", "top bottom", "top top", "right right",
        "2px left", "3% right", "top 4px", "bottom 5%", "left initial", "inherit top",
        "1fr", "center 2fr", "2dppx", "3dpi center", "center 4dpcm", "1s", "2Hz", "3deg",
        "1", "1px 2px 3px", "top left,", ",top left", "top,,bottom", "top,initial",
        /* Edge offsets and math are still unsupported. */
        "left 2px top 3px", "right 2px bottom", "calc(2px)"};
    for (size_t p = 0; p < N_ELEMENTS(properties); p++) {
        char css[160];
        snprintf(css, sizeof(css), "%s:left top", properties[p]);
        css_declaration_list *base = parse(css, false);
        for (size_t i = 0; i < N_ELEMENTS(values); i++) {
            snprintf(css, sizeof(css), "%s:left top;%s:%s", properties[p], properties[p], values[i]);
            css_declaration_list *list = parse(css, _i != 0);
            ck_assert_msg(css__declaration_list_count(list) == 1, "accepted invalid/unsupported position: %s", css);
            compare_sheets(css__declaration_list_sheet(base), css__declaration_list_sheet(list));
            css__declaration_list_destroy(list);
        }
        css__declaration_list_destroy(base);
    }
}
END_TEST

START_TEST(test_place_content_bytecode)
{
    static const char *const keywords[] = {"normal", "stretch", "flex-start", "flex-end", "center",
        "space-between", "space-around", "space-evenly", "start", "end"};
    /* The final two keywords are currently supported only on align-content.
     * Exercise every supported pair without conflating their enum encodings. */
    for (size_t a = 0; a < N_ELEMENTS(keywords); a++) {
        for (size_t b = 0; b < N_ELEMENTS(keywords) - 2; b++) {
            for (unsigned important = 0; important < 2; important++) {
                char shorthand[128], longhands[160];
                const char *priority = important ? "!important" : "";
                snprintf(shorthand, sizeof(shorthand), "place-content:%s %s%s", keywords[a], keywords[b], priority);
                snprintf(longhands, sizeof(longhands), "align-content:%s%s;justify-content:%s%s",
                    keywords[a], priority, keywords[b], priority);
                css_declaration_list *list = parse(shorthand, _i != 0), *reference = parse(longhands, _i != 0);
                ck_assert_msg(css__declaration_list_count(list) == 1, "rejected %s", shorthand);
                compare_sheets(css__declaration_list_sheet(list), css__declaration_list_sheet(reference));
                css__declaration_list_destroy(list);
                if (a == b) {
                    snprintf(shorthand, sizeof(shorthand), "place-content:%s%s", keywords[a], priority);
                    list = parse(shorthand, _i != 0);
                    ck_assert_msg(css__declaration_list_count(list) == 1, "rejected %s", shorthand);
                    compare_sheets(css__declaration_list_sheet(list), css__declaration_list_sheet(reference));
                    css__declaration_list_destroy(list);
                }
                css__declaration_list_destroy(reference);
            }
        }
    }
    static const char *const wide[] = {"inherit", "initial", "unset", "revert"};
    for (size_t i = 0; i < N_ELEMENTS(wide); i++) {
        char shorthand[64], longhands[96];
        snprintf(shorthand, sizeof(shorthand), "place-content:%s!important", wide[i]);
        snprintf(longhands, sizeof(longhands), "align-content:%s!important;justify-content:%s!important", wide[i], wide[i]);
        css_declaration_list *a = parse(shorthand, _i != 0), *b = parse(longhands, _i != 0);
        compare_sheets(css__declaration_list_sheet(a), css__declaration_list_sheet(b));
        css__declaration_list_destroy(a);
        css__declaration_list_destroy(b);
    }
}
END_TEST

START_TEST(test_place_content_invalid)
{
    static const char *const values[] = {"initial center", "center initial", "unset normal", "normal unset",
        "revert stretch", "stretch revert", "inherit flex-end", "flex-end inherit", "initial initial",
        "center center center", "center / center", "center,center", "auto", "center 2px",
        /* Unsupported alignment forms must not partially replace the fallback. */
        "baseline", "first baseline", "safe center", "center unsafe center", "center start"};
    css_declaration_list *base = parse("place-content:space-between flex-end", false);
    for (size_t i = 0; i < N_ELEMENTS(values); i++) {
        char css[160];
        snprintf(css, sizeof(css), "place-content:space-between flex-end;place-content:%s", values[i]);
        css_declaration_list *list = parse(css, _i != 0);
        ck_assert_msg(css__declaration_list_count(list) == 1, "accepted invalid/unsupported shorthand: %s", css);
        compare_sheets(css__declaration_list_sheet(base), css__declaration_list_sheet(list));
        css__declaration_list_destroy(list);
    }
    css__declaration_list_destroy(base);
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

/* CSS Images 3 defaults and grammar, for ordinary stylesheets as well as CSSOM.
 * Invalid later images must leave the whole earlier declaration intact. */
START_TEST(test_gradient_grammar)
{
    static const char *const invalid[] = {
        "radial-gradient(circle red,blue)", "radial-gradient(ellipse red,blue)",
        "linear-gradient(to right red,blue)", "linear-gradient(90deg red,blue)",
        "linear-gradient(1foodeg,red,blue)",
        "linear-gradient(1e3deg,red,blue)", /* Exponents are still unsupported; never read only the 1. */
        "linear-gradient(red .00000001,blue)", "linear-gradient(red,blue -.00000001)",
        "radial-gradient(red 1,blue)", "linear-gradient(red 0 px,blue)",
        "linear-gradient(red 0 %,blue)", "linear-gradient(123456,blue)",
        "radial-gradient(f00,blue)", "linear-gradient(12ab56,blue)",
        "linear-gradient(red 1fr,blue)", "linear-gradient(red 1deg,blue)",
        "linear-gradient(red,,blue)", "radial-gradient(circle,red,blue,)",
    };
    for (unsigned quirks = 0; quirks < 2; quirks++) {
        css_stylesheet_params configured = params;
        configured.allow_quirks = quirks != 0;
        for (size_t i = 0; i < N_ELEMENTS(invalid); i++) {
            for (unsigned shorthand = 0; shorthand < 2; shorthand++) {
                const char *property = shorthand ? "background" : "background-image";
                char css[320];
                snprintf(css, sizeof(css), "%s:linear-gradient(red,blue);%s:%s;color:green", property, property, invalid[i]);
                css_declaration_list *list;
                ck_assert_int_eq(css__declaration_list_create(&configured, &list), CSS_OK);
                size_t length = strlen(css);
                for (size_t offset = 0; offset < length;) {
                    size_t chunk = _i == 0 ? length : 1;
                    check_append(css__declaration_list_append(list, (const uint8_t *)css + offset, chunk));
                    offset += chunk;
                }
                ck_assert_int_eq(css__declaration_list_complete(list), CSS_OK);
                ck_assert_msg(css__declaration_list_count(list) == 2,
                    "accepted %s, quirks %u, chunks %d", css, quirks, _i);
                css_declaration_order *order;
                ck_assert_int_eq(css__declaration_order_create(list, &order), CSS_OK);
                lwc_string *value;
                ck_assert_int_eq(serialize_property(order, property, &value), CSS_OK);
                ck_assert_str_eq(lwc_string_data(value), "linear-gradient(red, blue)");
                lwc_string_unref(value);
                css__declaration_order_destroy(order);
                css__declaration_list_destroy(list);
            }
        }
        /* Zero and named/hex colours stay valid in both parsing modes. */
        const char *valid = "background-image:radial-gradient( ellipse, #f00 -0,blue 100%)";
        css_declaration_list *list;
        ck_assert_int_eq(css__declaration_list_create(&configured, &list), CSS_OK);
        check_append(css__declaration_list_append(list, (const uint8_t *)valid, strlen(valid)));
        ck_assert_int_eq(css__declaration_list_complete(list), CSS_OK);
        ck_assert_uint_eq(css__declaration_list_count(list), 1);
        css__declaration_list_destroy(list);
    }
    /* Exercise the parser's existing stop bound without adding a second bound. */
    for (unsigned radial = 0; radial < 2; radial++) {
        char css[240];
        snprintf(css, sizeof(css), "background-image:%s-gradient(%s)", radial ? "radial" : "linear",
            "red,blue,red,blue,red,blue,red,blue,red,blue");
        css_declaration_list *list = parse(css, _i != 0);
        ck_assert_uint_eq(css__declaration_list_count(list), 1);
        css_declaration_order *order;
        ck_assert_int_eq(css__declaration_order_create(list, &order), CSS_OK);
        lwc_string *value;
        ck_assert_int_eq(serialize_property(order, "background-image", &value), CSS_OK);
        ck_assert(strstr(lwc_string_data(value), "red, blue, red, blue, red, blue, red, blue, red, blue)") != NULL);
        lwc_string_unref(value);
        css__declaration_order_destroy(order);
        css__declaration_list_destroy(list);
        css[strlen(css) - 1] = '\0';
        strcat(css, ",red)");
        list = parse(css, _i != 0);
        ck_assert_uint_eq(css__declaration_list_count(list), 0);
        css__declaration_list_destroy(list);
    }
    css_declaration_list *implicit = parse("background-image:radial-gradient(red,blue)", _i != 0);
    css_declaration_list *explicit = parse("background-image:radial-gradient(ellipse,red,blue)", _i != 0);
    compare_sheets(css__declaration_list_sheet(implicit), css__declaration_list_sheet(explicit));
    const css_style *style = inline_style(css__declaration_list_sheet(implicit));
    ck_assert_uint_eq(style->bytecode[1], 1); /* CSS_RADIAL_SHAPE_ELLIPSE */
    css__declaration_list_destroy(implicit);
    css__declaration_list_destroy(explicit);
}
END_TEST

/* SVG 2 stroke-width accepts numbers, lengths and percentages, but no
 * negative values or unrelated units. Invalid declarations retain the fallback
 * in ordinary stylesheets as well as CSSOM, including quirks mode. */
START_TEST(test_stroke_width_grammar)
{
    static const char *const invalid[] = {
        "-1", "-1px", "-1%", "-.00000001", "-.00000001px", "-.00000001%",
        "1deg", "1turn", "1s", "1hz", "1dpi", "1dppx", "1fr", "1foopx",
        "0 px", "2 px", "0 fr", "0% px", "1px 2px", "auto", "thin", "none", "initial 1px",
    };
    for (unsigned quirks = 0; quirks < 2; quirks++) {
        css_stylesheet_params configured = params;
        configured.allow_quirks = quirks != 0;
        for (size_t i = 0; i < N_ELEMENTS(invalid); i++) {
            char css[160];
            snprintf(css, sizeof(css), "stroke-width:7px;stroke-width:%s;color:green", invalid[i]);
            css_declaration_list *list;
            ck_assert_int_eq(css__declaration_list_create(&configured, &list), CSS_OK);
            size_t length = strlen(css);
            for (size_t offset = 0; offset < length;) {
                size_t chunk = _i == 0 ? length : 1;
                check_append(css__declaration_list_append(list, (const uint8_t *)css + offset, chunk));
                offset += chunk;
            }
            ck_assert_int_eq(css__declaration_list_complete(list), CSS_OK);
            ck_assert_msg(css__declaration_list_count(list) == 2,
                "accepted %s, quirks %u, chunks %d", css, quirks, _i);
            css_declaration_order *order;
            ck_assert_int_eq(css__declaration_order_create(list, &order), CSS_OK);
            lwc_string *value;
            ck_assert_int_eq(serialize_property(order, "stroke-width", &value), CSS_OK);
            ck_assert_str_eq(lwc_string_data(value), "7px");
            lwc_string_unref(value);
            css__declaration_order_destroy(order);
            css__declaration_list_destroy(list);
        }
        const struct { const char *text; css_fixed value; uint32_t unit; } valid[] = {
            {"2", INTTOFIX(2), UNIT_PX}, {"0", 0, UNIT_PX}, {"-0", 0, UNIT_PX},
            {"2px", INTTOFIX(2), UNIT_PX}, {"2%", INTTOFIX(2), UNIT_PCT}, {"2em", INTTOFIX(2), UNIT_EM},
        };
        for (size_t i = 0; i < N_ELEMENTS(valid); i++) {
            char css[64];
            snprintf(css, sizeof(css), "stroke-width:%s!important", valid[i].text);
            css_declaration_list *list;
            ck_assert_int_eq(css__declaration_list_create(&configured, &list), CSS_OK);
            check_append(css__declaration_list_append(list, (const uint8_t *)css, strlen(css)));
            ck_assert_int_eq(css__declaration_list_complete(list), CSS_OK);
            const css_style *style = inline_style(css__declaration_list_sheet(list));
            ck_assert_msg(style->used == 3, "rejected %s, quirks %u", css, quirks);
            ck_assert_uint_eq(style->bytecode[0], buildOPV(CSS_PROP_STROKE_WIDTH, FLAG_IMPORTANT, STROKE_WIDTH_SET));
            ck_assert_int_eq((css_fixed)style->bytecode[1], valid[i].value);
            ck_assert_uint_eq(style->bytecode[2], valid[i].unit);
            css__declaration_list_destroy(list);
        }
    }
}
END_TEST

/* Grid 2 sections 7.2.1, 7.2.3 and 7.7. Invalid suffixes must reject the
 * entire declaration, and legacy length quirks must not change this grammar. */
START_TEST(test_grid_value_grammar)
{
    static const char *const tracks[] = {
        "-1px", "-.00000001px", "-1%", "-1fr", "1deg", "1s", "1hz", "1dpi", "1dppx",
        "1", ".00000001", "0 px", "minmax(0 em,1fr)", "minmax(1px,0 px)",
        "minmax(0 1fr)", "minmax(1fr,2fr)", "minmax(1deg,2px)", "minmax(0,1deg)",
        "minmax(-.00000001px,1fr)", "minmax(0,-1fr)", "minmax(0,1)",
        "minmax(0,)", "minmax(0,1px,2px)", "minmax(minmax(0,1px),2px)",
        "repeat(0,1px)", "repeat(-1,1px)", "repeat(1.0,1px)", "repeat(1px,1px)",
        "repeat(2 1px)", "repeat(2,)", "repeat(2,1px,2px)", "repeat(2,repeat(2,1px))",
        "1px repeat(2,)", "1px repeat(2,1px,2px)", "1px minmax(0 1fr)",
        "none 1px", "1px none", "1px initial", "1px !important garbage",
        /* Existing implementation limit; never accept a truncated value. */
        "repeat(33,1px)", "1px repeat(32,1px)", "repeat(32,1px) 1px", "repeat(17,1px 2px)",
        /* Features not implemented by the current track model. */
        "repeat(auto-fill,1px)", "[start] 1px [end]", "subgrid", "fit-content(10px)",
    };
    static const char *const flows[] = {
        "dense dense", "row row", "column column", "row column", "column row", "dense row column",
        "dense row dense", "row dense row", "column dense dense", "row initial", "row, dense", "row unknown",
    };
    const char *const properties[] = {"grid-template-columns", "grid-template-rows", "grid-auto-flow"};
    for (unsigned quirks = 0; quirks < 2; quirks++) {
        css_stylesheet_params configured = params;
        configured.allow_quirks = quirks != 0;
        for (size_t p = 0; p < N_ELEMENTS(properties); p++) {
            const char *const *invalid = p == 2 ? flows : tracks;
            size_t count = p == 2 ? N_ELEMENTS(flows) : N_ELEMENTS(tracks);
            const char *fallback = p == 2 ? "column" : "7px 9px";
            for (size_t i = 0; i < count; i++) {
                char css[256];
                snprintf(css, sizeof(css), "%s:%s;%s:%s;color:green", properties[p], fallback, properties[p], invalid[i]);
                css_declaration_list *list;
                ck_assert_int_eq(css__declaration_list_create(&configured, &list), CSS_OK);
                size_t length = strlen(css);
                for (size_t offset = 0; offset < length;) {
                    size_t chunk = _i == 0 ? length : 1;
                    check_append(css__declaration_list_append(list, (const uint8_t *)css + offset, chunk));
                    offset += chunk;
                }
                ck_assert_int_eq(css__declaration_list_complete(list), CSS_OK);
                ck_assert_msg(css__declaration_list_count(list) == 2,
                    "accepted %s, quirks %u, chunks %d", css, quirks, _i);
                css_declaration_order *order;
                ck_assert_int_eq(css__declaration_order_create(list, &order), CSS_OK);
                lwc_string *value;
                ck_assert_int_eq(serialize_property(order, properties[p], &value), CSS_OK);
                ck_assert_str_eq(lwc_string_data(value), fallback);
                lwc_string_unref(value);
                css__declaration_order_destroy(order);
                css__declaration_list_destroy(list);
            }
        }
    }
}
END_TEST

START_TEST(test_grid_value_bytecode)
{
    static const char *const forms[][3] = {
        {"grid-template-columns", "repeat(2,10px minmax(0,1fr))", "10px minmax(0px,1fr) 10px minmax(0px,1fr)"},
        {"grid-template-rows", "repeat(2,10px minmax(0,1fr))", "10px minmax(0px,1fr) 10px minmax(0px,1fr)"},
        {"grid-template-columns", "repeat(1,auto min-content max-content)", "auto min-content max-content"},
        {"grid-template-rows", "repeat(1,0 -0px -0%)", "0px 0px 0%"},
        {"grid-auto-flow", "dense row", "row dense"},
        {"grid-auto-flow", "dense", "row dense"},
        {"grid-auto-flow", "dense column", "column dense"},
    };
    for (size_t i = 0; i < N_ELEMENTS(forms); i++) {
        char first[160], second[160];
        snprintf(first, sizeof(first), "%s:%s", forms[i][0], forms[i][1]);
        snprintf(second, sizeof(second), "%s:%s", forms[i][0], forms[i][2]);
        css_declaration_list *a = parse(first, _i != 0), *b = parse(second, _i != 0);
        ck_assert_msg(css__declaration_list_count(a) == 1 && css__declaration_list_count(b) == 1,
            "failed %s / %s", first, second);
        compare_sheets(css__declaration_list_sheet(a), css__declaration_list_sheet(b));
        css__declaration_list_destroy(a);
        css__declaration_list_destroy(b);
    }
}
END_TEST

/* Invalid transform arguments must never replace an earlier valid value,
 * including in quirks mode. CSS Transforms does not opt into unitless lengths. */
START_TEST(test_transform_grammar)
{
    static const char *const invalid[] = {
        "translate(1deg)", "translate(1px,2deg)", "translate(1fr)", "translate(1px,2fr)",
        "translate(1s)", "translate(1px,2s)", "translate(1hz)", "translate(1px,2hz)",
        "translate(1dpi)", "translate(1px,2dppx)", "translateX(1fr)", "translateY(1deg)",
        "translate(1)", "translate(1px,2)", "translate(.00000001)", "translate(1px,-.00000001)",
        "translate(0 px)", "translate(1px,0 em)", "rotate(1)", "rotate(.00000001)",
        "rotate(0 deg)", "rotate(10%)", "rotate(1px)", "rotate(1foodeg)", "rotate(1e3deg)",
        "scale(1px)", "scale(1,2px)", "scaleX(1deg)", "scaleY(1,2)",
        "translate()", "translate(1px,)", "translate(1px 2px)", "translate(1px,2px,3px)",
        "translateX(1px,2px)", "rotate(0,1deg)", "scale(1,)", "scale(1 2)",
        "scale(1) none", "none scale(1)", "scale(1) garbage", "scale(1),rotate(0)",
        "scale(1) unknown(0)", "scale(1) translate(2px,3deg)",
    };
    for (unsigned quirks = 0; quirks < 2; quirks++) {
        css_stylesheet_params configured = params;
        configured.allow_quirks = quirks != 0;
        for (size_t i = 0; i < N_ELEMENTS(invalid); i++) {
            char css[240];
            snprintf(css, sizeof(css), "transform:translate(7px,9px);transform:%s;color:green", invalid[i]);
            css_declaration_list *list;
            ck_assert_int_eq(css__declaration_list_create(&configured, &list), CSS_OK);
            size_t length = strlen(css);
            for (size_t offset = 0; offset < length;) {
                size_t chunk = _i == 0 ? length : 1;
                check_append(css__declaration_list_append(list, (const uint8_t *)css + offset, chunk));
                offset += chunk;
            }
            ck_assert_int_eq(css__declaration_list_complete(list), CSS_OK);
            ck_assert_msg(css__declaration_list_count(list) == 2,
                "accepted %s, quirks %u, chunks %d", css, quirks, _i);
            css_declaration_order *order;
            ck_assert_int_eq(css__declaration_order_create(list, &order), CSS_OK);
            lwc_string *value;
            ck_assert_int_eq(serialize_property(order, "transform", &value), CSS_OK);
            ck_assert_str_eq(lwc_string_data(value), "translate(7px, 9px)");
            lwc_string_unref(value);
            css__declaration_order_destroy(order);
            css__declaration_list_destroy(list);
        }
        const char *valid = "transform:translate(0,-0) rotate(-0) scale(50%,150%)";
        css_declaration_list *list;
        ck_assert_int_eq(css__declaration_list_create(&configured, &list), CSS_OK);
        check_append(css__declaration_list_append(list, (const uint8_t *)valid, strlen(valid)));
        ck_assert_int_eq(css__declaration_list_complete(list), CSS_OK);
        ck_assert_uint_eq(css__declaration_list_count(list), 1);
        css__declaration_list_destroy(list);
    }
}
END_TEST

START_TEST(test_transform_bytecode)
{
    static const char *const forms[][2] = {
        {"translate(2px)", "translate(2px,0px)"},
        {"translate(0,-0)", "translate(0px,0px)"},
        {"scale(2)", "scale(2,2)"},
        {"scale(50%)", "scale(50%,50%)"},
        {"rotate(0)", "rotate(0deg)"},
        {"TRANSLATEX(3PX)scaleY(2)rotate(45DEG)", "translateX(3px) scaleY(2) rotate(45deg)"},
    };
    for (size_t i = 0; i < N_ELEMENTS(forms); i++) {
        char first[160], second[160];
        snprintf(first, sizeof(first), "transform:%s", forms[i][0]);
        snprintf(second, sizeof(second), "transform:%s", forms[i][1]);
        css_declaration_list *a = parse(first, _i != 0), *b = parse(second, _i != 0);
        ck_assert_uint_eq(css__declaration_list_count(a), 1);
        ck_assert_uint_eq(css__declaration_list_count(b), 1);
        compare_sheets(css__declaration_list_sheet(a), css__declaration_list_sheet(b));
        css__declaration_list_destroy(a);
        css__declaration_list_destroy(b);
    }
}
END_TEST

/* !important changes only the OPV, never the property payload. A trailing
 * declaration checks that traversal resumes at a real property boundary. */
START_TEST(test_property_payload_important)
{
    static const char *const values[] = {
        "grid-template-columns:40px 80px", "grid-template-rows:repeat(2,minmax(0,1fr) 20px)",
        "grid-template-columns:repeat(32,minmax(1px,2fr))", "grid-template-rows:none",
        "grid-template-columns:initial", "grid-template-rows:inherit",
        "transform:translate(1px,2px) scale(2,0) rotate(30deg) translateX(3px) translateY(4px) scaleX(5) scaleY(6)",
        "transform:none", "transform:initial", "transform:revert",
        "fill:#ff000001", "fill:currentcolor", "fill:none", "fill:initial", "fill:inherit",
        "stroke:#80008080", "stroke:currentcolor", "stroke:none", "stroke:initial", "stroke:unset",
        "stroke-width:32768px", "stroke-width:2", "stroke-width:3%", "stroke-width:initial",
        "fill-opacity:.5", "stroke-opacity:.25",
        "cursor:auto", "cursor:url(Cur.cur),url(Other.cur),pointer",
        "color:lab(50 10 20 / .5)", "fill:oklab(.5 .1 -.1)", "stroke:lch(50 10 -90deg)",
        "outline-color:oklch(50% 20% 100grad / none)",
        "azimuth:behind left", "azimuth:-90deg", "elevation:20grad", "pause-before:20ms", "pause-after:30%",
        "pause-before:initial", "pause-after:inherit",
        "pitch:2khz", "pitch-range:50", "richness:50", "stress:50", "speech-rate:120", "volume:50%", "volume:50",
        "speak:spell-out", "play-during:url(A.ogg) mix repeat", "play-during:url(A.ogg) repeat", "play-during:none",
        "voice-family:One Two,male,'X'", "cue-before:none", "cue-after:url(A.ogg)", "cue-before:inherit",
    };
    for (size_t i = 0; i < N_ELEMENTS(values); i++) {
        char input[256];
        snprintf(input, sizeof(input), "%s;color:red", values[i]);
        css_declaration_list *normal = parse(input, _i != 0);
        snprintf(input, sizeof(input), "%s!important;color:red", values[i]);
        css_declaration_list *priority = parse(input, _i != 0);
        const css_style *a = inline_style(css__declaration_list_sheet(normal));
        const css_style *b = inline_style(css__declaration_list_sheet(priority));
        ck_assert_msg(a->used > 2 && a->used == b->used, "wrong bytecode size: %s", values[i]);
        ck_assert_msg(b->bytecode[0] == (a->bytecode[0] | ((css_code_t)FLAG_IMPORTANT << 10)),
            "wrong OPV: %s", values[i]);
        ck_assert_msg(memcmp(a->bytecode + 1, b->bytecode + 1, (a->used - 1) * sizeof(css_code_t)) == 0,
            "!important corrupted payload or next declaration: %s", values[i]);
        css__declaration_list_destroy(normal);
        css__declaration_list_destroy(priority);
    }
    /* Shorthands emit two OPVs, each of which must receive the flag. */
    static const char *const pairs[][2] = {
        {"pause:20ms 30%!important;color:red", "pause-before:20ms!important;pause-after:30%!important;color:red"},
        {"pause:initial!important;color:red", "pause-before:initial!important;pause-after:initial!important;color:red"},
        {"cue:none url(A.ogg)!important;color:red", "cue-before:none!important;cue-after:url(A.ogg)!important;color:red"},
        {"cue:inherit!important;color:red", "cue-before:inherit!important;cue-after:inherit!important;color:red"},
    };
    for (size_t i = 0; i < N_ELEMENTS(pairs); i++) {
        css_declaration_list *shorthand = parse(pairs[i][0], _i != 0);
        css_declaration_list *longhands = parse(pairs[i][1], _i != 0);
        compare_sheets(css__declaration_list_sheet(shorthand), css__declaration_list_sheet(longhands));
        css__declaration_list_destroy(shorthand);
        css__declaration_list_destroy(longhands);
    }
}
END_TEST

START_TEST(test_transform_many_functions)
{
    enum { COUNT = 256 };
    const char *part = "translateX(1px) rotate(2deg) ";
    size_t length = 10 + strlen(part) * COUNT;
    char *input = malloc(length + 1);
    ck_assert_ptr_nonnull(input);
    memcpy(input, "transform:", 10);
    for (size_t i = 0; i < COUNT; i++)
        memcpy(input + 10 + i * strlen(part), part, strlen(part));
    input[length] = '\0';
    css_declaration_list *list = parse(input, _i != 0);
    ck_assert_uint_eq(css__declaration_list_count(list), 1);
    const css_style *style = inline_style(css__declaration_list_sheet(list));
    ck_assert_uint_eq(style->bytecode[1], 2 * COUNT);
    css_declaration_order *order;
    ck_assert_int_eq(css__declaration_order_create(list, &order), CSS_OK);
    lwc_string *value;
    ck_assert_int_eq(serialize_property(order, "transform", &value), CSS_OK);
    input[length - 1] = '\0'; /* The specified value has no trailing whitespace. */
    ck_assert_str_eq(lwc_string_data(value), input + 10);
    lwc_string_unref(value);
    css__declaration_order_destroy(order);
    css__declaration_list_destroy(list);
    free(input);
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

/* CSS Values 4 10.8/10.9: syntax and dimensional validity are determined
 * before the declaration participates in the cascade, including inside font
 * and other shorthands. Expectations are independent of browser output. */
START_TEST(test_calc_grammar)
{
    static const struct { const char *property, *value; bool accepted; } cases[] = {
        {"width", "calc(1px + 2em)", true},
        {"width", "calc(50% - 2px)", true},
        {"width", "calc(0 + 2px)", false},
        {"width", "calc(2px + 0)", false},
        {"width", "calc(3 - 2)", false},
        {"width", "calc(0)", false},
        {"width", "calc(2px * 3px)", false},
        {"width", "calc(0 * 2px + 1)", false},
        {"width", "calc(1fr)", false},
        {"width", "calc(1fr + 2px)", false},
        {"width", "calc(1deg)", false},
        {"width", "calc(1px+ 2px)", false},
        {"width", "calc((1px)+ (2px))", false},
        {"width", "calc(1px +(2px))", false},
        {"width", "calc(1px/**/+/**/2px)", false},
        {"width", "calc(1px /*before*/ + /*after*/ 2px)", true},
        {"width", "calc(1px - -2px)", true},
        {"width", "calc(12px/2 + 1px)", true},
        {"width", "calc(12px/2/3)", true},
        {"width", "calc(12px / (1 + 2))", true},
        {"width", "calc(12px / calc(1 + 2))", true},
        {"width", "calc(1px + CALC(2em * 3))", true},
        {"width", "calc(calc(1px) * 2)", true},
        {"width", "calc(1px +)", false},
        {"width", "calc(1px /)", false},
        {"width", "calc(1px *)", false},
        {"width", "calc()", false},
        {"width", "calc(())", false},
        {"width", "calc(1px, 2px)", false},
        {"line-height", "calc(1 + .25)", true},
        {"line-height", "calc(100% + 2px)", true},
        {"line-height", "calc(1 + 2px)", false},
        {"line-height", "calc(25% + .25)", false},
        {"z-index", "calc(1 + 2 / (3 + 4))", true},
        {"z-index", "calc(1px)", false},
        {"flex-grow", "calc(1 + 2)", true},
        {"flex-shrink", "calc(1px)", false},
        {"flex-basis", "calc(2 * (5% + 1px))", true},
        {"flex", "1 1 calc(2 + 1px)", false},
        {"font", "italic calc(1em + 2px)/calc(1 + .25) serif", true},
        {"font", "italic calc(1em + 2)/1.25 serif", false},
        {"inline-size", "calc(100% - 10px)", true},
        {"max-block-size", "calc(2 + 1px)", false},
        {"margin-left", "calc(-2px + 1%)", true},
        {"padding-top", "calc(1fr)", false},
        {"margin", "1px calc(1px + 1)", false},
        {"border-top-width", "calc(1% + 1px)", false},
        {"border-top-width", "calc(1px / 2 + 1px)", true},
        {"gap", "calc(1px + 1%) calc(2px)", true},
        {"column-width", "calc(1% + 1px)", false},
        {"word-spacing", "calc(1em + 2px)", true},
        {"word-spacing", "calc(1%)", false},
        {"pause-before", "calc(1s + 250ms)", true},
        {"pause-before", "calc(1s + 1)", false},
        {"pitch", "calc(1khz + 2hz)", true},
        {"pitch", "calc(1hz + 1)", false},
        {"width", "calc(infinity * 1px)", true}, {"width", "calc(NaN * 1%)", true},
        {"line-height", "calc(-infinity)", true},
        {"width", "calc(infinity)", false}, {"width", "infinity", false},
        {"width", "calc(infinitypx)", false}, {"width", "calc(+infinity * 1px)", false},
        {"width", "calc(NaN + 1px)", false}, {"width", "calc(infinity * 1fr)", false},
    };
    for (size_t i = 0; i < N_ELEMENTS(cases); i++) {
        char input[180];
        snprintf(input, sizeof(input), "%s:%s", cases[i].property, cases[i].value);
        css_declaration_list *list = parse(input, _i != 0);
        ck_assert_msg(css__declaration_list_count(list) == (size_t)cases[i].accepted,
            "calc case %zu (%s): expected %s", i, input, cases[i].accepted ? "accepted" : "rejected");
        css__declaration_list_destroy(list);
    }
}
END_TEST

START_TEST(test_calc_nesting_and_truncation)
{
    /* At least 32 nesting levels and 32 terms are required by Values 4 10.8.
     * Excess nesting is deliberately rejected, not allowed to exhaust C stack. */
    for (unsigned depth = 32; depth <= 512; depth *= 16) {
        char input[1200];
        size_t pos = (size_t)sprintf(input, "width:calc(");
        memset(input + pos, '(', depth);
        pos += depth;
        memcpy(input + pos, "1px", 3);
        pos += 3;
        memset(input + pos, ')', depth + 1);
        input[pos + depth + 1] = 0;
        css_declaration_list *list = parse(input, _i != 0);
        ck_assert_uint_eq(css__declaration_list_count(list), depth == 32 ? 1 : 0);
        css__declaration_list_destroy(list);
    }
    char input[2200] = "width:calc(1px";
    for (unsigned i = 1; i < 256; i++)
        strcat(input, " + 1px");
    strcat(input, ")");
    css_declaration_list *list = parse(input, _i != 0);
    ck_assert_uint_eq(css__declaration_list_count(list), 1);
    css__declaration_list_destroy(list);
    list = parse("width:calc(12px / (1 + 2))", false);
    const css_declaration *source = css__declaration_list_get(list, 0);
    css_stylesheet *sheet;
    ck_assert_int_eq(css_stylesheet_create(&params, &sheet), CSS_OK);
    css_style *style;
    ck_assert_int_eq(css__stylesheet_style_create(sheet, &style), CSS_OK);
    ck_assert_int_eq(css__stylesheet_style_append(style, 0x12345678), CSS_OK);
    parserutils_vector *tokens;
    ck_assert_int_eq(parserutils_vector_create(sizeof(css_token), 4, &tokens), PARSERUTILS_OK);
    for (size_t i = 0; i < source->token_count; i++) {
        int32_t ctx = 0;
        css_error error = css__parse_width(sheet->parser_frontend, tokens, &ctx, style);
        ck_assert_msg(error == CSS_INVALID, "truncated calc prefix %zu returned %d", i, error);
        ck_assert_int_eq(ctx, 0);
        ck_assert_uint_eq(style->used, 1);
        ck_assert_uint_eq(style->bytecode[0], 0x12345678);
        css_token token = {.type = source->tokens[i].type, .idata = source->tokens[i].text};
        ck_assert_int_eq(parserutils_vector_append(tokens, &token), PARSERUTILS_OK);
    }
    int32_t ctx = 0;
    ck_assert_int_eq(css__parse_width(sheet->parser_frontend, tokens, &ctx, style), CSS_OK);
    ck_assert_uint_eq(style->used, 4);
    ck_assert_uint_eq(ctx, source->token_count);
    parserutils_vector_destroy(tokens);
    css__stylesheet_style_destroy(style);
    css_stylesheet_destroy(sheet);
    css__declaration_list_destroy(list);
}
END_TEST

/* Exercise the actual generated property context and compiled program. Range
 * constraints belong to the property, not the syntax of inner expressions. */
START_TEST(test_calc_property_ranges)
{
    static const struct { const char *css; int result; } cases[] = {
        {"width:calc(1px - 6px)", 0},
        {"width:calc(1px / calc(-5 * 0))", 0},
        {"width:calc(calc(-10px) + 20px)", 10},
        {"height:calc(-5px)", 0},
        {"min-width:calc(-5px)", 0}, {"max-width:calc(-5px)", 0},
        {"min-height:calc(-5px)", 0}, {"max-height:calc(-5px)", 0},
        {"inline-size:calc(-5px)", 0}, {"block-size:calc(-5px)", 0},
        {"min-inline-size:calc(-5px)", 0}, {"max-block-size:calc(-5px)", 0},
        {"padding-left:calc(-5px)", 0}, {"border-left-width:calc(-5px)", 0},
        {"outline-width:calc(-5px)", 0}, {"font-size:calc(-5px)", 0},
        {"line-height:calc(-5)", 0}, {"flex-basis:calc(-5px)", 0},
        {"row-gap:calc(-5px)", 0}, {"column-gap:calc(-5px)", 0},
        {"column-width:calc(-5px)", 0},
        {"margin-left:calc(-5px)", -5}, {"left:calc(-5px)", -5},
        {"text-indent:calc(-5px)", -5}, {"letter-spacing:calc(-5px)", -5},
        {"word-spacing:calc(-5px)", -5}, {"vertical-align:calc(-5px)", -5},
        {"flex-grow:calc(-5)", 0}, {"flex-shrink:calc(NaN)", 0},
        {"column-count:calc(0)", 1}, {"orphans:calc(-infinity)", 1}, {"widows:calc(NaN)", 1},
        {"pitch-range:calc(infinity)", 100}, {"richness:calc(200)", 100}, {"stress:calc(-infinity)", 0},
        {"volume:calc(infinity)", 100}, {"volume:calc(200%)", 100},
        {"z-index:calc(-5)", -5}, {"order:calc(-5)", -5},
    };
    css_unit_ctx units = {.device_dpi = INTTOFIX(96)};
    css_calculator *calculator;
    ck_assert_int_eq(css_calculator_create(&calculator), CSS_OK);
    for (size_t i = 0; i < N_ELEMENTS(cases); i++) {
        css_declaration_list *list = parse(cases[i].css, _i != 0);
        ck_assert_msg(css__declaration_list_count(list) == 1, "%s was rejected", cases[i].css);
        const css_stylesheet *sheet = css__declaration_list_sheet(list);
        const css_style *style = inline_style(sheet);
        ck_assert_uint_eq(style->used, 3);
        lwc_string *expression;
        ck_assert_int_eq(css__stylesheet_string_get((css_stylesheet *)sheet, style->bytecode[2], &expression), CSS_OK);
        css_unit unit;
        css_fixed result;
        ck_assert_int_eq(css_calculator_calculate(calculator, &units, 200, expression, NULL,
            &unit, &result), CSS_OK);
        ck_assert_msg(result == INTTOFIX(cases[i].result), "%s: got %d, expected %d",
            cases[i].css, result, INTTOFIX(cases[i].result));
        css__declaration_list_destroy(list);
    }
    css_calculator_unref(calculator);
}
END_TEST

START_TEST(test_calc_readback_bounds)
{
    char flat[7000] = "width:calc(1px";
    for (unsigned i = 1; i < 1024; i++)
        strcat(flat, " + 1px");
    strcat(flat, ")");
    css_declaration_list *list = parse(flat, false);
    const css_declaration *source = css__declaration_list_get(list, 0);
    lwc_string *out;
    ck_assert_int_eq(css__declaration_calc_serialize(source->tokens, source->token_count, &out), CSS_OK);
    ck_assert_str_eq(lwc_string_data(out), "calc(1024px)");
    lwc_string_unref(out);
    css__declaration_list_destroy(list);

    list = parse("width:calc(12px / calc(1 + 2) + 3%)", false);
    source = css__declaration_list_get(list, 0);
    for (size_t i = 0; i < source->token_count; i++) {
        out = (void *)1;
        ck_assert_int_eq(css__declaration_calc_serialize(source->tokens, i, &out), i == 0 ? CSS_BADPARM : CSS_INVALID);
        ck_assert_ptr_null(out);
    }
    ck_assert_int_eq(css__declaration_calc_serialize(source->tokens, source->token_count, &out), CSS_OK);
    ck_assert_str_eq(lwc_string_data(out), "calc(3% + 4px)");
    lwc_string_unref(out);
    css__declaration_list_destroy(list);

    /* Numeric overflow uses the same standard keyword form. */
    size_t digits = DBL_MAX_10_EXP + 16;
    char *huge = malloc(digits + 64);
    ck_assert_ptr_nonnull(huge);
    strcpy(huge, "width:calc(");
    memset(huge + 11, '9', digits);
    strcpy(huge + 11 + digits, "px);color:red");
    list = parse(huge, false);
    free(huge);
    ck_assert_uint_eq(css__declaration_list_count(list), 2);
    source = css__declaration_list_get(list, 0);
    out = (void *)1;
    ck_assert_int_eq(css__declaration_calc_serialize(source->tokens, source->token_count, &out), CSS_OK);
    ck_assert_str_eq(lwc_string_data(out), "calc(infinity * 1px)");
    lwc_string_unref(out);
    css__declaration_list_destroy(list);
}
END_TEST

START_TEST(test_calc_readback_locale)
{
    char *saved = strdup(setlocale(LC_NUMERIC, NULL));
    ck_assert_ptr_nonnull(saved);
    static const char *const locales[] = {"C", "ro_RO.utf8", "de_DE.utf8"};
    for (size_t i = 0; i < N_ELEMENTS(locales); i++) {
        if (setlocale(LC_NUMERIC, locales[i]) == NULL)
            continue;
        css_declaration_list *list = parse("width:calc(50.25% - 1px / 3)", false);
        const css_declaration *source = css__declaration_list_get(list, 0);
        lwc_string *out;
        ck_assert_int_eq(css__declaration_calc_serialize(source->tokens, source->token_count, &out), CSS_OK);
        ck_assert_str_eq(lwc_string_data(out), "calc(50.25% - 0.333333px)");
        lwc_string_unref(out);
        css__declaration_list_destroy(list);
    }
    ck_assert_ptr_nonnull(setlocale(LC_NUMERIC, saved));
    free(saved);
}
END_TEST

#ifdef WISP_TEST_ALLOC_FAILURE
START_TEST(test_calc_allocation_rollback)
{
    /* Force every allocation through simplifier growth, string interning/storage
     * and each append to the final style; neither ctx nor earlier bytecode may
     * change on failure. No temporary css_style is required by calc anymore. */
    void (*limit)(unsigned int) = _i == 0 ? malloc_limit : _i == 1 ? calloc_limit : realloc_limit;
    css_declaration_list *list = parse("width:calc(12px / calc(1 + 2) + 3%)", false);
    const css_declaration *source = css__declaration_list_get(list, 0);
    parserutils_vector *tokens;
    ck_assert_int_eq(parserutils_vector_create(sizeof(css_token), 4, &tokens), PARSERUTILS_OK);
    for (size_t i = 0; i < source->token_count; i++) {
        css_token token = {.type = source->tokens[i].type, .idata = source->tokens[i].text};
        ck_assert_int_eq(parserutils_vector_append(tokens, &token), PARSERUTILS_OK);
    }
    for (unsigned spare = 0; spare < 3; spare++) {
        bool succeeded = false;
        for (unsigned attempt = 0; attempt < 128 && !succeeded; attempt++) {
            css_stylesheet *sheet;
            ck_assert_int_eq(css_stylesheet_create(&params, &sheet), CSS_OK);
            css_style *style;
            ck_assert_int_eq(css__stylesheet_style_create(sheet, &style), CSS_OK);
            while (style->used < style->allocated - spare)
                ck_assert_int_eq(css__stylesheet_style_append(style, 0x12345678), CSS_OK);
            uint32_t used = style->used;
            int32_t ctx = 0;
            limit(attempt);
            css_error error = css__parse_width(sheet->parser_frontend, tokens, &ctx, style);
            limit(UINT_MAX);
            if (error == CSS_OK) {
                ck_assert_uint_eq(style->used, used + 3);
                ck_assert_uint_eq(ctx, source->token_count);
                succeeded = true;
            } else {
                ck_assert_msg(error == CSS_NOMEM, "allocator %d / spare %u / attempt %u: %d", _i, spare, attempt, error);
                ck_assert_int_eq(ctx, 0);
                ck_assert_uint_eq(style->used, used);
            }
            for (size_t i = 0; i < used; i++)
                ck_assert_uint_eq(style->bytecode[i], 0x12345678);
            css__stylesheet_style_destroy(style);
            css_stylesheet_destroy(sheet);
        }
        ck_assert_msg(succeeded, "allocator %d / spare %u never completed", _i, spare);
    }
    parserutils_vector_destroy(tokens);
    css__declaration_list_destroy(list);
}
END_TEST

START_TEST(test_calc_readback_allocation_failure)
{
    void (*limit)(unsigned int) = _i == 0 ? malloc_limit : _i == 1 ? calloc_limit : realloc_limit;
    char css[1800] = "margin:calc(1px) calc(2em) calc(3%) calc(";
    /* Right-nesting exercises growth of the symbolic operand stack, whereas
     * large flat sums should reuse its two live operand slots. */
    for (size_t i = 0; i < 40; i++)
        strcat(css, "1px + calc(");
    strcat(css, "2em");
    for (size_t i = 0; i < 41; i++)
        strcat(css, ")");
    css_declaration_list *list = parse(css, false);
    css_declaration_order *order;
    ck_assert_int_eq(css__declaration_order_create(list, &order), CSS_OK);
    bool succeeded = false;
    unsigned failures = 0;
    for (unsigned attempt = 0; attempt < 128 && !succeeded; attempt++) {
        lwc_string *out = (void *)1;
        limit(attempt);
        css_error error = serialize_property(order, "margin", &out);
        limit(UINT_MAX);
        if (error == CSS_OK) {
            ck_assert_str_eq(lwc_string_data(out), "calc(1px) calc(2em) calc(3%) calc(2em + 40px)");
            lwc_string_unref(out);
            succeeded = true;
        } else {
            ck_assert_msg(error == CSS_NOMEM, "allocator %d attempt %u returned %d", _i, attempt, error);
            ck_assert_ptr_null(out);
            failures++;
        }
    }
    ck_assert_msg(succeeded && (_i == 1 || failures > 0), "allocator %d did not finish/recover", _i);
    css__declaration_order_destroy(order);
    css__declaration_list_destroy(list);
}
END_TEST
#endif

struct system_font_fixture {
    css_error error;
    lwc_string *family;
    unsigned calls;
    const css_system_font *override;
};

static css_error resolve_system_font(void *pw, lwc_string *name, css_system_font *font)
{
    struct system_font_fixture *fixture = pw;
    fixture->calls++;
    if (fixture->error != CSS_OK)
        return fixture->error;
    static const char *const names[] = {"caption", "icon", "menu", "message-box", "small-caption", "status-bar"};
    bool known = false;
    for (size_t i = 0; i < N_ELEMENTS(names); i++)
        known |= css__keyword_equal(name, names[i]);
    if (!known)
        return CSS_INVALID;
    if (fixture->override != NULL) {
        *font = *fixture->override;
        if (font->family != NULL)
            font->family = lwc_string_ref(font->family);
        return CSS_OK;
    }
    *font = (css_system_font){.style = CSS_FONT_STYLE_ITALIC, .variant = CSS_FONT_VARIANT_NORMAL,
        .weight = CSS_FONT_WEIGHT_BOLD, .size = {INTTOFIX(16), CSS_UNIT_PX},
        .line_height = {INTTOFIX(20), CSS_UNIT_PX}, .family = lwc_string_ref(fixture->family)};
    return CSS_OK;
}

static css_declaration_list *parse_system(const css_stylesheet_params *system_params, const char *input, bool chunks)
{
    css_declaration_list *list;
    ck_assert_int_eq(css__declaration_list_create(system_params, &list), CSS_OK);
    size_t length = strlen(input);
    for (size_t pos = 0; pos < length; pos += chunks ? 1 : length)
        check_append(css__declaration_list_append(list, (const uint8_t *)input + pos, chunks ? 1 : length));
    ck_assert_int_eq(css__declaration_list_complete(list), CSS_OK);
    return list;
}

/* This readback policy preserves the existing parse-time resolution model;
 * CSSWG issue 1586 has no settled serialization rule for system-font longhands. */
START_TEST(test_system_font_readback)
{
    static const char *const keywords[] = {"C\\41 PTION", "icon", "MENU", "message-box", "small-caption", "status-bar"};
    static const char *const canonical[] = {"caption", "icon", "menu", "message-box", "small-caption", "status-bar"};
    struct system_font_fixture fixture = {0};
    ck_assert_int_eq(lwc_intern_string("sERIF", 5, &fixture.family), lwc_error_ok);
    css_stylesheet_params system_params = params;
    system_params.font = resolve_system_font;
    system_params.font_pw = &fixture;
    for (size_t k = 0; k < N_ELEMENTS(keywords); k++) {
        char input[80];
        snprintf(input, sizeof(input), "font: /* system */ %s !important", keywords[k]);
        css_declaration_list *list = parse_system(&system_params, input, _i != 0);
        ck_assert_uint_eq(css__declaration_list_count(list), 1);
        unsigned calls = fixture.calls;
        css_declaration_list *reference = parse("font:italic bold 16px/20px serif!important", false);
        compare_sheets(css__declaration_list_sheet(list), css__declaration_list_sheet(reference));
        css_declaration_order *order;
        ck_assert_int_eq(css__declaration_order_create(list, &order), CSS_OK);
        static const char *const names[] = {"font", "font-style", "font-variant", "font-weight", "font-size",
            "line-height", "font-family"};
        const char *expected[] = {canonical[k], "italic", "normal", "bold", "16px", "20px", "serif"};
        for (size_t i = 0; i < N_ELEMENTS(names); i++) {
            lwc_string *value;
            ck_assert_int_eq(serialize_property(order, names[i], &value), CSS_OK);
            ck_assert_msg(strcmp(lwc_string_data(value), expected[i]) == 0, "%s / %s: got %s", keywords[k], names[i],
                lwc_string_data(value));
            lwc_string_unref(value);
        }
        lwc_string *text;
        ck_assert_int_eq(css__declaration_block_serialize(order, &text), CSS_OK);
        snprintf(input, sizeof(input), "font: %s !important;", canonical[k]);
        ck_assert_str_eq(lwc_string_data(text), input);
        lwc_string_unref(text);
        ck_assert_uint_eq(fixture.calls, calls);
        css__declaration_order_destroy(order);
        css__declaration_list_destroy(reference);
        css__declaration_list_destroy(list);
    }
    /* A resolved but subsequently rejected font must not leak into the next
     * declaration, an ordinary font, or a deferred shorthand. */
    css_declaration_list *list = parse_system(&system_params,
        "font:caption junk; color:red; font:12px serif; font:caption var(--x)", _i != 0);
    ck_assert_uint_eq(css__declaration_list_count(list), 3);
    for (size_t i = 0; i < 3; i++)
        ck_assert_ptr_null(css__declaration_list_get(list, i)->system_font);
    css__declaration_list_destroy(list);
    lwc_string_unref(fixture.family);
}
END_TEST

static void check_system_store(css_declaration_store *store, const char *property, const char *expected)
{
    lwc_string *value;
    css_error error = property != NULL ? css__declaration_store_value(store, property, strlen(property), &value)
                                      : css__declaration_store_serialize(store, &value);
    ck_assert_int_eq(error, CSS_OK);
    ck_assert_msg(strcmp(lwc_string_data(value), expected) == 0, "%s: expected %s, got %s",
        property != NULL ? property : "cssText", expected, lwc_string_data(value));
    lwc_string_unref(value);
}

START_TEST(test_system_font_editing)
{
    struct system_font_fixture fixture = {0};
    const char *family = "System \"Font\"\\Family";
    ck_assert_int_eq(lwc_intern_string(family, strlen(family), &fixture.family), lwc_error_ok);
    css_system_font font = {.style = CSS_FONT_STYLE_OBLIQUE, .variant = CSS_FONT_VARIANT_SMALL_CAPS,
        .weight = CSS_FONT_WEIGHT_600, .size = {INTTOFIX(16) + (1 << (CSS_RADIX_POINT - 2)), CSS_UNIT_PT},
        .line_height = {INTTOFIX(125), CSS_UNIT_PCT}, .family = fixture.family};
    fixture.override = &font;
    css_stylesheet_params system_params = params;
    system_params.font = resolve_system_font;
    system_params.font_pw = &fixture;
    css_declaration_list *list = parse_system(&system_params, "font:menu!important;color:red", _i != 0);
    css_declaration_store *store;
    ck_assert_int_eq(css__declaration_store_create(list, &store), CSS_OK);
    css__declaration_list_destroy(list);
    unsigned calls = fixture.calls;
    fixture.error = CSS_BADPARM; /* Readback and non-font edits must not call it. */
    check_system_store(store, "font", "menu");
    check_system_store(store, "font-size", "16.25pt");
    check_system_store(store, "font-family", "\"System \\\"Font\\\"\\\\Family\"");
    check_system_store(store, NULL, "font: menu !important; color: red;");
    bool changed;
    css_declaration_assignment input = {"font-size", "18px", "important", 9, 4, 9};
    ck_assert_int_eq(css__declaration_store_set_property(store, &system_params, &input, &changed), CSS_OK);
    ck_assert(changed);
    check_system_store(store, "font", "oblique small-caps 600 18px / 125% \"System \\\"Font\\\"\\\\Family\"");
    check_system_store(store, NULL,
        "font: oblique small-caps 600 18px / 125% \"System \\\"Font\\\"\\\\Family\" !important; color: red;");
    input = (css_declaration_assignment){"font-weight", "normal", "", 11, 6, 0};
    ck_assert_int_eq(css__declaration_store_set_property(store, &system_params, &input, &changed), CSS_OK);
    ck_assert(changed);
    check_system_store(store, "font", "");
    check_system_store(store, NULL, "font-style: oblique !important; font-variant: small-caps !important; "
        "font-weight: normal; font-size: 18px !important; line-height: 125% !important; "
        "font-family: \"System \\\"Font\\\"\\\\Family\" !important; color: red;");
    /* The edited block must also be independently reparsable without a client
     * font callback, including escaping, priorities and percentage line-height. */
    lwc_string *text, *again;
    ck_assert_int_eq(css__declaration_store_serialize(store, &text), CSS_OK);
    list = parse(lwc_string_data(text), _i != 0);
    css_declaration_order *order;
    ck_assert_int_eq(css__declaration_order_create(list, &order), CSS_OK);
    ck_assert_int_eq(css__declaration_block_serialize(order, &again), CSS_OK);
    ck_assert_str_eq(lwc_string_data(text), lwc_string_data(again));
    lwc_string_unref(text);
    lwc_string_unref(again);
    css__declaration_order_destroy(order);
    css__declaration_list_destroy(list);
    input = (css_declaration_assignment){"font-weight", "600", "important", 11, 3, 9};
    ck_assert_int_eq(css__declaration_store_set_property(store, &system_params, &input, &changed), CSS_OK);
    ck_assert(changed);
    ck_assert_int_eq(css__declaration_store_remove(store, "font-style", 10, &changed), CSS_OK);
    ck_assert(changed);
    check_system_store(store, "font", "");
    check_system_store(store, NULL, "font-variant: small-caps !important; font-weight: 600 !important; "
        "font-size: 18px !important; line-height: 125% !important; "
        "font-family: \"System \\\"Font\\\"\\\\Family\" !important; color: red;");
    ck_assert_uint_eq(fixture.calls, calls);
    fixture.error = CSS_OK;
    input = (css_declaration_assignment){"font", "menu", "", 4, 4, 0};
    ck_assert_int_eq(css__declaration_store_set_property(store, &system_params, &input, &changed), CSS_OK);
    ck_assert(changed);
    ck_assert_int_eq(css__declaration_store_set_property(store, &system_params, &input, &changed), CSS_OK);
    ck_assert(!changed);
    font.size.size = INTTOFIX(20);
    ck_assert_int_eq(css__declaration_store_set_property(store, &system_params, &input, &changed), CSS_OK);
    ck_assert_msg(changed, "a changed resolver result cannot be skipped just because the keyword is unchanged");
    check_system_store(store, "font-size", "20pt");
    lwc_string_unref(fixture.family);
    fixture.family = NULL;
    check_system_store(store, "font-family", "\"System \\\"Font\\\"\\\\Family\"");
    css__declaration_store_destroy(store);
}
END_TEST

/* Invalid callback descriptors must fail before reaching bytecode or the
 * specified-value snapshot, rather than asserting on a non-length unit. */
START_TEST(test_system_font_invalid_result)
{
    struct system_font_fixture fixture = {0};
    ck_assert_int_eq(lwc_intern_string("System Font", 11, &fixture.family), lwc_error_ok);
    css_system_font font = {.style = CSS_FONT_STYLE_NORMAL, .variant = CSS_FONT_VARIANT_NORMAL,
        .weight = CSS_FONT_WEIGHT_NORMAL, .size = {INTTOFIX(12), CSS_UNIT_PX},
        .line_height = {INTTOFIX(16), CSS_UNIT_PX}, .family = fixture.family};
    switch (_i) {
    case 0:
        font.size.unit = CSS_UNIT_CALC;
        break;
    case 1:
        font.line_height.unit = CSS_UNIT_DEG;
        break;
    case 2:
        font.size.size = -1;
        break;
    case 3:
        font.line_height.size = -1;
        break;
    case 4:
        font.family = NULL;
        break;
    case 5:
        font.style = CSS_FONT_STYLE_INHERIT;
        break;
    case 6:
        font.variant = CSS_FONT_VARIANT_INHERIT;
        break;
    case 7:
        font.weight = CSS_FONT_WEIGHT_INHERIT;
        break;
    }
    fixture.override = &font;
    css_stylesheet_params system_params = params;
    system_params.font = resolve_system_font;
    system_params.font_pw = &fixture;
    css_declaration_list *list;
    ck_assert_int_eq(css__declaration_list_create(&system_params, &list), CSS_OK);
    const char *input = "font:menu";
    css_error error = css__declaration_list_append(list, (const uint8_t *)input, strlen(input));
    if (error == CSS_OK || error == CSS_NEEDDATA)
        error = css__declaration_list_complete(list);
    ck_assert_msg(error == CSS_BADPARM, "invalid system font variant %d returned %d", _i, error);
    ck_assert_ptr_null(css__declaration_list_sheet(list));
    css__declaration_list_destroy(list);
    lwc_string_unref(fixture.family);
}
END_TEST

#ifdef WISP_TEST_ALLOC_FAILURE
START_TEST(test_system_font_allocation_failure)
{
    void (*limit)(unsigned int) = _i == 0 ? malloc_limit : _i == 1 ? calloc_limit : realloc_limit;
    struct system_font_fixture fixture = {0};
    ck_assert_int_eq(lwc_intern_string("System Font", 11, &fixture.family), lwc_error_ok);
    css_stylesheet_params system_params = params;
    system_params.font = resolve_system_font;
    system_params.font_pw = &fixture;
    const char *input = "font:caption junk; font:menu; font:icon!important; color:red";
    css_declaration_list *warm = parse_system(&system_params, input, false);
    css__declaration_list_destroy(warm);
    bool succeeded = false;
    for (unsigned attempt = 0; attempt < 1024 && !succeeded; attempt++) {
        css_declaration_list *list = NULL;
        css_declaration_store *store = NULL;
        lwc_string *text = NULL;
        limit(attempt);
        css_error error = css__declaration_list_create(&system_params, &list);
        if (error == CSS_OK) {
            error = css__declaration_list_append(list, (const uint8_t *)input, strlen(input));
            if (error == CSS_OK || error == CSS_NEEDDATA)
                error = css__declaration_list_complete(list);
        }
        if (error == CSS_OK)
            error = css__declaration_store_create(list, &store);
        css__declaration_list_destroy(list);
        if (error == CSS_OK) {
            bool changed;
            /* Force scalar serialization of retained system-font components. */
            error = css__declaration_store_remove(store, "font-size", 9, &changed);
        }
        if (error == CSS_OK)
            error = css__declaration_store_serialize(store, &text);
        limit(UINT_MAX);
        ck_assert_msg(error == CSS_OK || error == CSS_NOMEM, "allocator %d, attempt %u: %d", _i, attempt, error);
        if (error == CSS_OK) {
            ck_assert_str_eq(lwc_string_data(text), "font-style: italic !important; font-variant: normal !important; "
                "font-weight: bold !important; line-height: 20px !important; font-family: \"System Font\" !important; "
                "color: red;");
            succeeded = true;
        } else {
            ck_assert_ptr_null(text);
        }
        if (text != NULL)
            lwc_string_unref(text);
        css__declaration_store_destroy(store);
    }
    ck_assert_msg(succeeded, "system font allocator %d never completed", _i);
    /* A failed snapshot copy must also leave an existing store unchanged. */
    css_declaration_list *list = parse_system(&system_params, "font:menu", false);
    css_declaration_store *store;
    ck_assert_int_eq(css__declaration_store_create(list, &store), CSS_OK);
    css__declaration_list_destroy(list);
    list = parse_system(&system_params, "font:icon", false);
    succeeded = false;
    for (unsigned attempt = 0; attempt < 256 && !succeeded; attempt++) {
        bool changed = true;
        limit(attempt);
        css_error error = css__declaration_store_set(store, css__declaration_list_get(list, 0), &changed);
        limit(UINT_MAX);
        if (error == CSS_OK) {
            ck_assert(changed);
            succeeded = true;
        } else {
            ck_assert_int_eq(error, CSS_NOMEM);
            ck_assert(!changed);
            check_system_store(store, "font", "menu");
            check_system_store(store, "font-size", "16px");
        }
    }
    ck_assert(succeeded);
    check_system_store(store, "font", "icon");
    css__declaration_list_destroy(list);
    css__declaration_store_destroy(store);
    lwc_string_unref(fixture.family);
}
END_TEST
#endif

START_TEST(test_font_callback_failure)
{
    struct system_font_fixture fixture = {.error = _i == 0 ? CSS_NOMEM : CSS_BADPARM};
    css_stylesheet_params system_params = params;
    system_params.font = resolve_system_font;
    system_params.font_pw = &fixture;
    css_stylesheet *sheet;
    ck_assert_int_eq(css_stylesheet_create(&system_params, &sheet), CSS_OK);
    css_style *style;
    ck_assert_int_eq(css__stylesheet_style_create(sheet, &style), CSS_OK);
    ck_assert_int_eq(css__stylesheet_style_append(style, 0x12345678), CSS_OK);
    css_language *language = sheet->parser_frontend;
    /* italic would otherwise begin an ordinary font shorthand. A client
     * failure cannot be silently treated as an unrecognized system font. */
    css_token token = {.type = CSS_TOKEN_IDENT, .idata = language->strings[ITALIC]};
    parserutils_vector *tokens;
    ck_assert_int_eq(parserutils_vector_create(sizeof(css_token), 4, &tokens), PARSERUTILS_OK);
    ck_assert_int_eq(parserutils_vector_append(tokens, &token), PARSERUTILS_OK);
    int32_t ctx = 0;
    ck_assert_int_eq(css__parse_font(language, tokens, &ctx, style), fixture.error);
    ck_assert_int_eq(ctx, 0);
    ck_assert_uint_eq(style->used, 1);
    ck_assert_uint_eq(style->bytecode[0], 0x12345678);
    parserutils_vector_destroy(tokens);
    css__stylesheet_style_destroy(style);
    css_stylesheet_destroy(sheet);
}
END_TEST

#ifdef WISP_TEST_ALLOC_FAILURE
/* Force growth at every emitted bytecode position, including size, defaults,
 * line-height, family termination, CSS-wide expansion and resolved system fonts. */
START_TEST(test_font_rollback)
{
    static const char *const forms[] = {"font:normal italic 16px/1.25 serif", "font:16px serif", "font:initial"};
    bool system = _i >= (int)N_ELEMENTS(forms);
    lwc_string *caption;
    ck_assert_int_eq(lwc_intern_string("caption", 7, &caption), lwc_error_ok);
    struct system_font_fixture fixture = {0};
    const char *family = _i == 4 ? "System Family" : "serif";
    ck_assert_int_eq(lwc_intern_string(family, strlen(family), &fixture.family), lwc_error_ok);
    css_stylesheet_params system_params = params;
    system_params.font = system ? resolve_system_font : NULL;
    system_params.font_pw = &fixture;
    css_stylesheet *sheet;
    ck_assert_int_eq(css_stylesheet_create(&system_params, &sheet), CSS_OK);
    css_language *language = sheet->parser_frontend;
    css_declaration_list *list = parse(system ? "font:initial" : forms[_i], false);
    const css_declaration *declaration = css__declaration_list_get(list, 0);
    parserutils_vector *tokens;
    ck_assert_int_eq(parserutils_vector_create(sizeof(css_token), 4, &tokens), PARSERUTILS_OK);
    for (size_t i = 0; i < declaration->token_count; i++) {
        css_token token = {.type = declaration->tokens[i].type, .idata = declaration->tokens[i].text};
        if (system)
            token.idata = caption;
        ck_assert_int_eq(parserutils_vector_append(tokens, &token), PARSERUTILS_OK);
    }
    /* First obtain the valid result and populate the system family's string
     * table. Later failures target style growth rather than string interning. */
    css_style *reference;
    int32_t ctx = 0;
    ck_assert_int_eq(css__stylesheet_style_create(sheet, &reference), CSS_OK);
    ck_assert_int_eq(css__parse_font(language, tokens, &ctx, reference), CSS_OK);
    for (unsigned spare = 0; spare < reference->used; spare++) {
        css_style *style;
        ck_assert_int_eq(css__stylesheet_style_create(sheet, &style), CSS_OK);
        uint32_t before = style->allocated - spare;
        for (uint32_t i = 0; i < before; i++)
            ck_assert_int_eq(css__stylesheet_style_append(style, 0x12345678), CSS_OK);
        ctx = 0;
        realloc_limit(0);
        css_error error = css__parse_font(language, tokens, &ctx, style);
        realloc_limit(UINT_MAX);
        ck_assert_msg(error == CSS_NOMEM, "font form %d spare %u: %d", _i, spare, error);
        ck_assert_int_eq(ctx, 0);
        ck_assert_uint_eq(style->used, before);
        for (uint32_t i = 0; i < before; i++)
            ck_assert_uint_eq(style->bytecode[i], 0x12345678);
        ck_assert_int_eq(css__parse_font(language, tokens, &ctx, style), CSS_OK);
        ck_assert_uint_eq(style->used, before + reference->used);
        ck_assert_int_eq(memcmp(style->bytecode + before, reference->bytecode,
            reference->used * sizeof(css_code_t)), 0);
        css__stylesheet_style_destroy(style);
    }
    css__stylesheet_style_destroy(reference);
    parserutils_vector_destroy(tokens);
    css__declaration_list_destroy(list);
    css_stylesheet_destroy(sheet);
    lwc_string_unref(fixture.family);
    lwc_string_unref(caption);
}
END_TEST
#endif

#ifdef WISP_TEST_ALLOC_FAILURE
START_TEST(test_gap_rollback)
{
    static const char *const forms[] = {"grid-gap:3px", "grid-gap:3px 4px", "grid-gap:normal", "grid-gap:initial"};
    css_declaration_list *list = parse(forms[_i], false);
    const css_declaration *declaration = css__declaration_list_get(list, 0);
    ck_assert_ptr_nonnull(declaration);
    css_stylesheet *sheet;
    ck_assert_int_eq(css_stylesheet_create(&params, &sheet), CSS_OK);
    parserutils_vector *tokens;
    ck_assert_int_eq(parserutils_vector_create(sizeof(css_token), 4, &tokens), PARSERUTILS_OK);
    for (size_t i = 0; i < declaration->token_count; i++) {
        css_token token = {.type = declaration->tokens[i].type, .idata = declaration->tokens[i].text};
        ck_assert_int_eq(parserutils_vector_append(tokens, &token), PARSERUTILS_OK);
    }
    unsigned words = _i < 2 ? 6 : 2;
    for (unsigned spare = 0; spare < words; spare++) {
        css_style *style;
        ck_assert_int_eq(css__stylesheet_style_create(sheet, &style), CSS_OK);
        uint32_t before = style->allocated - spare;
        for (uint32_t i = 0; i < before; i++)
            ck_assert_int_eq(css__stylesheet_style_append(style, 0x12345678), CSS_OK);
        int32_t ctx = 0;
        realloc_limit(0);
        css_error error = declaration->property->handler(sheet->parser_frontend, tokens, &ctx, style);
        realloc_limit(UINT_MAX);
        ck_assert_msg(error == CSS_NOMEM, "%s spare %u: %d", forms[_i], spare, error);
        ck_assert_int_eq(ctx, 0);
        ck_assert_uint_eq(style->used, before);
        for (uint32_t i = 0; i < before; i++)
            ck_assert_uint_eq(style->bytecode[i], 0x12345678);
        ck_assert_int_eq(declaration->property->handler(sheet->parser_frontend, tokens, &ctx, style), CSS_OK);
        ck_assert_uint_eq(style->used, before + words);
        css__stylesheet_style_destroy(style);
    }
    parserutils_vector_destroy(tokens);
    css_stylesheet_destroy(sheet);
    css__declaration_list_destroy(list);
}
END_TEST
#endif

#ifdef WISP_TEST_ALLOC_FAILURE
START_TEST(test_logical_pair_rollback)
{
    static const char *const forms[] = {"3px", "3px 4px", "initial", "inherit"};
    char css[96];
    unsigned variant = _i % N_ELEMENTS(forms);
    snprintf(css, sizeof(css), "%s:%s", logical_pairs[_i / N_ELEMENTS(forms)], forms[variant]);
    css_declaration_list *list = parse(css, false);
    const css_declaration *declaration = css__declaration_list_get(list, 0);
    ck_assert_ptr_nonnull(declaration);
    css_stylesheet *sheet;
    ck_assert_int_eq(css_stylesheet_create(&params, &sheet), CSS_OK);
    parserutils_vector *tokens;
    ck_assert_int_eq(parserutils_vector_create(sizeof(css_token), 4, &tokens), PARSERUTILS_OK);
    for (size_t i = 0; i < declaration->token_count; i++) {
        css_token token = {.type = declaration->tokens[i].type, .idata = declaration->tokens[i].text};
        ck_assert_int_eq(parserutils_vector_append(tokens, &token), PARSERUTILS_OK);
    }
    unsigned words = variant < 2 ? 6 : 2;
    for (unsigned spare = 0; spare < words; spare++) {
        css_style *style;
        ck_assert_int_eq(css__stylesheet_style_create(sheet, &style), CSS_OK);
        uint32_t before = style->allocated - spare;
        for (uint32_t i = 0; i < before; i++)
            ck_assert_int_eq(css__stylesheet_style_append(style, 0x12345678), CSS_OK);
        int32_t ctx = 0;
        realloc_limit(0);
        css_error error = declaration->property->handler(sheet->parser_frontend, tokens, &ctx, style);
        realloc_limit(UINT_MAX);
        ck_assert_msg(error == CSS_NOMEM, "%s spare %u: %d", css, spare, error);
        ck_assert_int_eq(ctx, 0);
        ck_assert_uint_eq(style->used, before);
        for (uint32_t i = 0; i < before; i++)
            ck_assert_uint_eq(style->bytecode[i], 0x12345678);
        ck_assert_int_eq(declaration->property->handler(sheet->parser_frontend, tokens, &ctx, style), CSS_OK);
        ck_assert_uint_eq(style->used, before + words);
        css__stylesheet_style_destroy(style);
    }
    parserutils_vector_destroy(tokens);
    css_stylesheet_destroy(sheet);
    css__declaration_list_destroy(list);
}
END_TEST
#endif

#ifdef WISP_TEST_ALLOC_FAILURE
START_TEST(test_position_rollback)
{
    css_stylesheet *sheet;
    ck_assert_int_eq(css_stylesheet_create(&params, &sheet), CSS_OK);
    css_language *language = sheet->parser_frontend;
    lwc_string *x, *y;
    ck_assert_int_eq(lwc_intern_string("2px", 3, &x), lwc_error_ok);
    ck_assert_int_eq(lwc_intern_string("3", 1, &y), lwc_error_ok);
    parserutils_vector *tokens;
    ck_assert_int_eq(parserutils_vector_create(sizeof(css_token), 4, &tokens), PARSERUTILS_OK);
    css_token parts[] = {{.type = CSS_TOKEN_DIMENSION, .idata = x}, {.type = CSS_TOKEN_S},
        {.type = CSS_TOKEN_PERCENTAGE, .idata = y}};
    for (size_t i = 0; i < N_ELEMENTS(parts); i++)
        ck_assert_int_eq(parserutils_vector_append(tokens, &parts[i]), PARSERUTILS_OK);
    css_prop_handler handler = _i == 0 ? css__parse_background_position : css__parse_object_position;
    unsigned emitted = _i == 0 ? 6 : 5;
    for (unsigned spare = 0; spare < emitted; spare++) {
        css_style *style;
        ck_assert_int_eq(css__stylesheet_style_create(sheet, &style), CSS_OK);
        uint32_t before = style->allocated - spare;
        for (uint32_t i = 0; i < before; i++)
            ck_assert_int_eq(css__stylesheet_style_append(style, 0x12345678), CSS_OK);
        int32_t ctx = 0;
        realloc_limit(0);
        css_error error = handler(language, tokens, &ctx, style);
        realloc_limit(UINT_MAX);
        ck_assert_msg(error == CSS_NOMEM, "position %d spare %u: %d", _i, spare, error);
        ck_assert_int_eq(ctx, 0);
        ck_assert_uint_eq(style->used, before);
        for (uint32_t i = 0; i < before; i++)
            ck_assert_uint_eq(style->bytecode[i], 0x12345678);
        ck_assert_int_eq(handler(language, tokens, &ctx, style), CSS_OK);
        ck_assert_uint_eq(style->used, before + emitted);
        css__stylesheet_style_destroy(style);
    }
    parserutils_vector_destroy(tokens);
    lwc_string_unref(x);
    lwc_string_unref(y);
    css_stylesheet_destroy(sheet);
}
END_TEST

/* Force either emitted word to allocate, for explicit, defaulted and CSS-wide
 * pairs. No failure may publish a partial shorthand or consume its tokens. */
START_TEST(test_pair_keyword_rollback)
{
    unsigned variant = _i % 3;
    bool white_space = _i >= 3;
    css_prop_handler handler = white_space ? css__parse_white_space : css__parse_place_content;
    css_stylesheet *sheet;
    ck_assert_int_eq(css_stylesheet_create(&params, &sheet), CSS_OK);
    css_language *language = sheet->parser_frontend;
    css_token parts[] = {{.type = CSS_TOKEN_IDENT, .idata = language->strings[white_space ? PRESERVE : CENTER]},
        {.type = CSS_TOKEN_S}, {.type = CSS_TOKEN_IDENT, .idata = language->strings[white_space ? NOWRAP : FLEX_END]}};
    if (variant == 2)
        parts[0].idata = language->strings[INITIAL];
    parserutils_vector *tokens;
    ck_assert_int_eq(parserutils_vector_create(sizeof(css_token), 4, &tokens), PARSERUTILS_OK);
    for (size_t i = 0; i < (variant == 0 ? N_ELEMENTS(parts) : 1); i++)
        ck_assert_int_eq(parserutils_vector_append(tokens, &parts[i]), PARSERUTILS_OK);
    for (unsigned spare = 0; spare < 2; spare++) {
        css_style *style;
        ck_assert_int_eq(css__stylesheet_style_create(sheet, &style), CSS_OK);
        uint32_t before = style->allocated - spare;
        for (uint32_t i = 0; i < before; i++)
            ck_assert_int_eq(css__stylesheet_style_append(style, 0x12345678), CSS_OK);
        int32_t ctx = 0;
        realloc_limit(0);
        css_error error = handler(language, tokens, &ctx, style);
        realloc_limit(UINT_MAX);
        ck_assert_msg(error == CSS_NOMEM, "pair-keyword case %d spare %u: %d", _i, spare, error);
        ck_assert_int_eq(ctx, 0);
        ck_assert_uint_eq(style->used, before);
        for (uint32_t i = 0; i < before; i++)
            ck_assert_uint_eq(style->bytecode[i], 0x12345678);
        ck_assert_int_eq(handler(language, tokens, &ctx, style), CSS_OK);
        ck_assert_uint_eq(style->used, before + 2);
        css__stylesheet_style_destroy(style);
    }
    parserutils_vector_destroy(tokens);
    css_stylesheet_destroy(sheet);
}
END_TEST

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
                      "color:hsl(none 34.5678912% 56% / .234567);border-color:#1234 hsl(12 none 56%) hwb(78 9% none);"
                      "clip:rect(.1234567px, auto, -1.2345678em, 1px);"
                      "list-style:none inside url(Marker.png);columns:.1234567em 3;"
                      "grid-area:2/3/span 4/5 span;grid-column:2 span/3;"
                      "place-content:center flex-end;background-position:top left,.1234567px -2.3456789%;"
                      "object-position:center -.1234567em;"
                      "background-repeat:repeat no-repeat, no-repeat repeat,repeat repeat;"
                      "text-decoration:blink overline underline;"
                      "transform:translate(.1234567em,2.3456789%) scale(12.3456789%,-.5678912) rotate(.1234567turn);"
                      "font-family:One/**/Two\\20 , 'a\\22 b\\a c',SERIF;";
    static const char *const names[] = {"margin", "--x", "background-image", "background-attachment", "border-style",
        "content", "quotes", "counter-reset", "aspect-ratio", "border-spacing", "clip", "font-family", "flex",
        "flex-flow", "color", "border-color", "list-style", "columns", "border", "border-left", "outline", "column-rule",
        "grid-area", "grid-column", "grid-row", "grid-row-end", "place-content", "background-position", "object-position",
        "background-repeat", "text-decoration", "background", "background-color", "font", "font-size", "line-height",
        "transform", "transform", "grid-template-columns", "grid-template-rows", "grid-auto-flow",
        "fill", "stroke", "fill-opacity", "stroke-opacity", "stroke-width", "cursor",
        "azimuth", "elevation", "pause-before", "pause-after", "pitch-range", "pitch", "play-during", "richness",
        "speech-rate", "stress", "voice-family", "volume",
        "cue", "pause", "color", "color", "color", "color", "color", "color"};
    const char *name = names[_i];
    void (*limit)(unsigned) = _i == 37 ? calloc_limit : malloc_limit;
    /* Keep full-border inputs separate: they reset the quad style/colour cases. */
    if (strcmp(name, "border") == 0 || strcmp(name, "border-left") == 0)
        css = "border:.1234567px solid rgb(1.2 3.4 5.6)";
    else if (strcmp(name, "outline") == 0)
        css = "outline:1.2345678em dotted #1234";
    else if (strcmp(name, "column-rule") == 0)
        css = "column-rule:.1234567px dashed hsl(12 34% 56%)";
    /* Exercise each background component through shorthand extraction, then
     * the same longhand serializer already covered by the ordinary inputs. */
    if (strncmp(name, "background", 10) == 0)
        css = "background:url('a,b\\\"c\\\\d\\1 e.png') no-repeat .1234567em -2.3456789% fixed,"
              "linear-gradient(45.1234567deg,hsl(none 34.5678912% 56% / .4) .1234567em,blue) repeat no-repeat top "
              "rgb(1.2 3.4 5.6 / .4)";
    if (strcmp(name, "font") == 0 || strcmp(name, "font-size") == 0 || strcmp(name, "line-height") == 0)
        css = "font:italic small-caps bold 1.2345678em / 1.3456789 One Two,serif";
    if (strncmp(name, "grid-template-", 14) == 0 || strcmp(name, "grid-auto-flow") == 0)
        css = "grid-template-columns:repeat(2,minmax(.1234567em,.7654321fr) 12.3456789%);"
              "grid-template-rows:minmax(0,123456789.1234567em) repeat(2,auto);grid-auto-flow:dense column";
    if (strcmp(name, "fill") == 0 || strncmp(name, "stroke", 6) == 0 || strcmp(name, "fill-opacity") == 0)
        css = "fill:hsl(none 34.5678912% 56% / .234567);stroke:rgb(1.2 3.4 5.6 / .4);"
              "fill-opacity:.1234567;stroke-opacity:.7654321;stroke-width:12.3456789em";
    if (strcmp(name, "cursor") == 0)
        css = "cursor:url('Cursor.cur'),url('Other.png'),POINTER";
    if (_i >= 47 && _i < 61)
        css = "azimuth:behind left;elevation:1.2345678deg;pause-before:123.456789ms;pause-after:1.2345678s;"
              "pitch-range:12.3456789;pitch:1.2345678khz;play-during:url('a b.ogg') repeat mix;richness:23.4567891;"
              "speak-header:ONCE;speak-numeral:DIGITS;speak-punctuation:CODE;speak:NONE;"
              "speech-rate:123.4567891;stress:34.5678912;voice-family:One Two,MALE;volume:45.6789123%;"
              "cue:url('a b.ogg') none;pause:123.456789ms 12.3456789%;";
    if (_i >= 61) {
        static const char *const colour_cases[] = {
            "color:lab(56.7891234% -123.456789% 66.88% / .23456789)",
            "color:lch(56.7891234% 12345678901234567890.1234567% -90deg / .23456789)",
            "color:oklab(56.7891234% -.000125% 123456789.1234567% / .23456789)",
            "color:oklch(none -12.34% none / none)",
            "color:color(srgb 1234567890123456789.123456789% -.00015% none / .123456789)",
            "color:rgb(123.456789 none 34% / none)",
        };
        css = colour_cases[_i - 61];
    }
    css_declaration_list *list = parse(css, false);
    css_declaration_order *order;
    ck_assert_int_eq(css__declaration_order_create(list, &order), CSS_OK);
    bool succeeded = false;
    unsigned failures = 0;
    for (unsigned attempt = 0; attempt < 200; attempt++) {
        lwc_string *value = (void *)1;
        limit(attempt);
        css_error error = serialize_property(order, name, &value);
        limit(UINT_MAX);
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


START_TEST(test_computed_fixed_serialization)
{
    const struct { css_fixed value; const char *text; } cases[] = {
        {0, "0"}, {F_1, "1"}, {F_1 / 2, "0.5"}, {1, "0.000977"},
        {-1, "-0.000977"}, {INT32_MIN, "-2097152"}, {INT32_MAX, "2097151.999023"}
    };
    for (unsigned i = 0; i < sizeof(cases) / sizeof(*cases); i++) {
        lwc_string *text = NULL;
        ck_assert_int_eq(css__declaration_fixed_serialize(cases[i].value, &text), CSS_OK);
        ck_assert_str_eq(lwc_string_data(text), cases[i].text);
        lwc_string_unref(text);
    }
}
END_TEST

START_TEST(test_cssom_pseudo_parser)
{
    const struct { const char *text; css_pseudo_element pseudo; } valid[] = {
        {":before", CSS_PSEUDO_ELEMENT_BEFORE}, {"::AFTER", CSS_PSEUDO_ELEMENT_AFTER},
        {"::b\\65 fore", CSS_PSEUDO_ELEMENT_BEFORE}, {"::/**/before", CSS_PSEUDO_ELEMENT_BEFORE},
        {"::first-line", CSS_PSEUDO_ELEMENT_FIRST_LINE}, {":first-letter", CSS_PSEUDO_ELEMENT_FIRST_LETTER},
        {"::marker", CSS_PSEUDO_ELEMENT_MARKER}, {"::placeholder ", CSS_PSEUDO_ELEMENT_PLACEHOLDER}
    };
    for (unsigned i = 0; i < sizeof(valid) / sizeof(*valid); i++) {
        css_pseudo_element result;
        ck_assert_msg(css_parse_pseudo_element((const uint8_t *)valid[i].text, strlen(valid[i].text), &result) == CSS_OK,
            "Rejected pseudo %s", valid[i].text);
        ck_assert_int_eq(result, valid[i].pseudo);
    }
    const char *invalid[] = {"", "before", ":marker", ":::before", ": before", "::before()", "::before junk",
        "::part(x)", "::slotted(*)", "::unknown", "::before::after", "::b\xc3\xa9" "fore"};
    for (unsigned i = 0; i < sizeof(invalid) / sizeof(*invalid); i++) {
        css_pseudo_element result;
        ck_assert_msg(css_parse_pseudo_element((const uint8_t *)invalid[i], strlen(invalid[i]), &result) == CSS_INVALID,
            "Accepted pseudo %s", invalid[i]);
        ck_assert_int_eq(result, CSS_PSEUDO_ELEMENT_COUNT);
    }
    css_pseudo_element result;
    const uint8_t embedded[] = ":before\0junk";
    ck_assert_int_eq(css_parse_pseudo_element(embedded, sizeof(embedded) - 1, &result), CSS_INVALID);
}
END_TEST

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
    tcase_add_loop_test(tc, test_font_prefix_bytecode, 0, 2);
    tcase_add_loop_test(tc, test_calc_grammar, 0, 2);
    tcase_add_loop_test(tc, test_calc_nesting_and_truncation, 0, 2);
    tcase_add_loop_test(tc, test_calc_property_ranges, 0, 2);
    tcase_add_test(tc, test_calc_readback_bounds);
    tcase_add_test(tc, test_calc_readback_locale);
    tcase_add_loop_test(tc, test_font_invalid_components, 0, 2);
    tcase_add_loop_test(tc, test_system_font_readback, 0, 2);
    tcase_add_loop_test(tc, test_system_font_editing, 0, 2);
    tcase_add_loop_test(tc, test_font_callback_failure, 0, 2);
    tcase_add_loop_test(tc, test_system_font_invalid_result, 0, 8);
    tcase_add_loop_test(tc, test_font_face_names, 0, 2);
    tcase_add_loop_test(tc, test_reserved_font_names, 0, 2);
    tcase_add_loop_test(tc, test_long_font_name, 0, 2);
    tcase_add_loop_test(tc, test_clip_separators, 0, 2);
    tcase_add_loop_test(tc, test_geometry_invalid_units, 0, 2);
    tcase_add_loop_test(tc, test_gradient_grammar, 0, 2);
    tcase_add_loop_test(tc, test_stroke_width_grammar, 0, 2);
    tcase_add_loop_test(tc, test_grid_value_grammar, 0, 2);
    tcase_add_loop_test(tc, test_grid_value_bytecode, 0, 2);
    tcase_add_loop_test(tc, test_transform_grammar, 0, 2);
    tcase_add_loop_test(tc, test_transform_bytecode, 0, 2);
    tcase_add_loop_test(tc, test_transform_many_functions, 0, 2);
    tcase_add_loop_test(tc, test_property_payload_important, 0, 2);
    tcase_add_loop_test(tc, test_geometry_bytecode, 0, 2);
    tcase_add_loop_test(tc, test_flex_bytecode, 0, 2);
    tcase_add_loop_test(tc, test_list_columns_bytecode, 0, 2);
    tcase_add_loop_test(tc, test_list_columns_invalid, 0, 2);
    tcase_add_loop_test(tc, test_border_shorthand_bytecode, 0, 2);
    tcase_add_loop_test(tc, test_border_shorthand_invalid, 0, 2);
    tcase_add_loop_test(tc, test_grid_placement_bytecode, 0, 2);
    tcase_add_loop_test(tc, test_grid_placement_invalid, 0, 2);
    tcase_add_loop_test(tc, test_position_bytecode, 0, 2);
    tcase_add_loop_test(tc, test_repeat_bytecode, 0, 2);
    tcase_add_loop_test(tc, test_background_readback, 0, 2);
    tcase_add_loop_test(tc, test_background_many_layers, 0, 2);
    tcase_add_test(tc, test_background_quirks);
    tcase_add_loop_test(tc, test_decoration_bytecode, 0, 2);
    tcase_add_loop_test(tc, test_repeat_decoration_invalid, 0, 2);
    tcase_add_loop_test(tc, test_position_invalid, 0, 2);
    tcase_add_loop_test(tc, test_white_space_invalid, 0, 2);
    tcase_add_loop_test(tc, test_logical_pair_invalid, 0, 2 * N_ELEMENTS(logical_pairs));
    tcase_add_loop_test(tc, test_place_content_bytecode, 0, 2);
    tcase_add_loop_test(tc, test_place_content_invalid, 0, 2);
    tcase_add_test(tc, test_quirky_border_values);
    tcase_add_loop_test(tc, test_colour_alpha_roundtrip, 0, 2);
    tcase_add_test(tc, test_quirky_colour_values);
    tcase_add_loop_test(tc, test_missing_colour_roundtrip, 0, 2);
    tcase_add_loop_test(tc, test_colour_rendering_values, 0, 2);
    tcase_add_loop_test(tc, test_missing_rgb_precision, 0, 2);
    tcase_add_loop_test(tc, test_extreme_polar_readback, 0, 2);
    tcase_add_loop_test(tc, test_colour_invalid, 0, 2);
    tcase_add_loop_test(tc, test_unsupported_colour_fallbacks, 0, 2);
    tcase_add_loop_test(tc, test_flex_invalid, 0, 2);
    tcase_add_loop_test(tc, test_long_url, 0, 2);
    tcase_add_loop_test(tc, test_overflow_axes, 0, 2);
    tcase_add_test(tc, test_value_arguments);
    tcase_add_test(tc, test_computed_fixed_serialization);
    tcase_add_test(tc, test_cssom_pseudo_parser);
#ifdef WISP_TEST_ALLOC_FAILURE
    tcase_add_loop_test(tc, test_value_allocation_failure, 0, 67);
    tcase_add_loop_test(tc, test_system_font_allocation_failure, 0, 3);
    tcase_add_loop_test(tc, test_calc_allocation_rollback, 0, 3);
    tcase_add_loop_test(tc, test_calc_readback_allocation_failure, 0, 3);
    tcase_add_loop_test(tc, test_position_rollback, 0, 2);
    tcase_add_loop_test(tc, test_font_rollback, 0, 5);
    tcase_add_loop_test(tc, test_gap_rollback, 0, 4);
    tcase_add_loop_test(tc, test_logical_pair_rollback, 0, 4 * N_ELEMENTS(logical_pairs));
    tcase_add_loop_test(tc, test_pair_keyword_rollback, 0, 6);
    tcase_add_loop_test(tc, test_grid_placement_rollback, 0, 7);
    tcase_add_loop_test(tc, test_font_face_allocation_failure, 0, 3);
    tcase_add_test(tc, test_content_terminator_allocation_failure);
#endif
    tcase_add_loop_test(tc, test_declaration_order, 0, ORDER_CASE_COUNT);
    tcase_add_test(tc, test_property_metadata);
    tcase_add_loop_test(tc, test_aggregate_longhand_parsers, 0, 2);
    tcase_add_loop_test(tc, test_alias_bytecode, 0, 2);
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

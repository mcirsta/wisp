/* Accepted inline declarations must retain parser output without changing the
 * compiled style. This exercises the real generated and manual property parsers. */
#include <check.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "parse/declarations.h"
#include "parse/language.h"
#include "stylesheet.h"

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
    {"opacity:.5; @import 'no.css'; height:2px", {"opacity", "height"}, 0},
    {"font-family:ubuntu,urban; --u:U+400-4FF; --fn:u(red)", {"font-family", "--u", "--fn"}, 0},
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
    {"u", CSS_TOKEN_IDENT, "u"},
    {"ubuntu", CSS_TOKEN_IDENT, "ubuntu"},
    {"urban", CSS_TOKEN_IDENT, "urban"},
    {"u(", CSS_TOKEN_FUNCTION, "u"},
    {"U+400-4FF", CSS_TOKEN_UNICODE_RANGE, "400-4FF"},
    {"--\\54 one", CSS_TOKEN_CUSTOM_PROPERTY, "--Tone"},
    {"--\\Theme", CSS_TOKEN_CUSTOM_PROPERTY, "--Theme"},
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

#ifdef WISP_TEST_ALLOC_FAILURE
START_TEST(test_allocation_failure)
{
    void (*limit)(unsigned int) = _i == 0 ? malloc_limit : _i == 1 ? calloc_limit : realloc_limit;
    const char *declarations = "color:red; width:2px!important; --Tone:green; margin:var(--space, 1px 2px);"
                               "padding:1em; display:flex; height:3px; opacity:.5; color:blue; --extra:;"
                               /* Force the parser's token vector to grow too. */
                               "--large:red red red red red red red red red red red red red red red red "
                               "red red red red red red red red red red red red red red red red red red;"
                               "content:'a\\\r\nb';";
    /* Grow the captured-source buffer after a retained declaration exists. */
    char css[16384];
    size_t pos = (size_t)sprintf(css, "opacity:1; /*");
    memset(css + pos, 'c', 8192);
    pos += 8192;
    snprintf(css + pos, sizeof(css) - pos, "*/%s", declarations);
    const size_t expected = 13;
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
    tcase_add_test(tc, test_owned_tokens);
    tcase_add_test(tc, test_lifecycle);
    tcase_add_test(tc, test_growth);
#ifdef WISP_TEST_ALLOC_FAILURE
    tcase_add_loop_test(tc, test_allocation_failure, 0, 3);
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

/* Parsed-selector scope, all origins/imports, and conservative future fallback. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <libcss/select.h>
#include "select/scope.h"

static css_error resolve_url(void *pw, const char *base, lwc_string *rel, lwc_string **abs)
{
    (void)pw;
    (void)base;
    *abs = lwc_string_ref(rel);
    return CSS_OK;
}

static css_stylesheet *sheet(const char *text, bool finish, css_error expected)
{
    css_stylesheet_params params = {.params_version = CSS_STYLESHEET_PARAMS_VERSION_1,
        .level = CSS_LEVEL_DEFAULT, .charset = "UTF-8", .url = "checked-scope", .resolve = resolve_url};
    css_stylesheet *result;
    assert(css_stylesheet_create(&params, &result) == CSS_OK);
    css_error error = css_stylesheet_append_data(result, (const uint8_t *)text, strlen(text));
    assert(error == CSS_OK || error == CSS_NEEDDATA);
    if (finish)
        assert(css_stylesheet_data_done(result) == expected);
    return result;
}

static void expect(css_select_ctx *ctx, bool expected, const char *description)
{
    bool safe = !expected;
    assert(css_select_ctx_checked_parent_scope(ctx, &safe, NULL) == CSS_OK);
    if (safe != expected) {
        fprintf(stderr, "%s: expected checked parent scope %d, got %d\n", description, expected, safe);
        assert(safe == expected);
    }
}

static void test_selectors(void)
{
    const struct { const char *text; bool safe; } cases[] = {
        {"input:checked { color:red }", true},
        {"input:not(:checked) + label { color:red }", true},
        {"body .sidebar > input:checked ~ .panel .child::before { content:'x' }", true},
        {"input:checked + label + .panel { --color:red }", true},
        {"input:checked ~ .panel :not([hidden]) { color:var(--color) }", true},
        {"input:CHECKED ~ div::after { content:'checked' }", true},
        {"input:checked ~ :nth-child(2n+1 of .panel) { color:red }", true},
        {"input:checked ~ :nth-last-child(2 of .panel) { color:red }", true},
        {"input:checked ~ :nth-of-type(2) { color:red }", true},
        {":root :lang(en) input:checked ~ .panel { color:red }", true},
        {"@media print { input:not(:checked) ~ .panel { color:red } }", true},
        {"@layer tabs { @supports (display:block) { input:checked + label { color:red } } }", true},
        /* :host is parser-accepted but not classified as a local dependency. */
        {":host { color:red } input:checked + label { color:blue }", false},
        {"input:not(:host) + label { color:red }", false},
        {"@media print { :host { color:red } }", false},
        {"@layer future { :host { color:red } }", false},
        {"@supports (display:block) { :host { color:red } }", false},
    };
    css_select_ctx *ctx;
    assert(css_select_ctx_create(&ctx) == CSS_OK);
    expect(ctx, true, "empty context");
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        css_stylesheet *s = sheet(cases[i].text, true, CSS_OK);
        assert(s->rule_count > 0); /* A discarded rule must not make this pass. */
        assert(css_select_ctx_append_sheet(ctx, s, CSS_ORIGIN_AUTHOR, NULL) == CSS_OK);
        expect(ctx, cases[i].safe, cases[i].text);
        assert(css_select_ctx_remove_sheet(ctx, s) == CSS_OK);
        assert(css_stylesheet_destroy(s) == CSS_OK);
    }
    css_select_ctx_destroy(ctx);
}

static void test_imports_and_context_changes(void)
{
    css_select_ctx *ctx;
    css_stylesheet *a = sheet("@import 'b.css' print; input:checked + label { color:red }", true, CSS_IMPORTS_PENDING);
    css_stylesheet *b = sheet("@import 'c.css';", true, CSS_IMPORTS_PENDING);
    css_stylesheet *c = sheet("input:not(:checked) ~ div { color:blue }", false, CSS_OK);
    assert(css_select_ctx_create(&ctx) == CSS_OK);
    assert(css_select_ctx_append_sheet(ctx, a, CSS_ORIGIN_AUTHOR, NULL) == CSS_OK);
    expect(ctx, false, "missing import, even in another medium");
    assert(css_stylesheet_register_import(a, b) == CSS_OK);
    expect(ctx, false, "missing transitive import");
    assert(css_stylesheet_register_import(b, c) == CSS_OK);
    expect(ctx, false, "import still being parsed");
    assert(css_stylesheet_data_done(c) == CSS_OK);
    expect(ctx, true, "all imports complete");

    css_stylesheet *unsafe = sheet(":host { color:red }", true, CSS_OK);
    const css_origin origins[] = {CSS_ORIGIN_UA, CSS_ORIGIN_USER, CSS_ORIGIN_AUTHOR};
    for (size_t i = 0; i < sizeof(origins) / sizeof(origins[0]); i++) {
        assert(css_select_ctx_insert_sheet(ctx, unsafe, 0, origins[i], "print") == CSS_OK);
        expect(ctx, false, "unsupported selector in any origin/media");
        assert(css_stylesheet_set_disabled(unsafe, true) == CSS_OK);
        expect(ctx, false, "disabled sheet conservatively included");
        assert(css_select_ctx_remove_sheet(ctx, unsafe) == CSS_OK);
        expect(ctx, true, "removed sheet must not leave a cached context decision");
    }

    css_stylesheet *late = sheet("@import 'unsafe.css';", true, CSS_IMPORTS_PENDING);
    assert(css_select_ctx_append_sheet(ctx, late, CSS_ORIGIN_AUTHOR, NULL) == CSS_OK);
    expect(ctx, false, "late pending import");
    assert(css_stylesheet_register_import(late, unsafe) == CSS_OK);
    expect(ctx, false, "late imported unsupported selector");
    css_select_ctx_destroy(ctx);
    css_stylesheet_destroy(late);
    css_stylesheet_destroy(a);
    css_stylesheet_destroy(b);
    css_stylesheet_destroy(c);
    css_stylesheet_destroy(unsafe);

    a = sheet("@import 'b.css';", true, CSS_IMPORTS_PENDING);
    b = sheet("@import 'a.css';", true, CSS_IMPORTS_PENDING);
    assert(css_stylesheet_register_import(a, b) == CSS_OK);
    assert(css_stylesheet_register_import(b, a) == CSS_OK);
    assert(css_select_ctx_create(&ctx) == CSS_OK);
    assert(css_select_ctx_append_sheet(ctx, a, CSS_ORIGIN_AUTHOR, NULL) == CSS_OK);
    expect(ctx, false, "cyclic import graph");
    css_select_ctx_destroy(ctx);
    css_stylesheet_destroy(a);
    css_stylesheet_destroy(b);
}

static void test_future_representations(void)
{
    css_stylesheet *s = sheet("input:checked { color:red }", true, CSS_OK);
    css_selector *selector = ((css_rule_selector *)s->rule_list)->selectors[0];
    css_selector_detail original = selector->data;
    selector->data.comb = 7;
    assert(!css__selector_checked_parent_scope(s, selector));
    selector->data = original;
    selector->data.type = 15;
    assert(!css__selector_checked_parent_scope(s, selector));
    selector->data = original;
    selector->data.value_type = 3;
    assert(!css__selector_checked_parent_scope(s, selector));
    selector->data = original;
    css_stylesheet_destroy(s);
}

static void test_revisions(void)
{
    css_select_ctx *ctx;
    css_stylesheet *a = sheet("@import 'b.css';", true, CSS_IMPORTS_PENDING);
    css_stylesheet *b = sheet("input:checked + label { color:red }", true, CSS_OK);
    assert(css_stylesheet_register_import(a, b) == CSS_OK);
    assert(css_select_ctx_create(&ctx) == CSS_OK);
    assert(css_select_ctx_append_sheet(ctx, a, CSS_ORIGIN_AUTHOR, NULL) == CSS_OK);
    uint64_t first, next;
    bool safe;
    assert(css_select_ctx_checked_parent_scope(ctx, &safe, &first) == CSS_OK && safe);
    assert(css_select_ctx_checked_parent_scope(ctx, &safe, &next) == CSS_OK && safe && first == next);
    assert(css_stylesheet_set_disabled(b, true) == CSS_OK);
    assert(css_select_ctx_checked_parent_scope(ctx, &safe, &next) == CSS_OK && safe && next > first);
    first = next;
    assert(css_stylesheet_set_disabled(b, false) == CSS_OK);
    assert(css_select_ctx_checked_parent_scope(ctx, &safe, &next) == CSS_OK && safe && next > first);
    first = next;
    assert(css_select_ctx_remove_sheet(ctx, a) == CSS_OK);
    assert(css_select_ctx_checked_parent_scope(ctx, &safe, &next) == CSS_OK && safe && next > first);
    css_select_ctx_destroy(ctx);
    css_stylesheet_destroy(a);
    css_stylesheet_destroy(b);
}

int main(void)
{
    bool safe;
    assert(css_select_ctx_checked_parent_scope(NULL, &safe, NULL) == CSS_BADPARM);
    test_selectors();
    test_imports_and_context_changes();
    test_future_representations();
    test_revisions();
    puts("PASS");
    return 0;
}

/* CSSOM 6.6.1 native declaration editing: specification-defined assignments,
 * removals, logical ordering and ownership. No browser reference baselines. */
#include <check.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "parse/declaration_store.h"
#include "parse/property.h"

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

static const css_stylesheet_params input_params = {.params_version = CSS_STYLESHEET_PARAMS_VERSION_1,
    .level = CSS_LEVEL_DEFAULT,
    .url = "https://example.org/",
    .resolve = resolve_url};

static css_declaration_assignment assignment(const char *property, const char *value, const char *priority)
{
    return (css_declaration_assignment){property, value, priority, strlen(property), strlen(value), strlen(priority)};
}

static css_declaration_list *parse(const char *css, bool chunks)
{
    css_stylesheet_params params = {.params_version = CSS_STYLESHEET_PARAMS_VERSION_1,
        .level = CSS_LEVEL_DEFAULT,
        .url = "https://example.org/",
        .resolve = resolve_url};
    css_declaration_list *list;
    ck_assert_int_eq(css__declaration_list_create(&params, &list), CSS_OK);
    size_t length = strlen(css);
    for (size_t pos = 0; pos < length;) {
        size_t n = chunks ? 1 : length;
        css_error error = css__declaration_list_append(list, (const uint8_t *)css + pos, n);
        ck_assert_msg(error == CSS_OK || error == CSS_NEEDDATA, "parse %s: %d", css, error);
        pos += n;
    }
    ck_assert_int_eq(css__declaration_list_complete(list), CSS_OK);
    return list;
}

static css_declaration_store *create(const char *css, bool chunks)
{
    css_declaration_list *list = parse(css, chunks);
    css_declaration_store *store;
    ck_assert_int_eq(css__declaration_store_create(list, &store), CSS_OK);
    css__declaration_list_destroy(list);
    return store;
}

static bool set(css_declaration_store *store, const char *css, bool chunks)
{
    css_declaration_list *list = parse(css, chunks);
    ck_assert_uint_eq(css__declaration_list_count(list), 1);
    bool changed;
    ck_assert_int_eq(css__declaration_store_set(store, css__declaration_list_get(list, 0), &changed), CSS_OK);
    css__declaration_list_destroy(list);
    return changed;
}

static void check_text(const css_declaration_store *store, const char *expected)
{
    lwc_string *text;
    ck_assert_int_eq(css__declaration_store_serialize(store, &text), CSS_OK);
    ck_assert_msg(strcmp(lwc_string_data(text), expected) == 0, "expected %s\ngot %s", expected, lwc_string_data(text));
    lwc_string_unref(text);
}

static void check_value(const css_declaration_store *store, const char *name, const char *expected, bool priority)
{
    lwc_string *text;
    ck_assert_int_eq(css__declaration_store_value(store, name, strlen(name), &text), CSS_OK);
    ck_assert_msg(
        strcmp(lwc_string_data(text), expected) == 0, "%s: expected %s, got %s", name, expected, lwc_string_data(text));
    lwc_string_unref(text);
    bool important;
    ck_assert_int_eq(css__declaration_store_priority(store, name, strlen(name), &important), CSS_OK);
    ck_assert_msg(important == priority, "%s: expected priority %d, got %d", name, priority, important);
}

static void check_order(const css_declaration_store *store, const char *names)
{
    for (size_t i = 0; i < css__declaration_store_count(store); i++) {
        const css_declaration_contribution *value = css__declaration_store_get(store, i);
        size_t n = strlen(value->name);
        ck_assert_msg(strncmp(value->name, names, n) == 0 && (names[n] == ' ' || names[n] == '\0'),
            "item %zu: got %s, expected remaining %s", i, value->name, names);
        names += n;
        if (*names == ' ')
            names++;
        /* Owned values have no association with a discarded compiled sheet. */
        ck_assert_uint_eq(value->source->bytecode_length, 0);
    }
    ck_assert_str_eq(names, "");
    ck_assert_ptr_null(css__declaration_store_get(store, css__declaration_store_count(store)));
}

static const struct {
    const char *initial, *assignment, *expected, *order;
} assignments[] = {
    {"font:italic small-caps 16px/1.5 serif;color:red", "font-variant-caps:normal",
        "font: italic 16px / 1.5 serif; color: red;",
        "font-style font-variant-caps font-weight font-size line-height font-family color"},
    {"font-variant-caps:normal;color:red", "font-variant:small-caps!important",
        "font-variant: small-caps !important; color: red;", "font-variant-caps color"},
    {"text-decoration:underline!important;color:red", "text-decoration-line:overline",
        "text-decoration: overline; color: red;", "text-decoration-line color"},
    {"text-decoration-line:overline!important;color:red", "text-decoration:underline",
        "text-decoration: underline; color: red;", "text-decoration-line color"},
    {"font-variant:var(--Caps);text-decoration:var(--Lines);color:red", "opacity:.5",
        "font-variant: var(--Caps); text-decoration: var(--Lines); color: red; opacity: 0.5;",
        "font-variant-caps text-decoration-line color opacity"},
    {"font-variant-caps:var(--Caps);text-decoration-line:var(--Lines);color:red", "opacity:.5",
        "font-variant-caps: var(--Caps); text-decoration-line: var(--Lines); color: red; opacity: 0.5;",
        "font-variant-caps text-decoration-line color opacity"},
    {"", "color:red", "color: red;", "color"},
    {"width:1px;color:red;width:2px", "width:3px", "color: red; width: 3px;", "color width"},
    {"width:1px;color:red", "width:2px", "width: 2px; color: red;", "width color"},
    {"width:1px!important;color:red", "WIDTH:2px", "width: 2px; color: red;", "width color"},
    {"width:1px;color:red", "width:2px!important", "width: 2px !important; color: red;", "width color"},
    {"margin:1px!important;color:red", "margin-left:2px",
        "margin-top: 1px !important; margin-right: 1px !important; margin-bottom: 1px !important; "
        "margin-left: 2px; color: red;",
        "margin-top margin-right margin-bottom margin-left color"},
    {"margin:1px!important;color:red", "margin:2px", "margin: 2px; color: red;",
        "margin-top margin-right margin-bottom margin-left color"},
    {"margin-left:3px;color:red", "margin:2px", "margin: 2px; color: red;",
        "margin-left color margin-top margin-right margin-bottom"},
    {"margin-top:1px;margin-block-start:2px;color:red", "margin-top:3px",
        "margin-block-start: 2px; color: red; margin-top: 3px;", "margin-block-start color margin-top"},
    {"margin-top:1px;margin-block-start:2px;color:red", "margin-top:1px",
        "margin-block-start: 2px; color: red; margin-top: 1px;", "margin-block-start color margin-top"},
    {"margin-block-start:2px;margin-top:1px;color:red", "margin-block-start:3px",
        "margin-top: 1px; color: red; margin-block-start: 3px;", "margin-top color margin-block-start"},
    {"margin-top:1px;padding-inline:2px;color:red", "margin-top:3px",
        "margin-top: 3px; padding-inline: 2px; color: red;",
        "margin-top padding-inline-start padding-inline-end color"},
    {"margin:1px;margin-inline:2px;color:red", "margin:3px", "margin-inline: 2px; color: red; margin: 3px;",
        "margin-inline-start margin-inline-end color margin-top margin-right margin-bottom margin-left"},
    {"margin-inline:2px;margin:1px;color:red", "margin-inline:3px", "margin: 1px; color: red; margin-inline: 3px;",
        "margin-top margin-right margin-bottom margin-left color margin-inline-start margin-inline-end"},
    {"width:10px;inline-size:12px;color:red", "width:15px", "inline-size: 12px; color: red; width: 15px;",
        "inline-size color width"},
    {"top:1px;inset-inline:2px;color:red", "top:3px", "inset-inline: 2px; color: red; top: 3px;",
        "inset-inline-start inset-inline-end color top"},
    {"grid-gap:1px;color:red", "GRID-ROW-GAP:2px", "gap: 2px 1px; color: red;", "row-gap column-gap color"},
    {"--Tone:Old!important;--tone:other;color:red", "--Tone:MiXeD/**/new",
        "--Tone: MiXeD/**/new; --tone: other; color: red;", "--Tone --tone color"},
    {"--a\\:b:old;--other:two", "--a\\:b: new", "--a\\:b: new; --other: two;", "--a:b --other"},
    {"margin:var(--x)!important;color:red", "margin-left:2px",
        "margin-top:  !important; margin-right:  !important; margin-bottom:  !important; margin-left: 2px; color: red;",
        "margin-top margin-right margin-bottom margin-left color"},
    {"margin:var(--x);margin-left:2px;color:red", "margin:var(--x)", "margin: var(--x); color: red;",
        "margin-top margin-right margin-bottom margin-left color"},
    {"background:red;color:blue", "background-image:url(a)", "background: url(\"a\") red; color: blue;",
        "background-image background-position background-repeat-x background-repeat-y "
        "background-attachment background-color color"},
    {"color:red", "--empty: ", "color: red; --empty: ;", "color --empty"},
};
enum { ASSIGNMENT_COUNT = sizeof(assignments) / sizeof(assignments[0]) };

START_TEST(test_assignments)
{
    size_t index = (size_t)_i % ASSIGNMENT_COUNT;
    bool chunks = _i >= ASSIGNMENT_COUNT;
    css_declaration_store *store = create(assignments[index].initial, chunks);
    ck_assert_msg(set(store, assignments[index].assignment, chunks), "assignment %zu was not applied", index);
    check_text(store, assignments[index].expected);
    check_order(store, assignments[index].order);
    ck_assert_msg(!set(store, assignments[index].assignment, chunks), "exact repeat %zu wasn't a no-op", index);
    check_text(store, assignments[index].expected);
    css__declaration_store_destroy(store);
}
END_TEST

static const struct {
    const char *initial, *name, *old_value, *expected;
    bool important, changed;
} removals[] = {
    {"font-variant:small-caps!important;color:red", "font-variant-caps", "small-caps", "color: red;", true, true},
    {"font-variant-caps:small-caps!important;color:red", "font-variant", "small-caps", "color: red;", true, true},
    {"font:italic small-caps 16px serif;color:red", "font-variant-caps", "small-caps",
        "font-style: italic; font-weight: normal; font-size: 16px; line-height: normal; font-family: serif; color: red;",
        false, true},
    {"text-decoration:underline!important;color:red", "text-decoration-line", "underline", "color: red;", true, true},
    {"text-decoration-line:underline!important;color:red", "text-decoration", "underline", "color: red;", true, true},
    {"font-variant:var(--Caps);color:red", "font-variant-caps", "", "color: red;", false, true},
    {"text-decoration:var(--Lines);color:red", "text-decoration-line", "", "color: red;", false, true},
    {"", "width", "", "", false, false},
    {"width:1px;color:red", "WIDTH", "1px", "color: red;", false, true},
    {"width:1px!important;color:red", "w\\69 dth", "", "width: 1px !important; color: red;", false, false},
    {"margin:1px!important;color:red", "MARGIN", "1px", "color: red;", true, true},
    {"margin:1px!important;margin-left:2px;color:red", "margin", "1px", "color: red;", true, true},
    {"margin:1px;margin-left:2px!important;color:red", "margin", "", "color: red;", false, true},
    {"margin-left:2px!important;color:red", "margin", "", "color: red;", false, true},
    {"margin:var(--x);margin-left:2px;color:red", "margin", "", "color: red;", false, true},
    {"margin:var(--x)!important", "margin-left", "",
        "margin-top:  !important; margin-right:  !important; "
        "margin-bottom:  !important;",
        true, true},
    {"margin-inline:1px 2px!important;color:red", "MARGIN-INLINE-START", "1px",
        "margin-inline-end: 2px !important; color: red;", true, true},
    {"grid-gap:2px!important;color:red", "GRID-GAP", "2px", "color: red;", true, true},
    {"--Tone: MiXeD/**/Tokens!important;--tone:other", "--Tone", "MiXeD/**/Tokens", "--tone: other;", true, true},
    {"--Tone:MiXeD", "--tone", "", "--Tone: MiXeD;", false, false},
    {"--a\\:b:one;--other:two", "--a:b", "one", "--other: two;", false, true},
    {"--a\\:b:one", "--a\\:b", "", "--a\\:b: one;", false, false},
};
enum { REMOVAL_COUNT = sizeof(removals) / sizeof(removals[0]) };

START_TEST(test_removals)
{
    const size_t index = (size_t)_i % REMOVAL_COUNT;
    css_declaration_store *store = create(removals[index].initial, _i >= REMOVAL_COUNT);
    check_value(store, removals[index].name, removals[index].old_value, removals[index].important);
    bool changed;
    ck_assert_int_eq(
        css__declaration_store_remove(store, removals[index].name, strlen(removals[index].name), &changed), CSS_OK);
    ck_assert_msg(changed == removals[index].changed, "removal %zu: unexpected changed flag", index);
    check_text(store, removals[index].expected);
    check_value(store, removals[index].name, "", false);
    ck_assert_int_eq(
        css__declaration_store_remove(store, removals[index].name, strlen(removals[index].name), &changed), CSS_OK);
    ck_assert(!changed);
    css__declaration_store_destroy(store);
}
END_TEST

START_TEST(test_shorthand_catalogue)
{
    size_t count;
    const struct css_prop_entry *const *properties = css_prop_shorthands(&count);
    for (size_t i = 0; i < count; i++) {
        const struct css_prop_entry *property = properties[i];
        char css[128], expected[128];
        snprintf(css, sizeof(css), "%s:INITIAL!important", property->name);
        css_declaration_store *store = create("color:red!important", false);
        ck_assert(set(store, css, false));
        check_value(store, property->name, "initial", true);
        for (size_t j = 0; j < property->longhand_count; j++)
            check_value(store, property->longhands[j], "initial", true);
        snprintf(css, sizeof(css), "%s:var(--v)", property->name);
        ck_assert(set(store, css, false));
        check_value(store, property->name, "var(--v)", false);
        snprintf(expected, sizeof(expected), "color: red !important; %s: var(--v);", property->name);
        check_text(store, expected);
        bool changed;
        ck_assert_int_eq(
            css__declaration_store_remove(store, property->name, strlen(property->name), &changed), CSS_OK);
        ck_assert(changed);
        check_text(store, "color: red !important;");
        css__declaration_store_destroy(store);
    }
}
END_TEST

START_TEST(test_aural_values_survive_edits)
{
    const char *values = "azimuth: 220deg; elevation: 0deg; pitch-range: 55; pitch: 1.5khz; "
        "play-during: url(\"Theme.wav\") mix repeat; richness: 60; speak-header: always; "
        "speak-numeral: digits; speak-punctuation: code; speak: spell-out; speech-rate: 150; "
        "stress: 40; voice-family: \"Voice Name\", female; volume: 25%; ";
    char initial[768], expected[768];
    snprintf(initial, sizeof(initial), "%spause:1000ms 2000ms!important;cue:url(Before.wav) url(After.wav);"
        "opacity:1", values);
    css_declaration_store *store = create(initial, _i != 0);
    ck_assert(set(store, "opacity:.5", _i != 0));
    snprintf(expected, sizeof(expected), "%spause: 1s 2s !important; cue: url(\"Before.wav\") "
        "url(\"After.wav\"); opacity: 0.5;", values);
    check_text(store, expected);
    ck_assert(set(store, "pause-before:500ms", _i != 0));
    ck_assert(set(store, "cue-after:none", _i != 0));
    check_value(store, "pause-before", "0.5s", false);
    check_value(store, "pause-after", "2s", true);
    check_value(store, "cue-before", "url(\"Before.wav\")", false);
    check_value(store, "cue-after", "none", false);
    snprintf(expected, sizeof(expected), "%spause-before: 0.5s; pause-after: 2s !important; "
        "cue: url(\"Before.wav\") none; opacity: 0.5;", values);
    check_text(store, expected);
    bool changed;
    ck_assert_int_eq(css__declaration_store_remove(store, "pause-before", 12, &changed), CSS_OK);
    ck_assert(changed);
    ck_assert_int_eq(css__declaration_store_remove(store, "cue-after", 9, &changed), CSS_OK);
    ck_assert(changed);
    snprintf(expected, sizeof(expected), "%spause-after: 2s !important; cue-before: url(\"Before.wav\"); "
        "opacity: 0.5;", values);
    check_text(store, expected);
    css__declaration_store_destroy(store);
}
END_TEST

START_TEST(test_lab_values_survive_edits)
{
    css_declaration_store *store = create("background:lab(56.2% 0 66.88%);"
        "border:2px solid oklch(54% 25% -90deg)!important;color:lch(none -1 none / none);opacity:1", _i != 0);
    ck_assert(set(store, "opacity:.5", _i != 0));
    check_value(store, "background-color", "lab(56.2 0 83.6)", false);
    check_value(store, "border-color", "oklch(0.54 0.1 270)", true);
    check_value(store, "color", "lch(none 0 none / none)", false);
    ck_assert(set(store, "border-left-color:lab(50 0 0)", _i != 0));
    check_value(store, "border-left-color", "lab(50 0 0)", false);
    check_value(store, "border-right-color", "oklch(0.54 0.1 270)", true);
    css__declaration_store_destroy(store);
}
END_TEST

START_TEST(test_srgb_values_survive_edits)
{
    css_declaration_store *store = create("background:linear-gradient(rgb(none 255 0),color(srgb 0 0 1)) "
        "color(srgb -10% .5 1);border:2px solid rgb(255 none 0)!important;opacity:1", _i != 0);
    ck_assert(set(store, "opacity:.5", _i != 0));
    check_value(store, "background",
        "linear-gradient(color(srgb none 1 0), color(srgb 0 0 1)) color(srgb -0.1 0.5 1)", false);
    check_value(store, "border-color", "color(srgb 1 none 0)", true);
    ck_assert(set(store, "border-left-color:color(srgb .2 .4 .6 / none)", _i != 0));
    check_value(store, "border-left-color", "color(srgb 0.2 0.4 0.6 / none)", false);
    check_value(store, "border-right-color", "color(srgb 1 none 0)", true);
    /* Reparse cssText after unrelated edits: missing and extended coordinates
     * must survive the route that will synchronize element.style with the DOM. */
    lwc_string *text;
    ck_assert_int_eq(css__declaration_store_serialize(store, &text), CSS_OK);
    css_declaration_store *copy = create(lwc_string_data(text), _i != 0);
    check_text(copy, lwc_string_data(text));
    check_value(copy, "background-color", "color(srgb -0.1 0.5 1)", false);
    check_value(copy, "border-right-color", "color(srgb 1 none 0)", true);
    lwc_string_unref(text);
    css__declaration_store_destroy(copy);
    css__declaration_store_destroy(store);
}
END_TEST

START_TEST(test_extended_polar_values_survive_edits)
{
    css_declaration_store *store = create("background:linear-gradient(hsl(30 200 40),hwb(0 -10 10));"
        "border:2px solid hsl(30 200 40)!important;color:color(srgb 1.2 .4 -.1);opacity:1", _i != 0);
    ck_assert(set(store, "opacity:.5", _i != 0));
    check_value(store, "background-image", "linear-gradient(rgb(255, 102, 0), rgb(229.5, 0, 0))", false);
    check_value(store, "border-color", "rgb(255, 102, 0)", true);
    ck_assert(set(store, "border-left-color:hwb(0 -10 10 / 50%)", _i != 0));
    check_value(store, "border-left-color", "rgba(229.5, 0, 0, 0.5)", false);
    check_value(store, "border-right-color", "rgb(255, 102, 0)", true);
    /* Legacy bounded readback must not clamp explicit color(srgb) or erase
     * fractional coordinates when the entire block is synchronized. */
    lwc_string *text;
    ck_assert_int_eq(css__declaration_store_serialize(store, &text), CSS_OK);
    css_declaration_store *copy = create(lwc_string_data(text), _i != 0);
    check_text(copy, lwc_string_data(text));
    check_value(copy, "color", "color(srgb 1.2 0.4 -0.1)", false);
    check_value(copy, "border-left-color", "rgba(229.5, 0, 0, 0.5)", false);
    check_value(copy, "border-right-color", "rgb(255, 102, 0)", true);
    lwc_string_unref(text);
    css__declaration_store_destroy(copy);
    css__declaration_store_destroy(store);
}
END_TEST

START_TEST(test_nonfinite_values_survive_edits)
{
    css_declaration_store *store = create("margin:var(--m);width:calc(1px / 0);color:red", false);
    ck_assert(set(store, "margin-left:2px", false));
    const css_declaration *pending = css__declaration_store_get(store, 0)->source;
    check_value(store, "width", "calc(infinity * 1px)", false);
    check_value(store, "color", "red", false);
    check_value(store, "margin", "", false);
    ck_assert_ptr_eq(pending, css__declaration_store_get(store, 0)->source);
    ck_assert(pending->deferred);
    ck_assert_str_eq(lwc_string_data(pending->source_value), "var(--m)");
    /* Assign the preserved shorthand borrowed from this store itself. */
    bool changed;
    ck_assert_int_eq(css__declaration_store_set(store, pending, &changed), CSS_OK);
    ck_assert(changed);
    check_value(store, "margin", "var(--m)", false);
    lwc_string *out;
    ck_assert_int_eq(css__declaration_store_serialize(store, &out), CSS_OK);
    css_declaration_store *copy = create(lwc_string_data(out), false);
    check_text(copy, lwc_string_data(out));
    check_value(copy, "width", "calc(infinity * 1px)", false);
    lwc_string_unref(out);
    css__declaration_store_destroy(copy);
    ck_assert(set(store, "width:calc(0px / 0)", false));
    check_value(store, "width", "calc(NaN * 1px)", false);
    ck_assert_int_eq(css__declaration_store_remove(store, "width", 5, &changed), CSS_OK);
    ck_assert(changed);
    check_text(store, "margin: var(--m); color: red;");
    css__declaration_store_destroy(store);
}
END_TEST

START_TEST(test_pending_sources_survive_edits)
{
    css_declaration_store *store = create("margin:var(--Margins)!important;font:var(--Font);"
        "text-decoration:var(--Lines);color:red", _i != 0);
    const css_declaration *margin = css__declaration_store_get(store, 0)->source;
    const css_declaration *font = css__declaration_store_get(store, 4)->source;
    const css_declaration *decoration = css__declaration_store_get(store, 10)->source;
    ck_assert(set(store, "margin-left:2px", _i != 0));
    ck_assert(set(store, "font-variant-caps:small-caps", _i != 0));
    ck_assert(set(store, "opacity:.5", _i != 0));
    /* CSS Values 5 requires empty pending-substitution readback. The store,
     * not a parse of cssText, remains authoritative after an API edit. */
    check_value(store, "margin-top", "", true);
    check_value(store, "font-family", "", false);
    check_value(store, "font-variant", "small-caps", false);
    check_value(store, "text-decoration", "var(--Lines)", false);
    check_value(store, "text-decoration-line", "", false);
    for (size_t i = 0; i < css__declaration_store_count(store); i++) {
        const css_declaration_contribution *entry = css__declaration_store_get(store, i);
        const css_declaration *expected = NULL;
        if (i < 3)
            expected = margin;
        else if (i == 4 || (i >= 6 && i <= 9))
            expected = font;
        else if (i == 10)
            expected = decoration;
        if (expected != NULL) {
            ck_assert_ptr_eq(entry->source, expected);
            ck_assert(expected->deferred);
        }
    }
    bool changed;
    ck_assert_int_eq(css__declaration_store_remove(store, "margin-right", 12, &changed), CSS_OK);
    ck_assert(changed);
    check_value(store, "margin-right", "", false);
    ck_assert_ptr_eq(css__declaration_store_get(store, 0)->source, margin);
    /* A longhand var() is observable; replacing it by an otherwise identical
     * shorthand var() changes that observable value and cannot be a no-op. */
    ck_assert(set(store, "text-decoration-line:var(--Lines)", _i != 0));
    check_value(store, "text-decoration-line", "var(--Lines)", false);
    check_value(store, "text-decoration", "", false);
    ck_assert(set(store, "text-decoration:var(--Lines)", _i != 0));
    check_value(store, "text-decoration-line", "", false);
    check_value(store, "text-decoration", "var(--Lines)", false);
    ck_assert(!set(store, "text-decoration:var(--Lines)", _i != 0));
    css__declaration_store_destroy(store);
}
END_TEST

START_TEST(test_background_repeat_edits)
{
    css_declaration_store *store = create("background:url(a) repeat-x,url(b) repeat-y blue", _i != 0);
    check_order(store, "background-image background-position background-repeat-x background-repeat-y "
        "background-attachment background-color");
    ck_assert(set(store, "background-repeat-x:no-repeat", _i != 0));
    check_value(store, "background-repeat-x", "no-repeat", false);
    check_value(store, "background-repeat-y", "no-repeat, repeat", false);
    check_value(store, "background-repeat", "", false);
    check_value(store, "background", "", false);
    /* Unequal specified lists must round-trip independently, without extending
     * or truncating either list as painting does. */
    lwc_string *text;
    ck_assert_int_eq(css__declaration_store_serialize(store, &text), CSS_OK);
    css_declaration_store *copy = create(lwc_string_data(text), _i != 0);
    check_text(copy, lwc_string_data(text));
    check_value(copy, "background-repeat-x", "no-repeat", false);
    check_value(copy, "background-repeat-y", "no-repeat, repeat", false);
    lwc_string_unref(text);
    css__declaration_store_destroy(copy);
    ck_assert(set(store, "background-repeat-x:no-repeat,repeat", _i != 0));
    check_value(store, "background-repeat", "no-repeat, repeat", false);
    check_text(store, "background: url(\"a\") no-repeat, url(\"b\") blue;");
    bool changed;
    ck_assert_int_eq(css__declaration_store_remove(store, "background-repeat-x", 19, &changed), CSS_OK);
    ck_assert(changed);
    check_value(store, "background-repeat-x", "", false);
    check_value(store, "background-repeat-y", "no-repeat, repeat", false);
    ck_assert(set(store, "background-repeat:var(--R)!important", _i != 0));
    const css_declaration *pending = NULL;
    for (size_t i = 0; i < css__declaration_store_count(store); i++) {
        const css_declaration_contribution *entry = css__declaration_store_get(store, i);
        if (strcmp(entry->name, "background-repeat-y") == 0)
            pending = entry->source;
    }
    ck_assert_ptr_nonnull(pending);
    ck_assert(set(store, "background-repeat-x:no-repeat", _i != 0));
    check_value(store, "background-repeat-x", "no-repeat", false);
    check_value(store, "background-repeat-y", "", true);
    for (size_t i = 0; i < css__declaration_store_count(store); i++) {
        const css_declaration_contribution *entry = css__declaration_store_get(store, i);
        if (strcmp(entry->name, "background-repeat-y") == 0) {
            ck_assert_ptr_eq(entry->source, pending);
            ck_assert(entry->source->deferred);
        }
    }
    ck_assert_int_eq(css__declaration_store_remove(store, "background-repeat", 17, &changed), CSS_OK);
    ck_assert(changed);
    check_value(store, "background-repeat-y", "", false);
    check_value(store, "background-color", "blue", false);
    css__declaration_store_destroy(store);
}
END_TEST

START_TEST(test_calc_values_survive_edits)
{
    css_declaration_store *store = create("margin:calc(50% - (2px + 1px)) calc(1em * 2)!important;"
        "font:italic calc(10px + 2px)/calc(1 + .25) serif;"
        "border:calc(1px + 2px) solid red;flex:calc(1 + 1) 1 calc(100% / 2);"
        "columns:calc(10px + 2em) calc(1 + 1);width:calc(0% + 1px);opacity:1", _i != 0);
    ck_assert(set(store, "opacity:.5", _i != 0));
    check_value(store, "margin", "calc(50% - 3px) calc(2em)", true);
    check_value(store, "font", "italic calc(12px) / calc(1.25) serif", false);
    check_value(store, "flex", "calc(2) 1 calc(50%)", false);
    check_value(store, "columns", "calc(2em + 10px) calc(2)", false);
    check_value(store, "width", "calc(0% + 1px)", false);
    ck_assert(set(store, "margin-left:4px", _i != 0));
    ck_assert(set(store, "line-height:2", _i != 0));
    ck_assert(set(store, "border-left-color:blue", _i != 0));
    ck_assert(set(store, "column-count:3", _i != 0));
    check_value(store, "margin-left", "4px", false);
    check_value(store, "margin-right", "calc(2em)", true);
    check_value(store, "margin-top", "calc(50% - 3px)", true);
    check_value(store, "font-size", "calc(12px)", false);
    check_value(store, "line-height", "2", false);
    check_value(store, "border-left-width", "calc(3px)", false);
    check_value(store, "border-left-color", "blue", false);
    check_value(store, "column-width", "calc(2em + 10px)", false);
    /* The original stylesheet is already destroyed by create/set. Edits own
     * the surviving math tokens; cssText must reparse and serialize stably. */
    lwc_string *text;
    ck_assert_int_eq(css__declaration_store_serialize(store, &text), CSS_OK);
    css_declaration_store *copy = create(lwc_string_data(text), _i != 0);
    check_text(copy, lwc_string_data(text));
    check_value(copy, "width", "calc(0% + 1px)", false);
    check_value(copy, "flex-basis", "calc(50%)", false);
    check_value(copy, "margin-right", "calc(2em)", true);
    lwc_string_unref(text);
    css__declaration_store_destroy(copy);
    bool changed;
    ck_assert_int_eq(css__declaration_store_remove(store, "margin-left", 11, &changed), CSS_OK);
    ck_assert(changed);
    check_value(store, "margin-left", "", false);
    check_value(store, "margin-right", "calc(2em)", true);
    css__declaration_store_destroy(store);
}
END_TEST

START_TEST(test_length_and_borrowed_names)
{
    css_declaration_store *store = create("width:2px;--a\\:b:one;--other:two", false);
    lwc_string *out;
    ck_assert_int_eq(css__declaration_store_value(store, "width\0junk", 10, &out), CSS_OK);
    ck_assert_str_eq(lwc_string_data(out), "");
    lwc_string_unref(out);
    ck_assert_int_eq(css__declaration_store_value(store, "WIDTH-trailing", 5, &out), CSS_OK);
    ck_assert_str_eq(lwc_string_data(out), "2px");
    lwc_string_unref(out);
    const char *name = css__declaration_store_get(store, 1)->name;
    bool changed;
    ck_assert_int_eq(css__declaration_store_remove(store, name, strlen(name), &changed), CSS_OK);
    ck_assert(changed);
    check_text(store, "width: 2px; --other: two;");
    css__declaration_store_destroy(store);
}
END_TEST

static void count_string(lwc_string *string, void *pw)
{
    (void)string;
    (*(size_t *)pw)++;
}

static void find_pending_string(lwc_string *string, void *pw)
{
    if (strcmp(lwc_string_data(string), "var(--retained-until-last-member)") == 0)
        *(bool *)pw = true;
}

START_TEST(test_shared_source_lifetime)
{
    css_declaration_store *store = create("margin:var(--retained-until-last-member);color:red", false);
    const char *members[] = {"margin-top", "margin-right", "margin-bottom", "margin-left"};
    const css_declaration *source = css__declaration_store_get(store, 0)->source;
    for (size_t i = 1; i < 4; i++)
        ck_assert_ptr_eq(css__declaration_store_get(store, i)->source, source);
    for (size_t i = 0; i < 4; i++) {
        bool changed;
        ck_assert_int_eq(css__declaration_store_remove(store, members[i], strlen(members[i]), &changed), CSS_OK);
        ck_assert(changed);
        bool retained = false;
        lwc_iterate_strings(find_pending_string, &retained);
        ck_assert_msg(retained == (i < 3), "source lifetime after removing %s", members[i]);
    }
    check_text(store, "color: red;");
    css__declaration_store_destroy(store);
}
END_TEST

START_TEST(test_repeated_edits_release_sources)
{
    css_declaration_store *store = create("margin:var(--m);--value:token-1000", false);
    size_t initial = 0;
    lwc_iterate_strings(count_string, &initial);
    for (unsigned i = 1001; i < 1257; i++) {
        char css[64];
        snprintf(css, sizeof(css), "--value:token-%u", i);
        ck_assert(set(store, css, false));
        size_t retained = 0;
        lwc_iterate_strings(count_string, &retained);
        ck_assert_msg(retained == initial, "edit %u retained %zu strings, initially %zu", i, retained, initial);
        ck_assert_uint_eq(css__declaration_store_count(store), 5);
    }
    check_value(store, "margin", "var(--m)", false);
    check_value(store, "--value", "token-1256", false);
    css__declaration_store_destroy(store);
}
END_TEST

START_TEST(test_bad_arguments)
{
    css_declaration_store *store = (void *)1;
    ck_assert_int_eq(css__declaration_store_create(NULL, &store), CSS_BADPARM);
    ck_assert_ptr_null(store);
    ck_assert_int_eq(css__declaration_store_create(NULL, NULL), CSS_BADPARM);
    bool flag = true;
    ck_assert_int_eq(css__declaration_store_set(NULL, NULL, &flag), CSS_BADPARM);
    ck_assert(!flag);
    ck_assert_int_eq(css__declaration_store_set(NULL, NULL, NULL), CSS_BADPARM);
    ck_assert_int_eq(css__declaration_store_remove(NULL, "", 0, &flag), CSS_BADPARM);
    ck_assert_int_eq(css__declaration_store_remove(NULL, "", 0, NULL), CSS_BADPARM);
    ck_assert_int_eq(css__declaration_store_priority(NULL, "", 0, &flag), CSS_BADPARM);
    ck_assert_int_eq(css__declaration_store_priority(NULL, "", 0, NULL), CSS_BADPARM);
    lwc_string *out = (void *)1;
    ck_assert_int_eq(css__declaration_store_value(NULL, "", 0, &out), CSS_BADPARM);
    ck_assert_ptr_null(out);
    ck_assert_int_eq(css__declaration_store_value(NULL, "", 0, NULL), CSS_BADPARM);
    ck_assert_int_eq(css__declaration_store_serialize(NULL, &out), CSS_BADPARM);
    ck_assert_ptr_null(out);
    ck_assert_int_eq(css__declaration_store_serialize(NULL, NULL), CSS_BADPARM);
    ck_assert_uint_eq(css__declaration_store_count(NULL), 0);
    ck_assert_ptr_null(css__declaration_store_get(NULL, 0));
    css__declaration_store_destroy(NULL);
}
END_TEST

/* A NULL expected value denotes a CSSOM no-op, not empty-string readback. */
static const struct {
    const char *name, *value, *priority, *expected;
    bool important;
} inputs[] = {
    {"color", "hsl(30 200 40)", "", "rgb(255, 102, 0)"},
    {"background-color", "hwb(0 -10 10 / 50%)", "", "rgba(229.5, 0, 0, 0.5)"},
    {"border", "solid hsl(30 200 40)", "important", "solid rgb(255, 102, 0)", true},
    {"color", "rgb(none 50% 255 / none)", "", "color(srgb none 0.5 1 / none)"},
    {"background-color", "COLOR(SRGB 25% +0 .5 / 50%)", "", "color(srgb 0.25 0 0.5 / 0.5)"},
    {"border", "solid color(srgb 1 none 0)", "important", "solid color(srgb 1 none 0)", true},
    {"color", "color(srgb 1,0,0)", "", NULL},
    {"color", "LAB(56.2% 0 66.88%)", "", "lab(56.2 0 83.6)"},
    {"background-color", "OKLAB(54% -25% -5%)", "", "oklab(0.54 -0.1 -0.02)"},
    {"border-color", "lch(50% 100% -90deg) oklch(none -1 none / none)", "",
        "lch(50 150 270) oklch(none 0 none / none)"},
    {"fill", "lab(none none none / none)", "", "lab(none none none / none)"},
    {"color", "lch(50 20 30px)", "", NULL},
    {"azimuth", "BEHIND LEFT", "important", "220deg", true},
    {"elevation", "-0", "", "0deg"},
    {"pause-before", "123.456789ms", "", "0.123457s"},
    {"pause-after", "20%", "", "20%"},
    {"pitch-range", "34.5678912", "", "34.567891"},
    {"pitch", "2KHZ", "", "2khz"},
    {"play-during", "url(Sound.ogg) REPEAT MIX", "", "url(\"Sound.ogg\") mix repeat"},
    {"richness", "23.4567891", "", "23.456789"},
    {"speak-header", "ONCE", "", "once"},
    {"speak-numeral", "DIGITS", "", "digits"},
    {"speak-punctuation", "CODE", "", "code"},
    {"speak", "SPELL-OUT", "", "spell-out"},
    {"speech-rate", "123.4567891", "", "123.456789"},
    {"stress", "45.6789123", "", "45.678912"},
    {"voice-family", "One Two,MALE,'Male'", "", "\"One Two\", male, \"Male\""},
    {"volume", "250%", "", "250%"},
    {"cue", "url(Sound.ogg) NONE", "important", "url(\"Sound.ogg\") none", true},
    {"pause", "1000ms 1s", "important", "1s", true},
    {"azimuth", "left left", "", NULL},
    {"elevation", "91deg", "", NULL},
    {"play-during", "url(Sound.ogg) mix mix", "", NULL},
    {"play-during", "repeat url(Sound.ogg)", "", NULL},
    {"voice-family", "One,", "", NULL},
    {"cue", "none initial", "", NULL},
    {"cue", "none url(A.ogg) none", "", NULL},
    {"pause", "1s 2s 3s", "", NULL},
    {"pause", "initial 2s", "", NULL},
    {"pause", "2s!important", "", NULL},

    {"fill", "NONE", "", "none"},
    {"fill", "#1234", "important", "rgba(17, 34, 51, 0.267)", true},
    {"stroke", "hsl(120 none 50 / .5)", "", "hsl(120 none 50% / 0.5)"},
    {"stroke", "CURRENTcolor", "", "currentcolor"},
    {"fill-opacity", "-1.23456789", "", "-1.234568"},
    {"stroke-opacity", "+2.3456789", "", "2.345679"},
    {"stroke-width", "+1.23456789", "important", "1.234568", true},
    {"stroke-width", "2EM", "", "2em"},
    {"stroke-width", "-.000000001px", "", NULL},
    {"stroke-width", "1fr", "", NULL},
    {"stroke-width", "0 px", "", NULL},
    {"cursor", "url(Cur.cur),POINTER", "important", "url(\"Cur.cur\"), pointer", true},
    {"cursor", "url(Cur.cur)", "", NULL},
    {"cursor", "url(Cur.cur),inherit", "", NULL},
    {"cursor", "pointer!important", "", NULL},

    {"grid-template-columns", "REPEAT(+002,MINMAX(0,1FR) 2PX)", "important",
        "repeat(2, minmax(0px, 1fr) 2px)", true},
    {"grid-template-rows", "minmax(1EM,2%)repeat(1,AUTO)", "", "minmax(1em, 2%) repeat(1, auto)"},
    {"grid-template-rows", "repeat(2,minmax(0,1fr)", "", "repeat(2, minmax(0px, 1fr))"},
    {"grid-template-rows", "0px repeat(2,)", "", NULL},
    {"grid-template-columns", "minmax(0 1fr)", "", NULL},
    {"grid-template-columns", "minmax(1fr,2fr)", "", NULL},
    {"grid-template-rows", "-.00000001px", "", NULL},
    {"grid-template-rows", "repeat(32,1px) 2px", "", NULL},
    {"grid-auto-flow", "DENSE COLUMN", "important", "column dense", true},
    {"grid-auto-flow", "ROW DENSE", "", "dense"},
    {"grid-auto-flow", "dense dense", "", NULL},
    {"grid-auto-flow", "row column", "", NULL},
    {"transform", "TRANSLATEX(1.23456789em) scale(50%,.5) rotate(-0)", "important",
        "translateX(1.234568em) scale(0.5) rotate(0deg)", true},
    {"transform", "rotate(45deg", "", "rotate(45deg)"},
    {"transform", "translate(1px,2deg)", "", NULL},
    {"transform", "translate(.00000001)", "", NULL},
    {"transform", "scale(1) unknown(0)", "", NULL},
    {"color", "HSLA(NONE 100 50 / 50%)", "important", "hsl(none 100% 50% / 0.5)", true},
    {"background", "hwb(20 none 30% / none)", "", "hwb(20 none 30% / none)"},
    {"border", "solid hsl(-.5turn none 50%)", "", "solid hsl(180 none 50%)"},
    {"background-image", "radial-gradient(hsl(none 100 50),hwb(120 none 0))", "",
        "radial-gradient(hsl(none 100% 50%), hwb(120 none 0%))"},

    {"background-image", "linear-gradient(TO bottom,red 0%,blue 100%)", "important", "linear-gradient(red, blue)", true},
    {"background", "radial-gradient(ellipse,red,blue) no-repeat fixed", "", "radial-gradient(red, blue) no-repeat fixed"},
    {"background-image", "linear-gradient(red,blue", "", "linear-gradient(red, blue)"},
    {"background-image", "linear-gradient(45.1234567deg,red,blue)", "", "linear-gradient(45.123457deg, red, blue)"},
    {"background-image", "radial-gradient(circle red,blue)", "", NULL},
    {"background-image", "linear-gradient(1foodeg,red,blue)", "", NULL},
    {"background-image", "linear-gradient(red -.00000001,blue)", "", NULL},

    {"color", "red", "", "red"},
    {"CoLoR", " RED /**/", "IMPORTANT", "red", true},
    {"width", "2px", "", "2px"},
    {"margin", "1px 2px", "important", "1px 2px", true},
    {"GRID-GAP", "2px 3px", "", "2px 3px"},
    {"margin", "var(--x, 1PX  2px)", "", "var(--x, 1PX  2px)"},
    {"width", "var(--x, rgb(1, 2, 3))", "", "var(--x, rgb(1, 2, 3))"},
    {"color", "rgb(1, 2, 3", "", "rgb(1, 2, 3)"},
    {"content", "'hello", "", "\"hello\""},
    {"content", "\"; !important }\"", "", "\"; !important }\""},
    {"background-image", "url(a.png)", "", "url(\"a.png\")"},
    {"background-image", "url(a.png", "", "url(\"a.png\")"},
    {"background-image", "url(\"a.png", "", "url(\"a.png\")"},
    {"background-image", "url(", "", "url(\"\")"},
    {"background-image", "u\\72l(a.png)", "", "url(\"a.png\")"},
    {"background-image", "url(a\\", "", "url(\"a�\")"},
    {"--x", "  MiXeD/**/tokens  ", "", "MiXeD/**/tokens"},
    {"--x", " ", "", " "},
    {"--x", "/**/", "", "/**/"},
    {"--x", "fn(!important; { x: y; })", "", "fn(!important; { x: y; })"},
    {"--x", "[ { x: y; } ]", "important", "[ { x: y; } ]", true},
    {"--x", "fn([x", "", "fn([x"},
    {"--x", "url(\"x\" extra)", "", "url(\"x\" extra)"},
    {"--x", "var(--a, fn(!important))", "", "var(--a, fn(!important))"},
    {"--x", "escaped\\;separator", "", "escaped\\;separator"},
    {"--x", "\\!important", "", "\\!important"},
    {"--x", "x/* unfinished !important;  ", "", "x/* unfinished !important;  "},
    {"--Case", "new", "", "new"},
    {"--a:b; c", "value", "", "value"},
    {"--a\\31", "value", "", "value"},
    {"--știri", "Știri", "", "Știri"},
    {"width", "", "wrong priority", ""},
    {"--x", "", "!important", ""},
    {"color", "red; width:500px", "", NULL},
    {"color", "red;", "", NULL},
    {"color", "red}", "", NULL},
    {"color", "red !important", "", NULL},
    {"color", "red !/**/IMPORTANT", "important", NULL},
    {"color", "red !\\69mportant", "", NULL},
    {"color", "red", " important", NULL},
    {"color", "red", "important ", NULL},
    {"color", "red", "!important", NULL},
    {"color", "red", "\\69mportant", NULL},
    {"color", "red", "IMPORTANT/**/", NULL},
    {"color", "red junk", "", NULL},
    {"color", " ", "", NULL},
    {"color", "/**/", "", NULL},
    {"width", "2px\ncolor:red", "", NULL},
    {"width", "var(--x);junk", "", NULL},
    {"width", "var(--x) !important", "", NULL},
    {"content", "'broken\nstring'", "", NULL},
    {"--x", "x;", "", NULL},
    {"--x", "!important", "", NULL},
    {"--x", "x !invalid", "", NULL},
    {"--x", "x)", "", NULL},
    {"--x", "fn([x)]", "", NULL},
    {"--x", "{x: y; ]}", "", NULL},
    {"--x", "url(a b)", "", NULL},
    {"--x", "url(a\tb)", "", NULL},
    {"--x", "url(a(b))", "", NULL},
    {"--x", "url(a\"b)", "", NULL},
    {"--x", "url(a\\\nb)", "", NULL},
    {"--x", "url(a b\\); color:red)", "", NULL},
    {"--x", "u\\72l(a b)", "", NULL},
    {"--x", "fn(url(a b))", "", NULL},
    {"--x", "url(a b", "", NULL},
    {"--x", "var(--a, var(--b,))", "", "var(--a, var(--b,))"},
    {"--x", "var(--a,)", "", "var(--a,)"},
    {"--x", "var(/**/--a/**/,x)", "", "var(/**/--a/**/,x)"},
    {"--x", "var(\\2d -a)", "", "var(\\2d -a)"},
    {"background-image", "url(var(--x))", "", NULL},
    {"unknown-property", "red", "", NULL},
    {"color:red;width", "2px", "", NULL},
    {" color", "red", "", NULL},
    {"c\\6flor", "red", "", NULL},
    {"--", "value", "", NULL},
};
enum { INPUT_COUNT = sizeof(inputs) / sizeof(inputs[0]) };

START_TEST(test_set_property_input)
{
    css_declaration_store *store = create(
        "color:blue;width:4px!important;--x:old;--Case:old;--case:other;height:17px", false);
    lwc_string *before;
    ck_assert_int_eq(css__declaration_store_serialize(store, &before), CSS_OK);
    const css_declaration_contribution *borrowed = css__declaration_store_get(store, 0);
    css_declaration_assignment input = assignment(inputs[_i].name, inputs[_i].value, inputs[_i].priority);
    bool changed;
    css_error error = css__declaration_store_set_property(store, &input_params, &input, &changed);
    ck_assert_msg(
        error == CSS_OK, "input %d (%s, %s, %s): error %d", _i, input.property, input.value, input.priority, error);
    if (inputs[_i].expected == NULL) {
        ck_assert_msg(!changed, "invalid input %d changed the store: %s = %s", _i, input.property, input.value);
        ck_assert_ptr_eq(borrowed, css__declaration_store_get(store, 0));
        check_text(store, lwc_string_data(before));
    } else {
        ck_assert_msg(changed, "input %d was not applied: %s = %s", _i, input.property, input.value);
        check_value(store, input.property, inputs[_i].expected, inputs[_i].important);
    }
    check_value(store, "height", "17px", false);
    check_value(store, "--case", "other", false);
    lwc_string_unref(before);
    css__declaration_store_destroy(store);
}
END_TEST

START_TEST(test_whole_value_chunks)
{
    /* The low-level parser receives decoded names and no priority. Check both
     * streaming boundaries and EOF recovery against the same native values. */
    const struct css_prop_entry *property = css_prop_lookup(inputs[_i].name, strlen(inputs[_i].name));
    if (inputs[_i].expected == NULL || inputs[_i].value[0] == '\0')
        return;
    const char *name = property != NULL ? property->name : inputs[_i].name;
    lwc_string *interned;
    ck_assert_int_eq(lwc_intern_string(name, strlen(name), &interned), lwc_error_ok);
    css_declaration_list *list;
    ck_assert_int_eq(css__declaration_list_create_for_value(&input_params, interned, &list), CSS_OK);
    lwc_string_unref(interned);
    for (size_t i = 0; i < strlen(inputs[_i].value); i++) {
        css_error error = css__declaration_list_append(list, (const uint8_t *)inputs[_i].value + i, 1);
        ck_assert_msg(
            error == CSS_OK || error == CSS_NEEDDATA, "chunk %zu of %s: error %d", i, inputs[_i].value, error);
    }
    ck_assert_int_eq(css__declaration_list_complete(list), CSS_OK);
    ck_assert_uint_eq(css__declaration_list_count(list), 1);
    css_declaration_store *store;
    ck_assert_int_eq(css__declaration_store_create(list, &store), CSS_OK);
    css__declaration_list_destroy(list);
    check_value(store, name, inputs[_i].expected, false);
    css__declaration_store_destroy(store);
}
END_TEST

START_TEST(test_input_lengths_and_arguments)
{
    css_declaration_store *store = create("color:blue;--x:old", false);
    css_declaration_assignment input = assignment("color", "red", "important");
    bool changed;
    input.property = "color\0junk";
    input.property_length = 10;
    ck_assert_int_eq(css__declaration_store_set_property(store, &input_params, &input, &changed), CSS_OK);
    ck_assert(!changed);
    input.property_length = 5;
    input.value = "red;";
    input.value_length = 3;
    ck_assert_int_eq(css__declaration_store_set_property(store, &input_params, &input, &changed), CSS_OK);
    ck_assert(changed);
    check_value(store, "color", "red", true);
    input.property = "--x\0suffix";
    input.property_length = 10;
    ck_assert_int_eq(css__declaration_store_set_property(store, &input_params, &input, &changed), CSS_OK);
    ck_assert(!changed);
    input = assignment("--x", "new", "important");
    input.priority = "important\0junk";
    input.priority_length = 14;
    ck_assert_int_eq(css__declaration_store_set_property(store, &input_params, &input, &changed), CSS_OK);
    ck_assert(!changed);
    input.value_length = 0;
    ck_assert_int_eq(css__declaration_store_set_property(store, &input_params, &input, &changed), CSS_OK);
    ck_assert(changed);
    check_value(store, "--x", "", false);
    ck_assert_int_eq(css__declaration_store_set_property(NULL, &input_params, &input, &changed), CSS_BADPARM);
    ck_assert(!changed);
    ck_assert_int_eq(css__declaration_store_set_property(store, NULL, &input, &changed), CSS_BADPARM);
    ck_assert_int_eq(css__declaration_store_set_property(store, &input_params, NULL, &changed), CSS_BADPARM);
    ck_assert_int_eq(css__declaration_store_set_property(store, &input_params, &input, NULL), CSS_BADPARM);
    css__declaration_store_destroy(store);
}
END_TEST

START_TEST(test_url_grammar_in_stylesheets)
{
    css_declaration_store *store = create("--x:url(a b);color:blue;--y:fn(url(a b));width:3px;"
                                        "background-image:u\\72l(a\\)b.png);--z:url(a\tb)",
        _i != 0);
    check_value(store, "--x", "", false);
    check_value(store, "--y", "", false);
    check_value(store, "--z", "", false);
    check_value(store, "color", "blue", false);
    check_value(store, "width", "3px", false);
    check_value(store, "background-image", "url(\"a)b.png\")", false);
    css__declaration_store_destroy(store);
}
END_TEST

START_TEST(test_invalid_value_chunks)
{
    const char *values[] = {"red; width:2px", "red !important", "{x: y; ]}", "fn(url(a b\\)c))", "url(a\\\nb)",
        "u\\72l(a b)", "\"bad\nstring\"", "x)"};
    lwc_string *name;
    ck_assert_int_eq(lwc_intern_string("--x", 3, &name), lwc_error_ok);
    for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); i++) {
        css_declaration_list *list;
        ck_assert_int_eq(css__declaration_list_create_for_value(&input_params, name, &list), CSS_OK);
        css_error error = CSS_OK;
        for (size_t j = 0; j < strlen(values[i]) && (error == CSS_OK || error == CSS_NEEDDATA); j++)
            error = css__declaration_list_append(list, (const uint8_t *)values[i] + j, 1);
        if (error == CSS_OK || error == CSS_NEEDDATA)
            error = css__declaration_list_complete(list);
        ck_assert_msg(error == CSS_INVALID, "invalid chunked value %s: %d", values[i], error);
        ck_assert_int_eq(css__declaration_list_complete(list), CSS_INVALID);
        ck_assert_uint_eq(css__declaration_list_count(list), 0);
        css__declaration_list_destroy(list);
    }
    lwc_string_unref(name);
}
END_TEST

#ifdef WISP_TEST_ALLOC_FAILURE
static void (*const allocators[])(unsigned) = {malloc_limit, calloc_limit, realloc_limit};

START_TEST(test_input_allocation_failure)
{
    css_declaration_store *store = create("--x:old!important;color:red", false);
    /* Force nested-component stack growth, lexer unescaping and EOF closure. */
    char value[256];
    memset(value, '(', 80);
    strcpy(value + 80, "u\\72l(quoted\\)name.png) !important");
    css_declaration_assignment input = assignment("--x", value, "IMPORTANT");
    const char *expected = value;
    unsigned allocator = (unsigned)_i % 3;
    if (_i >= 3) {
        input = assignment("background", "linear-gradient(to right,red -.1234567em,blue),"
            "radial-gradient(circle,rgb(1 2 3 / .4) 0%,white) no-repeat fixed", "IMPORTANT");
        expected = "linear-gradient(to right, red -0.123457em, blue), "
            "radial-gradient(circle, rgba(1, 2, 3, 0.4), white) no-repeat fixed";
    }
    const css_declaration_contribution *old = css__declaration_store_get(store, 0);
    bool succeeded = false;
    size_t baseline = 0;
    lwc_iterate_strings(count_string, &baseline);
    for (unsigned n = 0; n < 1024; n++) {
        bool changed = true;
        allocators[allocator](n);
        css_error error = css__declaration_store_set_property(store, &input_params, &input, &changed);
        allocators[allocator](UINT_MAX);
        if (error == CSS_OK) {
            ck_assert(changed);
            ck_assert_uint_gt(n, 0);
            check_value(store, input.property, expected, true);
            succeeded = true;
            break;
        }
        ck_assert_msg(error == CSS_NOMEM, "input allocator %d failure %u: %d", _i, n, error);
        ck_assert(!changed);
        ck_assert_ptr_eq(old, css__declaration_store_get(store, 0));
        check_text(store, "--x: old !important; color: red;");
        size_t retained = 0;
        lwc_iterate_strings(count_string, &retained);
        ck_assert_msg(retained == baseline, "input allocator %d failure %u retained %zu strings, originally %zu", _i,
            n, retained, baseline);
    }
    ck_assert(succeeded);
    input = assignment(input.property, "", "not a priority");
    bool changed;
    allocators[allocator](0);
    css_error error = css__declaration_store_set_property(store, &input_params, &input, &changed);
    allocators[allocator](UINT_MAX);
    ck_assert_int_eq(error, CSS_OK);
    ck_assert(changed);
    check_value(store, input.property, "", false);
    css__declaration_store_destroy(store);
}
END_TEST

START_TEST(test_create_allocation_failure)
{
    css_declaration_list *list = parse(
        "margin:1px; margin-left:var(--m); width:2px; --X:Token!important;--X:lost", false);
    bool succeeded = false;
    for (unsigned n = 0; n < 64; n++) {
        css_declaration_store *store = (void *)1;
        allocators[_i](n);
        css_error error = css__declaration_store_create(list, &store);
        allocators[_i](UINT_MAX);
        if (error == CSS_OK) {
            ck_assert_uint_gt(n, 0);
            check_value(store, "--X", "Token", true);
            check_value(store, "margin-left", "var(--m)", false);
            css__declaration_store_destroy(store);
            succeeded = true;
            break;
        }
        ck_assert_msg(error == CSS_NOMEM, "create allocator %d failure %u: %d", _i, n, error);
        ck_assert_ptr_null(store);
    }
    ck_assert(succeeded);
    css__declaration_list_destroy(list);
}
END_TEST

START_TEST(test_set_allocation_failure)
{
    css_declaration_store *store = create("margin-left:1px!important; margin-inline:2px; color:red", false);
    css_declaration_list *list = parse("margin:var(--m)", false);
    const css_declaration_contribution *old = css__declaration_store_get(store, 0);
    bool succeeded = false;
    for (unsigned n = 0; n < 64; n++) {
        bool changed = true;
        allocators[_i](n);
        css_error error = css__declaration_store_set(store, css__declaration_list_get(list, 0), &changed);
        allocators[_i](UINT_MAX);
        if (error == CSS_OK) {
            ck_assert(changed);
            ck_assert_msg(n > 0 || _i == 1, "expected fault coverage for allocator %d", _i);
            check_text(store, "margin-inline: 2px; color: red; margin: var(--m);");
            succeeded = true;
            break;
        }
        ck_assert_msg(error == CSS_NOMEM, "set allocator %d failure %u: %d", _i, n, error);
        ck_assert(!changed);
        ck_assert_ptr_eq(old, css__declaration_store_get(store, 0));
        check_text(store, "margin-left: 1px !important; margin-inline: 2px; color: red;");
        check_order(store, "margin-left margin-inline-start margin-inline-end color");
    }
    ck_assert(succeeded);
    /* Repeating the exact assignment and removing it require no allocation. */
    bool changed;
    allocators[_i](0);
    css_error error = css__declaration_store_set(store, css__declaration_list_get(list, 0), &changed);
    allocators[_i](UINT_MAX);
    ck_assert_int_eq(error, CSS_OK);
    ck_assert(!changed);
    allocators[_i](0);
    error = css__declaration_store_remove(store, "margin", 6, &changed);
    allocators[_i](UINT_MAX);
    ck_assert_int_eq(error, CSS_OK);
    ck_assert(changed);
    css__declaration_list_destroy(list);
    css__declaration_store_destroy(store);
}
END_TEST
#endif

static void unexpected_string(lwc_string *string, void *pw)
{
    (*(unsigned *)pw)++;
    fprintf(stderr, "Leaked interned string: %s\n", lwc_string_data(string));
}

int main(void)
{
    Suite *suite = suite_create("CSS declaration store");
    TCase *tc = tcase_create("Native editing");
    tcase_add_loop_test(tc, test_assignments, 0, ASSIGNMENT_COUNT * 2);
    tcase_add_loop_test(tc, test_removals, 0, REMOVAL_COUNT * 2);
    tcase_add_test(tc, test_shorthand_catalogue);
    tcase_add_loop_test(tc, test_aural_values_survive_edits, 0, 2);
    tcase_add_loop_test(tc, test_lab_values_survive_edits, 0, 2);
    tcase_add_loop_test(tc, test_srgb_values_survive_edits, 0, 2);
    tcase_add_loop_test(tc, test_extended_polar_values_survive_edits, 0, 2);
    tcase_add_loop_test(tc, test_calc_values_survive_edits, 0, 2);
    tcase_add_loop_test(tc, test_background_repeat_edits, 0, 2);
    tcase_add_test(tc, test_nonfinite_values_survive_edits);
    tcase_add_loop_test(tc, test_pending_sources_survive_edits, 0, 2);
    tcase_add_test(tc, test_length_and_borrowed_names);
    tcase_add_test(tc, test_repeated_edits_release_sources);
    tcase_add_test(tc, test_shared_source_lifetime);
    tcase_add_loop_test(tc, test_set_property_input, 0, INPUT_COUNT);
    tcase_add_loop_test(tc, test_whole_value_chunks, 0, INPUT_COUNT);
    tcase_add_test(tc, test_input_lengths_and_arguments);
    tcase_add_loop_test(tc, test_url_grammar_in_stylesheets, 0, 2);
    tcase_add_test(tc, test_invalid_value_chunks);
    tcase_add_test(tc, test_bad_arguments);
#ifdef WISP_TEST_ALLOC_FAILURE
    tcase_add_loop_test(tc, test_create_allocation_failure, 0, 3);
    tcase_add_loop_test(tc, test_set_allocation_failure, 0, 3);
    tcase_add_loop_test(tc, test_input_allocation_failure, 0, 6);
#endif
    suite_add_tcase(suite, tc);
    SRunner *runner = srunner_create(suite);
    srunner_set_fork_status(runner, CK_NOFORK);
    srunner_run_all(runner, CK_NORMAL);
    int failed = srunner_ntests_failed(runner);
    srunner_free(runner);
    unsigned leaked = 0;
    lwc_iterate_strings(unexpected_string, &leaked);
    return failed || leaked ? EXIT_FAILURE : EXIT_SUCCESS;
}

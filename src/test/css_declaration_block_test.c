/* CSSOM section 6.6 declaration-block serialization. Expected strings come from
 * the specified shorthand preference, ordering, priority and escaping rules. */
#include <check.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "parse/declaration_block.h"
#include "parse/declaration_value.h"
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

static css_declaration_list *parse(const char *css, bool chunks)
{
    css_stylesheet_params params = {.params_version = CSS_STYLESHEET_PARAMS_VERSION_1,
        .level = CSS_LEVEL_DEFAULT,
        .charset = "UTF-8",
        .url = "https://example.org/",
        .resolve = resolve_url};
    css_declaration_list *list;
    ck_assert_int_eq(css__declaration_list_create(&params, &list), CSS_OK);
    size_t length = strlen(css);
    for (size_t pos = 0; pos < length;) {
        size_t n = chunks ? 1 : length;
        css_error error = css__declaration_list_append(list, (const uint8_t *)css + pos, n);
        ck_assert_msg(error == CSS_OK || error == CSS_NEEDDATA, "parse: %s: %d", css, error);
        pos += n;
    }
    ck_assert_int_eq(css__declaration_list_complete(list), CSS_OK);
    return list;
}

static const struct {
    const char *input, *expected;
    bool pending; /* Partial pending shorthands serialize empty longhands, not round-trippable values. */
} cases[] = {
    {"background-repeat:repeat-x,repeat-y;background-repeat-x:no-repeat",
        "background-repeat-y: no-repeat, repeat; background-repeat-x: no-repeat;"},
    {"background-repeat-x:repeat,no-repeat;background-repeat-y:no-repeat,repeat",
        "background-repeat: repeat-x, repeat-y;"},
    {"background:repeat-y,repeat-x red;background-repeat-x:no-repeat,repeat",
        "background: repeat-y, repeat-x red;"},
    {"background-repeat:repeat-x!important;background-repeat-y:repeat",
        "background-repeat: repeat-x !important;"},
    {"background-repeat:var(--R);background-repeat-x:no-repeat",
        "background-repeat-y: ; background-repeat-x: no-repeat;", true},
    {"background-repeat-x:inherit;background-repeat-y:initial",
        "background-repeat-x: inherit; background-repeat-y: initial;"},
    {"font-variant:small-caps;font-variant-caps:normal;text-decoration:underline;text-decoration-line:overline",
        "font-variant: normal; text-decoration: overline;"},
    {"font-variant-caps:small-caps!important;font-variant:normal;"
     "text-decoration-line:overline!important;text-decoration:underline",
        "font-variant: small-caps !important; text-decoration: overline !important;"},
    {"font:italic small-caps 16px serif;font-variant-caps:normal",
        "font: italic 16px serif;"},
    {"font-variant-caps:var(--Caps);text-decoration-line:var(--Lines)",
        "font-variant-caps: var(--Caps); text-decoration-line: var(--Lines);"},
    {"font-variant:var(--Caps);text-decoration:var(--Lines)",
        "font-variant: var(--Caps); text-decoration: var(--Lines);"},
    {"font:var(--Font);font-variant-caps:small-caps",
        "font-style: ; font-weight: ; font-size: ; line-height: ; font-family: ; font-variant: small-caps;", true},
    {"background:linear-gradient(hsl(30 200 40),hwb(0 -10 10));border:solid hsl(30 200 40)!important",
        "background: linear-gradient(rgb(255, 102, 0), rgb(229.5, 0, 0)); border: solid rgb(255, 102, 0) !important;"},
    {"background:linear-gradient(rgb(none 255 0),color(srgb 0 0 100%)) color(srgb .5 0 0);"
     "border:solid color(srgb 50% .25 none / 50%)!important",
        "background: linear-gradient(color(srgb none 1 0), color(srgb 0 0 1)) color(srgb 0.5 0 0); "
        "border: solid color(srgb 0.5 0.25 none / 0.5) !important;"},
    {"azimuth:BEHIND LEFT;elevation:0;pause-before:1000MS;pause-after:1S;"
     "pitch-range:50;pitch:2KHZ;play-during:url(A.ogg) repeat mix;richness:25;"
     "speak-header:ONCE;speak-numeral:DIGITS;speak-punctuation:CODE;speak:NONE;"
     "speech-rate:120;stress:50;voice-family:One Two,MALE;volume:200%;cue:none URL(A.ogg)",
        "azimuth: 220deg; elevation: 0deg; pause: 1s; pitch-range: 50; pitch: 2khz; "
        "play-during: url(\"A.ogg\") mix repeat; richness: 25; speak-header: once; speak-numeral: digits; "
        "speak-punctuation: code; speak: none; speech-rate: 120; stress: 50; voice-family: \"One Two\", male; "
        "volume: 200%; cue: none url(\"A.ogg\");"},
    {"background:linear-gradient(lab(50% 0 0),oklab(50% 0 0)) lch(50% 0 0);"
     "border:solid oklch(54% 25% -90deg)!important;color:lab(none none none / none)",
        "background: linear-gradient(lab(50 0 0), oklab(0.5 0 0)) lch(50 0 0); "
        "border: solid oklch(0.54 0.1 270) !important; color: lab(none none none / none);"},
    {"pause:1000ms;pause-after:2s!important", "pause-before: 1s; pause-after: 2s !important;"},
    {"cue:url(A.ogg);cue-before:none", "cue: none url(\"A.ogg\");"},
    {"cue:var(--Cue);cue-before:none", "cue-after: ; cue-before: none;", true},
    {"fill:#123;stroke:CURRENTcolor;fill-opacity:2;stroke-opacity:-.25;stroke-width:+2;cursor:url(Cur.cur),POINTER!important",
        "fill: rgb(17, 34, 51); stroke: currentcolor; fill-opacity: 2; stroke-opacity: -0.25; "
        "stroke-width: 2; cursor: url(\"Cur.cur\"), pointer !important;"},

    {"grid-template-columns:repeat(+02,minmax(0,1FR) 2px);grid-template-rows:NONE;grid-auto-flow:dense column!important",
        "grid-template-columns: repeat(2, minmax(0px, 1fr) 2px); grid-template-rows: none; grid-auto-flow: column dense !important;"},
    {"transform:translateX(1.23456789em) scale(50%,.5) rotate(-0);color:red",
        "transform: translateX(1.234568em) scale(0.5) rotate(0deg); color: red;"},
    {"background:linear-gradient(hsl(none 100 50),hwb(120 none 0));border:solid hsl(0 none 50 / none)",
        "background: linear-gradient(hsl(none 100% 50%), hwb(120 none 0%)); border: solid hsl(0 none 50% / none);"},

    {"", ""},
    {"width:garbage;unknown-name:1;", ""},
    {"CoLoR:RED; opacity:+.5000001", "color: red; opacity: 0.5;"},
    {"width:1px;color:red;width:2px", "color: red; width: 2px;"},
    {"width:1px!important;color:red;width:2px", "width: 1px !important; color: red;"},
    {"margin:1px 2px 1px 2px", "margin: 1px 2px;"},
    {"margin-right:2px; margin-left:2px; margin-top:1px; margin-bottom:1px", "margin: 1px 2px;"},
    {"margin-top:1px;color:red;margin-right:2px;margin-bottom:1px;margin-left:2px", "margin: 1px 2px; color: red;"},
    {"margin:1px;margin-left:2px!important",
        "margin-top: 1px; margin-right: 1px; margin-bottom: 1px; margin-left: 2px !important;"},
    {"margin:1px!important;margin-left:2px", "margin: 1px !important;"},
    {"margin:initial", "margin: initial;"},
    {"margin:initial;margin-left:inherit",
        "margin-top: initial; margin-right: initial; margin-bottom: initial; margin-left: inherit;"},
    {"margin:1px;margin-block-start:2px;margin-left:3px",
        "margin-top: 1px; margin-right: 1px; margin-bottom: 1px; margin-block-start: 2px; margin-left: 3px;"},
    {"margin-block-start:2px;margin:1px", "margin-block-start: 2px; margin: 1px;"},
    {"margin:1px;margin-block-start:2px", "margin: 1px; margin-block-start: 2px;"},
    {"margin-block-start:2px; margin-top:4px; margin-block-end:3px",
        "margin-block-start: 2px; margin-top: 4px; margin-block-end: 3px;"},
    {"margin-block-start:2px; color:red; margin-block-end:3px", "margin-block: 2px 3px; color: red;"},
    {"margin-top:1px;margin-inline:3px; margin-right:2px;margin-bottom:1px;margin-left:2px",
        "margin-top: 1px; margin-inline: 3px; margin-right: 2px; margin-bottom: 1px; margin-left: 2px;"},
    {"margin-top:1px;padding-inline:3px; margin-right:2px;margin-bottom:1px;margin-left:2px",
        "margin: 1px 2px; padding-inline: 3px;"},
    {"padding:1px;padding-block-start:2px;padding-left:3px",
        "padding-top: 1px; padding-right: 1px; padding-bottom: 1px; padding-block-start: 2px; padding-left: 3px;"},
    {"inset:1px;inset-inline:2px;left:3px", "top: 1px; right: 1px; bottom: 1px; inset-inline: 2px; left: 3px;"},
    {"border:red solid 1px", "border: 1px solid red;"},
    {"border:1px solid red;border-left-color:blue",
        "border-width: 1px; border-style: solid; border-color: red red red blue;"},
    {"border-top:1px solid red;border-right:2px solid blue",
        "border-top: 1px solid red; border-right: 2px solid blue;"},
    {"border:initial", "border: initial;"},
    {"font:normal italic 16px/1.5 sans-serif", "font: italic 16px / 1.5 sans-serif;"},
    {"font-size:16px;font-family:One Two", "font-size: 16px; font-family: \"One Two\";"},
    {"background:url(a.png) no-repeat red", "background: url(\"a.png\") no-repeat red;"},
    {"background-image:url(a),url(b);background-position:0% 0%;background-repeat:repeat;"
     "background-attachment:scroll;background-color:red",
        "background-image: url(\"a\"), url(\"b\"); background-position: 0% 0%; background-repeat: repeat; "
        "background-attachment: scroll; background-color: red;"},
    {"flex:1;flex-flow:row wrap", "flex: 1 1 0px; flex-flow: wrap;"},
    {"list-style:none inside;columns:12em auto", "list-style: inside none; columns: 12em;"},
    {"grid-row:2 / 4;grid-column:3 / 5", "grid-area: 2 / 3 / 4 / 5;"},
    {"GRID-GAP:2px 3px;grid-row-gap:4px", "gap: 4px 3px;"},
    {"grid-gap:2px!important;column-gap:3px!important", "gap: 2px 3px !important;"},
    {"--Tone: MiXeD/**/Tokens ;--tone:other;--Tone:last", "--tone: other; --Tone: last;"},
    {"--a:;--b: \t !important;--c:/**/", "--a: ; --b:  !important; --c: /**/;"},
    {"--a\\:b:one;--a\\;b:two;--a\\ b:three;--\\31 :four;--știri:five",
        "--a\\:b: one; --a\\;b: two; --a\\ b: three; --1: four; --știri: five;"},
    {"--x\\7f y:one;--x\\\\y:two", "--x\\7f y: one; --x\\\\y: two;"},
    {"width:var(--Width, 12PX);--Width: 10px", "width: var(--Width, 12PX); --Width: 10px;"},
    {"margin:var(--M, 1px  2px)!important", "margin: var(--M, 1px  2px) !important;"},
    {"grid-gap:var(--G);color:red", "gap: var(--G); color: red;"},
    {"margin:var(--M);margin-left:1px", "margin-top: ; margin-right: ; margin-bottom: ; margin-left: 1px;", true},
    {"border:var(--B);border-left-color:blue",
        "border-top-width: ; border-right-width: ; border-bottom-width: ; border-left-width: ; "
        "border-top-style: ; border-right-style: ; border-bottom-style: ; border-left-style: ; "
        "border-top-color: ; border-right-color: ; border-bottom-color: ; border-left-color: blue;",
        true},
    /* Unsupported values must fail atomically, even after serializable values. */
    {"color:red;width:calc(100% - 2px)", "color: red; width: calc(100% - 2px);"},
    {"color:red;width:calc(1px / 0)", "color: red; width: calc(infinity * 1px);"},
    {"margin:1px;background-image:linear-gradient(red,blue)",
        "margin: 1px; background-image: linear-gradient(red, blue);"},
    {"color:red;grid-template-columns:20px 1fr", "color: red; grid-template-columns: 20px 1fr;"},
};
enum { CASE_COUNT = sizeof(cases) / sizeof(cases[0]) };

static const css_declaration_contribution *find_value(const css_declaration_order *order, const char *name)
{
    for (size_t i = 0; i < css__declaration_order_count(order); i++) {
        const css_declaration_contribution *value = css__declaration_order_get(order, i);
        if (strcmp(value->name, name) == 0)
            return value;
    }
    return NULL;
}

static void check_roundtrip(const css_declaration_order *before, lwc_string *serialized)
{
    css_declaration_list *list = parse(lwc_string_data(serialized), false);
    css_declaration_order *after;
    ck_assert_int_eq(css__declaration_order_create(list, &after), CSS_OK);
    ck_assert_uint_eq(css__declaration_order_count(before), css__declaration_order_count(after));
    for (size_t i = 0; i < css__declaration_order_count(before); i++) {
        const css_declaration_contribution *old = css__declaration_order_get(before, i);
        const css_declaration_contribution *new = find_value(after, old->name);
        ck_assert_msg(new != NULL, "round trip lost %s in %s", old->name, lwc_string_data(serialized));
        ck_assert_int_eq(old->source->important, new->source->important);
        lwc_string *a, *b;
        ck_assert_int_eq(css__declaration_value_serialize(old, &a), CSS_OK);
        ck_assert_int_eq(css__declaration_value_serialize(new, &b), CSS_OK);
        ck_assert_msg(a == b, "round trip changed %s: %s -> %s", old->name, lwc_string_data(a), lwc_string_data(b));
        lwc_string_unref(a);
        lwc_string_unref(b);
    }
    lwc_string *again;
    ck_assert_int_eq(css__declaration_block_serialize(after, &again), CSS_OK);
    ck_assert_str_eq(lwc_string_data(serialized), lwc_string_data(again));
    lwc_string_unref(again);
    css__declaration_order_destroy(after);
    css__declaration_list_destroy(list);
}

START_TEST(test_block)
{
    size_t index = (size_t)_i % CASE_COUNT;
    css_declaration_list *list = parse(cases[index].input, _i >= CASE_COUNT);
    css_declaration_order *order;
    ck_assert_int_eq(css__declaration_order_create(list, &order), CSS_OK);
    lwc_string *out = NULL;
    css_error error = css__declaration_block_serialize(order, &out);
    if (cases[index].expected == NULL) {
        ck_assert_int_eq(error, CSS_INVALID);
        ck_assert_ptr_null(out);
    } else {
        ck_assert_msg(error == CSS_OK, "case %zu: %s: %d", index, cases[index].input, error);
        ck_assert_msg(strcmp(lwc_string_data(out), cases[index].expected) == 0, "case %zu: %s\nexpected %s\ngot %s",
            index, cases[index].input, cases[index].expected, lwc_string_data(out));
        if (!cases[index].pending)
            check_roundtrip(order, out);
        lwc_string_unref(out);
    }
    css__declaration_order_destroy(order);
    css__declaration_list_destroy(list);
}
END_TEST

START_TEST(test_bad_arguments)
{
    lwc_string *out = (void *)1;
    ck_assert_int_eq(css__declaration_block_serialize(NULL, &out), CSS_BADPARM);
    ck_assert_ptr_null(out);
    ck_assert_int_eq(css__declaration_block_serialize(NULL, NULL), CSS_BADPARM);
}
END_TEST

START_TEST(test_large_custom_block)
{
    enum { COUNT = 512 };
    char *css = malloc(COUNT * 64);
    ck_assert_ptr_nonnull(css);
    size_t used = 0;
    for (size_t i = 0; i < COUNT; i++)
        used += (size_t)sprintf(css + used, "--v%zu:Old!important;--v%zu:new;", i, i);
    css_declaration_list *list = parse(css, false);
    free(css);
    css_declaration_order *order;
    ck_assert_int_eq(css__declaration_order_create(list, &order), CSS_OK);
    lwc_string *out;
    ck_assert_int_eq(css__declaration_block_serialize(order, &out), CSS_OK);
    ck_assert_uint_eq(css__declaration_order_count(order), COUNT);
    check_roundtrip(order, out);
    lwc_string_unref(out);
    css__declaration_order_destroy(order);
    css__declaration_list_destroy(list);
}
END_TEST

START_TEST(test_shorthand_catalogue)
{
    size_t count;
    const struct css_prop_entry *const *properties = css_prop_shorthands(&count);
    for (size_t i = 0; i < count; i++) {
        char css[128], expected[128];
        snprintf(css, sizeof(css), "%s:INITIAL!important", properties[i]->name);
        snprintf(expected, sizeof(expected), "%s: initial !important;", properties[i]->name);
        css_declaration_list *list = parse(css, false);
        css_declaration_order *order;
        ck_assert_int_eq(css__declaration_order_create(list, &order), CSS_OK);
        ck_assert_msg(
            css__declaration_order_count(order) == properties[i]->longhand_count, "catalogue membership: %s", css);
        lwc_string *out;
        ck_assert_int_eq(css__declaration_block_serialize(order, &out), CSS_OK);
        ck_assert_str_eq(lwc_string_data(out), expected);
        check_roundtrip(order, out);
        css__declaration_order_destroy(order);
        css__declaration_list_destroy(list);
        /* The result is owned independently of the source declarations. */
        ck_assert_str_eq(lwc_string_data(out), expected);
        lwc_string_unref(out);
    }
}
END_TEST

START_TEST(test_margin_permutations)
{
    static const char *const declarations[] = {
        "margin-top: 1px;", "margin-right: 2px;", "margin-bottom: 3px;", "margin-left: 4px;"};
    unsigned permutation[4] = {0, 1, 2, 3}, n = (unsigned)_i;
    for (unsigned remaining = 4; remaining > 0; remaining--) {
        unsigned pick = n % remaining, selected = permutation[pick];
        n /= remaining;
        memmove(permutation + pick, permutation + pick + 1, (remaining - pick - 1) * sizeof(*permutation));
        permutation[remaining - 1] = selected;
    }
    for (unsigned insertion = 0; insertion <= 4; insertion++) {
        char css[256] = "", expected[256] = "";
        const char *logical = "margin-block-start: 8px;";
        for (unsigned i = 0; i <= 4; i++) {
            if (i == insertion)
                strcat(css, logical);
            if (i < 4)
                strcat(css, declarations[permutation[i]]);
        }
        if (insertion == 0)
            snprintf(expected, sizeof(expected), "%s margin: 1px 2px 3px 4px;", logical);
        else if (insertion == 4)
            snprintf(expected, sizeof(expected), "margin: 1px 2px 3px 4px; %s", logical);
        else {
            for (unsigned i = 0; i < 4; i++) {
                if (i > 0)
                    strcat(expected, " ");
                if (i == insertion) {
                    strcat(expected, logical);
                    strcat(expected, " ");
                }
                strcat(expected, declarations[permutation[i]]);
            }
        }
        css_declaration_list *list = parse(css, false);
        css_declaration_order *order;
        ck_assert_int_eq(css__declaration_order_create(list, &order), CSS_OK);
        lwc_string *out;
        ck_assert_int_eq(css__declaration_block_serialize(order, &out), CSS_OK);
        ck_assert_msg(strcmp(lwc_string_data(out), expected) == 0,
            "permutation %d insertion %u: %s\nexpected %s\ngot %s", _i, insertion, css, expected, lwc_string_data(out));
        check_roundtrip(order, out);
        lwc_string_unref(out);
        css__declaration_order_destroy(order);
        css__declaration_list_destroy(list);
    }
}
END_TEST

#ifdef WISP_TEST_ALLOC_FAILURE
START_TEST(test_allocation_failure)
{
    css_declaration_list *list = parse("--a\\:b: MiXeD/**/value; margin:1px 2px; border:solid red;"
                                       "font:italic 16px One Two,serif; grid-gap:var(--g); width:12em!important",
        false);
    css_declaration_order *order;
    ck_assert_int_eq(css__declaration_order_create(list, &order), CSS_OK);
    void (*limit)(unsigned) = _i == 0 ? malloc_limit : calloc_limit;
    bool succeeded = false;
    for (unsigned n = 0; n < 512; n++) {
        lwc_string *out = (void *)1;
        limit(n);
        css_error error = css__declaration_block_serialize(order, &out);
        limit(UINT_MAX);
        if (error == CSS_OK) {
            ck_assert_uint_gt(n, 0);
            check_roundtrip(order, out);
            lwc_string_unref(out);
            succeeded = true;
            break;
        }
        ck_assert_msg(error == CSS_NOMEM, "allocator %d failure %u: %d", _i, n, error);
        ck_assert_ptr_null(out);
    }
    ck_assert(succeeded);
    css__declaration_order_destroy(order);
    css__declaration_list_destroy(list);
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
    Suite *suite = suite_create("CSS declaration block");
    TCase *tc = tcase_create("Specified cssText");
    tcase_add_loop_test(tc, test_block, 0, CASE_COUNT * 2);
    tcase_add_test(tc, test_bad_arguments);
    tcase_add_test(tc, test_large_custom_block);
    tcase_add_test(tc, test_shorthand_catalogue);
    tcase_add_loop_test(tc, test_margin_permutations, 0, 24);
#ifdef WISP_TEST_ALLOC_FAILURE
    tcase_add_loop_test(tc, test_allocation_failure, 0, 2);
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

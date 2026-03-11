/*
 * Unit tests for CSS var() resolution (Phase 5)
 *
 * Tests css__resolve_var_value(): variable lookup, fallback handling,
 * tokenization, re-parsing through property handlers, and error cases.
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include <libwapcaplet/libwapcaplet.h>
#include <libcss/libcss.h>

#include "bytecode/bytecode.h"
#include "bytecode/opcodes.h"
#include "stylesheet.h"
#include "select/variables.h"

#define UNUSED(x) ((void)(x))

static css_error resolve_url(void *pw, const char *base,
    lwc_string *rel, lwc_string **abs)
{
    UNUSED(pw);
    UNUSED(base);
    *abs = lwc_string_ref(rel);
    return CSS_OK;
}

/**
 * Create a minimal stylesheet for testing.
 * The stylesheet provides string storage and propstrings.
 */
static css_stylesheet *create_test_sheet(void)
{
    css_stylesheet *sheet = NULL;
    css_stylesheet_params params;

    memset(&params, 0, sizeof(params));
    params.params_version = CSS_STYLESHEET_PARAMS_VERSION_1;
    params.level = CSS_LEVEL_21;
    params.charset = "UTF-8";
    params.url = "test://var-resolve";
    params.title = NULL;
    params.allow_quirks = false;
    params.inline_style = false;
    params.resolve = resolve_url;
    params.resolve_pw = NULL;
    params.import = NULL;
    params.import_pw = NULL;
    params.color = NULL;
    params.color_pw = NULL;
    params.font = NULL;
    params.font_pw = NULL;

    assert(css_stylesheet_create(&params, &sheet) == CSS_OK);
    assert(sheet != NULL);

    /* Mark as complete so propstrings etc. are usable */
    assert(css_stylesheet_data_done(sheet) == CSS_OK);

    return sheet;
}

int main(void)
{
    css_stylesheet *sheet;
    css_var_context *var_ctx = NULL;
    css_style *resolved = NULL;
    css_error error;
    lwc_string *name_color, *name_missing;
    lwc_string *val_blue, *val_red, *val_invalid;

    /* Intern test strings */
    assert(lwc_intern_string("--color", 7, &name_color) == lwc_error_ok);
    assert(lwc_intern_string("--missing", 9, &name_missing) == lwc_error_ok);
    assert(lwc_intern_string("blue", 4, &val_blue) == lwc_error_ok);
    assert(lwc_intern_string("red", 3, &val_red) == lwc_error_ok);
    assert(lwc_intern_string("not-a-color", 11, &val_invalid) == lwc_error_ok);

    /* Create test stylesheet */
    sheet = create_test_sheet();

    /* Create variable context with some variables */
    error = css__variables_ctx_create(&var_ctx);
    assert(error == CSS_OK);

    error = css__variables_ctx_set(var_ctx, name_color, val_blue);
    assert(error == CSS_OK);

    /* ---- Test 1: Basic resolution ----
     * Variable --color = "blue", resolve as CSS_PROP_COLOR.
     * Should successfully parse "blue" and produce bytecode. */
    resolved = NULL;
    error = css__resolve_var_value(
        CSS_PROP_COLOR, name_color, NULL, var_ctx, sheet, &resolved);
    assert(error == CSS_OK);
    assert(resolved != NULL);
    assert(resolved->used > 0);
    /* The bytecode should contain a valid color OPV */
    css_code_t opv = *resolved->bytecode;
    assert(getOpcode(opv) == CSS_PROP_COLOR);
    css__stylesheet_style_destroy(resolved);
    printf("Test 1 (basic resolution): PASS\n");

    /* ---- Test 2: Fallback on missing variable ----
     * Variable --missing not in context, fallback = "red".
     * Should parse "red" successfully. */
    resolved = NULL;
    error = css__resolve_var_value(
        CSS_PROP_COLOR, name_missing, val_red, var_ctx, sheet, &resolved);
    assert(error == CSS_OK);
    assert(resolved != NULL);
    assert(resolved->used > 0);
    opv = *resolved->bytecode;
    assert(getOpcode(opv) == CSS_PROP_COLOR);
    css__stylesheet_style_destroy(resolved);
    printf("Test 2 (fallback): PASS\n");

    /* ---- Test 3: No variable, no fallback → CSS_INVALID ----
     * Variable --missing not in context, no fallback. */
    resolved = NULL;
    error = css__resolve_var_value(
        CSS_PROP_COLOR, name_missing, NULL, var_ctx, sheet, &resolved);
    assert(error == CSS_INVALID);
    assert(resolved == NULL);
    printf("Test 3 (no var, no fallback): PASS\n");

    /* ---- Test 4: Invalid value for property type ----
     * Variable --color exists but set to "not-a-color",
     * which is not valid for CSS_PROP_COLOR → CSS_INVALID */
    error = css__variables_ctx_set(var_ctx, name_color, val_invalid);
    assert(error == CSS_OK);

    resolved = NULL;
    error = css__resolve_var_value(
        CSS_PROP_COLOR, name_color, NULL, var_ctx, sheet, &resolved);
    assert(error == CSS_INVALID);
    assert(resolved == NULL);
    printf("Test 4 (invalid value): PASS\n");

    /* ---- Test 5: Fallback used when var value is invalid ----
     * Variable --color = "not-a-color" (invalid for color),
     * but since the variable IS found, the invalid value is used
     * (not the fallback). This matches CSS spec behavior:
     * fallback is only used when variable is NOT DEFINED. */
    resolved = NULL;
    error = css__resolve_var_value(
        CSS_PROP_COLOR, name_color, val_red, var_ctx, sheet, &resolved);
    /* Variable IS found (--color), so its value "not-a-color" is used.
     * This is invalid for color → CSS_INVALID.
     * Per CSS spec: fallback is NOT used when the var is defined but invalid. */
    assert(error == CSS_INVALID);
    assert(resolved == NULL);
    printf("Test 5 (var exists but invalid, fallback not used): PASS\n");

    /* ---- Test 6: Restore valid value and retest ---- */
    error = css__variables_ctx_set(var_ctx, name_color, val_blue);
    assert(error == CSS_OK);

    resolved = NULL;
    error = css__resolve_var_value(
        CSS_PROP_COLOR, name_color, NULL, var_ctx, sheet, &resolved);
    assert(error == CSS_OK);
    assert(resolved != NULL);
    assert(resolved->used > 0);
    css__stylesheet_style_destroy(resolved);
    printf("Test 6 (restored valid value): PASS\n");

    /* ---- Test 7: Recursive var() references ----
     * Variable --a = "var(--color)"
     * Resolving var(--a) should yield the value of --color ("blue").
     * Currently our pipeline tokenizes without recursive text resolution,
     * so it will see `var(--color)` as tokens, failing to parse as color. */
    lwc_string *name_a, *val_var_color;
    assert(lwc_intern_string("--a", 3, &name_a) == lwc_error_ok);
    assert(lwc_intern_string("var(--color)", 12, &val_var_color) == lwc_error_ok);

    error = css__variables_ctx_set(var_ctx, name_a, val_var_color);
    assert(error == CSS_OK);

    resolved = NULL;
    error = css__resolve_var_value(
        CSS_PROP_COLOR, name_a, NULL, var_ctx, sheet, &resolved);
    assert(error == CSS_OK); /* This will fail right now because it doesn't parse */
    assert(resolved != NULL);
    assert(resolved->used > 0);
    opv = *resolved->bytecode;
    assert(getOpcode(opv) == CSS_PROP_COLOR);
    css__stylesheet_style_destroy(resolved);
    printf("Test 7 (recursive resolution): PASS\n");

    lwc_string_unref(name_a);
    lwc_string_unref(val_var_color);

    /* Cleanup */
    css__variables_ctx_destroy(var_ctx);
    css_stylesheet_destroy(sheet);

    lwc_string_unref(name_color);
    lwc_string_unref(name_missing);
    lwc_string_unref(val_blue);
    lwc_string_unref(val_red);
    lwc_string_unref(val_invalid);

    printf("PASS\n");
    return 0;
}

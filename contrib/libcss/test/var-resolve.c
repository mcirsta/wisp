/*
 * Unit tests for CSS variable context operations.
 *
 * Tests css__variables_ctx_create/clone/set/get/destroy.
 * Full var() resolution pipeline is tested via integration tests
 * (test/css-vars.html) since css__resolve_var_property() requires
 * a complete css_select_state with computed style.
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include <libwapcaplet/libwapcaplet.h>
#include <libcss/libcss.h>

#include "select/variables.h"

int main(void)
{
    css_var_context *ctx = NULL;
    css_var_context *clone = NULL;
    css_error error;
    lwc_string *name_a, *name_b, *name_c;
    lwc_string *val_blue, *val_red, *val_green;

    /* Intern test strings */
    assert(lwc_intern_string("--a", 3, &name_a) == lwc_error_ok);
    assert(lwc_intern_string("--b", 3, &name_b) == lwc_error_ok);
    assert(lwc_intern_string("--c", 3, &name_c) == lwc_error_ok);
    assert(lwc_intern_string("blue", 4, &val_blue) == lwc_error_ok);
    assert(lwc_intern_string("red", 3, &val_red) == lwc_error_ok);
    assert(lwc_intern_string("#00ff00", 7, &val_green) == lwc_error_ok);

    /* ---- Test 1: Create context ---- */
    error = css__variables_ctx_create(&ctx);
    assert(error == CSS_OK);
    assert(ctx != NULL);
    printf("Test 1 (create): PASS\n");

    /* ---- Test 2: Set and get ---- */
    error = css__variables_ctx_set(ctx, name_a, val_blue);
    assert(error == CSS_OK);
    lwc_string *result = css__variables_ctx_get(ctx, name_a);
    assert(result == val_blue);
    printf("Test 2 (set/get): PASS\n");

    /* ---- Test 3: Get missing variable ---- */
    result = css__variables_ctx_get(ctx, name_b);
    assert(result == NULL);
    printf("Test 3 (get missing): PASS\n");

    /* ---- Test 4: Overwrite existing ---- */
    error = css__variables_ctx_set(ctx, name_a, val_red);
    assert(error == CSS_OK);
    result = css__variables_ctx_get(ctx, name_a);
    assert(result == val_red);
    printf("Test 4 (overwrite): PASS\n");

    /* ---- Test 5: Multiple variables ---- */
    error = css__variables_ctx_set(ctx, name_b, val_blue);
    assert(error == CSS_OK);
    error = css__variables_ctx_set(ctx, name_c, val_green);
    assert(error == CSS_OK);
    assert(css__variables_ctx_get(ctx, name_a) == val_red);
    assert(css__variables_ctx_get(ctx, name_b) == val_blue);
    assert(css__variables_ctx_get(ctx, name_c) == val_green);
    printf("Test 5 (multiple vars): PASS\n");

    /* ---- Test 6: Clone context ---- */
    error = css__variables_ctx_clone(ctx, &clone);
    assert(error == CSS_OK);
    assert(clone != NULL);
    assert(css__variables_ctx_get(clone, name_a) == val_red);
    assert(css__variables_ctx_get(clone, name_b) == val_blue);
    assert(css__variables_ctx_get(clone, name_c) == val_green);
    printf("Test 6 (clone): PASS\n");

    /* ---- Test 7: Clone independence ---- */
    error = css__variables_ctx_set(ctx, name_a, val_green);
    assert(error == CSS_OK);
    /* Original changed, clone should still have old value */
    assert(css__variables_ctx_get(ctx, name_a) == val_green);
    assert(css__variables_ctx_get(clone, name_a) == val_red);
    printf("Test 7 (clone independence): PASS\n");

    /* Cleanup */
    css__variables_ctx_destroy(clone);
    css__variables_ctx_destroy(ctx);

    lwc_string_unref(name_a);
    lwc_string_unref(name_b);
    lwc_string_unref(name_c);
    lwc_string_unref(val_blue);
    lwc_string_unref(val_red);
    lwc_string_unref(val_green);

    printf("PASS\n");
    return 0;
}

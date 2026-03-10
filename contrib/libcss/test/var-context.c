/*
 * Unit tests for CSS variable context (variables.c)
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include <libwapcaplet/libwapcaplet.h>
#include <libcss/errors.h>

#include "select/variables.h"

int main(void)
{
    css_var_context *ctx = NULL;
    css_error error;
    lwc_string *name1, *name2, *name3;
    lwc_string *val1, *val2, *val3;

    /* Intern test strings */
    assert(lwc_intern_string("--color", 7, &name1) == lwc_error_ok);
    assert(lwc_intern_string("--size", 6, &name2) == lwc_error_ok);
    assert(lwc_intern_string("--missing", 9, &name3) == lwc_error_ok);
    assert(lwc_intern_string("blue", 4, &val1) == lwc_error_ok);
    assert(lwc_intern_string("16px", 4, &val2) == lwc_error_ok);
    assert(lwc_intern_string("red", 3, &val3) == lwc_error_ok);

    /* Test 1: Create empty context */
    error = css__variables_ctx_create(&ctx);
    assert(error == CSS_OK);
    assert(ctx != NULL);
    assert(ctx->count == 0);
    printf("Test 1 (create): PASS\n");

    /* Test 2: Get from empty context returns NULL */
    assert(css__variables_ctx_get(ctx, name1) == NULL);
    printf("Test 2 (get empty): PASS\n");

    /* Test 3: Set and get a variable */
    error = css__variables_ctx_set(ctx, name1, val1);
    assert(error == CSS_OK);
    assert(ctx->count == 1);
    assert(css__variables_ctx_get(ctx, name1) == val1);
    printf("Test 3 (set+get): PASS\n");

    /* Test 4: Set a second variable */
    error = css__variables_ctx_set(ctx, name2, val2);
    assert(error == CSS_OK);
    assert(ctx->count == 2);
    assert(css__variables_ctx_get(ctx, name1) == val1);
    assert(css__variables_ctx_get(ctx, name2) == val2);
    printf("Test 4 (second var): PASS\n");

    /* Test 5: Override existing variable */
    error = css__variables_ctx_set(ctx, name1, val3);
    assert(error == CSS_OK);
    assert(ctx->count == 2);  /* Count unchanged — replaced, not appended */
    assert(css__variables_ctx_get(ctx, name1) == val3);
    assert(css__variables_ctx_get(ctx, name2) == val2);
    printf("Test 5 (override): PASS\n");

    /* Test 6: Get missing variable returns NULL */
    assert(css__variables_ctx_get(ctx, name3) == NULL);
    printf("Test 6 (get missing): PASS\n");

    /* Test 7: Clone context */
    css_var_context *clone = NULL;
    error = css__variables_ctx_clone(ctx, &clone);
    assert(error == CSS_OK);
    assert(clone != NULL);
    assert(clone->count == 2);
    assert(css__variables_ctx_get(clone, name1) == val3);
    assert(css__variables_ctx_get(clone, name2) == val2);
    printf("Test 7 (clone): PASS\n");

    /* Test 8: Cloned context is independent — modify original */
    error = css__variables_ctx_set(ctx, name1, val1);
    assert(error == CSS_OK);
    assert(css__variables_ctx_get(ctx, name1) == val1);     /* original changed */
    assert(css__variables_ctx_get(clone, name1) == val3);   /* clone unchanged */
    printf("Test 8 (clone independence): PASS\n");

    /* Test 9: Clone NULL source creates empty */
    css_var_context *empty_clone = NULL;
    error = css__variables_ctx_clone(NULL, &empty_clone);
    assert(error == CSS_OK);
    assert(empty_clone != NULL);
    assert(empty_clone->count == 0);
    printf("Test 9 (clone NULL): PASS\n");

    /* Test 10: Destroy (should not crash/leak) */
    css__variables_ctx_destroy(ctx);
    css__variables_ctx_destroy(clone);
    css__variables_ctx_destroy(empty_clone);
    css__variables_ctx_destroy(NULL);  /* safe no-op */
    printf("Test 10 (destroy): PASS\n");

    /* Clean up interned strings */
    lwc_string_unref(name1);
    lwc_string_unref(name2);
    lwc_string_unref(name3);
    lwc_string_unref(val1);
    lwc_string_unref(val2);
    lwc_string_unref(val3);

    printf("PASS\n");
    return 0;
}

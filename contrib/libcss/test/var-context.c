/* Lifetime and immutability tests for CSS variable environments. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <libwapcaplet/libwapcaplet.h>
#include "select/variables.h"

int main(void)
{
    css_var_context *parent = NULL, *child = NULL, *shared = NULL;
    lwc_string *name, *other, *blue, *red;
    assert(lwc_intern_string("--color", 7, &name) == lwc_error_ok);
    assert(lwc_intern_string("--other", 7, &other) == lwc_error_ok);
    assert(lwc_intern_string("blue", 4, &blue) == lwc_error_ok);
    assert(lwc_intern_string("red", 3, &red) == lwc_error_ok);
    assert(css__variables_ctx_create(&parent) == CSS_OK);
    assert(parent->count == 0 && parent->lookup_valid);
    assert(css__variables_ctx_get(parent, name) == NULL);
    assert(css__variables_ctx_set(parent, name, red) == CSS_OK);
    assert(css__variables_ctx_set(parent, name, blue) == CSS_OK);
    assert(parent->count == 1 && parent->lookup_valid);
    assert(css__variables_ctx_get(parent, name) == blue);
    /* No mutable parent may escape into a child. */
    assert(css__variables_ctx_clone_inherited(parent, &child) == CSS_BADPARM);
    assert(css__variables_ctx_finalize(&parent) == CSS_OK);
    assert(parent->finalized);
    assert(css__variables_ctx_set(parent, name, red) == CSS_BADPARM);
    assert(css__variables_ctx_clone_inherited(parent, &shared) == CSS_OK);
    assert(css__variables_ctx_finalize(&shared) == CSS_OK);
    assert(shared == parent); /* Empty overlay reuses the completed environment. */
    assert(css__variables_ctx_clone_inherited(parent, &child) == CSS_OK);
    assert(css__variables_ctx_set(child, name, red) == CSS_OK);
    assert(css__variables_ctx_finalize(&child) == CSS_OK);
    assert(child != parent && child->count == 1);
    assert(css__variables_ctx_get(child, name) == red);
    assert(css__variables_ctx_get(parent, name) == blue);
    css__variables_ctx_destroy(parent);
    css__variables_ctx_destroy(shared);
    assert(css__variables_ctx_get(child, name) == red);
    assert(css__variables_ctx_get(child->parent, name) == blue);
    /* Exercise both deep lookup and teardown without recursive C calls. */
    for (unsigned i = 0; i < 10000; i++) {
        css_var_context *next;
        assert(css__variables_ctx_clone_inherited(child, &next) == CSS_OK);
        assert(css__variables_ctx_set(next, other, blue) == CSS_OK);
        assert(css__variables_ctx_finalize(&next) == CSS_OK);
        css__variables_ctx_destroy(child);
        child = next;
    }
    assert(css__variables_ctx_get(child, name) == red);
    css__variables_ctx_destroy(child);
    css__variables_ctx_destroy(NULL);
    lwc_string_unref(name);
    lwc_string_unref(other);
    lwc_string_unref(blue);
    lwc_string_unref(red);
    printf("PASS\n");
    return 0;
}

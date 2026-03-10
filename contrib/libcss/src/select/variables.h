/*
 * This file is part of LibCSS
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 */

#ifndef css_select_variables_h_
#define css_select_variables_h_

#include <libwapcaplet/libwapcaplet.h>
#include <libcss/errors.h>

/**
 * A single custom property binding: name → value.
 * Both are ref-counted lwc_strings owned by this entry.
 */
typedef struct css_var_entry {
    lwc_string *name;   /* e.g. "--primary" */
    lwc_string *value;  /* raw CSS text, e.g. "blue" */
} css_var_entry;

/**
 * Per-element variable context: flat array of name→value pairs.
 * Typical size is 0-20 entries; linear scan with lwc pointer
 * comparison is faster than a hash map at these sizes.
 */
typedef struct css_var_context {
    css_var_entry *entries;
    uint32_t count;
    uint32_t capacity;
} css_var_context;

/**
 * Create an empty variable context.
 */
css_error css__variables_ctx_create(css_var_context **out);

/**
 * Clone a variable context (full deep copy with lwc_string_ref).
 * If src is NULL, creates an empty context.
 */
css_error css__variables_ctx_clone(const css_var_context *src, css_var_context **out);

/**
 * Destroy a variable context and unref all strings.
 */
void css__variables_ctx_destroy(css_var_context *ctx);

/**
 * Set a variable in the context. If name already exists, its value
 * is replaced. Both name and value are ref'd by this function.
 */
css_error css__variables_ctx_set(css_var_context *ctx,
    lwc_string *name, lwc_string *value);

/**
 * Look up a variable by name.
 * Returns the value lwc_string (not ref'd — caller must ref if keeping),
 * or NULL if not found.
 */
lwc_string *css__variables_ctx_get(const css_var_context *ctx,
    lwc_string *name);

#endif

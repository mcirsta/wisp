/*
 * This file is part of LibCSS
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 */

#include <stdlib.h>
#include <string.h>

#include "select/variables.h"

#define VAR_CTX_INITIAL_CAPACITY 4

css_error css__variables_ctx_create(css_var_context **out)
{
    css_var_context *ctx = calloc(1, sizeof(*ctx));
    if (ctx == NULL)
        return CSS_NOMEM;

    *out = ctx;
    return CSS_OK;
}

css_error css__variables_ctx_clone(const css_var_context *src, css_var_context **out)
{
    css_var_context *ctx;
    css_error error;

    error = css__variables_ctx_create(&ctx);
    if (error != CSS_OK)
        return error;

    if (src != NULL && src->count > 0) {
        ctx->entries = malloc(src->count * sizeof(css_var_entry));
        if (ctx->entries == NULL) {
            free(ctx);
            return CSS_NOMEM;
        }

        ctx->capacity = src->count;
        ctx->count = src->count;

        for (uint32_t i = 0; i < src->count; i++) {
            ctx->entries[i].name = lwc_string_ref(src->entries[i].name);
            ctx->entries[i].value = lwc_string_ref(src->entries[i].value);
        }
    }

    *out = ctx;
    return CSS_OK;
}

void css__variables_ctx_destroy(css_var_context *ctx)
{
    if (ctx == NULL)
        return;

    for (uint32_t i = 0; i < ctx->count; i++) {
        lwc_string_unref(ctx->entries[i].name);
        lwc_string_unref(ctx->entries[i].value);
    }

    free(ctx->entries);
    free(ctx);
}

css_error css__variables_ctx_set(css_var_context *ctx,
    lwc_string *name, lwc_string *value)
{
    /* Check for existing entry (pointer comparison — interned strings) */
    for (uint32_t i = 0; i < ctx->count; i++) {
        if (ctx->entries[i].name == name) {
            /* Replace value */
            lwc_string_unref(ctx->entries[i].value);
            ctx->entries[i].value = lwc_string_ref(value);
            return CSS_OK;
        }
    }

    /* New entry — grow if needed */
    if (ctx->count >= ctx->capacity) {
        uint32_t new_cap = ctx->capacity == 0
            ? VAR_CTX_INITIAL_CAPACITY
            : ctx->capacity * 2;
        css_var_entry *new_entries = realloc(ctx->entries,
            new_cap * sizeof(css_var_entry));
        if (new_entries == NULL)
            return CSS_NOMEM;
        ctx->entries = new_entries;
        ctx->capacity = new_cap;
    }

    ctx->entries[ctx->count].name = lwc_string_ref(name);
    ctx->entries[ctx->count].value = lwc_string_ref(value);
    ctx->count++;

    return CSS_OK;
}

lwc_string *css__variables_ctx_get(const css_var_context *ctx,
    lwc_string *name)
{
    if (ctx == NULL)
        return NULL;

    for (uint32_t i = 0; i < ctx->count; i++) {
        if (ctx->entries[i].name == name)
            return ctx->entries[i].value;
    }

    return NULL;
}

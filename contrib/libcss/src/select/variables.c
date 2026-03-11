/*
 * This file is part of LibCSS
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 */

#include <stdlib.h>
#include <string.h>

#include "select/variables.h"
#include "utils/utils.h"
#include "lex/lex.h"
#include "utils/parserutilserror.h"
#include "utils/css_utils.h"
#include "bytecode/bytecode.h"
#include "select/dispatch.h"
#include "select/select.h"

#include <parserutils/utils/vector.h>
#include <libcss/functypes.h>
#include <libcss/stylesheet.h>
#include <libcss/properties.h>
#include "parse/properties/properties.h"

/* css_prop_entry struct and lookup function from the gperf table
 * (defined in language.c via prop_hash_table.inc). */
struct css_prop_entry {
    const char *name;
    css_prop_handler handler;
    int opcode;
};
const struct css_prop_entry *css_prop_lookup(
    const char *str, size_t len);

#define VAR_CTX_INITIAL_CAPACITY 4

#define MAX_VAR_DEPTH 10

static css_error css__resolve_tokens(
    lwc_string *value,
    const css_var_context *var_ctx,
    parserutils_vector *out_tokens,
    int depth);

/**
 * Handle a `var(` ... `)` sequence from the token stream.
 */
static css_error css__handle_var_function(
    css_lexer *lexer,
    const css_var_context *var_ctx,
    parserutils_vector *out_tokens,
    int depth)
{
    css_error error;
    parserutils_error perr;
    css_token *t = NULL;
    lwc_string *var_name = NULL;
    parserutils_vector *fallback_tokens = NULL;
    bool has_fallback = false;

    /* Skip whitespace */
    while (1) {
        error = css__lexer_get_token(lexer, &t);
        if (error != CSS_OK) goto cleanup;
        if (t->type == CSS_TOKEN_EOF) { error = CSS_INVALID; goto cleanup; }
        if (t->type != CSS_TOKEN_S) break;
    }

    if (t->type != CSS_TOKEN_IDENT && t->type != CSS_TOKEN_CUSTOM_PROPERTY) {
        fprintf(stderr, "var() missing ident or custom property name: got token type %d (data: %.*s)\n", t->type, (int)t->data.len, t->data.data ? (char*)t->data.data : "");
        error = CSS_INVALID; goto cleanup;
    }
    lwc_error lerr = lwc_intern_string((char *)t->data.data, t->data.len, &var_name);
    if (lerr != lwc_error_ok) {
        error = css_error_from_lwc_error(lerr); goto cleanup;
    }

    /* Skip whitespace */
    while (1) {
        error = css__lexer_get_token(lexer, &t);
        if (error != CSS_OK) goto cleanup;
        if (t->type == CSS_TOKEN_EOF) { error = CSS_INVALID; goto cleanup; }
        if (t->type != CSS_TOKEN_S) break;
    }

    if (t->type == CSS_TOKEN_CHAR && t->data.len == 1 && t->data.data[0] == ',') {
        has_fallback = true;
        perr = parserutils_vector_create(sizeof(css_token), 16, &fallback_tokens);
        if (perr != PARSERUTILS_OK) {
            error = css_error_from_parserutils_error(perr); goto cleanup;
        }

        while (1) {
            error = css__lexer_get_token(lexer, &t);
            if (error != CSS_OK) goto cleanup;
            if (t->type == CSS_TOKEN_EOF) { error = CSS_INVALID; goto cleanup; }
            if (t->type != CSS_TOKEN_S) break;
        }

        int parens = 1;
        while (1) {
            if (t->type == CSS_TOKEN_EOF) { error = CSS_INVALID; goto cleanup; }

            if (t->type == CSS_TOKEN_CHAR && t->data.len == 1) {
                if (t->data.data[0] == '(') parens++;
                if (t->data.data[0] == ')') parens--;
            } else if (t->type == CSS_TOKEN_FUNCTION) {
                parens++;
            }

            if (parens == 0) break;

            if (t->type < CSS_TOKEN_LAST_INTERN && t->data.data != NULL) {
                lerr = lwc_intern_string((char *)t->data.data, t->data.len, &t->idata);
                if (lerr != lwc_error_ok) { error = css_error_from_lwc_error(lerr); goto cleanup; }
            } else {
                t->idata = NULL;
            }

            perr = parserutils_vector_append(fallback_tokens, (css_token *)t);
            if (perr != PARSERUTILS_OK) {
                if (t->idata != NULL) lwc_string_unref(t->idata);
                error = css_error_from_parserutils_error(perr); goto cleanup;
            }

            error = css__lexer_get_token(lexer, &t);
            if (error != CSS_OK) goto cleanup;
        }
    } else if (t->type == CSS_TOKEN_CHAR && t->data.len == 1 && t->data.data[0] == ')') {
        has_fallback = false;
    } else {
        fprintf(stderr, "var() missing closing paren\n");
        error = CSS_INVALID; goto cleanup;
    }

    lwc_string *resolved_val = css__variables_ctx_get(var_ctx, var_name);

    if (resolved_val != NULL) {
        size_t initial_len = 0;
        parserutils_vector_get_length(out_tokens, &initial_len);

        error = css__resolve_tokens(resolved_val, var_ctx, out_tokens, depth + 1);

        if (error != CSS_OK) {
            /* If resolving failed (e.g., due to a cycle), rollback the tokens we appended */
            size_t current_len = 0;
            parserutils_vector_get_length(out_tokens, &current_len);
            while (current_len > initial_len) {
                const css_token *tok = parserutils_vector_peek(out_tokens, current_len - 1);
                if (tok != NULL && tok->idata != NULL) lwc_string_unref(tok->idata);
                parserutils_vector_remove_last(out_tokens);
                current_len--;
            }
        }
    } else {
        error = CSS_INVALID;
    }

    if (error != CSS_OK) {
        if (has_fallback) {
            error = CSS_OK;
            int32_t ctx2 = 0;
            const css_token *fb_tok;
            while ((fb_tok = parserutils_vector_iterate(fallback_tokens, &ctx2)) != NULL) {
                css_token copy = *fb_tok;
                if (copy.idata != NULL) {
                    lwc_string_ref(copy.idata);
                }
                perr = parserutils_vector_append(out_tokens, &copy);
                if (perr != PARSERUTILS_OK) {
                    if (copy.idata != NULL) lwc_string_unref(copy.idata);
                    error = css_error_from_parserutils_error(perr); goto cleanup;
                }
            }
        } else {
            goto cleanup;
        }
    }

    error = CSS_OK;
cleanup:
    if (var_name != NULL) lwc_string_unref(var_name);
    if (fallback_tokens != NULL) {
        int32_t ctx = 0;
        const css_token *tok;
        while ((tok = parserutils_vector_iterate(fallback_tokens, &ctx)) != NULL) {
            if (tok->idata != NULL) lwc_string_unref(tok->idata);
        }
        parserutils_vector_destroy(fallback_tokens);
    }
    return error;
}

static css_error css__resolve_tokens(
    lwc_string *value,
    const css_var_context *var_ctx,
    parserutils_vector *out_tokens,
    int depth)
{
    parserutils_inputstream *input = NULL;
    css_lexer *lexer = NULL;
    css_error error = CSS_OK;
    parserutils_error perr;

    if (depth > MAX_VAR_DEPTH) return CSS_INVALID;

    perr = parserutils_inputstream_create("UTF-8", 0, NULL, &input);
    if (perr != PARSERUTILS_OK) { return css_error_from_parserutils_error(perr); }

    const char *data = lwc_string_data(value);
    size_t len = lwc_string_length(value);
    perr = parserutils_inputstream_append(input, (const uint8_t *)data, len);
    if (perr != PARSERUTILS_OK) { error = css_error_from_parserutils_error(perr); goto cleanup; }
    
    perr = parserutils_inputstream_append(input, NULL, 0);
    if (perr != PARSERUTILS_OK) { error = css_error_from_parserutils_error(perr); goto cleanup; }

    error = css__lexer_create(input, &lexer);
    if (error != CSS_OK) goto cleanup;

    while (1) {
        css_token *token = NULL;
        error = css__lexer_get_token(lexer, &token);
        if (error != CSS_OK) goto cleanup;

        if (token->type == CSS_TOKEN_EOF) break;

        if (token->type == CSS_TOKEN_FUNCTION) {
            bool is_var = false;
            lwc_string *func_name = NULL;
            if (token->data.data != NULL) {
                lwc_error lerr = lwc_intern_string((char *)token->data.data, token->data.len, &func_name);
                if (lerr == lwc_error_ok) {
                    if (lwc_string_length(func_name) == 3 &&
                        strncasecmp(lwc_string_data(func_name), "var", 3) == 0) {
                        is_var = true;
                    }
                    lwc_string_unref(func_name);
                }
            }

            if (is_var) {
                error = css__handle_var_function(lexer, var_ctx, out_tokens, depth);
                if (error != CSS_OK) goto cleanup;
                continue;
            }
        }

        if (token->type < CSS_TOKEN_LAST_INTERN && token->data.data != NULL) {
            lwc_error lerr = lwc_intern_string((char *) token->data.data, token->data.len, &token->idata);
            if (lerr != lwc_error_ok) { error = css_error_from_lwc_error(lerr); goto cleanup; }
        } else {
            token->idata = NULL;
        }

        size_t vec_len = 0;
        parserutils_vector_get_length(out_tokens, &vec_len);
        if (token->type == CSS_TOKEN_S && vec_len == 0) {
            if (token->idata != NULL) lwc_string_unref(token->idata);
            continue;
        }

        perr = parserutils_vector_append(out_tokens, (css_token *)token);
        if (perr != PARSERUTILS_OK) {
            if (token->idata != NULL) lwc_string_unref(token->idata);
            error = css_error_from_parserutils_error(perr); goto cleanup;
        }
    }

cleanup:
    if (lexer != NULL) css__lexer_destroy(lexer);
    if (input != NULL) parserutils_inputstream_destroy(input);
    return error;
}

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

css_error css__resolve_var_property(
    lwc_string *prop_name,
    lwc_string *raw_value,
    const css_var_context *var_ctx,
    css_stylesheet *sheet,
    bool important,
    css_select_state *state)
{
    css_error error = CSS_OK;
    parserutils_error perr;
    parserutils_vector *tokens = NULL;
    css_style *result_style = NULL;

    /* Step 1: Resolve all var() references in the raw value text */
    perr = parserutils_vector_create(sizeof(css_token), 16, &tokens);
    if (perr != PARSERUTILS_OK) {
        return css_error_from_parserutils_error(perr);
    }

    error = css__resolve_tokens(raw_value, var_ctx, tokens, 1);
    if (error != CSS_OK) {
        goto cleanup;
    }

    {
        size_t vec_len = 0;
        parserutils_vector_get_length(tokens, &vec_len);
        if (vec_len == 0) {
            error = CSS_INVALID;
            goto cleanup;
        }
    }

    /* Append EOF token for the parser */
    {
        css_token eof_token;
        eof_token.type = CSS_TOKEN_EOF;
        eof_token.data.data = NULL;
        eof_token.data.len = 0;
        eof_token.idata = NULL;
        eof_token.col = 0;
        eof_token.line = 0;
        perr = parserutils_vector_append(tokens, &eof_token);
        if (perr != PARSERUTILS_OK) {
            error = css_error_from_parserutils_error(perr);
            goto cleanup;
        }
    }

    /* Step 2: Look up the property handler by name via gperf */
    const struct css_prop_entry *entry = css_prop_lookup(
        lwc_string_data(prop_name), lwc_string_length(prop_name));
    if (entry == NULL) {
        fprintf(stderr, "  Property '%s' not found in gperf table\n",
            lwc_string_data(prop_name));
        error = CSS_INVALID;
        goto cleanup;
    }

    /* Step 3: Call the handler to parse resolved tokens into bytecode */
    css_language resolve_lang;
    memset(&resolve_lang, 0, sizeof(resolve_lang));
    resolve_lang.sheet = sheet;
    resolve_lang.strings = sheet->propstrings;

    error = css__stylesheet_style_create(sheet, &result_style);
    if (error != CSS_OK) goto cleanup;

    int32_t token_ctx = 0;
    error = entry->handler(&resolve_lang, tokens, &token_ctx, result_style);
    if (error != CSS_OK) {
        css__stylesheet_style_destroy(result_style);
        result_style = NULL;
        error = CSS_INVALID;
        goto cleanup;
    }

    /* Step 4: Cascade all OPVs in the resulting style.
     * Longhands produce 1 OPV; shorthands produce multiple. */
    {
        css_style rs = *result_style;
        while (rs.used > 0) {
            css_code_t result_opv = *rs.bytecode;
            advance_bytecode(&rs, sizeof(result_opv));

            opcode_t result_op = getOpcode(result_opv);

            /* Preserve importance from the original var() declaration */
            if (important)
                result_opv |= FLAG_IMPORTANT;

            if (result_op < CSS_N_PROPERTIES) {
                error = prop_dispatch[result_op].cascade(
                    result_opv, &rs, state);
                if (error != CSS_OK) {
                    css__stylesheet_style_destroy(result_style);
                    goto cleanup;
                }
            }
        }
    }

    css__stylesheet_style_destroy(result_style);
    result_style = NULL;

cleanup:
    if (result_style != NULL) css__stylesheet_style_destroy(result_style);
    if (tokens != NULL) {
        int32_t ctx = 0;
        const css_token *t;
        while ((t = parserutils_vector_iterate(tokens, &ctx)) != NULL) {
            if (t->idata != NULL) lwc_string_unref(t->idata);
        }
        parserutils_vector_destroy(tokens);
    }
    return error;
}

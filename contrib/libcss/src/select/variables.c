/*
 * This file is part of LibCSS.
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 */
#include <assert.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "select/variables.h"
#include "select/select.h"
#include "select/dispatch.h"
#include "bytecode/bytecode.h"
#include "parse/properties/properties.h"
#include "parse/property.h"
#include "lex/lex.h"
#include "utils/parserutilserror.h"
#include "utils/css_utils.h"
#include <libcss/properties.h>
#include <libcss/stylesheet.h>

enum css_var_keyword { VAR_VALUE, VAR_INITIAL, VAR_INHERIT, VAR_REVERT };

/* References include unused fallback branches for cycle detection. Positions
 * let expansion skip an unused fallback without allocating or parsing it. */
typedef struct css_var_reference {
    lwc_string *name; /* Borrowed from this value's owned tokens. */
    int32_t start;
    int32_t after;
    int32_t fallback; /* -1 when absent; after - 1 for an empty fallback. */
} css_var_reference;

struct css_var_value {
    uint32_t refcnt;
    parserutils_vector *tokens;
    lwc_string *raw; /* Used only by the context-test text setter. */
    css_var_reference *refs;
    uint32_t ref_count;
    enum css_var_keyword keyword;
};

struct css_var_lookup {
    lwc_string *name;
    uint32_t index;
};

/* Keep each origin/importance winner until revert has been applied. These
 * records exist only while building a cascade, never in published values. */
struct css_var_candidates {
    struct {
        css_var_value *value;
        uint32_t specificity;
    } slot[CSS_ORIGIN_AUTHOR + 1][2];
};

static void tokens_destroy(parserutils_vector *tokens)
{
    int32_t i = 0;
    const css_token *t;
    if (tokens == NULL)
        return;
    while ((t = parserutils_vector_iterate(tokens, &i)) != NULL) {
        if (t->idata != NULL)
            lwc_string_unref(t->idata);
    }
    parserutils_vector_destroy(tokens);
}

static css_error tokens_append(parserutils_vector *out, const css_token *token)
{
    css_token copy = *token;
    parserutils_error err;
    if (copy.idata != NULL) {
        copy.idata = lwc_string_ref(copy.idata);
        copy.data.data = (uint8_t *)lwc_string_data(copy.idata);
        copy.data.len = lwc_string_length(copy.idata);
    } else {
        /* Non-interned token types are represented by their type alone. */
        copy.data.data = NULL;
        copy.data.len = 0;
    }
    err = parserutils_vector_append(out, &copy);
    if (err != PARSERUTILS_OK && copy.idata != NULL)
        lwc_string_unref(copy.idata);
    return css_error_from_parserutils_error(err);
}

static bool token_char(const css_token *t, char c)
{
    return t != NULL && t->type == CSS_TOKEN_CHAR && t->idata != NULL &&
        lwc_string_length(t->idata) == 1 && lwc_string_data(t->idata)[0] == c;
}

static bool token_word(const css_token *t, const char *word)
{
    return t != NULL && t->idata != NULL &&
        lwc_string_length(t->idata) == strlen(word) &&
        strncasecmp(lwc_string_data(t->idata), word, strlen(word)) == 0;
}

static bool token_var(const css_token *t)
{
    return t->type == CSS_TOKEN_FUNCTION && token_word(t, "var");
}

static css_var_value *value_ref(css_var_value *value)
{
    if (value != NULL)
        value->refcnt++;
    return value;
}

static void value_unref(css_var_value *value)
{
    if (value == NULL || --value->refcnt != 0)
        return;
    tokens_destroy(value->tokens);
    if (value->raw != NULL)
        lwc_string_unref(value->raw);
    free(value->refs);
    free(value);
}

static css_error value_create(css_var_value **out)
{
    css_var_value *value = calloc(1, sizeof(*value));
    parserutils_error err;
    if (value == NULL)
        return CSS_NOMEM;
    value->refcnt = 1;
    err = parserutils_vector_create(sizeof(css_token), 16, &value->tokens);
    if (err != PARSERUTILS_OK) {
        free(value);
        return css_error_from_parserutils_error(err);
    }
    *out = value;
    return CSS_OK;
}

/* Validate and index var() once, retaining nested fallbacks as token ranges. */
static css_error value_index(css_var_value *value)
{
    int32_t i = 0;
    uint32_t capacity = 0;
    const css_token *t;
    const css_token *single = NULL;
    unsigned nonspace = 0;
    while ((t = parserutils_vector_iterate(value->tokens, &i)) != NULL) {
        if (t->type != CSS_TOKEN_S) {
            nonspace++;
            single = t;
        }
        if (token_var(t))
            capacity++;
    }
    if (nonspace == 1 && single->type == CSS_TOKEN_IDENT) {
        if (token_word(single, "initial"))
            value->keyword = VAR_INITIAL;
        else if (token_word(single, "inherit") || token_word(single, "unset"))
            value->keyword = VAR_INHERIT;
        else if (token_word(single, "revert"))
            value->keyword = VAR_REVERT;
    }
    if (capacity == 0)
        return CSS_OK;
    value->refs = calloc(capacity, sizeof(*value->refs));
    if (value->refs == NULL)
        return CSS_NOMEM;
    i = 0;
    while ((t = parserutils_vector_iterate(value->tokens, &i)) != NULL) {
        css_var_reference *ref;
        int32_t j;
        unsigned depth;
        if (!token_var(t))
            continue;
        ref = &value->refs[value->ref_count++];
        ref->start = i - 1;
        ref->fallback = -1;
        j = i;
        do {
            t = parserutils_vector_iterate(value->tokens, &j);
        } while (t != NULL && t->type == CSS_TOKEN_S);
        if (!css__token_is_custom_property(t))
            return CSS_INVALID;
        ref->name = t->idata;
        do {
            t = parserutils_vector_iterate(value->tokens, &j);
        } while (t != NULL && t->type == CSS_TOKEN_S);
        if (token_char(t, ')')) {
            ref->after = j;
            continue;
        }
        if (!token_char(t, ','))
            return CSS_INVALID;
        ref->fallback = j;
        depth = 1;
        while ((t = parserutils_vector_iterate(value->tokens, &j)) != NULL) {
            if (t->type == CSS_TOKEN_FUNCTION || token_char(t, '('))
                depth++;
            else if (token_char(t, ')') && --depth == 0)
                break;
        }
        if (depth != 0)
            return CSS_INVALID;
        ref->after = j;
    }
    return CSS_OK;
}

css_error css__stylesheet_var_value_add(css_stylesheet *sheet,
    const parserutils_vector *tokens, int32_t start, int32_t end, uint32_t *id)
{
    css_var_value *value;
    css_error error = value_create(&value);
    if (error != CSS_OK)
        return error;
    for (int32_t i = start; i < end; i++) {
        const css_token *t = parserutils_vector_peek(tokens, i);
        if (t == NULL) {
            error = CSS_BADPARM;
            goto fail;
        }
        error = tokens_append(value->tokens, t);
        if (error != CSS_OK)
            goto fail;
    }
    error = value_index(value);
    if (error != CSS_OK)
        goto fail;
    if (sheet->var_values_count == sheet->var_values_capacity) {
        uint32_t cap = sheet->var_values_capacity;
        css_var_value **values;
        if (cap > UINT32_MAX / 2) {
            error = CSS_NOMEM;
            goto fail;
        }
        cap = cap == 0 ? 16 : cap * 2;
        if (sizeof(*values) > SIZE_MAX / cap) {
            error = CSS_NOMEM;
            goto fail;
        }
        values = realloc(sheet->var_values, (size_t)cap * sizeof(*values));
        if (values == NULL) {
            error = CSS_NOMEM;
            goto fail;
        }
        sheet->var_values = values;
        sheet->var_values_capacity = cap;
    }
    *id = sheet->var_values_count;
    sheet->var_values[sheet->var_values_count++] = value;
    return CSS_OK;
fail:
    value_unref(value);
    return error;
}

css_var_value *css__stylesheet_var_value_get(css_stylesheet *sheet, uint32_t id)
{
    return id < sheet->var_values_count ? sheet->var_values[id] : NULL;
}

void css__stylesheet_var_values_destroy(css_stylesheet *sheet)
{
    for (uint32_t i = 0; i < sheet->var_values_count; i++)
        value_unref(sheet->var_values[i]);
    free(sheet->var_values);
}

static bool find_local(const css_var_context *ctx, lwc_string *name, uint32_t *index)
{
    if (ctx == NULL || ctx->count == 0)
        return false;
    if (ctx->lookup_valid) {
        uint32_t mask = ctx->lookup_capacity - 1;
        uint32_t slot = lwc_string_hash_value(name) & mask;
        while (ctx->lookup[slot].name != NULL) {
            if (ctx->lookup[slot].name == name) {
                *index = ctx->lookup[slot].index;
                return true;
            }
            slot = (slot + 1) & mask;
        }
        return false;
    }
    for (uint32_t i = 0; i < ctx->count; i++) {
        if (ctx->entries[i].name == name) {
            *index = i;
            return true;
        }
    }
    return false;
}

static css_var_value *find_value(const css_var_context *ctx, lwc_string *name)
{
    uint32_t i;
    for (; ctx != NULL; ctx = ctx->parent) {
        if (find_local(ctx, name, &i))
            return ctx->entries[i].value;
    }
    return NULL;
}

static void lookup_insert(css_var_context *ctx, uint32_t i)
{
    uint32_t mask = ctx->lookup_capacity - 1;
    uint32_t slot = lwc_string_hash_value(ctx->entries[i].name) & mask;
    while (ctx->lookup[slot].name != NULL)
        slot = (slot + 1) & mask;
    ctx->lookup[slot].name = ctx->entries[i].name;
    ctx->lookup[slot].index = i;
}

static void lookup_update(css_var_context *ctx)
{
    if (ctx->lookup_valid && ctx->count <= ctx->lookup_capacity / 2) {
        lookup_insert(ctx, ctx->count - 1);
    } else {
        uint32_t capacity = 8;
        css_var_lookup *lookup;
        while (capacity / 2 < ctx->count) {
            if (capacity > UINT32_MAX / 2) {
                ctx->lookup_valid = false;
                return;
            }
            capacity *= 2;
        }
        lookup = calloc(capacity, sizeof(*lookup));
        if (lookup == NULL) {
            ctx->lookup_valid = false;
            return; /* Linear lookup remains correct on allocation failure. */
        }
        free(ctx->lookup);
        ctx->lookup = lookup;
        ctx->lookup_capacity = capacity;
        ctx->lookup_valid = true;
        for (uint32_t i = 0; i < ctx->count; i++)
            lookup_insert(ctx, i);
    }
}

css_error css__variables_ctx_create(css_var_context **out)
{
    css_var_context *ctx = calloc(1, sizeof(*ctx));
    if (ctx == NULL)
        return CSS_NOMEM;
    ctx->refcnt = 1;
    ctx->lookup_valid = true;
    *out = ctx;
    return CSS_OK;
}

css_var_context *css__variables_ctx_ref(css_var_context *ctx)
{
    if (ctx != NULL)
        ctx->refcnt++;
    return ctx;
}

css_error css__variables_ctx_clone_inherited(css_var_context *parent, css_var_context **out)
{
    css_error error;
    if (parent != NULL && !parent->finalized)
        return CSS_BADPARM;
    error = css__variables_ctx_create(out);
    if (error == CSS_OK)
        (*out)->parent = css__variables_ctx_ref(parent);
    return error;
}

static void candidates_destroy(css_var_candidates *candidates)
{
    if (candidates == NULL)
        return;
    for (unsigned o = 0; o <= CSS_ORIGIN_AUTHOR; o++)
        for (unsigned important = 0; important < 2; important++)
            value_unref(candidates->slot[o][important].value);
    free(candidates);
}

void css__variables_ctx_destroy(css_var_context *ctx)
{
    /* Iterative release: a deep inheritance chain must not use the C stack. */
    while (ctx != NULL && --ctx->refcnt == 0) {
        css_var_context *parent = ctx->parent;
        for (uint32_t i = 0; i < ctx->count; i++) {
            lwc_string_unref(ctx->entries[i].name);
            value_unref(ctx->entries[i].value);
            candidates_destroy(ctx->entries[i].candidates);
        }
        free(ctx->entries);
        free(ctx->lookup);
        free(ctx);
        ctx = parent;
    }
}

css_error css__variables_ctx_cascade(css_var_context *ctx, lwc_string *name,
    css_var_value *value, css_origin origin, uint32_t specificity, bool important)
{
    uint32_t i;
    css_var_candidates *candidates;
    if (ctx == NULL || ctx->finalized || value == NULL ||
        origin < CSS_ORIGIN_UA || origin > CSS_ORIGIN_AUTHOR)
        return CSS_BADPARM;
    if (!find_local(ctx, name, &i)) {
        if (ctx->count == ctx->capacity) {
            uint32_t cap = ctx->capacity;
            css_var_entry *entries;
            if (cap > UINT32_MAX / 2)
                return CSS_NOMEM;
            cap = cap == 0 ? 4 : cap * 2;
            if (sizeof(*entries) > SIZE_MAX / cap)
                return CSS_NOMEM;
            entries = realloc(ctx->entries, (size_t)cap * sizeof(*entries));
            if (entries == NULL)
                return CSS_NOMEM;
            ctx->entries = entries;
            ctx->capacity = cap;
        }
        candidates = calloc(1, sizeof(*candidates));
        if (candidates == NULL)
            return CSS_NOMEM;
        i = ctx->count++;
        ctx->entries[i] = (css_var_entry){lwc_string_ref(name), NULL, candidates};
        lookup_update(ctx);
    }
    candidates = ctx->entries[i].candidates;
    if (candidates->slot[origin][important].value == NULL ||
        specificity >= candidates->slot[origin][important].specificity) {
        value_unref(candidates->slot[origin][important].value);
        candidates->slot[origin][important].value = value_ref(value);
        candidates->slot[origin][important].specificity = specificity;
    }
    return CSS_OK;
}

static css_var_value *cascade_winner(const css_var_candidates *candidates)
{
    int max_origin = CSS_ORIGIN_AUTHOR;
    while (max_origin >= CSS_ORIGIN_UA) {
        css_var_value *value = NULL;
        int origin;
        /* Important origins have reverse precedence: UA, user, author. */
        for (origin = CSS_ORIGIN_UA; origin <= max_origin; origin++) {
            value = candidates->slot[origin][1].value;
            if (value != NULL)
                break;
        }
        if (value == NULL) {
            for (origin = max_origin; origin >= CSS_ORIGIN_UA; origin--) {
                value = candidates->slot[origin][0].value;
                if (value != NULL)
                    break;
            }
        }
        if (value == NULL || value->keyword != VAR_REVERT)
            return value;
        max_origin = origin - 1;
    }
    return NULL; /* Reverting UA origin defaults to inheritance. */
}

static css_error append_expansion(parserutils_vector *out, parserutils_vector *tokens)
{
    size_t n, m;
    int32_t i = 0;
    const css_token *t;
    parserutils_vector_get_length(out, &n);
    parserutils_vector_get_length(tokens, &m);
    if (n > CSS_VAR_MAX_TOKENS || m > CSS_VAR_MAX_TOKENS - n)
        return CSS_INVALID;
    while ((t = parserutils_vector_iterate(tokens, &i)) != NULL) {
        css_error error = tokens_append(out, t);
        if (error != CSS_OK)
            return error;
    }
    return CSS_OK;
}

static const css_var_reference *reference_at(const css_var_value *value, int32_t pos)
{
    uint32_t lo = 0, hi = value->ref_count;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        if (value->refs[mid].start < pos)
            lo = mid + 1;
        else
            hi = mid;
    }
    assert(lo < value->ref_count && value->refs[lo].start == pos);
    return &value->refs[lo];
}

static css_error resolve_range(const css_var_value *value, int32_t start, int32_t end,
    const css_var_context *ctx, parserutils_vector *out, unsigned depth)
{
    if (depth > CSS_VAR_MAX_DEPTH)
        return CSS_INVALID;
    for (int32_t i = start; i < end;) {
        const css_token *t = parserutils_vector_peek(value->tokens, i);
        css_error error;
        if (token_var(t)) {
            const css_var_reference *ref = reference_at(value, i);
            const css_var_value *binding = find_value(ctx, ref->name);
            if (binding != NULL) {
                /* Bindings are completed values, including inherited ones.
                 * Output overflow invalidates the consumer, not the binding. */
                error = append_expansion(out, binding->tokens);
            } else if (ref->fallback >= 0) {
                error = resolve_range(value, ref->fallback, ref->after - 1, ctx, out, depth + 1);
            } else {
                return CSS_INVALID;
            }
            i = ref->after;
        } else {
            size_t n;
            parserutils_vector_get_length(out, &n);
            if (n >= CSS_VAR_MAX_TOKENS)
                return CSS_INVALID;
            error = tokens_append(out, t);
            i++;
        }
        if (error != CSS_OK)
            return error;
    }
    return CSS_OK;
}

static css_error compute_value(css_var_context *ctx, uint32_t i)
{
    css_var_value *raw = ctx->entries[i].value;
    css_var_value *computed = NULL;
    css_error error;
    size_t length;
    if (raw == NULL)
        return CSS_OK;
    parserutils_vector_get_length(raw->tokens, &length);
    if (raw->ref_count == 0) {
        if (length > CSS_VAR_MAX_TOKENS) {
            value_unref(raw);
            ctx->entries[i].value = NULL;
        }
        return CSS_OK; /* Share immutable tokens when no substitution is needed. */
    }
    error = value_create(&computed);
    if (error != CSS_OK)
        return error;
    error = resolve_range(raw, 0, (int32_t)length, ctx, computed->tokens, 0);
    if (error != CSS_OK) {
        value_unref(computed);
        computed = NULL;
        if (error != CSS_INVALID)
            return error;
    }
    value_unref(raw);
    ctx->entries[i].value = computed;
    return CSS_OK;
}

/* Iterative Tarjan SCC traversal. Dependencies are ONLY local winners;
 * inherited values have already been computed. Components are completed in
 * dependency order so expansion never recursively traverses a variable chain. */
typedef struct var_graph_node {
    uint32_t index, low, edge;
    bool on_stack;
} var_graph_node;

css_error css__variables_ctx_finalize(css_var_context **context)
{
    css_var_context *ctx = *context;
    var_graph_node *nodes = NULL;
    uint32_t *walk = NULL, *stack = NULL;
    uint32_t walk_len = 0, stack_len = 0, serial = 0;
    css_error error = CSS_OK;
    if (ctx == NULL || ctx->finalized)
        return CSS_OK;
    if (ctx->parent != NULL && !ctx->parent->finalized)
        return CSS_BADPARM;
    if (ctx->count == 0) {
        ctx->finalized = true;
        if (ctx->parent != NULL) {
            *context = css__variables_ctx_ref(ctx->parent);
            css__variables_ctx_destroy(ctx);
        }
        return CSS_OK;
    }
    for (uint32_t i = 0; i < ctx->count; i++) {
        css_var_entry *entry = &ctx->entries[i];
        css_var_value *value = cascade_winner(entry->candidates);
        if (value == NULL || value->keyword == VAR_INHERIT)
            value = find_value(ctx->parent, entry->name);
        else if (value->keyword == VAR_INITIAL)
            value = NULL;
        entry->value = value_ref(value);
        candidates_destroy(entry->candidates);
        entry->candidates = NULL;
    }
    nodes = calloc(ctx->count, sizeof(*nodes));
    walk = calloc(ctx->count, sizeof(*walk));
    stack = calloc(ctx->count, sizeof(*stack));
    if (nodes == NULL || walk == NULL || stack == NULL) {
        error = CSS_NOMEM;
        goto cleanup;
    }
    for (uint32_t root = 0; root < ctx->count; root++) {
        if (nodes[root].index != 0)
            continue;
        walk[walk_len++] = root;
        while (walk_len != 0) {
            uint32_t v = walk[walk_len - 1];
            css_var_value *value = ctx->entries[v].value;
            var_graph_node *node = &nodes[v];
            bool descend = false;
            if (node->index == 0) {
                node->index = node->low = ++serial;
                node->on_stack = true;
                stack[stack_len++] = v;
            }
            while (value != NULL && node->edge < value->ref_count) {
                uint32_t w;
                lwc_string *name = value->refs[node->edge++].name;
                if (!find_local(ctx, name, &w))
                    continue;
                if (nodes[w].index == 0) {
                    walk[walk_len++] = w;
                    descend = true;
                    break;
                }
                if (nodes[w].on_stack && nodes[w].index < node->low)
                    node->low = nodes[w].index;
            }
            if (descend)
                continue;
            if (node->low == node->index) {
                uint32_t first = stack_len;
                bool cyclic;
                do {
                    first--;
                } while (stack[first] != v);
                cyclic = stack_len - first > 1;
                if (!cyclic && value != NULL) {
                    for (uint32_t r = 0; r < value->ref_count; r++)
                        if (value->refs[r].name == ctx->entries[v].name)
                            cyclic = true;
                }
                if (cyclic) {
                    for (uint32_t j = first; j < stack_len; j++) {
                        css_var_entry *entry = &ctx->entries[stack[j]];
                        value_unref(entry->value);
                        entry->value = NULL;
                    }
                } else {
                    error = compute_value(ctx, v);
                    if (error != CSS_OK)
                        goto cleanup;
                }
                while (stack_len > first)
                    nodes[stack[--stack_len]].on_stack = false;
            }
            walk_len--;
            if (walk_len != 0) {
                var_graph_node *parent = &nodes[walk[walk_len - 1]];
                if (node->low < parent->low)
                    parent->low = node->low;
            }
        }
    }
    ctx->finalized = true;
cleanup:
    free(nodes);
    free(walk);
    free(stack);
    return error;
}

/* Text input is deliberately confined to the internal context test API. */
css_error css__variables_ctx_set(css_var_context *ctx, lwc_string *name, lwc_string *raw)
{
    parserutils_inputstream *input = NULL;
    css_lexer *lexer = NULL;
    css_var_value *value = NULL;
    css_error error;
    parserutils_error perr;
    if (ctx == NULL || ctx->finalized)
        return CSS_BADPARM;
    perr = parserutils_inputstream_create("UTF-8", 0, NULL, &input);
    if (perr != PARSERUTILS_OK)
        return css_error_from_parserutils_error(perr);
    perr = parserutils_inputstream_append(input, (const uint8_t *)lwc_string_data(raw), lwc_string_length(raw));
    if (perr == PARSERUTILS_OK)
        perr = parserutils_inputstream_append(input, NULL, 0);
    error = css_error_from_parserutils_error(perr);
    if (error != CSS_OK)
        goto cleanup;
    error = css__lexer_create(input, &lexer);
    if (error != CSS_OK)
        goto cleanup;
    error = value_create(&value);
    if (error != CSS_OK)
        goto cleanup;
    value->raw = lwc_string_ref(raw);
    for (;;) {
        css_token *t;
        error = css__lexer_get_token(lexer, &t);
        if (error != CSS_OK || t->type == CSS_TOKEN_EOF)
            break;
        if (t->type < CSS_TOKEN_LAST_INTERN) {
            error = css_error_from_lwc_error(lwc_intern_string((char *)t->data.data, t->data.len, &t->idata));
            if (error != CSS_OK)
                break;
            error = tokens_append(value->tokens, t);
            lwc_string_unref(t->idata);
            t->idata = NULL;
        } else {
            error = tokens_append(value->tokens, t);
        }
        if (error != CSS_OK)
            break;
    }
    if (error == CSS_OK)
        error = value_index(value);
    if (error == CSS_OK)
        error = css__variables_ctx_cascade(ctx, name, value, CSS_ORIGIN_AUTHOR, 0, false);
cleanup:
    value_unref(value);
    if (lexer != NULL)
        css__lexer_destroy(lexer);
    parserutils_inputstream_destroy(input);
    return error;
}

lwc_string *css__variables_ctx_get(const css_var_context *ctx, lwc_string *name)
{
    uint32_t i;
    for (; ctx != NULL; ctx = ctx->parent) {
        if (find_local(ctx, name, &i)) {
            css_var_value *value = ctx->finalized ? ctx->entries[i].value :
                cascade_winner(ctx->entries[i].candidates);
            return value != NULL ? value->raw : NULL;
        }
    }
    return NULL;
}
static css_error css__cascade_resolved_style(
    css_style *result_style,
    bool important,
    css_select_state *state)
{
    css_style rs = *result_style;

    while (rs.used > 0) {
        css_code_t result_opv = *rs.bytecode;
        opcode_t result_op;

        advance_bytecode(&rs, sizeof(result_opv));
        result_op = getOpcode(result_opv);

        if (important)
            result_opv = buildOPV(result_op,
                getFlags(result_opv) | FLAG_IMPORTANT, getValue(result_opv));

        if (result_op < CSS_N_PROPERTIES) {
            css_error error = prop_dispatch[result_op].cascade(
                result_opv, &rs, state);
            if (error != CSS_OK)
                return error;
        }
    }

    return CSS_OK;
}

static css_error css__cascade_unset_property(
    const struct css_prop_entry *entry,
    css_stylesheet *sheet,
    bool important,
    css_select_state *state)
{
    css_error error;
    parserutils_error perr;
    parserutils_vector *tokens = NULL;
    css_style *result_style = NULL;
    css_language resolve_lang;
    css_token unset_token;
    css_token eof_token;
    lwc_string *unset = NULL;
    int32_t token_ctx = 0;

    if (entry == NULL)
        return CSS_INVALID;

    error = css_error_from_lwc_error(
        lwc_intern_string("unset", SLEN("unset"), &unset));
    if (error != CSS_OK)
        return error;

    perr = parserutils_vector_create(sizeof(css_token), 2, &tokens);
    if (perr != PARSERUTILS_OK) {
        lwc_string_unref(unset);
        return css_error_from_parserutils_error(perr);
    }

    unset_token.type = CSS_TOKEN_IDENT;
    unset_token.data.data = (uint8_t *)"unset";
    unset_token.data.len = SLEN("unset");
    unset_token.idata = unset;
    unset_token.col = 0;
    unset_token.line = 0;

    eof_token.type = CSS_TOKEN_EOF;
    eof_token.data.data = NULL;
    eof_token.data.len = 0;
    eof_token.idata = NULL;
    eof_token.col = 0;
    eof_token.line = 0;

    perr = parserutils_vector_append(tokens, &unset_token);
    if (perr != PARSERUTILS_OK) {
        error = css_error_from_parserutils_error(perr);
        goto cleanup;
    }

    perr = parserutils_vector_append(tokens, &eof_token);
    if (perr != PARSERUTILS_OK) {
        error = css_error_from_parserutils_error(perr);
        goto cleanup;
    }

    memset(&resolve_lang, 0, sizeof(resolve_lang));
    resolve_lang.sheet = sheet;
    resolve_lang.strings = sheet->propstrings;

    error = css__stylesheet_style_create(sheet, &result_style);
    if (error != CSS_OK)
        goto cleanup;

    error = entry->handler(&resolve_lang, tokens, &token_ctx, result_style);
    if (error == CSS_OK)
        error = css__cascade_resolved_style(result_style, important, state);

cleanup:
    if (result_style != NULL)
        css__stylesheet_style_destroy(result_style);
    if (tokens != NULL)
        parserutils_vector_destroy(tokens);
    if (unset != NULL)
        lwc_string_unref(unset);

    return error;
}

css_error css__resolve_var_property(lwc_string *prop_name, uint32_t value_id,
    css_var_context *ctx, css_stylesheet *sheet, bool important, css_select_state *state)
{
    css_var_value *value = css__stylesheet_var_value_get(sheet, value_id);
    const struct css_prop_entry *entry = css_prop_lookup(lwc_string_data(prop_name), lwc_string_length(prop_name));
    css_var_value *expanded = NULL;
    css_style *style = NULL;
    css_language lang;
    css_error error;
    size_t length;
    int32_t position = 0;
    const css_token *tail;
    if (value == NULL || entry == NULL || ctx == NULL || !ctx->finalized)
        return CSS_BADPARM;
    error = value_create(&expanded);
    if (error != CSS_OK)
        return error;
    parserutils_vector_get_length(value->tokens, &length);
    error = resolve_range(value, 0, (int32_t)length, ctx, expanded->tokens, 0);
    if (error != CSS_OK)
        goto cleanup;
    consumeWhitespace(expanded->tokens, &position);
    if (parserutils_vector_peek(expanded->tokens, position) == NULL) {
        error = CSS_INVALID;
        goto cleanup;
    }
    memset(&lang, 0, sizeof(lang));
    lang.sheet = sheet;
    lang.strings = sheet->propstrings;
    error = css__stylesheet_style_create(sheet, &style);
    if (error != CSS_OK)
        goto cleanup;
    error = entry->handler(&lang, expanded->tokens, &position, style);
    if (error != CSS_OK)
        goto cleanup;
    consumeWhitespace(expanded->tokens, &position);
    tail = parserutils_vector_peek(expanded->tokens, position);
    if (tail != NULL) {
        error = CSS_INVALID; /* A valid prefix cannot hide trailing junk. */
        goto cleanup;
    }
    error = css__cascade_resolved_style(style, important, state);
cleanup:
    if (style != NULL)
        css__stylesheet_style_destroy(style);
    value_unref(expanded);
    if (error == CSS_INVALID)
        error = css__cascade_unset_property(entry, sheet, important, state);
    return error;
}

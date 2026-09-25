/*
 * This file is part of LibCSS.
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 */
#ifndef css_select_variables_h_
#define css_select_variables_h_

#include <libwapcaplet/libwapcaplet.h>
#include <libcss/errors.h>
#include <libcss/types.h>
#include <parserutils/utils/vector.h>

/* Owned, immutable tokens: declarations live on a stylesheet; computed values
 * live in finalized environments. Neither contains pointers into lexer input. */
typedef struct css_var_value css_var_value;
typedef struct css_var_lookup css_var_lookup;
typedef struct css_var_candidates css_var_candidates;

typedef struct css_var_entry {
    lwc_string *name;
    css_var_value *value; /* NULL is a guaranteed-invalid local binding. */
    css_var_candidates *candidates; /* Cascade metadata, freed on finalization. */
} css_var_entry;

/* A mutable cascade builder becomes an immutable computed environment when
 * finalized. Only local bindings are stored; parents are already finalized.
 * An invalid local value masks the parent. Refcounts are single-threaded. */
typedef struct css_var_context {
    uint32_t refcnt;
    struct css_var_context *parent;
    css_var_entry *entries;
    uint32_t count;
    uint32_t capacity;
    css_var_lookup *lookup;
    uint32_t lookup_capacity;
    bool lookup_valid;
    bool finalized;
} css_var_context;

struct css_stylesheet;
struct css_select_state;

css_error css__variables_ctx_create(css_var_context **out);
css_var_context *css__variables_ctx_ref(css_var_context *ctx);
css_error css__variables_ctx_clone_inherited(css_var_context *parent,
    css_var_context **out);
/* Finalize local values in dependency order. Empty overlays reuse the parent.
 * On error the builder must be destroyed, not retried or published. */
css_error css__variables_ctx_finalize(css_var_context **ctx);
void css__variables_ctx_destroy(css_var_context *ctx);
css_error css__variables_ctx_cascade(css_var_context *ctx, lwc_string *name,
    css_var_value *value, css_origin origin, uint32_t specificity, bool important);

/* Internal context-test helpers. The selection path uses parsed declarations. */
css_error css__variables_ctx_set(css_var_context *ctx,
    lwc_string *name, lwc_string *raw);
lwc_string *css__variables_ctx_get(const css_var_context *ctx, lwc_string *name);

/* Capture parser tokens once and return a stylesheet-local declaration ID. */
css_error css__stylesheet_var_value_add(struct css_stylesheet *sheet,
    const parserutils_vector *tokens, int32_t start, int32_t end, uint32_t *id);
css_var_value *css__stylesheet_var_value_get(struct css_stylesheet *sheet, uint32_t id);
void css__stylesheet_var_values_destroy(struct css_stylesheet *sheet);

#define CSS_VAR_MAX_TOKENS 65536u
/* Only fallback syntax nesting is recursive; dependency chains are iterative. */
#define CSS_VAR_MAX_DEPTH 64

css_error css__resolve_var_property(lwc_string *prop_name, uint32_t value_id,
    css_var_context *ctx, struct css_stylesheet *sheet, bool important,
    struct css_select_state *state);

#endif

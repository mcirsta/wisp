/* Compile owned declarations without passing through CSSOM serialization.
 * Licensed under the MIT License. */
#include <assert.h>
#include <limits.h>
#include <string.h>

#include "parse/declaration_store.h"
#include "parse/property.h"
#include "select/variables.h"
#include "stylesheet.h"
#include "utils/parserutilserror.h"

struct compiler {
    css_stylesheet *sheet;
    css_style *parsed, *key, *output;
    parserutils_vector *initial;
    const css_declaration *source;
    uint32_t name_id, value_id;
};

/* The store owns decoded tokens, including recovered EOF punctuation. Borrow
 * their strings while invoking the ordinary property handlers; no re-lexing or
 * reconstructed source spelling can change their meaning. */
static css_error source_tokens(const css_declaration *source, parserutils_vector **out)
{
    if (source->token_count > INT32_MAX)
        return CSS_NOMEM;
    parserutils_error error = parserutils_vector_create(sizeof(css_token), 16, out);
    if (error != PARSERUTILS_OK)
        return css_error_from_parserutils_error(error);
    for (size_t i = 0; i < source->token_count; i++) {
        const css_declaration_token *owned = &source->tokens[i];
        css_token token = {.type = owned->type, .idata = owned->text};
        if (token.idata != NULL) {
            token.data.data = (uint8_t *)lwc_string_data(token.idata);
            token.data.len = lwc_string_length(token.idata);
        }
        error = parserutils_vector_append(*out, &token);
        if (error != PARSERUTILS_OK)
            return css_error_from_parserutils_error(error);
    }
    return CSS_OK;
}

static css_error saved_font(void *pw, lwc_string *name, css_system_font *font)
{
    (void)name;
    *font = *(const css_system_font *)pw;
    font->family = lwc_string_ref(font->family);
    return CSS_OK;
}

/* Reuse adjacent contributions' source parse/token record. A shorthand's
 * surviving members normally occur together in the owned declaration order. */
static css_error compile_source(struct compiler *c, const css_declaration *source)
{
    if (c->source == source)
        return CSS_OK;
    parserutils_vector *tokens = NULL;
    css_error error = source_tokens(source, &tokens);
    if (error != CSS_OK)
        goto out;
    if (source->property == NULL || source->deferred) {
        error = css__stylesheet_var_value_add(c->sheet, tokens, 0, (int32_t)source->token_count, &c->value_id);
        if (error == CSS_OK)
            error = css__stylesheet_string_add(c->sheet, lwc_string_ref(source->name), &c->name_id);
    } else {
        css_font_resolution_fn font = c->sheet->font;
        void *font_pw = c->sheet->font_pw;
        bool quirks = c->sheet->quirks_allowed;
        /* A literal system font keeps the resolution captured when assigned;
         * other literal font values must not acquire a new system-font meaning. */
        c->sheet->font = source->system_font != NULL ? saved_font : NULL;
        c->sheet->font_pw = source->system_font;
        c->sheet->quirks_allowed = source->quirks_allowed;
        css_language lang = {.sheet = c->sheet, .strings = c->sheet->propstrings};
        int32_t position = 0;
        c->parsed->used = 0;
        error = source->property->handler(&lang, tokens, &position, c->parsed);
        consumeWhitespace(tokens, &position);
        if (error == CSS_OK && parserutils_vector_peek(tokens, position) != NULL)
            error = CSS_INVALID;
        c->sheet->font = font;
        c->sheet->font_pw = font_pw;
        c->sheet->quirks_allowed = quirks;
    }
    if (error == CSS_OK)
        c->source = source;
out:
    if (tokens != NULL)
        parserutils_vector_destroy(tokens);
    return error;
}

/* Ask the registered longhand parser for its native identity. This also keeps
 * generated aliases/logical flags authoritative, without another mapping table. */
static css_error member_key(struct compiler *c, const char *name, css_code_t *key)
{
    const struct css_prop_entry *property = css_prop_lookup(name, strlen(name));
    if (property == NULL || property->shorthand)
        return CSS_INVALID;
    css_language lang = {.sheet = c->sheet, .strings = c->sheet->propstrings};
    int32_t position = 0;
    c->key->used = 0;
    css_error error = property->handler(&lang, c->initial, &position, c->key);
    if (error != CSS_OK)
        return error;
    if (c->key->used != 1 || !hasFlagValue(c->key->bytecode[0]))
        return CSS_INVALID;
    *key = c->key->bytecode[0];
    return CSS_OK;
}

static css_error compile_member(struct compiler *c, const css_declaration_contribution *member, bool whole_shorthand)
{
    const css_declaration *source = member->source;
    css_error error = compile_source(c, source);
    if (error != CSS_OK)
        return error;
    css_code_t key = 0;
    if (source->property != NULL && !whole_shorthand) {
        error = member_key(c, member->name, &key);
        if (error != CSS_OK)
            return error;
    }
    uint8_t flags = source->important ? FLAG_IMPORTANT : 0;
    if (source->property == NULL || source->deferred) {
        bool custom = source->property == NULL;
        /* Keep the original deferred grammar, but apply only this surviving
         * member after substitution. Removed members never reset stylesheet CSS. */
        unsigned target = !custom && source->property->shorthand && !whole_shorthand ? getOpcode(key) + 1 : 0;
        if (target != 0)
            flags |= getFlags(key) & FLAG_LOGICAL_INLINE;
        error = css__stylesheet_style_appendOPV(
            c->output, custom ? CSS_PROP_CUSTOM_PROPERTY : CSS_PROP_VAR_DEFERRED, flags, target);
        if (error == CSS_OK)
            error = css__stylesheet_style_vappend(c->output, 2, c->name_id, c->value_id);
        if (custom)
            c->sheet->rule_list->has_custom_properties = true;
        c->sheet->uses_variables = true;
        return error;
    }
    css_style cursor = *c->parsed;
    bool found = false;
    while (cursor.used != 0) {
        css_style span = cursor;
        css_code_t opv = *cursor.bytecode;
        advance_bytecode(&cursor, sizeof(css_code_t));
        css__stylesheet_style_skip(&cursor, opv);
        if (getOpcode(opv) != getOpcode(key) || ((getFlags(opv) ^ getFlags(key)) & FLAG_LOGICAL_INLINE) != 0)
            continue;
        span.used -= cursor.used;
        /* Change only the copied OPV's priority, never its operand words. */
        error = css__stylesheet_style_append(c->output, buildOPV(getOpcode(opv), getFlags(opv) | flags, getValue(opv)));
        if (error == CSS_OK && span.used > 1) {
            advance_bytecode(&span, sizeof(css_code_t));
            error = css__stylesheet_merge_style(c->output, &span);
        }
        if (error != CSS_OK)
            return error;
        found = true;
    }
    return found ? CSS_OK : CSS_INVALID;
}

/* Keep untouched pending shorthands as one ordinary deferred instruction. An
 * unrelated API edit should not multiply their substitution/grammar work. Only
 * combine adjacent members, preserving declaration order against other sources. */
static size_t whole_pending_shorthand(const css_declaration_store *store, size_t index)
{
    const css_declaration *source = css__declaration_store_get(store, index)->source;
    const struct css_prop_entry *property = source->property;
    if (!source->deferred || property == NULL || !property->shorthand ||
        property->longhand_count > css__declaration_store_count(store) - index)
        return 0;
    for (size_t i = 0; i < property->longhand_count; i++) {
        const css_declaration_contribution *member = css__declaration_store_get(store, index + i);
        if (member->source != source || strcmp(member->name, property->longhands[i]) != 0)
            return 0;
    }
    return property->longhand_count;
}

css_error css__declaration_store_compile(
    const css_declaration_store *store, const css_stylesheet_params *params, css_stylesheet **out)
{
    if (out == NULL)
        return CSS_BADPARM;
    *out = NULL;
    if (store == NULL || params == NULL)
        return CSS_BADPARM;
    struct compiler c = {0};
    css_stylesheet_params options = *params;
    options.inline_style = true;
    options.charset = "UTF-8";
    css_error error = css_stylesheet_create(&options, &c.sheet);
    if (error != CSS_OK)
        return error;
    /* Let the ordinary inline parser create the empty selector rule. */
    error = css_stylesheet_data_done(c.sheet);
    if (error != CSS_OK)
        goto out;
    assert(c.sheet->rule_count == 1 && c.sheet->rule_list->type == CSS_RULE_SELECTOR);
    error = css__stylesheet_style_create(c.sheet, &c.parsed);
    if (error == CSS_OK)
        error = css__stylesheet_style_create(c.sheet, &c.key);
    if (error == CSS_OK)
        error = css__stylesheet_style_create(c.sheet, &c.output);
    if (error != CSS_OK)
        goto out;
    error = css_error_from_parserutils_error(parserutils_vector_create(sizeof(css_token), 1, &c.initial));
    if (error != CSS_OK)
        goto out;
    css_token initial = {.type = CSS_TOKEN_IDENT, .idata = c.sheet->propstrings[INITIAL]};
    error = css_error_from_parserutils_error(parserutils_vector_append(c.initial, &initial));
    for (size_t i = 0; error == CSS_OK && i < css__declaration_store_count(store);) {
        size_t whole = whole_pending_shorthand(store, i);
        error = compile_member(&c, css__declaration_store_get(store, i), whole != 0);
        i += whole != 0 ? whole : 1;
    }
    if (error == CSS_OK) {
        error = css__stylesheet_rule_append_style(c.sheet, c.sheet->rule_list, c.output);
        if (error == CSS_OK)
            c.output = NULL; /* Transferred to sheet. */
    }
out:
    if (c.initial != NULL)
        parserutils_vector_destroy(c.initial);
    if (c.parsed != NULL)
        css__stylesheet_style_destroy(c.parsed);
    if (c.key != NULL)
        css__stylesheet_style_destroy(c.key);
    if (c.output != NULL)
        css__stylesheet_style_destroy(c.output);
    if (error == CSS_OK)
        *out = c.sheet;
    else
        css_stylesheet_destroy(c.sheet);
    return error;
}

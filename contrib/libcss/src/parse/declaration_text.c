/*
 * This file is part of LibCSS.
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 */

#include <stdlib.h>
#include <string.h>

#include "parse/declaration_text.h"
#include "parse/declaration_value.h"
#include "parse/property.h"
#include "utils/css_utils.h"

/* Measure first, then write into one exact-sized allocation. Long data URLs do
 * not need a pessimistic escape-sized buffer or one allocation per list item. */
struct text_output {
    char *data;
    size_t length, capacity;
};

static css_error append(struct text_output *out, const char *text, size_t length, bool lowercase)
{
    if (length > out->capacity - out->length)
        return CSS_NOMEM;
    if (out->data != NULL) {
        if (lowercase) {
            for (size_t i = 0; i < length; i++) {
                char ch = text[i];
                out->data[out->length + i] = ch >= 'A' && ch <= 'Z' ? ch + ('a' - 'A') : ch;
            }
        } else if (length != 0) {
            memcpy(out->data + out->length, text, length);
        }
    }
    out->length += length;
    return CSS_OK;
}

/* CSSOM §2.1 string/identifier serialization. Only ASCII bytes need escaping; copying
 * the intervening UTF-8 spans preserves non-ASCII code points unchanged. */
static css_error escaped_contents(struct text_output *out, const char *text, size_t length, bool identifier)
{
    css_error error = CSS_OK;
    size_t start = 0;
    for (size_t i = 0; error == CSS_OK && i < length; i++) {
        unsigned char ch = text[i];
        bool hex_escape = (ch > 0 && ch < 0x20) || ch == 0x7f ||
            (identifier && ch >= '0' && ch <= '9' && (i == 0 || (i == 1 && text[0] == '-')));
        bool literal = identifier ?
            (ch >= 0x80 || ch == '_' || (ch == '-' && length != 1) ||
                (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9')) :
            (ch >= 0x20 && ch != '"' && ch != '\\');
        if (literal && !hex_escape)
            continue;
        error = append(out, text + start, i - start, false);
        if (error != CSS_OK)
            return error;
        char escape[4];
        size_t size = 0;
        if (ch == 0) {
            error = append(out, "\xef\xbf\xbd", 3, false);
        } else {
            escape[size++] = '\\';
            if (!hex_escape) {
                escape[size++] = (char)ch;
            } else {
                static const char hex[] = "0123456789abcdef";
                if (ch >= 16)
                    escape[size++] = hex[ch >> 4];
                escape[size++] = hex[ch & 15];
                escape[size++] = ' ';
            }
            error = append(out, escape, size, false);
        }
        start = i + 1;
    }
    if (error == CSS_OK)
        error = append(out, text + start, length - start, false);
    return error;
}

static css_error escaped(struct text_output *out, lwc_string *string, bool identifier)
{
    css_error error = identifier ? CSS_OK : append(out, "\"", 1, false);
    if (error == CSS_OK)
        error = escaped_contents(out, lwc_string_data(string), lwc_string_length(string), identifier);
    if (error == CSS_OK && !identifier)
        error = append(out, "\"", 1, false);
    return error;
}

static bool character(const css_declaration_token *token, char ch)
{
    return token != NULL && token->type == CSS_TOKEN_CHAR && token->text != NULL &&
        lwc_string_length(token->text) == 1 && lwc_string_data(token->text)[0] == ch;
}

static css_error url(struct text_output *out, lwc_string *text)
{
    css_error error = append(out, "url(", 4, false);
    if (error == CSS_OK)
        error = escaped(out, text, false);
    if (error == CSS_OK)
        error = append(out, ")", 1, false);
    return error;
}

/* Walk the captured, validated tokens, never the rendering bytecode's resolved
 * URLs or fixed-point integers. No allocation is needed per token/list item. */
static const css_declaration_token *next_token(const css_declaration *source, size_t *position)
{
    while (*position < source->token_count) {
        const css_declaration_token *token = &source->tokens[(*position)++];
        if (token->type != CSS_TOKEN_S)
            return token;
    }
    return NULL;
}

static css_error content_function(const css_declaration *source, size_t *position,
    const css_declaration_token *function, struct text_output *out)
{
    bool attr = css__keyword_equal(function->text, "attr");
    bool counters = css__keyword_equal(function->text, "counters");
    if (!attr && !counters && !css__keyword_equal(function->text, "counter"))
        return CSS_INVALID;
    const css_declaration_token *name = next_token(source, position);
    if (name == NULL || name->type != CSS_TOKEN_IDENT)
        return CSS_INVALID;
    const char *prefix = attr ? "attr(" : counters ? "counters(" : "counter(";
    css_error error = append(out, prefix, strlen(prefix), false);
    if (error == CSS_OK)
        error = escaped(out, name->text, true);
    if (error != CSS_OK)
        return error;
    const css_declaration_token *token = next_token(source, position);
    if (counters) {
        if (!character(token, ','))
            return CSS_INVALID;
        token = next_token(source, position);
        if (token == NULL || token->type != CSS_TOKEN_STRING)
            return CSS_INVALID;
        error = append(out, ", ", 2, false);
        if (error == CSS_OK)
            error = escaped(out, token->text, false);
        token = next_token(source, position);
    }
    if (!attr && character(token, ',')) {
        token = next_token(source, position);
        if (token == NULL || token->type != CSS_TOKEN_IDENT)
            return CSS_INVALID;
        /* CSSOM's <counter> serialization omits the default decimal style. */
        if (error == CSS_OK && !css__keyword_equal(token->text, "decimal")) {
            error = append(out, ", ", 2, false);
            if (error == CSS_OK)
                error = append(out, lwc_string_data(token->text), lwc_string_length(token->text), true);
        }
        token = next_token(source, position);
    }
    if (!character(token, ')'))
        return CSS_INVALID;
    if (error == CSS_OK)
        error = append(out, ")", 1, false);
    return error;
}

static css_error serialize_sequence(const css_declaration *source, struct text_output *out)
{
    bool content = source->property->value_kind == CSS_DECL_VALUE_CONTENT;
    size_t position = 0;
    const css_declaration_token *token;
    bool first = true;
    while ((token = next_token(source, &position)) != NULL) {
        css_error error = first ? CSS_OK : append(out, " ", 1, false);
        if (error != CSS_OK)
            return error;
        first = false;
        switch (token->type) {
        case CSS_TOKEN_IDENT:
            error = append(out, lwc_string_data(token->text), lwc_string_length(token->text), true);
            break;
        case CSS_TOKEN_STRING:
            error = escaped(out, token->text, false);
            break;
        case CSS_TOKEN_URI:
            error = content ? url(out, token->text) : CSS_INVALID;
            break;
        case CSS_TOKEN_FUNCTION:
            error = content ? content_function(source, &position, token, out) : CSS_INVALID;
            break;
        default:
            return CSS_INVALID;
        }
        if (error != CSS_OK)
            return error;
    }
    return first ? CSS_INVALID : CSS_OK;
}

/* An accepted counter integer is [+-]?[0-9]+. Keep the specified magnitude,
 * including values beyond rendering storage; omit the property's default per
 * CSSOM's shortest-equivalent rule. Duplicate names retain their order. */
static css_error counter_integer(struct text_output *out, lwc_string *string, char default_value)
{
    const char *text = lwc_string_data(string);
    size_t length = lwc_string_length(string);
    bool negative = text[0] == '-';
    size_t start = negative || text[0] == '+' ? 1 : 0;
    while (start < length && text[start] == '0')
        start++;
    if (start == length) {
        negative = false;
        start--;
    }
    if (!negative && length - start == 1 && text[start] == default_value)
        return CSS_OK;
    css_error error = append(out, negative ? " -" : " ", negative ? 2 : 1, false);
    if (error == CSS_OK)
        error = append(out, text + start, length - start, false);
    return error;
}

static css_error serialize_counters(const css_declaration *source, struct text_output *out)
{
    if (source->token_count == 1 && source->tokens[0].type == CSS_TOKEN_IDENT &&
        css__keyword_equal(source->tokens[0].text, "none"))
        return append(out, "none", 4, false);
    size_t position = 0;
    const css_declaration_token *token = next_token(source, &position);
    bool first = true;
    char default_value = source->property->opcode == CSS_PROP_COUNTER_INCREMENT ? '1' : '0';
    while (token != NULL) {
        if (token->type != CSS_TOKEN_IDENT)
            return CSS_INVALID;
        css_error error = first ? CSS_OK : append(out, " ", 1, false);
        if (error == CSS_OK)
            error = escaped(out, token->text, true);
        if (error != CSS_OK)
            return error;
        first = false;
        token = next_token(source, &position);
        if (token != NULL && token->type == CSS_TOKEN_NUMBER) {
            error = counter_integer(out, token->text, default_value);
            if (error != CSS_OK)
                return error;
            token = next_token(source, &position);
        }
    }
    return first ? CSS_INVALID : CSS_OK;
}

/* The existing renderer only distinguishes the five CSS Fonts 3 generics.
 * Do not turn newer generic keywords into quoted, named fonts in CSSOM. Their
 * native representation/selection needs extending before exposing readback. */
static bool unsupported_font_generic(lwc_string *name)
{
    static const char *const names[] = {"system-ui", "ui-serif", "ui-sans-serif", "ui-monospace", "ui-rounded",
        "math", "emoji", "fangsong"};
    for (size_t i = 0; i < N_ELEMENTS(names); i++) {
        if (css__keyword_equal(name, names[i]))
            return true;
    }
    return false;
}

/* CSSOM serializes named families as strings. Join decoded identifiers with
 * single spaces, preserving whitespace inside each identifier. Writing their
 * escaped contents directly avoids allocating a temporary joined name. */
static css_error family_name(const css_declaration *source, size_t *position,
    const css_declaration_token **token, struct text_output *out)
{
    css_error error;
    if ((*token)->type == CSS_TOKEN_STRING) {
        error = escaped(out, (*token)->text, false);
        *token = next_token(source, position);
        return error;
    }
    if ((*token)->type != CSS_TOKEN_IDENT)
        return CSS_INVALID;
    bool voice = source->property->value_kind == CSS_DECL_VALUE_VOICE_FAMILY;
    bool generic = voice ? (css__keyword_equal((*token)->text, "male") ||
        css__keyword_equal((*token)->text, "female") || css__keyword_equal((*token)->text, "child")) :
        css__is_font_family_generic((*token)->text);
    if (generic) {
        error = append(out, lwc_string_data((*token)->text), lwc_string_length((*token)->text), true);
        *token = next_token(source, position);
        return error;
    }
    error = append(out, "\"", 1, false);
    bool first = true;
    while (*token != NULL && (*token)->type == CSS_TOKEN_IDENT) {
        if (!voice && unsupported_font_generic((*token)->text))
            return CSS_INVALID;
        if (error == CSS_OK && !first)
            error = append(out, " ", 1, false);
        if (error == CSS_OK)
            error = escaped_contents(out, lwc_string_data((*token)->text), lwc_string_length((*token)->text), false);
        if (error != CSS_OK)
            return error;
        first = false;
        *token = next_token(source, position);
    }
    return append(out, "\"", 1, false);
}

static css_error serialize_family(const css_declaration *source, struct text_output *out)
{
    size_t position = 0;
    const css_declaration_token *token = next_token(source, &position);
    if (token == NULL)
        return CSS_INVALID;
    for (;;) {
        css_error error = family_name(source, &position, &token, out);
        if (error != CSS_OK || token == NULL)
            return error;
        if (!character(token, ','))
            return CSS_INVALID;
        error = append(out, ", ", 2, false);
        if (error != CSS_OK)
            return error;
        token = next_token(source, &position);
        if (token == NULL)
            return CSS_INVALID;
    }
}

static css_error serialize_decoration(const css_declaration *source, struct text_output *out)
{
    if (source->token_count == 1 && source->tokens[0].type == CSS_TOKEN_IDENT &&
        css__keyword_equal(source->tokens[0].text, "none"))
        return append(out, "none", 4, false);
    /* Canonical grammar order for the engine's CSS2 line-decoration set. */
    static const char *const names[] = {"underline", "overline", "line-through", "blink"};
    unsigned seen = 0;
    for (size_t i = 0; i < source->token_count; i++) {
        const css_declaration_token *token = &source->tokens[i];
        if (token->type == CSS_TOKEN_S)
            continue;
        if (token->type != CSS_TOKEN_IDENT)
            return CSS_INVALID;
        size_t index;
        for (index = 0; index < N_ELEMENTS(names); index++) {
            if (css__keyword_equal(token->text, names[index]))
                break;
        }
        if (index == N_ELEMENTS(names) || (seen & (1u << index)))
            return CSS_INVALID;
        seen |= 1u << index;
    }
    bool first = true;
    for (size_t i = 0; i < N_ELEMENTS(names); i++) {
        if (!(seen & (1u << i)))
            continue;
        css_error error = first ? CSS_OK : append(out, " ", 1, false);
        if (error == CSS_OK)
            error = append(out, names[i], strlen(names[i]), false);
        if (error != CSS_OK)
            return error;
        first = false;
    }
    return first ? CSS_INVALID : CSS_OK;
}

static css_error serialize_list(const css_declaration *source, struct text_output *out)
{
    enum css_declaration_value_kind kind = source->property->value_kind;
    bool need_value = true;
    for (size_t i = 0; i < source->token_count; i++) {
        const css_declaration_token *token = &source->tokens[i];
        if (token->type == CSS_TOKEN_S)
            continue;
        css_error error;
        if (!need_value) {
            if (kind == CSS_DECL_VALUE_URI || !character(token, ','))
                return CSS_INVALID;
            error = append(out, ", ", 2, false);
        } else if (token->type == CSS_TOKEN_IDENT) {
            error = append(out, lwc_string_data(token->text), lwc_string_length(token->text), true);
        } else if (token->type == CSS_TOKEN_URI && kind != CSS_DECL_VALUE_KEYWORD_LIST) {
            error = url(out, token->text);
        } else {
            /* Gradients need their own specified-value representation. */
            return CSS_INVALID;
        }
        if (error != CSS_OK)
            return error;
        need_value = !need_value;
    }
    return need_value ? CSS_INVALID : CSS_OK;
}

/* These are accepted track-list tokens, not computed tracks: retain repeat()
 * and specified units instead of expanding/fixing them from layout bytecode.
 * The parser owns grammar validation; this walk only formats its supported
 * components. In particular, a repeat count is a number, while bare zero in
 * a track breadth is a length. */
static css_error serialize_grid_tracks(const css_declaration *source, struct text_output *out)
{
    bool after_value = false, repeat_count = false;
    for (size_t i = 0; i < source->token_count; i++) {
        const css_declaration_token *token = &source->tokens[i];
        if (token->type == CSS_TOKEN_S)
            continue;
        css_error error;
        if (character(token, ',')) {
            error = append(out, ", ", 2, false);
            after_value = false;
        } else if (character(token, ')')) {
            error = append(out, ")", 1, false);
            after_value = true;
        } else {
            error = after_value ? append(out, " ", 1, false) : CSS_OK;
            if (error != CSS_OK)
                return error;
            if (token->type == CSS_TOKEN_FUNCTION) {
                repeat_count = css__keyword_equal(token->text, "repeat");
                if (!repeat_count && !css__keyword_equal(token->text, "minmax"))
                    return CSS_INVALID;
                error = append(out, lwc_string_data(token->text), lwc_string_length(token->text), true);
                if (error == CSS_OK)
                    error = append(out, "(", 1, false);
                after_value = false;
            } else if (token->type == CSS_TOKEN_IDENT) {
                error = append(out, lwc_string_data(token->text), lwc_string_length(token->text), true);
                after_value = true;
            } else if (token->type == CSS_TOKEN_NUMBER || token->type == CSS_TOKEN_DIMENSION ||
                token->type == CSS_TOKEN_PERCENTAGE) {
                lwc_string *number;
                error = css__declaration_numeric_serialize(token, !repeat_count, &number);
                if (error == CSS_OK) {
                    error = append(out, lwc_string_data(number), lwc_string_length(number), false);
                    lwc_string_unref(number);
                }
                repeat_count = false;
                after_value = true;
            } else {
                return CSS_INVALID;
            }
        }
        if (error != CSS_OK)
            return error;
    }
    return after_value ? CSS_OK : CSS_INVALID;
}

static css_error serialize_grid_flow(const css_declaration *source, struct text_output *out)
{
    bool column = false, dense = false;
    for (size_t i = 0; i < source->token_count; i++) {
        const css_declaration_token *token = &source->tokens[i];
        if (token->type == CSS_TOKEN_S)
            continue;
        if (token->type != CSS_TOKEN_IDENT)
            return CSS_INVALID;
        column |= css__keyword_equal(token->text, "column");
        dense |= css__keyword_equal(token->text, "dense");
    }
    /* CSSOM canonical order and omission: dense implies row. */
    const char *value = column ? (dense ? "column dense" : "column") : (dense ? "dense" : "row");
    return append(out, value, strlen(value), false);
}

static css_error serialize_play_during(const css_declaration *source, struct text_output *out)
{
    size_t position = 0;
    const css_declaration_token *token = next_token(source, &position);
    if (token == NULL)
        return CSS_INVALID;
    if (token->type == CSS_TOKEN_IDENT)
        return serialize_list(source, out); /* auto or none */
    if (token->type != CSS_TOKEN_URI)
        return CSS_INVALID;
    css_error error = url(out, token->text);
    bool mix = false, repeat = false;
    while ((token = next_token(source, &position)) != NULL) {
        if (token->type != CSS_TOKEN_IDENT)
            return CSS_INVALID;
        mix |= css__keyword_equal(token->text, "mix");
        repeat |= css__keyword_equal(token->text, "repeat");
    }
    if (error == CSS_OK && mix)
        error = append(out, " mix", 4, false);
    if (error == CSS_OK && repeat)
        error = append(out, " repeat", 7, false);
    return error;
}

static css_error serialize(const css_declaration *source, struct text_output *out)
{
    switch (source->property->value_kind) {
    case CSS_DECL_VALUE_CONTENT:
    case CSS_DECL_VALUE_STRING_LIST:
        return serialize_sequence(source, out);
    case CSS_DECL_VALUE_COUNTER_LIST:
        return serialize_counters(source, out);
    case CSS_DECL_VALUE_FONT_FAMILY:
    case CSS_DECL_VALUE_VOICE_FAMILY:
        return serialize_family(source, out);
    case CSS_DECL_VALUE_PLAY_DURING:
        return serialize_play_during(source, out);
    case CSS_DECL_VALUE_TEXT_DECORATION:
        return serialize_decoration(source, out);
    case CSS_DECL_VALUE_GRID_TRACKS:
        return serialize_grid_tracks(source, out);
    case CSS_DECL_VALUE_GRID_FLOW:
        return serialize_grid_flow(source, out);
    default:
        return serialize_list(source, out);
    }
}

css_error css__declaration_text_serialize(const css_declaration *source, lwc_string **out)
{
    struct text_output text = {.capacity = SIZE_MAX};
    css_error error = serialize(source, &text);
    if (error != CSS_OK)
        return error;
    text.capacity = text.length;
    text.length = 0;
    text.data = malloc(text.capacity);
    if (text.data == NULL)
        return CSS_NOMEM;
    error = serialize(source, &text);
    if (error == CSS_OK && lwc_intern_string(text.data, text.length, out) != lwc_error_ok)
        error = CSS_NOMEM;
    free(text.data);
    return error;
}

static css_error format_block(const css_declaration_text *declarations, size_t count, struct text_output *out)
{
    for (size_t i = 0; i < count; i++) {
        const css_declaration_text *decl = &declarations[i];
        css_error error = i == 0 ? CSS_OK : append(out, " ", 1, false);
        if (error == CSS_OK)
            error = escaped_contents(out, decl->name, strlen(decl->name), true);
        if (error == CSS_OK)
            error = append(out, ": ", 2, false);
        /* CSSOM omits a value containing only CSS whitespace. Custom values
         * containing comments or an escaped space still retain their text. */
        const char *value = lwc_string_data(decl->value);
        size_t length = lwc_string_length(decl->value), pos = 0;
        while (pos < length && (value[pos] == ' ' || value[pos] == '\t' || value[pos] == '\n' ||
            value[pos] == '\r' || value[pos] == '\f'))
            pos++;
        if (error == CSS_OK && pos != length)
            error = append(out, value, length, false);
        if (error == CSS_OK && decl->important)
            error = append(out, " !important", 11, false);
        if (error == CSS_OK)
            error = append(out, ";", 1, false);
        if (error != CSS_OK)
            return error;
    }
    return CSS_OK;
}

css_error css__declaration_text_block(const css_declaration_text *declarations, size_t count, lwc_string **out)
{
    if (out == NULL)
        return CSS_BADPARM;
    *out = NULL;
    if (count != 0 && declarations == NULL)
        return CSS_BADPARM;
    struct text_output text = {.capacity = SIZE_MAX};
    css_error error = format_block(declarations, count, &text);
    if (error != CSS_OK)
        return error;
    if (text.length == 0)
        return lwc_intern_string("", 0, out) == lwc_error_ok ? CSS_OK : CSS_NOMEM;
    text.capacity = text.length;
    text.length = 0;
    text.data = malloc(text.capacity);
    if (text.data == NULL)
        return CSS_NOMEM;
    error = format_block(declarations, count, &text);
    if (error == CSS_OK && lwc_intern_string(text.data, text.length, out) != lwc_error_ok)
        error = CSS_NOMEM;
    free(text.data);
    return error;
}

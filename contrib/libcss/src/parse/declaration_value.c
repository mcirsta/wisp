/*
 * This file is part of LibCSS.
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 */

#include <stdlib.h>
#include <string.h>

#include "parse/declaration_value.h"
#include "parse/declaration_text.h"
#include "parse/property.h"
#include "utils/css_utils.h"

static css_error intern(const char *text, size_t length, lwc_string **out)
{
    return lwc_intern_string(text, length, out) == lwc_error_ok ? CSS_OK : CSS_NOMEM;
}

static const char *wide_keyword(const css_declaration *source)
{
    if (source->token_count != 1 || source->tokens[0].type != CSS_TOKEN_IDENT)
        return NULL;
    static const char *const keywords[] = {"inherit", "initial", "unset", "revert", "revert-layer"};
    for (size_t i = 0; i < sizeof(keywords) / sizeof(keywords[0]); i++) {
        if (css__keyword_equal(source->tokens[0].text, keywords[i]))
            return keywords[i];
    }
    return NULL;
}

/* CSS Variables 1 §4.1: retain spelling; an empty custom value serializes
 * as one space so passing it to setProperty does not remove the property. */
static css_error serialize_unparsed(const css_declaration *source, lwc_string **out)
{
    if (source->value_start == source->value_end)
        return intern(" ", 1, out);
    return intern(
        lwc_string_data(source->source_value) + source->value_start, source->value_end - source->value_start, out);
}

static bool digit(char ch)
{
    return ch >= '0' && ch <= '9';
}

/* CSSOM §6.7.2 numbers use decimal notation and at most six fractional digits. Work
 * directly on the parsed token's decimal spelling: rendering bytecode has only
 * ten fractional bits, and strtod/snprintf would introduce locale and binary
 * rounding differences. The current property parser does not accept exponents.
 * This is serialization after validation, not another property parser. */
static css_error serialize_numeric(const css_declaration_token *token, bool length_value, lwc_string **out)
{
    const char *text = lwc_string_data(token->text);
    size_t length = lwc_string_length(token->text);
    if (length > SIZE_MAX - 10)
        return CSS_NOMEM;
    char *buffer = malloc(length + 10);
    if (buffer == NULL)
        return CSS_NOMEM;

    size_t pos = 0;
    bool negative = text[pos] == '-';
    if (negative || text[pos] == '+')
        pos++;
    size_t first = pos;
    while (pos < length && digit(text[pos]))
        pos++;
    size_t whole_end = pos;
    while (first < whole_end && text[first] == '0')
        first++;

    /* One spare leading digit absorbs a carry from 999.9999999. */
    size_t used = 1;
    if (first == whole_end) {
        buffer[used++] = '0';
    } else {
        memcpy(buffer + used, text + first, whole_end - first);
        used += whole_end - first;
    }
    size_t fraction = used;
    bool round_up = false;
    if (pos < length && text[pos] == '.') {
        buffer[used++] = '.';
        pos++;
        size_t start = pos;
        while (pos < length && digit(text[pos])) {
            size_t offset = pos - start;
            if (offset < 6)
                buffer[used++] = text[pos];
            else if (offset == 6)
                round_up = text[pos] >= '5';
            pos++;
        }
    }
    size_t begin = 1;
    if (round_up) {
        size_t i = used;
        while (i > 1) {
            i--;
            if (buffer[i] == '.')
                continue;
            if (buffer[i] != '9') {
                buffer[i]++;
                round_up = false;
                break;
            }
            buffer[i] = '0';
        }
        if (round_up) {
            buffer[0] = '1';
            begin = 0;
        }
    }
    if (used > fraction) {
        while (used > fraction + 1 && buffer[used - 1] == '0')
            used--;
        if (used == fraction + 1)
            used = fraction;
    }
    if (used - begin == 1 && buffer[begin] == '0')
        negative = false;
    size_t number_length = used - begin;
    memmove(buffer + (negative ? 1 : 0), buffer + begin, number_length);
    if (negative)
        buffer[0] = '-';
    used = number_length + (negative ? 1 : 0);

    if (token->type == CSS_TOKEN_PERCENTAGE) {
        buffer[used++] = '%';
    } else if (token->type == CSS_TOKEN_DIMENSION) {
        /* A validated dimension has a decoded, case-insensitive CSS unit. */
        for (; pos < length; pos++) {
            char ch = text[pos];
            buffer[used++] = ch >= 'A' && ch <= 'Z' ? ch + ('a' - 'A') : ch;
        }
    } else if (length_value) {
        buffer[used++] = 'p';
        buffer[used++] = 'x';
    }
    css_error error = intern(buffer, used, out);
    free(buffer);
    return error;
}

static size_t scalar_arity(enum css_declaration_value_kind kind)
{
    switch (kind) {
    case CSS_DECL_VALUE_PAIR_LENGTH:
    case CSS_DECL_VALUE_PAIR_KEYWORD:
        return 2;
    case CSS_DECL_VALUE_QUAD_LENGTH:
    case CSS_DECL_VALUE_QUAD_KEYWORD:
        return 4;
    default:
        return 1;
    }
}

/* These grammars have one scalar token per supplied component. Functions need
 * their own specified-value tree/serializer; do not split them at whitespace
 * or serialize their evaluated fixed-point result. */
static const css_declaration_token *scalar_component(const css_declaration_contribution *value, size_t arity)
{
    const css_declaration *source = value->source;
    const css_declaration_token *tokens[4];
    size_t count = 0;
    for (size_t i = 0; i < source->token_count; i++) {
        const css_declaration_token *token = &source->tokens[i];
        if (token->type == CSS_TOKEN_S)
            continue;
        if (count == arity ||
            (token->type != CSS_TOKEN_IDENT && token->type != CSS_TOKEN_NUMBER && token->type != CSS_TOKEN_PERCENTAGE &&
                token->type != CSS_TOKEN_DIMENSION))
            return NULL;
        tokens[count++] = token;
    }
    if (count == 0)
        return NULL;
    size_t component = value->component;
    if (component >= count)
        component = count == 1 ? 0 : component == 2 ? 0 : 1;
    return tokens[component];
}

css_error css__declaration_value_serialize(const css_declaration_contribution *value, lwc_string **out)
{
    if (out == NULL)
        return CSS_BADPARM;
    *out = NULL;
    if (value == NULL || value->source == NULL)
        return CSS_BADPARM;
    const css_declaration *source = value->source;
    const struct css_prop_entry *property = source->property;
    size_t members = property != NULL && property->shorthand ? property->longhand_count : 1;
    if (value->component >= members)
        return CSS_BADPARM;
    if (property == NULL)
        return serialize_unparsed(source, out);
    if (source->deferred)
        return property->shorthand ? intern("", 0, out) : serialize_unparsed(source, out);
    const char *wide = wide_keyword(source);
    if (wide != NULL)
        return intern(wide, strlen(wide), out);
    enum css_declaration_value_kind kind = property->value_kind;
    if (kind == CSS_DECL_VALUE_NONE)
        return CSS_INVALID;
    if (kind == CSS_DECL_VALUE_URI || kind == CSS_DECL_VALUE_IMAGE_LIST || kind == CSS_DECL_VALUE_KEYWORD_LIST ||
        kind == CSS_DECL_VALUE_CONTENT || kind == CSS_DECL_VALUE_STRING_LIST || kind == CSS_DECL_VALUE_COUNTER_LIST)
        return css__declaration_text_serialize(source, out);
    size_t arity = scalar_arity(kind);
    const css_declaration_token *token = scalar_component(value, arity);
    if (token == NULL)
        return CSS_INVALID;
    if (token->type == CSS_TOKEN_IDENT)
        return lwc_string_tolower(token->text, out) == lwc_error_ok ? CSS_OK : CSS_NOMEM;
    if (kind == CSS_DECL_VALUE_KEYWORD || kind == CSS_DECL_VALUE_PAIR_KEYWORD || kind == CSS_DECL_VALUE_QUAD_KEYWORD)
        return CSS_INVALID;
    return serialize_numeric(token, kind != CSS_DECL_VALUE_NUMBER && kind != CSS_DECL_VALUE_NUMBER_LENGTH, out);
}

static css_error serialize_scalars(const css_declaration_contribution *const *members, size_t count, lwc_string **out)
{
    lwc_string *values[4] = {0};
    css_error error = CSS_OK;
    for (size_t i = 0; i < count; i++) {
        error = css__declaration_value_serialize(members[i], &values[i]);
        if (error != CSS_OK)
            goto done;
    }
    /* Interned equality permits the shortest equivalent pair/quad spelling. */
    size_t used = count;
    if (used == 4 && values[3] == values[1])
        used--;
    if (used == 3 && values[2] == values[0])
        used--;
    if (used == 2 && values[1] == values[0])
        used--;
    if (used == 1) {
        *out = lwc_string_ref(values[0]);
        goto done;
    }
    size_t length = used - 1;
    for (size_t i = 0; i < used; i++) {
        size_t n = lwc_string_length(values[i]);
        if (n > SIZE_MAX - length) {
            error = CSS_NOMEM;
            goto done;
        }
        length += n;
    }
    char *buffer = malloc(length);
    if (buffer == NULL) {
        error = CSS_NOMEM;
        goto done;
    }
    size_t pos = 0;
    for (size_t i = 0; i < used; i++) {
        if (i != 0)
            buffer[pos++] = ' ';
        size_t n = lwc_string_length(values[i]);
        memcpy(buffer + pos, lwc_string_data(values[i]), n);
        pos += n;
    }
    error = intern(buffer, length, out);
    free(buffer);
done:
    for (size_t i = 0; i < count; i++) {
        if (values[i] != NULL)
            lwc_string_unref(values[i]);
    }
    return error;
}

css_error css__declaration_shorthand_serialize(const struct css_prop_entry *property,
    const css_declaration_contribution *const *members, size_t count, lwc_string **out)
{
    if (out == NULL)
        return CSS_BADPARM;
    *out = NULL;
    if (property == NULL || !property->shorthand || members == NULL || count == 0 || count != property->longhand_count)
        return CSS_BADPARM;
    const css_declaration *first = NULL;
    const char *wide = NULL;
    bool any_wide = false, same_wide = true, any_deferred = false, same_source = true;
    for (size_t i = 0; i < count; i++) {
        if (members[i] == NULL)
            return intern("", 0, out);
        const css_declaration *source = members[i]->source;
        if (source == NULL || members[i]->name == NULL || strcmp(members[i]->name, property->longhands[i]) != 0)
            return CSS_BADPARM;
        if (i == 0) {
            first = source;
            wide = wide_keyword(source);
        }
        if (source->important != first->important)
            return intern("", 0, out);
        const char *keyword = wide_keyword(source);
        any_wide |= keyword != NULL;
        same_wide &= keyword == wide;
        any_deferred |= source->deferred;
        same_source &= source == first;
    }
    /* CSS Values 5, Substitution in Shorthand Properties: only the original
     * shorthand may recover its text when every member still comes from it. */
    if (any_deferred) {
        if (same_source && first->property == property)
            return serialize_unparsed(first, out);
        return intern("", 0, out);
    }
    if (any_wide)
        return same_wide ? intern(wide, strlen(wide), out) : intern("", 0, out);
    if (scalar_arity(property->value_kind) == 1)
        return CSS_INVALID;
    return serialize_scalars(members, count, out);
}

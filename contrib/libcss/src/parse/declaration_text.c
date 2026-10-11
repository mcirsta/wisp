/*
 * This file is part of LibCSS.
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 */

#include <stdlib.h>
#include <string.h>

#include "parse/declaration_text.h"
#include "parse/property.h"

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

/* CSSOM §2.1 string serialization. Only ASCII bytes need escaping, so copying
 * the intervening UTF-8 spans preserves non-ASCII code points unchanged. */
static css_error quoted(struct text_output *out, lwc_string *string)
{
    const char *text = lwc_string_data(string);
    size_t length = lwc_string_length(string);
    css_error error = append(out, "\"", 1, false);
    size_t start = 0;
    for (size_t i = 0; error == CSS_OK && i < length; i++) {
        unsigned char ch = text[i];
        if (ch >= 0x20 && ch != 0x7f && ch != '"' && ch != '\\')
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
            if (ch == '"' || ch == '\\') {
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
    if (error == CSS_OK)
        error = append(out, "\"", 1, false);
    return error;
}

static bool comma(const css_declaration_token *token)
{
    return token->type == CSS_TOKEN_CHAR && token->text != NULL && lwc_string_length(token->text) == 1 &&
        lwc_string_data(token->text)[0] == ',';
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
            if (kind == CSS_DECL_VALUE_URI || !comma(token))
                return CSS_INVALID;
            error = append(out, ", ", 2, false);
        } else if (token->type == CSS_TOKEN_IDENT) {
            error = append(out, lwc_string_data(token->text), lwc_string_length(token->text), true);
        } else if (token->type == CSS_TOKEN_URI && kind != CSS_DECL_VALUE_KEYWORD_LIST) {
            error = append(out, "url(", 4, false);
            if (error == CSS_OK)
                error = quoted(out, token->text);
            if (error == CSS_OK)
                error = append(out, ")", 1, false);
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

css_error css__declaration_text_serialize(const css_declaration *source, lwc_string **out)
{
    struct text_output text = {.capacity = SIZE_MAX};
    css_error error = serialize_list(source, &text);
    if (error != CSS_OK)
        return error;
    text.capacity = text.length;
    text.length = 0;
    text.data = malloc(text.capacity);
    if (text.data == NULL)
        return CSS_NOMEM;
    error = serialize_list(source, &text);
    if (error == CSS_OK && lwc_intern_string(text.data, text.length, out) != lwc_error_ok)
        error = CSS_NOMEM;
    free(text.data);
    return error;
}

/*
 * This file is part of LibCSS.
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 */

#include <stdlib.h>
#include <string.h>

#include "parse/transform.h"
#include "parse/declaration_value.h"
#include "utils/css_utils.h"

static const css_declaration_token *next(const css_declaration *source, size_t *cursor)
{
    while (*cursor < source->token_count) {
        const css_declaration_token *token = &source->tokens[(*cursor)++];
        if (token->type != CSS_TOKEN_S)
            return token;
    }
    return NULL;
}

static bool character(const css_declaration_token *token, char ch)
{
    return token != NULL && token->type == CSS_TOKEN_CHAR && lwc_string_length(token->text) == 1 &&
        lwc_string_data(token->text)[0] == ch;
}

/* Transforms 2 section 12: scale percentages serialize as numbers. Shift the
 * decimal before rounding, preserving arbitrarily long accepted decimals and
 * avoiding both floating-point loss and the renderer's fixed-point limit. */
static css_error scale_number(const css_declaration_token *token, lwc_string **out)
{
    if (token->type != CSS_TOKEN_PERCENTAGE)
        return css__declaration_numeric_serialize(token, false, out);
    const char *text = lwc_string_data(token->text);
    size_t length = lwc_string_length(token->text);
    if (length > SIZE_MAX - 4)
        return CSS_NOMEM;
    char *buffer = malloc(length + 4);
    if (buffer == NULL)
        return CSS_NOMEM;
    size_t first = (text[0] == '+' || text[0] == '-') ? 1 : 0;
    size_t end = first;
    while (end < length && text[end] != '.')
        end++;
    size_t whole = end - first, used = first;
    if (first != 0)
        buffer[0] = text[0];
    if (whole > 2) {
        memcpy(buffer + used, text + first, whole - 2);
        used += whole - 2;
    } else {
        buffer[used++] = '0';
    }
    buffer[used++] = '.';
    for (size_t i = whole; i < 2; i++)
        buffer[used++] = '0';
    size_t tail = whole < 2 ? whole : 2;
    memcpy(buffer + used, text + end - tail, tail);
    used += tail;
    if (end < length) {
        memcpy(buffer + used, text + end + 1, length - end - 1);
        used += length - end - 1;
    }
    css_declaration_token number = {.type = CSS_TOKEN_NUMBER};
    css_error error = lwc_intern_string(buffer, used, &number.text) == lwc_error_ok ? CSS_OK : CSS_NOMEM;
    free(buffer);
    if (error == CSS_OK) {
        error = css__declaration_numeric_serialize(&number, false, out);
        lwc_string_unref(number.text);
    }
    return error;
}

static css_error function(const css_declaration *source, size_t *cursor, lwc_string **out)
{
    const css_declaration_token *token = next(source, cursor);
    if (token == NULL || token->type != CSS_TOKEN_FUNCTION)
        return CSS_INVALID;
    const struct css_transform_syntax *syntax = css__transform_syntax(token->text);
    if (syntax == NULL)
        return CSS_INVALID;
    lwc_string *parts[2] = {0};
    size_t count = 0;
    css_error error = CSS_OK;
    do {
        token = next(source, cursor);
        if (token == NULL || (token->type != CSS_TOKEN_NUMBER && token->type != CSS_TOKEN_DIMENSION &&
                token->type != CSS_TOKEN_PERCENTAGE)) {
            error = CSS_INVALID;
            goto done;
        }
        if (syntax->argument == CSS_TRANSFORM_NUMBER) {
            error = scale_number(token, &parts[count]);
        } else if (syntax->argument == CSS_TRANSFORM_ANGLE && token->type == CSS_TOKEN_NUMBER) {
            error = lwc_intern_string("0deg", 4, &parts[count]) == lwc_error_ok ? CSS_OK : CSS_NOMEM;
        } else {
            error = css__declaration_numeric_serialize(token, syntax->argument == CSS_TRANSFORM_LENGTH, &parts[count]);
        }
        if (error != CSS_OK)
            goto done;
        count++;
        token = next(source, cursor);
    } while (count < syntax->arity && character(token, ','));
    if (!character(token, ')')) {
        error = CSS_INVALID;
        goto done;
    }
    /* Transforms 1 section 3.1: omit only an equivalent optional component;
     * preserve function identities, order and specified units. */
    if (count == 2 && ((syntax->argument == CSS_TRANSFORM_NUMBER && parts[0] == parts[1]) ||
            (syntax->argument == CSS_TRANSFORM_LENGTH && css__number_sign(parts[1]) == 0)))
        count = 1;
    lwc_string *body;
    error = css__declaration_values_join(parts, count, ", ", &body);
    if (error == CSS_OK) {
        size_t prefix = strlen(syntax->name), length = lwc_string_length(body);
        char *text = length <= SIZE_MAX - prefix - 2 ? malloc(prefix + length + 2) : NULL;
        if (text == NULL) {
            error = CSS_NOMEM;
        } else {
            memcpy(text, syntax->name, prefix);
            text[prefix] = '(';
            memcpy(text + prefix + 1, lwc_string_data(body), length);
            text[prefix + length + 1] = ')';
            error = lwc_intern_string(text, prefix + length + 2, out) == lwc_error_ok ? CSS_OK : CSS_NOMEM;
            free(text);
        }
        lwc_string_unref(body);
    }
done:
    for (size_t i = 0; i < N_ELEMENTS(parts); i++) {
        if (parts[i] != NULL)
            lwc_string_unref(parts[i]);
    }
    return error;
}

css_error css__declaration_transform_serialize(const css_declaration *source, lwc_string **out)
{
    if (source->token_count == 1 && source->tokens[0].type == CSS_TOKEN_IDENT)
        return lwc_intern_string("none", 4, out) == lwc_error_ok ? CSS_OK : CSS_NOMEM;
    size_t count = 0;
    for (size_t i = 0; i < source->token_count;) {
        if (source->tokens[i].type == CSS_TOKEN_S) {
            i++;
            continue;
        }
        i = css__declaration_component_end(source, i);
        if (i == 0)
            return CSS_INVALID;
        count++;
    }
    if (count == 0)
        return CSS_INVALID;
    if (count > SIZE_MAX / sizeof(lwc_string *))
        return CSS_NOMEM;
    lwc_string *single[1] = {0};
    lwc_string **values = count == 1 ? single : calloc(count, sizeof(*values));
    if (values == NULL)
        return CSS_NOMEM;
    css_error error = CSS_OK;
    size_t cursor = 0;
    for (size_t i = 0; error == CSS_OK && i < count; i++)
        error = function(source, &cursor, &values[i]);
    if (error == CSS_OK)
        error = css__declaration_values_join(values, count, " ", out);
    for (size_t i = 0; i < count; i++) {
        if (values[i] != NULL)
            lwc_string_unref(values[i]);
    }
    if (values != single)
        free(values);
    return error;
}

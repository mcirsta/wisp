/*
 * This file is part of LibCSS.
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 */

#include <stdlib.h>

#include "parse/declarations.h"
#include "parse/language.h"
#include "stylesheet.h"

struct css_declaration_list {
    css_stylesheet *sheet;
    css_declaration *entries;
    size_t count;
    size_t capacity;
    css_error error;
    bool complete;
};

static void declaration_destroy(css_declaration *entry)
{
    if (entry->name != NULL)
        lwc_string_unref(entry->name);
    for (size_t i = 0; i < entry->token_count; i++) {
        if (entry->tokens[i].text != NULL)
            lwc_string_unref(entry->tokens[i].text);
    }
    free((void *)entry->tokens);
}

css_error css__declaration_list_create(const css_stylesheet_params *params, css_declaration_list **out)
{
    if (out == NULL)
        return CSS_BADPARM;
    *out = NULL;
    if (params == NULL)
        return CSS_BADPARM;

    css_declaration_list *list = calloc(1, sizeof(*list));
    if (list == NULL)
        return CSS_NOMEM;

    css_stylesheet_params inline_params = *params;
    inline_params.inline_style = true;
    inline_params.charset = "UTF-8";
    css_error error = css_stylesheet_create(&inline_params, &list->sheet);
    if (error != CSS_OK) {
        free(list);
        return error;
    }
    ((css_language *)list->sheet->parser_frontend)->declarations = list;
    *out = list;
    return CSS_OK;
}

css_error css__declaration_list_append(css_declaration_list *list, const uint8_t *data, size_t len)
{
    if (list == NULL || data == NULL)
        return CSS_BADPARM;
    if (list->error != CSS_OK)
        return list->error;
    if (list->complete)
        return CSS_INVALID;

    css_error error = css_stylesheet_append_data(list->sheet, data, len);
    if (error != CSS_OK && error != CSS_NEEDDATA)
        list->error = error;
    return error;
}

css_error css__declaration_list_complete(css_declaration_list *list)
{
    if (list == NULL)
        return CSS_BADPARM;
    if (list->error != CSS_OK)
        return list->error;
    if (list->complete)
        return CSS_INVALID;

    list->error = css_stylesheet_data_done(list->sheet);
    list->complete = (list->error == CSS_OK);
    return list->error;
}

void css__declaration_list_destroy(css_declaration_list *list)
{
    if (list == NULL)
        return;
    css_stylesheet_destroy(list->sheet);
    for (size_t i = 0; i < list->count; i++)
        declaration_destroy(&list->entries[i]);
    free(list->entries);
    free(list);
}

size_t css__declaration_list_count(const css_declaration_list *list)
{
    return list != NULL && list->complete ? list->count : 0;
}

const css_declaration *css__declaration_list_get(const css_declaration_list *list, size_t index)
{
    return index < css__declaration_list_count(list) ? &list->entries[index] : NULL;
}

const css_stylesheet *css__declaration_list_sheet(const css_declaration_list *list)
{
    return list != NULL && list->complete ? list->sheet : NULL;
}

static int32_t trim_whitespace(const parserutils_vector *tokens, int32_t start, int32_t end)
{
    const css_token *token;
    while (end > start && (token = parserutils_vector_peek(tokens, end - 1))->type == CSS_TOKEN_S)
        end--;
    return end;
}

css_error css__declaration_list_record(css_declaration_list *list, const css_token *name,
    const parserutils_vector *tokens, int32_t value_start, uint32_t bytecode_offset, uint32_t bytecode_length)
{
    css_declaration entry = {.bytecode_offset = bytecode_offset, .bytecode_length = bytecode_length};
    size_t length;
    parserutils_vector_get_length(tokens, &length);
    if (length > INT32_MAX)
        return CSS_NOMEM;
    int32_t end = trim_whitespace(tokens, value_start, (int32_t)length);

    /* The property parser has already validated the grammar. Only a final
     * top-level !important can end in these tokens; strings/functions are not
     * mistaken for the priority. Keep nested !important tokens in the value. */
    const css_token *last = end > value_start ? parserutils_vector_peek(tokens, end - 1) : NULL;
    bool important = false;
    if (last != NULL && last->type == CSS_TOKEN_IDENT &&
        lwc_string_caseless_isequal(last->idata, list->sheet->propstrings[IMPORTANT], &important) != lwc_error_ok)
        return CSS_NOMEM;
    if (important) {
        int32_t bang = trim_whitespace(tokens, value_start, end - 1) - 1;
        if (bang >= value_start && tokenIsChar(parserutils_vector_peek(tokens, bang), '!')) {
            entry.important = true;
            end = trim_whitespace(tokens, value_start, bang);
        }
    }

    if (name->type == CSS_TOKEN_CUSTOM_PROPERTY) {
        entry.name = lwc_string_ref(name->idata);
    } else if (lwc_string_tolower(name->idata, &entry.name) != lwc_error_ok) {
        return CSS_NOMEM;
    }
    entry.token_count = (size_t)(end - value_start);
    if (entry.token_count > SIZE_MAX / sizeof(css_declaration_token)) {
        lwc_string_unref(entry.name);
        return CSS_NOMEM;
    }
    if (entry.token_count != 0) {
        css_declaration_token *value = malloc(entry.token_count * sizeof(*value));
        if (value == NULL) {
            lwc_string_unref(entry.name);
            return CSS_NOMEM;
        }
        for (size_t i = 0; i < entry.token_count; i++) {
            const css_token *token = parserutils_vector_peek(tokens, value_start + (int32_t)i);
            value[i].type = token->type;
            value[i].text = token->idata != NULL ? lwc_string_ref(token->idata) : NULL;
        }
        entry.tokens = value;
    }

    if (list->count == list->capacity) {
        size_t capacity = list->capacity != 0 ? list->capacity * 2 : 8;
        if (capacity < list->capacity || capacity > SIZE_MAX / sizeof(*list->entries)) {
            declaration_destroy(&entry);
            return CSS_NOMEM;
        }
        css_declaration *entries = realloc(list->entries, capacity * sizeof(*entries));
        if (entries == NULL) {
            declaration_destroy(&entry);
            return CSS_NOMEM;
        }
        list->entries = entries;
        list->capacity = capacity;
    }
    list->entries[list->count++] = entry;
    return CSS_OK;
}

/*
 * This file is part of LibCSS.
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 */

#include <assert.h>
#include <stdlib.h>

#include "parse/declarations.h"
#include "parse/language.h"
#include "parse/property.h"
#include "stylesheet.h"
#include "utils/parserutilserror.h"

struct css_declaration_list {
    css_stylesheet *sheet;
    parserutils_buffer *source;
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
    if (entry->source_value != NULL)
        lwc_string_unref(entry->source_value);
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
    error = css_error_from_parserutils_error(parserutils_buffer_create(&list->source));
    if (error == CSS_OK)
        error = css__parser_capture_string_source(list->sheet->parser, list->source);
    if (error != CSS_OK) {
        css__declaration_list_destroy(list);
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
    if (list->complete) {
        parserutils_buffer_destroy(list->source);
        list->source = NULL;
    }
    return list->error;
}

void css__declaration_list_destroy(css_declaration_list *list)
{
    if (list == NULL)
        return;
    css_stylesheet_destroy(list->sheet);
    if (list->source != NULL)
        parserutils_buffer_destroy(list->source);
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

static bool source_whitespace(char ch)
{
    return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n' || ch == '\f';
}

/* Everything after the last non-whitespace token is whitespace or a skipped
 * comment. Inspect only that gap: trimming bytes inside the token would corrupt
 * escaped spaces, hex-escape terminators and strings recovered at EOF. Comments
 * also retain their spelling, including an unterminated comment's final spaces. */
static void value_bounds(css_declaration *entry, size_t protected_end)
{
    const char *text = lwc_string_data(entry->source_value);
    size_t length = lwc_string_length(entry->source_value);
    size_t end = protected_end;
    for (size_t i = protected_end; i < length;) {
        if (source_whitespace(text[i])) {
            i++;
        } else {
            assert(i + 1 < length && text[i] == '/' && text[i + 1] == '*');
            i += 2;
            while (i + 1 < length && (text[i] != '*' || text[i + 1] != '/'))
                i++;
            i = i + 1 < length ? i + 2 : length;
            end = i;
        }
    }
    size_t start = 0;
    while (start < end && source_whitespace(text[start]))
        start++;
    entry->value_start = start;
    entry->value_end = end;
}

css_error css__declaration_list_record(css_declaration_list *list, const css_token *name,
    const parserutils_vector *tokens, int32_t value_start, uint32_t bytecode_offset, uint32_t bytecode_length,
    bool deferred)
{
    css_declaration entry = {
        .bytecode_offset = bytecode_offset, .bytecode_length = bytecode_length, .deferred = deferred,
        .quirks_allowed = list->sheet->quirks_allowed};
    const css_token *colon = parserutils_vector_peek(tokens, value_start - 1);
    size_t source_start = colon->source_end;
    size_t source_end = css__parser_source_position(list->sheet->parser);
    consumeWhitespace(tokens, &value_start);
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
            const css_token *priority = parserutils_vector_peek(tokens, bang);
            source_end = priority->source_start;
            entry.important = true;
            end = trim_whitespace(tokens, value_start, bang);
        }
    }
    assert(source_start <= source_end && source_end <= list->source->length);
    if (lwc_intern_string((const char *)list->source->data + source_start, source_end - source_start,
            &entry.source_value) != lwc_error_ok)
        return CSS_NOMEM;

    const css_token *value_last = end > value_start ? parserutils_vector_peek(tokens, end - 1) : NULL;
    value_bounds(&entry, value_last != NULL ? value_last->source_end - source_start : 0);

    if (css__token_is_custom_property(name)) {
        entry.name = lwc_string_ref(name->idata);
    } else if (lwc_string_tolower(name->idata, &entry.name) != lwc_error_ok) {
        declaration_destroy(&entry);
        return CSS_NOMEM;
    }
    if (!css__token_is_custom_property(name)) {
        entry.property = css_prop_lookup(lwc_string_data(entry.name), lwc_string_length(entry.name));
        assert(entry.property != NULL);
    }
    entry.token_count = (size_t)(end - value_start);
    if (entry.token_count > SIZE_MAX / sizeof(css_declaration_token)) {
        entry.token_count = 0;
        declaration_destroy(&entry);
        return CSS_NOMEM;
    }
    if (entry.token_count != 0) {
        css_declaration_token *value = malloc(entry.token_count * sizeof(*value));
        if (value == NULL) {
            entry.token_count = 0;
            declaration_destroy(&entry);
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

size_t css__declaration_component_end(const css_declaration *source, size_t start)
{
    if (start >= source->token_count)
        return 0;
    size_t pos = start;
    size_t depth = source->tokens[pos++].type == CSS_TOKEN_FUNCTION ? 1 : 0;
    while (pos < source->token_count && depth != 0) {
        const css_declaration_token *token = &source->tokens[pos++];
        if (token->type == CSS_TOKEN_FUNCTION)
            depth++;
        else if (token->type == CSS_TOKEN_CHAR && lwc_string_length(token->text) == 1) {
            if (lwc_string_data(token->text)[0] == '(')
                depth++;
            else if (lwc_string_data(token->text)[0] == ')')
                depth--;
        }
    }
    return depth == 0 ? pos : 0;
}

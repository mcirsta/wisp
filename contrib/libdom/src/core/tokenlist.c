/*
 * This file is part of libdom.
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 * Copyright 2022 Daniel Silverstone <dsilvers@digital-scurf.org>
 */

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include <dom/core/element.h>
#include <dom/core/string.h>
#include <dom/core/tokenlist.h>

/* DOM ordered sets: https://dom.spec.whatwg.org/#interface-domtokenlist */
struct dom_tokenlist {
    uint32_t refcnt;
    dom_element *ele;
    dom_string *attr, *parsed_value;
    bool parsed;
    dom_string **entries;
    uint32_t len, alloc;
};

static bool token_space(unsigned char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
}

static dom_exception validate_token(dom_string *token)
{
    size_t length = dom_string_byte_length(token);
    if (length == 0)
        return DOM_SYNTAX_ERR;
    const char *text = dom_string_data(token);
    for (size_t i = 0; i < length; i++) {
        if (token_space(text[i]))
            return DOM_INVALID_CHARACTER_ERR;
    }
    return DOM_NO_ERR;
}

static void clear_entries(dom_tokenlist *list)
{
    while (list->len != 0)
        dom_string_unref(list->entries[--list->len]);
}

static uint32_t find_token(dom_tokenlist *list, dom_string *token)
{
    for (uint32_t i = 0; i < list->len; i++) {
        if (dom_string_isequal(list->entries[i], token))
            return i;
    }
    return list->len;
}

static dom_exception reserve(dom_tokenlist *list, uint32_t count)
{
    if (count <= list->alloc)
        return DOM_NO_ERR;
    size_t capacity = count;
    if (capacity < 4)
        capacity = 4;
    if (list->alloc <= UINT32_MAX / 2 && capacity < (size_t)list->alloc * 2)
        capacity = (size_t)list->alloc * 2;
    if (capacity > SIZE_MAX / sizeof(*list->entries))
        return DOM_NO_MEM_ERR;
    dom_string **entries = realloc(list->entries, capacity * sizeof(*entries));
    if (entries == NULL)
        return DOM_NO_MEM_ERR;
    list->entries = entries;
    list->alloc = capacity;
    return DOM_NO_ERR;
}

static dom_exception reparse(dom_tokenlist *list)
{
    dom_string *value = NULL;
    dom_exception err = dom_element_get_attribute(list->ele, list->attr, &value);
    if (err != DOM_NO_ERR)
        return err;
    /* Read the actual attribute, including removal, rather than relying on
     * deprecated mutation events and their allocation/failure paths. Strings
     * are immutable, so a retained identical value needs no parsing. */
    if (list->parsed &&
        (value == list->parsed_value ||
            (value != NULL && list->parsed_value != NULL && dom_string_isequal(value, list->parsed_value)))) {
        dom_string_unref(value);
        return DOM_NO_ERR;
    }
    list->parsed = false;
    clear_entries(list);
    size_t length = value == NULL ? 0 : dom_string_byte_length(value);
    const char *text = value == NULL ? "" : dom_string_data(value);
    for (size_t pos = 0; pos < length;) {
        if (token_space(text[pos])) {
            pos++;
            continue;
        }
        size_t start = pos;
        while (pos < length && !token_space(text[pos]))
            pos++;
        dom_string *token = NULL;
        err = dom_string_create((const uint8_t *)text + start, pos - start, &token);
        if (err != DOM_NO_ERR)
            break;
        if (find_token(list, token) == list->len) {
            err = list->len == UINT32_MAX ? DOM_NO_MEM_ERR : reserve(list, list->len + 1);
            if (err == DOM_NO_ERR)
                list->entries[list->len++] = dom_string_ref(token);
        }
        dom_string_unref(token);
        if (err != DOM_NO_ERR)
            break;
    }
    if (err == DOM_NO_ERR) {
        dom_string_unref(list->parsed_value);
        list->parsed_value = value;
        list->parsed = true;
    } else {
        dom_string_unref(value);
        clear_entries(list);
    }
    return err;
}

static dom_exception update(dom_tokenlist *list, bool *updated)
{
    /* The token set was edited: even a serialization/write failure must force
     * the next read to reparse the real attribute, not expose an uncommitted set. */
    list->parsed = false;
    if (list->len == 0 && list->parsed_value == NULL)
        return DOM_NO_ERR;
    size_t bytes = list->len == 0 ? 0 : list->len - 1;
    for (uint32_t i = 0; i < list->len; i++) {
        size_t length = dom_string_byte_length(list->entries[i]);
        if (length > SIZE_MAX - bytes)
            return DOM_NO_MEM_ERR;
        bytes += length;
    }
    char *buffer = malloc(bytes == 0 ? 1 : bytes);
    if (buffer == NULL)
        return DOM_NO_MEM_ERR;
    size_t pos = 0;
    for (uint32_t i = 0; i < list->len; i++) {
        size_t length = dom_string_byte_length(list->entries[i]);
        if (i != 0)
            buffer[pos++] = ' ';
        memcpy(buffer + pos, dom_string_data(list->entries[i]), length);
        pos += length;
    }
    dom_string *value = NULL;
    dom_exception err = dom_string_create((const uint8_t *)buffer, bytes, &value);
    free(buffer);
    if (err == DOM_NO_ERR) {
        *updated = true; /* The native setter can fail after a partial change. */
        err = dom_element_set_attribute(list->ele, list->attr, value);
    }
    dom_string_unref(value);
    return err;
}

static void remove_at(dom_tokenlist *list, uint32_t index)
{
    dom_string_unref(list->entries[index]);
    list->len--;
    memmove(list->entries + index, list->entries + index + 1, (list->len - index) * sizeof(*list->entries));
}

dom_exception dom_tokenlist_create(dom_element *ele, dom_string *attr, dom_tokenlist **result)
{
    dom_tokenlist *list = calloc(1, sizeof(*list));
    if (list == NULL)
        return DOM_NO_MEM_ERR;
    list->refcnt = 1;
    list->ele = (dom_element *)dom_node_ref(ele);
    list->attr = dom_string_ref(attr);
    *result = list;
    return DOM_NO_ERR;
}

void dom_tokenlist_ref(dom_tokenlist *list)
{
    assert(list != NULL);
    list->refcnt++;
}

void dom_tokenlist_unref(dom_tokenlist *list)
{
    assert(list != NULL);
    if (--list->refcnt != 0)
        return;
    clear_entries(list);
    free(list->entries);
    dom_string_unref(list->parsed_value);
    dom_string_unref(list->attr);
    dom_node_unref(list->ele);
    free(list);
}

dom_exception dom_tokenlist_get_length(dom_tokenlist *list, uint32_t *length)
{
    dom_exception err = reparse(list);
    if (err == DOM_NO_ERR)
        *length = list->len;
    return err;
}

dom_exception _dom_tokenlist_item(dom_tokenlist *list, uint32_t index, dom_string **value)
{
    dom_exception err = reparse(list);
    if (err == DOM_NO_ERR)
        *value = index < list->len ? dom_string_ref(list->entries[index]) : NULL;
    return err;
}

dom_exception dom_tokenlist_get_value(dom_tokenlist *list, dom_string **value)
{
    return dom_element_get_attribute(list->ele, list->attr, value);
}

dom_exception dom_tokenlist_set_value(dom_tokenlist *list, dom_string *value)
{
    list->parsed = false;
    return dom_element_set_attribute(list->ele, list->attr, value);
}

dom_exception dom_tokenlist_contains(dom_tokenlist *list, dom_string *value, bool *contains)
{
    dom_exception err = reparse(list);
    if (err == DOM_NO_ERR)
        *contains = find_token(list, value) != list->len;
    return err;
}

/* Validate every token before modifying the set; perform one attribute update
 * even for an empty argument list or an already-present/absent token. */
static dom_exception
change_tokens(dom_tokenlist *list, dom_string *const *tokens, uint32_t count, bool add, bool *updated)
{
    *updated = false;
    for (uint32_t i = 0; i < count; i++) {
        dom_exception err = validate_token(tokens[i]);
        if (err != DOM_NO_ERR)
            return err;
    }
    dom_exception err = reparse(list);
    if (err != DOM_NO_ERR)
        return err;
    if (add) {
        if (count > UINT32_MAX - list->len)
            return DOM_NO_MEM_ERR;
        err = reserve(list, list->len + count);
        if (err != DOM_NO_ERR)
            return err;
    }
    for (uint32_t i = 0; i < count; i++) {
        uint32_t index = find_token(list, tokens[i]);
        if (add && index == list->len)
            list->entries[list->len++] = dom_string_ref(tokens[i]);
        else if (!add && index != list->len)
            remove_at(list, index);
    }
    return update(list, updated);
}

dom_exception dom_tokenlist_add_tokens(dom_tokenlist *list, dom_string *const *tokens, uint32_t count, bool *updated)
{
    return change_tokens(list, tokens, count, true, updated);
}

dom_exception dom_tokenlist_remove_tokens(dom_tokenlist *list, dom_string *const *tokens, uint32_t count, bool *updated)
{
    return change_tokens(list, tokens, count, false, updated);
}

dom_exception dom_tokenlist_add(dom_tokenlist *list, dom_string *value)
{
    bool updated;
    return dom_tokenlist_add_tokens(list, &value, 1, &updated);
}

dom_exception dom_tokenlist_remove(dom_tokenlist *list, dom_string *value)
{
    bool updated;
    return dom_tokenlist_remove_tokens(list, &value, 1, &updated);
}

dom_exception dom_tokenlist_toggle(dom_tokenlist *list, dom_string *token, int force, bool *result, bool *updated)
{
    *updated = false;
    dom_exception err = validate_token(token);
    if (err == DOM_NO_ERR)
        err = reparse(list);
    if (err != DOM_NO_ERR)
        return err;
    uint32_t index = find_token(list, token);
    bool present = index != list->len;
    *result = force < 0 ? !present : force != 0;
    if (*result == present)
        return DOM_NO_ERR;
    if (present) {
        remove_at(list, index);
    } else {
        err = list->len == UINT32_MAX ? DOM_NO_MEM_ERR : reserve(list, list->len + 1);
        if (err != DOM_NO_ERR)
            return err;
        list->entries[list->len++] = dom_string_ref(token);
    }
    return update(list, updated);
}

dom_exception
dom_tokenlist_replace(dom_tokenlist *list, dom_string *token, dom_string *replacement, bool *result, bool *updated)
{
    *result = *updated = false;
    /* replace checks both empty strings before checking either for whitespace. */
    if (dom_string_byte_length(token) == 0 || dom_string_byte_length(replacement) == 0)
        return DOM_SYNTAX_ERR;
    dom_exception err = validate_token(token);
    if (err == DOM_NO_ERR)
        err = validate_token(replacement);
    if (err == DOM_NO_ERR)
        err = reparse(list);
    if (err != DOM_NO_ERR)
        return err;
    uint32_t index = find_token(list, token);
    if (index == list->len)
        return DOM_NO_ERR;
    uint32_t other = find_token(list, replacement);
    if (other < index) {
        remove_at(list, index);
    } else {
        dom_string_unref(list->entries[index]);
        list->entries[index] = dom_string_ref(replacement);
        if (other != index && other < list->len)
            remove_at(list, other);
    }
    *result = true;
    return update(list, updated);
}

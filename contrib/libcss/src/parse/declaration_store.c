/*
 * This file is part of LibCSS.
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 */

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "parse/declaration_store.h"
#include "parse/declaration_block.h"
#include "parse/declaration_value.h"
#include "parse/property.h"

/* Every source in a store is the first member of an owned_value. Contributions
 * retain it independently; replacing the last member releases it immediately.
 * Keeping the source identity is essential for pending shorthand readback. */
struct owned_value {
    css_declaration value;
    size_t references;
};

struct css_declaration_store {
    css_declaration_contribution *entries;
    size_t count, capacity;
};

static css_error value_copy(const css_declaration *value, const css_declaration **out)
{
    struct owned_value *owned = malloc(sizeof(*owned));
    if (owned == NULL)
        return CSS_NOMEM;
    css_error error = css__declaration_copy_value(value, &owned->value);
    if (error != CSS_OK) {
        free(owned);
        return error;
    }
    owned->references = 1;
    *out = &owned->value;
    return CSS_OK;
}

static const css_declaration *value_ref(const css_declaration *value)
{
    struct owned_value *owned = (struct owned_value *)value;
    owned->references++;
    return value;
}

static void value_unref(const css_declaration *value)
{
    struct owned_value *owned = (struct owned_value *)value;
    assert(owned->references != 0);
    if (--owned->references == 0) {
        css__declaration_fini(&owned->value);
        free(owned);
    }
}

static css_error reserve(css_declaration_store *store, size_t needed)
{
    if (needed <= store->capacity)
        return CSS_OK;
    size_t capacity = store->capacity;
    if (capacity <= SIZE_MAX / 2)
        capacity *= 2;
    if (capacity < needed)
        capacity = needed;
    if (capacity > SIZE_MAX / sizeof(*store->entries))
        return CSS_NOMEM;
    css_declaration_contribution *entries = realloc(store->entries, capacity * sizeof(*entries));
    if (entries == NULL)
        return CSS_NOMEM;
    store->entries = entries;
    store->capacity = capacity;
    return CSS_OK;
}

void css__declaration_store_destroy(css_declaration_store *store)
{
    if (store == NULL)
        return;
    for (size_t i = 0; i < store->count; i++)
        value_unref(store->entries[i].source);
    free(store->entries);
    free(store);
}

css_error css__declaration_store_create(const css_declaration_list *list, css_declaration_store **out)
{
    if (out == NULL)
        return CSS_BADPARM;
    *out = NULL;
    css_declaration_order *order;
    css_error error = css__declaration_order_create(list, &order);
    if (error != CSS_OK)
        return error;
    css_declaration_store *store = calloc(1, sizeof(*store));
    if (store == NULL) {
        css__declaration_order_destroy(order);
        return CSS_NOMEM;
    }
    size_t count = css__declaration_order_count(order);
    error = reserve(store, count);
    const css_declaration *previous = NULL, *copy = NULL;
    for (size_t i = 0; error == CSS_OK && i < count; i++) {
        const css_declaration_contribution *entry = css__declaration_order_get(order, i);
        /* Specified order groups the winners from each source declaration. */
        if (entry->source != previous)
            error = value_copy(entry->source, &copy);
        else
            value_ref(copy);
        if (error != CSS_OK)
            break;
        previous = entry->source;
        store->entries[store->count++] = (css_declaration_contribution){
            .name = copy->property != NULL && copy->property->shorthand ? entry->name : lwc_string_data(copy->name),
            .source = copy,
            .component = entry->component,
        };
    }
    css__declaration_order_destroy(order);
    if (error != CSS_OK) {
        css__declaration_store_destroy(store);
        return error;
    }
    *out = store;
    return CSS_OK;
}

size_t css__declaration_store_count(const css_declaration_store *store)
{
    return store != NULL ? store->count : 0;
}

const css_declaration_contribution *css__declaration_store_get(const css_declaration_store *store, size_t index)
{
    return index < css__declaration_store_count(store) ? &store->entries[index] : NULL;
}

struct property_key {
    const char *name;
    size_t length;
    const struct css_prop_entry *property;
    bool custom;
};

static struct property_key property_key(const char *name, size_t length)
{
    struct property_key key = {
        .name = name, .length = length, .custom = length >= 2 && name[0] == '-' && name[1] == '-'};
    if (!key.custom) {
        key.property = css_prop_lookup(name, length);
        if (key.property != NULL) {
            key.name = key.property->name;
            key.length = strlen(key.name);
        }
    }
    return key;
}

static bool matches(const struct property_key *key, const char *name)
{
    if (strlen(name) != key->length)
        return false;
    for (size_t i = 0; i < key->length; i++) {
        unsigned char ch = key->name[i];
        if (!key->custom && ch >= 'A' && ch <= 'Z')
            ch += 'a' - 'A';
        if (ch != (unsigned char)name[i])
            return false;
    }
    return true;
}

static size_t find(const css_declaration_store *store, const struct property_key *key)
{
    for (size_t i = 0; i < store->count; i++) {
        if (matches(key, store->entries[i].name))
            return i;
    }
    return store->count;
}

static bool needs_append(const css_declaration_store *store, size_t index)
{
    const struct css_prop_entry *target = css__declaration_contribution_property(&store->entries[index]);
    if (target == NULL || target->logical_group == CSS_LOGICAL_GROUP_NONE)
        return false;
    for (size_t i = index + 1; i < store->count; i++) {
        const struct css_prop_entry *later = css__declaration_contribution_property(&store->entries[i]);
        if (later != NULL && later->logical_group == target->logical_group && later->logical != target->logical)
            return true;
    }
    return false;
}

static size_t member_count(const css_declaration *value)
{
    return value->property != NULL && value->property->shorthand ? value->property->longhand_count : 1;
}

static const char *member_name(const css_declaration *value, size_t component)
{
    return value->property != NULL && value->property->shorthand ? value->property->longhands[component]
                                                                 : lwc_string_data(value->name);
}

/* Skip an exact repeat without allocation or serialization. Other equivalent
 * spellings may still report an update, as permitted by CSSOM. Deferred
 * shorthand members must also share a source before this can be a no-op. */
static bool unchanged(const css_declaration_store *store, const css_declaration *value)
{
    const css_declaration *first = NULL;
    for (size_t i = 0; i < member_count(value); i++) {
        const char *name = member_name(value, i);
        struct property_key key = property_key(name, strlen(name));
        size_t index = find(store, &key);
        if (index == store->count || needs_append(store, index))
            return false;
        const css_declaration_contribution *entry = &store->entries[index];
        const css_declaration *source = entry->source;
        if (entry->component != i || source->name != value->name || source->source_value != value->source_value ||
            source->important != value->important || source->quirks_allowed != value->quirks_allowed ||
            source->deferred != value->deferred)
            return false;
        if (value->deferred && first != NULL && first != source)
            return false;
        first = source;
    }
    return true;
}

css_error css__declaration_store_set(css_declaration_store *store, const css_declaration *value, bool *changed)
{
    if (changed == NULL)
        return CSS_BADPARM;
    *changed = false;
    if (store == NULL || value == NULL)
        return CSS_BADPARM;
    size_t count = member_count(value);
    if (count == 0)
        return CSS_INVALID;
    if (unchanged(store, value))
        return CSS_OK;
    if (count > SIZE_MAX - store->count)
        return CSS_NOMEM;
    const css_declaration *copy;
    css_error error = value_copy(value, &copy);
    if (error != CSS_OK)
        return error;
    error = reserve(store, store->count + count);
    if (error != CSS_OK) {
        value_unref(copy);
        return error;
    }
    /* All fallible work is complete. The copy also makes it safe for the caller
     * to assign a source borrowed from this store itself. */
    for (size_t i = 0; i < count; i++) {
        const char *name = member_name(copy, i);
        struct property_key key = property_key(name, strlen(name));
        size_t index = find(store, &key);
        if (index != store->count) {
            bool append = needs_append(store, index);
            value_unref(store->entries[index].source);
            if (append) {
                memmove(&store->entries[index], &store->entries[index + 1],
                    (store->count - index - 1) * sizeof(*store->entries));
                index = --store->count;
            }
        }
        if (index == store->count)
            store->count++;
        store->entries[index] = (css_declaration_contribution){name, value_ref(copy), i};
    }
    value_unref(copy);
    *changed = true;
    return CSS_OK;
}

static bool in_property(const struct property_key *key, const char *name)
{
    if (key->property == NULL || !key->property->shorthand)
        return matches(key, name);
    for (size_t i = 0; i < key->property->longhand_count; i++) {
        if (strcmp(name, key->property->longhands[i]) == 0)
            return true;
    }
    return false;
}

css_error css__declaration_store_set_property(css_declaration_store *store, const css_stylesheet_params *params,
    const css_declaration_assignment *input, bool *changed)
{
    if (changed == NULL)
        return CSS_BADPARM;
    *changed = false;
    if (store == NULL || params == NULL || input == NULL || input->property == NULL || input->value == NULL ||
        input->priority == NULL)
        return CSS_BADPARM;
    struct property_key key = property_key(input->property, input->property_length);
    if ((key.custom && (key.length == 2 || memchr(key.name, 0, key.length) != NULL)) ||
        (!key.custom && key.property == NULL))
        return CSS_OK;
    if (input->value_length == 0)
        return css__declaration_store_remove(store, key.name, key.length, changed);
    bool important = input->priority_length != 0;
    struct property_key priority = {.name = input->priority, .length = input->priority_length};
    if (important && !matches(&priority, "important"))
        return CSS_OK;
    lwc_string *name;
    if (lwc_intern_string(key.name, key.length, &name) != lwc_error_ok)
        return CSS_NOMEM;
    css_declaration_list *list = NULL;
    css_error error = css__declaration_list_create_for_value(params, name, &list);
    lwc_string_unref(name);
    if (error == CSS_OK) {
        error = css__declaration_list_append(list, (const uint8_t *)input->value, input->value_length);
        if (error == CSS_OK || error == CSS_NEEDDATA)
            error = css__declaration_list_complete(list);
    }
    if (error == CSS_OK) {
        assert(css__declaration_list_count(list) == 1);
        css_declaration value = *css__declaration_list_get(list, 0);
        value.important = important;
        error = css__declaration_store_set(store, &value, changed);
    } else if (error == CSS_INVALID) {
        error = CSS_OK;
    }
    css__declaration_list_destroy(list);
    return error;
}

css_error css__declaration_store_remove(css_declaration_store *store, const char *name, size_t length, bool *changed)
{
    if (changed == NULL)
        return CSS_BADPARM;
    *changed = false;
    if (store == NULL || name == NULL)
        return CSS_BADPARM;
    struct property_key key = property_key(name, length);
    if (key.property == NULL || !key.property->shorthand) {
        size_t index = find(store, &key);
        if (index != store->count) {
            /* name itself may be borrowed from the value being removed. */
            value_unref(store->entries[index].source);
            memmove(&store->entries[index], &store->entries[index + 1],
                (store->count - index - 1) * sizeof(*store->entries));
            store->count--;
            *changed = true;
        }
        return CSS_OK;
    }
    size_t kept = 0;
    for (size_t i = 0; i < store->count; i++) {
        if (in_property(&key, store->entries[i].name)) {
            value_unref(store->entries[i].source);
            *changed = true;
        } else {
            store->entries[kept++] = store->entries[i];
        }
    }
    store->count = kept;
    return CSS_OK;
}

static size_t get_members(
    const css_declaration_store *store, const struct property_key *key, const css_declaration_contribution **members)
{
    if (key->property == NULL || !key->property->shorthand) {
        size_t index = find(store, key);
        members[0] = css__declaration_store_get(store, index);
        return 1;
    }
    size_t count = key->property->longhand_count;
    for (size_t i = 0; i < count; i++) {
        const char *name = key->property->longhands[i];
        struct property_key member = property_key(name, strlen(name));
        members[i] = css__declaration_store_get(store, find(store, &member));
    }
    return count;
}

css_error
css__declaration_store_value(const css_declaration_store *store, const char *name, size_t length, lwc_string **out)
{
    if (out == NULL)
        return CSS_BADPARM;
    *out = NULL;
    if (store == NULL || name == NULL)
        return CSS_BADPARM;
    struct property_key key = property_key(name, length);
    const css_declaration_contribution *members[CSS_DECL_MAX_LONGHANDS];
    size_t count = get_members(store, &key, members);
    if (key.property != NULL && key.property->shorthand)
        return css__declaration_shorthand_serialize(key.property, members, count, out);
    if (members[0] == NULL)
        return lwc_intern_string("", 0, out) == lwc_error_ok ? CSS_OK : CSS_NOMEM;
    return css__declaration_value_serialize(members[0], out);
}

css_error
css__declaration_store_priority(const css_declaration_store *store, const char *name, size_t length, bool *important)
{
    if (important == NULL)
        return CSS_BADPARM;
    *important = false;
    if (store == NULL || name == NULL)
        return CSS_BADPARM;
    struct property_key key = property_key(name, length);
    const css_declaration_contribution *members[CSS_DECL_MAX_LONGHANDS];
    size_t count = get_members(store, &key, members);
    for (size_t i = 0; i < count; i++) {
        if (members[i] == NULL || !members[i]->source->important)
            return CSS_OK;
    }
    *important = count != 0;
    return CSS_OK;
}

css_error css__declaration_store_serialize(const css_declaration_store *store, lwc_string **out)
{
    if (store == NULL) {
        if (out != NULL)
            *out = NULL;
        return CSS_BADPARM;
    }
    return css__declaration_block_serialize_values(store->entries, store->count, out);
}

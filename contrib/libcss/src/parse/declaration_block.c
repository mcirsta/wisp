/*
 * This file is part of LibCSS.
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 */

#include <stdlib.h>
#include <string.h>

#include "parse/declaration_block.h"
#include "parse/declaration_text.h"
#include "parse/declaration_value.h"
#include "parse/property.h"

struct block_entry {
    const css_declaration_contribution *value;
    enum css_logical_property_group group;
    bool logical, serialized;
};

struct block_serialization {
    struct block_entry *entries;
    struct block_entry **by_name;
    size_t count;
};

static int compare_names(const void *a, const void *b)
{
    const struct block_entry *const *left = a, *const *right = b;
    return strcmp((*left)->value->name, (*right)->value->name);
}

static int compare_key(const void *name, const void *entry)
{
    const struct block_entry *const *value = entry;
    return strcmp(name, (*value)->value->name);
}

static struct block_entry *find_entry(const struct block_serialization *block, const char *name)
{
    struct block_entry **found = bsearch(name, block->by_name, block->count, sizeof(block->by_name[0]), compare_key);
    return found != NULL ? *found : NULL;
}

/* A shorthand must not move a physical longhand across an intervening logical
 * one in the same group, or vice versa. Already emitted declarations still
 * participate: the rule concerns the original specified order. */
static bool logical_conflict(const struct block_serialization *block, struct block_entry *const *members, size_t count)
{
    size_t first = block->count, last = 0;
    for (size_t i = 0; i < count; i++) {
        size_t position = (size_t)(members[i] - block->entries);
        if (position < first)
            first = position;
        if (position > last)
            last = position;
    }
    for (size_t i = first + 1; i < last; i++) {
        const struct block_entry *entry = &block->entries[i];
        if (entry->group == CSS_LOGICAL_GROUP_NONE)
            continue;
        bool member = false;
        for (size_t j = 0; j < count; j++)
            member |= entry == members[j];
        if (member)
            continue;
        for (size_t j = 0; j < count; j++) {
            if (entry->group == members[j]->group && entry->logical != members[j]->logical)
                return true;
        }
    }
    return false;
}

static css_error
try_shorthand(struct block_serialization *block, struct block_entry *entry, css_declaration_text *output)
{
    size_t count;
    const struct css_prop_entry *const *shorthands = css_prop_shorthands(&count);
    for (size_t s = 0; s < count; s++) {
        const struct css_prop_entry *shorthand = shorthands[s];
        size_t needed = shorthand->longhand_count;
        bool contains = false;
        for (size_t i = 0; i < needed; i++)
            contains |= strcmp(shorthand->longhands[i], entry->value->name) == 0;
        if (!contains)
            continue;
        /* Both the bound and the catalogue come from properties.toml. */
        struct block_entry *members[CSS_DECL_MAX_LONGHANDS];
        const css_declaration_contribution *values[CSS_DECL_MAX_LONGHANDS];
        size_t i;
        for (i = 0; i < needed; i++) {
            members[i] = find_entry(block, shorthand->longhands[i]);
            if (members[i] == NULL || members[i]->serialized ||
                members[i]->value->source->important != entry->value->source->important)
                break;
            values[i] = members[i]->value;
        }
        if (i != needed || logical_conflict(block, members, needed))
            continue;
        lwc_string *value = NULL;
        css_error error = css__declaration_shorthand_serialize(shorthand, values, needed, &value);
        if (error != CSS_OK)
            return error;
        if (lwc_string_length(value) == 0) {
            lwc_string_unref(value);
            continue;
        }
        *output = (css_declaration_text){shorthand->name, value, entry->value->source->important};
        for (size_t j = 0; j < needed; j++)
            members[j]->serialized = true;
        return CSS_OK;
    }
    return CSS_OK; /* No shorthand: the caller serializes this longhand. */
}

css_error css__declaration_block_serialize(const css_declaration_order *order, lwc_string **out)
{
    if (out == NULL)
        return CSS_BADPARM;
    *out = NULL;
    if (order == NULL)
        return CSS_BADPARM;
    struct block_serialization block = {.count = css__declaration_order_count(order)};
    if (block.count == 0)
        return css__declaration_text_block(NULL, 0, out);
    if (block.count > SIZE_MAX / sizeof(*block.entries) || block.count > SIZE_MAX / sizeof(*block.by_name) ||
        block.count > SIZE_MAX / sizeof(css_declaration_text))
        return CSS_NOMEM;
    block.entries = calloc(block.count, sizeof(*block.entries));
    block.by_name = malloc(block.count * sizeof(*block.by_name));
    css_declaration_text *text = calloc(block.count, sizeof(*text));
    css_error error = CSS_NOMEM;
    size_t written = 0;
    if (block.entries == NULL || block.by_name == NULL || text == NULL)
        goto done;
    for (size_t i = 0; i < block.count; i++) {
        struct block_entry *entry = &block.entries[i];
        entry->value = css__declaration_order_get(order, i);
        const struct css_prop_entry *property = entry->value->source->property;
        if (property != NULL) {
            if (property->shorthand) {
                const struct css_prop_entry *longhand = css_prop_lookup(entry->value->name, strlen(entry->value->name));
                /* A few logical pairs have no direct longhand parser yet.
                 * Their group/mapping metadata belongs to the source pair. */
                if (longhand != NULL)
                    property = longhand;
            }
            entry->group = property->logical_group;
            entry->logical = property->logical;
        }
        block.by_name[i] = entry;
    }
    qsort(block.by_name, block.count, sizeof(*block.by_name), compare_names);
    for (size_t i = 0; i < block.count; i++) {
        struct block_entry *entry = &block.entries[i];
        if (entry->serialized)
            continue;
        if (entry->value->source->property != NULL) {
            error = try_shorthand(&block, entry, &text[written]);
            if (error != CSS_OK)
                goto done;
        }
        if (text[written].value == NULL) {
            text[written].name = entry->value->name;
            text[written].important = entry->value->source->important;
            error = css__declaration_value_serialize(entry->value, &text[written].value);
            if (error != CSS_OK)
                goto done;
            entry->serialized = true;
        }
        written++;
    }
    error = css__declaration_text_block(text, written, out);
done:
    for (size_t i = 0; i < written; i++)
        lwc_string_unref(text[i].value);
    free(text);
    free(block.by_name);
    free(block.entries);
    return error;
}

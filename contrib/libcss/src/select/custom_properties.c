/*
 * This file is part of LibCSS
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 * Copyright 2024 CSS Custom Properties Implementation
 */

#include <stdlib.h>
#include <string.h>

#include "custom_properties.h"

/**
 * Compute hash bucket index for a property name
 *
 * Uses the lwc_string hash value for O(1) lookup.
 */
static inline uint32_t custom_prop_hash(lwc_string *name)
{
    /* lwc_string has a cached hash value */
    return lwc_string_hash_value(name) & (CSS_CUSTOM_PROP_HASH_SIZE - 1);
}

css_error css__custom_property_map_create(css_custom_property_map **map)
{
    css_custom_property_map *m;

    if (map == NULL)
        return CSS_BADPARM;

    m = calloc(1, sizeof(*m));
    if (m == NULL)
        return CSS_NOMEM;

    *map = m;
    return CSS_OK;
}

void css__custom_property_map_destroy(css_custom_property_map *map)
{
    uint32_t i;

    if (map == NULL)
        return;

    /* Free all properties in all buckets */
    for (i = 0; i < CSS_CUSTOM_PROP_HASH_SIZE; i++) {
        css_custom_property *prop = map->buckets[i];
        while (prop != NULL) {
            css_custom_property *next = prop->next;

            lwc_string_unref(prop->name);
            lwc_string_unref(prop->value);
            free(prop);

            prop = next;
        }
    }

    free(map);
}

css_error css__custom_property_map_clone(const css_custom_property_map *orig, css_custom_property_map **clone_out)
{
    css_custom_property_map *clone;
    uint32_t i;

    if (clone_out == NULL)
        return CSS_BADPARM;

    if (orig == NULL) {
        *clone_out = NULL;
        return CSS_OK;
    }

    clone = calloc(1, sizeof(*clone));
    if (clone == NULL)
        return CSS_NOMEM;

    /* Copy all properties */
    for (i = 0; i < CSS_CUSTOM_PROP_HASH_SIZE; i++) {
        css_custom_property *prop = orig->buckets[i];
        css_custom_property **dest = &clone->buckets[i];

        while (prop != NULL) {
            css_custom_property *new_prop = malloc(sizeof(*new_prop));
            if (new_prop == NULL) {
                css__custom_property_map_destroy(clone);
                return CSS_NOMEM;
            }

            new_prop->name = lwc_string_ref(prop->name);
            new_prop->value = lwc_string_ref(prop->value);
            new_prop->next = NULL;

            *dest = new_prop;
            dest = &new_prop->next;

            prop = prop->next;
        }
    }

    clone->count = orig->count;
    *clone_out = clone;

    return CSS_OK;
}

css_error css__custom_property_set(css_custom_property_map *map, lwc_string *name, lwc_string *value)
{
    uint32_t bucket;
    css_custom_property *prop;
    bool match;

    if (map == NULL || name == NULL || value == NULL)
        return CSS_BADPARM;

    bucket = custom_prop_hash(name);

    /* Check if property already exists */
    for (prop = map->buckets[bucket]; prop != NULL; prop = prop->next) {
        if (lwc_string_isequal(prop->name, name, &match) == lwc_error_ok && match) {
            /* Replace existing value */
            lwc_string_unref(prop->value);
            prop->value = lwc_string_ref(value);
            return CSS_OK;
        }
    }

    /* Create new property */
    prop = malloc(sizeof(*prop));
    if (prop == NULL)
        return CSS_NOMEM;

    prop->name = lwc_string_ref(name);
    prop->value = lwc_string_ref(value);
    prop->next = map->buckets[bucket];

    map->buckets[bucket] = prop;
    map->count++;

    return CSS_OK;
}

lwc_string *css__custom_property_get(const css_custom_property_map *map, lwc_string *name)
{
    uint32_t bucket;
    css_custom_property *prop;
    bool match;

    if (map == NULL || name == NULL)
        return NULL;

    bucket = custom_prop_hash(name);

    for (prop = map->buckets[bucket]; prop != NULL; prop = prop->next) {
        if (lwc_string_isequal(prop->name, name, &match) == lwc_error_ok && match) {
            return prop->value;
        }
    }

    return NULL;
}

css_error css__custom_property_map_merge(css_custom_property_map *dest, const css_custom_property_map *source)
{
    uint32_t i;
    css_error error;

    if (dest == NULL)
        return CSS_BADPARM;

    if (source == NULL)
        return CSS_OK;

    /* Iterate through all properties in source and set them in dest */
    for (i = 0; i < CSS_CUSTOM_PROP_HASH_SIZE; i++) {
        css_custom_property *prop = source->buckets[i];

        while (prop != NULL) {
            error = css__custom_property_set(dest, prop->name, prop->value);
            if (error != CSS_OK)
                return error;

            prop = prop->next;
        }
    }

    return CSS_OK;
}

bool css__custom_property_map_equal(const css_custom_property_map *a, const css_custom_property_map *b)
{
    uint32_t i;

    /* Both NULL - equal */
    if (a == NULL && b == NULL) {
        return true;
    }

    /* One NULL, one not - not equal */
    if (a == NULL || b == NULL) {
        return false;
    }

    /* Different counts means different maps */
    if (a->count != b->count) {
        return false;
    }

    /* Compare all properties in 'a' against 'b' */
    for (i = 0; i < CSS_CUSTOM_PROP_HASH_SIZE; i++) {
        css_custom_property *prop_a = a->buckets[i];

        while (prop_a != NULL) {
            /* Look up this property in 'b' */
            lwc_string *value_b = css__custom_property_get(b, prop_a->name);

            if (value_b == NULL) {
                /* Property exists in 'a' but not 'b' */
                return false;
            }

            /* Compare values */
            bool match;
            if (lwc_string_isequal(prop_a->value, value_b, &match) != lwc_error_ok || !match) {
                return false;
            }

            prop_a = prop_a->next;
        }
    }

    /* Since counts are equal and all props in 'a' exist in 'b' with same values,
     * the maps are equal (no need to check 'b' against 'a') */
    return true;
}

/* ========== Deferred var() list functions ========== */

css_error css__deferred_var_list_create(css_deferred_var_list **list)
{
    css_deferred_var_list *l;

    if (list == NULL)
        return CSS_BADPARM;

    l = calloc(1, sizeof(*l));
    if (l == NULL)
        return CSS_NOMEM;

    *list = l;
    return CSS_OK;
}

void css__deferred_var_list_destroy(css_deferred_var_list *list)
{
    if (list == NULL)
        return;

    /* Free all entries and unref their token strings */
    for (uint32_t i = 0; i < list->count; i++) {
        css_deferred_var_entry *entry = &list->entries[i];
        for (uint32_t j = 0; j < entry->token_count; j++) {
            if (entry->tokens[j].idata != NULL) {
                lwc_string_unref(entry->tokens[j].idata);
            }
        }
        free(entry->tokens);
    }
    free(list->entries);
    free(list);
}

css_error css__deferred_var_list_add(
    css_deferred_var_list *list, uint32_t prop_idx, const css_deferred_token *tokens, uint32_t token_count)
{
    if (list == NULL || tokens == NULL || token_count == 0)
        return CSS_BADPARM;

    /* Grow array if needed */
    if (list->count >= list->alloc) {
        uint32_t new_alloc = list->alloc == 0 ? 4 : list->alloc * 2;
        css_deferred_var_entry *new_entries = realloc(list->entries, new_alloc * sizeof(css_deferred_var_entry));
        if (new_entries == NULL)
            return CSS_NOMEM;
        list->entries = new_entries;
        list->alloc = new_alloc;
    }

    /* Allocate and copy tokens, ref'ing strings */
    css_deferred_token *tok_copy = malloc(token_count * sizeof(css_deferred_token));
    if (tok_copy == NULL)
        return CSS_NOMEM;

    for (uint32_t i = 0; i < token_count; i++) {
        tok_copy[i].type = tokens[i].type;
        tok_copy[i].idata = tokens[i].idata;
        if (tok_copy[i].idata != NULL) {
            tok_copy[i].idata = lwc_string_ref(tok_copy[i].idata);
        }
    }

    /* Add entry */
    css_deferred_var_entry *entry = &list->entries[list->count++];
    entry->prop_idx = prop_idx;
    entry->tokens = tok_copy;
    entry->token_count = token_count;

    return CSS_OK;
}

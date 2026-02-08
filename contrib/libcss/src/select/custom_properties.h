/*
 * This file is part of LibCSS
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 * Copyright 2024 CSS Custom Properties Implementation
 */

#ifndef css_select_custom_properties_h_
#define css_select_custom_properties_h_

#include <libwapcaplet/libwapcaplet.h>
#include <libcss/errors.h>

/**
 * Hash table size for custom properties (power of 2)
 * 32 buckets is a good balance for typical usage
 */
#define CSS_CUSTOM_PROP_HASH_SIZE 32

/**
 * A single custom property (CSS variable)
 *
 * Custom properties are stored in a hash table with chaining.
 * The name includes the leading "--" prefix.
 */
typedef struct css_custom_property {
    lwc_string *name; /**< Property name, e.g., "--my-color" */
    lwc_string *value; /**< Raw value string, e.g., "#ff0000" */
    struct css_custom_property *next; /**< Next property in hash chain */
} css_custom_property;

/**
 * Hash table of custom properties
 *
 * Used to store custom properties on computed styles.
 * Provides O(1) average lookup by property name.
 */
typedef struct css_custom_property_map {
    css_custom_property *buckets[CSS_CUSTOM_PROP_HASH_SIZE];
    uint32_t count; /**< Total number of properties */
} css_custom_property_map;

/**
 * Create a new custom property map
 *
 * \param map  Pointer to receive created map
 * \return CSS_OK on success, CSS_NOMEM on memory exhaustion
 */
css_error css__custom_property_map_create(css_custom_property_map **map);

/**
 * Destroy a custom property map
 *
 * \param map  Map to destroy (may be NULL)
 */
void css__custom_property_map_destroy(css_custom_property_map *map);

/**
 * Clone a custom property map
 *
 * \param orig       Original map to clone (may be NULL)
 * \param clone_out  Pointer to receive cloned map
 * \return CSS_OK on success, CSS_NOMEM on memory exhaustion
 */
css_error css__custom_property_map_clone(const css_custom_property_map *orig, css_custom_property_map **clone_out);

/**
 * Merge source map into destination map
 *
 * Properties from source will override existing properties in dest with
 * the same name. This is used for custom property inheritance.
 *
 * \param dest    Destination map to merge into
 * \param source  Source map to merge from
 * \return CSS_OK on success, CSS_NOMEM on memory exhaustion
 */
css_error css__custom_property_map_merge(css_custom_property_map *dest, const css_custom_property_map *source);

/**
 * Set a custom property value
 *
 * If the property already exists, its value is replaced.
 *
 * \param map    Map to add property to
 * \param name   Property name (will be ref'd)
 * \param value  Property value (will be ref'd)
 * \return CSS_OK on success, CSS_NOMEM on memory exhaustion
 */
css_error css__custom_property_set(css_custom_property_map *map, lwc_string *name, lwc_string *value);

/**
 * Get a custom property value
 *
 * \param map   Map to query
 * \param name  Property name to look up
 * \return Property value string, or NULL if not found
 */
lwc_string *css__custom_property_get(const css_custom_property_map *map, lwc_string *name);

/**
 * Compare two custom property maps for equality
 *
 * Two maps are equal if they have the same properties with the same values.
 * NULL maps are considered equal to each other.
 *
 * \param a  First map (may be NULL)
 * \param b  Second map (may be NULL)
 * \return true if maps are equal, false otherwise
 */
bool css__custom_property_map_equal(const css_custom_property_map *a, const css_custom_property_map *b);

/**
 * Check if a property name is a custom property (starts with "--")
 *
 * \param name  Property name to check
 * \return true if name starts with "--", false otherwise
 */
static inline bool css__is_custom_property(lwc_string *name)
{
    const char *data = lwc_string_data(name);
    size_t len = lwc_string_length(name);

    return len >= 2 && data[0] == '-' && data[1] == '-';
}

/**
 * A resolved token for deferred var() processing
 */
typedef struct css_deferred_token {
    uint32_t type; /**< Token type (css_token_type) */
    lwc_string *idata; /**< Token string data (ref'd) */
} css_deferred_token;

/**
 * A single deferred var() entry
 *
 * Stores the property index and resolved tokens (with lwc_string refs).
 * Tokens are resolved during cascade so composition doesn't need stylesheet.
 */
typedef struct css_deferred_var_entry {
    uint32_t prop_idx; /**< Property index (opcode) */
    css_deferred_token *tokens; /**< Array of resolved tokens (owned) */
    uint32_t token_count; /**< Number of tokens */
} css_deferred_var_entry;

/**
 * List of deferred var() entries for composition-time resolution
 *
 * NULL if no var() properties need resolution.
 */
typedef struct css_deferred_var_list {
    css_deferred_var_entry *entries; /**< Array of entries */
    uint32_t count; /**< Number of entries */
    uint32_t alloc; /**< Allocated capacity */
} css_deferred_var_list;

/**
 * Create a deferred var list
 *
 * \param list  Pointer to receive created list
 * \return CSS_OK on success, CSS_NOMEM on memory exhaustion
 */
css_error css__deferred_var_list_create(css_deferred_var_list **list);

/**
 * Destroy a deferred var list
 *
 * \param list  List to destroy (may be NULL)
 */
void css__deferred_var_list_destroy(css_deferred_var_list *list);

/**
 * Add an entry to the deferred var list
 *
 * \param list         List to add to
 * \param prop_idx     Property index
 * \param tokens       Array of resolved tokens (will be copied, strings ref'd)
 * \param token_count  Number of tokens
 * \return CSS_OK on success, CSS_NOMEM on memory exhaustion
 */
css_error css__deferred_var_list_add(
    css_deferred_var_list *list, uint32_t prop_idx, const css_deferred_token *tokens, uint32_t token_count);

#endif /* css_select_custom_properties_h_ */

/*
 * This file is part of LibCSS.
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 */

#include <stdlib.h>
#include <string.h>

#include "parse/declaration_order.h"
#include "parse/property.h"

struct css_declaration_order {
    size_t count;
    css_declaration_contribution entries[];
};

/* First put each property's winner before its other contributions. All source
 * pointers belong to the same immutable declaration array, so their relative
 * order is the original source order. No quadratic custom-property name scan. */
static int compare_property(const void *a, const void *b)
{
    const css_declaration_contribution *left = a, *right = b;
    int name = strcmp(left->name, right->name);
    if (name != 0)
        return name;
    if (left->source->important != right->source->important)
        return left->source->important ? -1 : 1;
    return (left->source < right->source) - (left->source > right->source);
}

static int compare_order(const void *a, const void *b)
{
    const css_declaration_contribution *left = a, *right = b;
    if (left->source != right->source)
        return (left->source > right->source) - (left->source < right->source);
    return (left->component > right->component) - (left->component < right->component);
}

css_error css__declaration_order_create(const css_declaration_list *list, css_declaration_order **out)
{
    if (out == NULL)
        return CSS_BADPARM;
    *out = NULL;
    if (list == NULL)
        return CSS_BADPARM;
    if (css__declaration_list_sheet(list) == NULL)
        return CSS_INVALID;

    size_t declarations = css__declaration_list_count(list);
    size_t count = 0;
    for (size_t i = 0; i < declarations; i++) {
        const css_declaration *decl = css__declaration_list_get(list, i);
        size_t members = decl->property != NULL && decl->property->shorthand ? decl->property->longhand_count : 1;
        if (members == 0)
            return CSS_INVALID; /* Legacy generator input lacks declaration metadata. */
        if (members > SIZE_MAX - count)
            return CSS_NOMEM;
        count += members;
    }
    if (count > (SIZE_MAX - sizeof(css_declaration_order)) / sizeof(css_declaration_contribution))
        return CSS_NOMEM;
    css_declaration_order *order = malloc(sizeof(*order) + count * sizeof(order->entries[0]));
    if (order == NULL)
        return CSS_NOMEM;
    order->count = 0;
    for (size_t i = 0; i < declarations; i++) {
        const css_declaration *decl = css__declaration_list_get(list, i);
        const struct css_prop_entry *property = decl->property;
        size_t members = property != NULL && property->shorthand ? property->longhand_count : 1;
        for (size_t j = 0; j < members; j++) {
            order->entries[order->count++] = (css_declaration_contribution){
                .name = property != NULL && property->shorthand ? property->longhands[j] : lwc_string_data(decl->name),
                .source = decl,
                .component = j,
            };
        }
    }
    qsort(order->entries, count, sizeof(order->entries[0]), compare_property);
    order->count = 0;
    for (size_t i = 0; i < count; i++) {
        if (order->count == 0 || strcmp(order->entries[i].name, order->entries[order->count - 1].name) != 0)
            order->entries[order->count++] = order->entries[i];
    }
    qsort(order->entries, order->count, sizeof(order->entries[0]), compare_order);
    *out = order;
    return CSS_OK;
}

void css__declaration_order_destroy(css_declaration_order *order)
{
    free(order);
}

size_t css__declaration_order_count(const css_declaration_order *order)
{
    return order != NULL ? order->count : 0;
}

const css_declaration_contribution *css__declaration_order_get(const css_declaration_order *order, size_t index)
{
    return index < css__declaration_order_count(order) ? &order->entries[index] : NULL;
}

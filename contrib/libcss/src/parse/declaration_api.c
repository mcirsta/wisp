/* Public boundary for owned declarations. Licensed under the MIT License. */
#include <libcss/declarations.h>
#include "parse/declaration_store.h"

css_error
css_declarations_parse(const css_stylesheet_params *params, const uint8_t *text, size_t length, css_declarations **out)
{
    if (out == NULL)
        return CSS_BADPARM;
    *out = NULL;
    if (text == NULL && length != 0)
        return CSS_BADPARM;
    css_declaration_list *list;
    css_error error = css__declaration_list_create(params, &list);
    if (error != CSS_OK)
        return error;
    if (length != 0)
        error = css__declaration_list_append(list, text, length);
    if (error == CSS_OK || error == CSS_NEEDDATA)
        error = css__declaration_list_complete(list);
    if (error == CSS_OK)
        error = css__declaration_store_create(list, out);
    css__declaration_list_destroy(list);
    return error;
}

void css_declarations_destroy(css_declarations *d)
{
    css__declaration_store_destroy(d);
}
size_t css_declarations_count(const css_declarations *d)
{
    return css__declaration_store_count(d);
}
const char *css_declarations_item(const css_declarations *d, size_t index)
{
    const css_declaration_contribution *item = css__declaration_store_get(d, index);
    return item != NULL ? item->name : NULL;
}
css_error css_declarations_value(const css_declarations *d, const char *name, size_t length, lwc_string **out)
{
    return css__declaration_store_value(d, name, length, out);
}
css_error css_declarations_priority(const css_declarations *d, const char *name, size_t length, bool *important)
{
    return css__declaration_store_priority(d, name, length, important);
}
css_error css_declarations_serialize(const css_declarations *d, lwc_string **out)
{
    return css__declaration_store_serialize(d, out);
}
css_error css_declarations_set(css_declarations *d, const css_stylesheet_params *params, const char *name,
    size_t name_length, const char *value, size_t value_length, const char *priority, size_t priority_length,
    bool *changed)
{
    css_declaration_assignment input = {name, value, priority, name_length, value_length, priority_length};
    return css__declaration_store_set_property(d, params, &input, changed);
}
css_error css_declarations_remove(css_declarations *d, const char *name, size_t length, bool *changed)
{
    return css__declaration_store_remove(d, name, length, changed);
}
css_error css_declarations_compile(const css_declarations *d, const css_stylesheet_params *params, css_stylesheet **out)
{
    return css__declaration_store_compile(d, params, out);
}

const char *const *css_declarations_supported_properties(size_t *count)
{
    static const char *const names[] = {
#include "declaration_names.inc"
    };
    *count = sizeof(names) / sizeof(*names);
    return names;
}

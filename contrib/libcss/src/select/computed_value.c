/* CSSOM serialization of computed values. Licensed under the MIT License. */
#include <libcss/computed.h>
#include <string.h>
#include "parse/property.h"
#include "parse/declaration_value.h"

static css_error keyword(const char *text, lwc_string **out)
{
    return lwc_intern_string(text, strlen(text), out) == lwc_error_ok ? CSS_OK : CSS_NOMEM;
}

static css_error overflow(uint8_t value, lwc_string **out)
{
    switch (value) {
    case CSS_OVERFLOW_VISIBLE: return keyword("visible", out);
    case CSS_OVERFLOW_HIDDEN: return keyword("hidden", out);
    case CSS_OVERFLOW_SCROLL: return keyword("scroll", out);
    case CSS_OVERFLOW_AUTO: return keyword("auto", out);
    default: return CSS_INVALID;
    }
}

css_error css_computed_property_value(
    const css_computed_style *style, const char *name, size_t length, lwc_string **out)
{
    if (style == NULL || name == NULL || out == NULL)
        return CSS_BADPARM;
    *out = NULL;
    if (length >= 2 && name[0] == '-' && name[1] == '-')
        return CSS_INVALID; /* Custom computed values require their variable environment. */
    const struct css_prop_entry *property = css_prop_lookup(name, length);
    if (property == NULL)
        return keyword("", out);
    switch (property->id) {
    case CSS_DECL_OPACITY: {
        css_fixed opacity;
        if (css_computed_opacity(style, &opacity) != CSS_OPACITY_SET)
            return CSS_INVALID;
        return css__declaration_fixed_serialize(opacity, out);
    }
    case CSS_DECL_VISIBILITY:
        switch (css_computed_visibility(style)) {
        case CSS_VISIBILITY_VISIBLE: return keyword("visible", out);
        case CSS_VISIBILITY_HIDDEN: return keyword("hidden", out);
        case CSS_VISIBILITY_COLLAPSE: return keyword("collapse", out);
        default: return CSS_INVALID;
        }
    case CSS_DECL_OVERFLOW_X: return overflow(css_computed_overflow_x(style), out);
    case CSS_DECL_OVERFLOW_Y: return overflow(css_computed_overflow_y(style), out);
    case CSS_DECL_OVERFLOW: {
        uint8_t x = css_computed_overflow_x(style), y = css_computed_overflow_y(style);
        if (x == y)
            return overflow(x, out);
        lwc_string *values[2] = {NULL, NULL};
        css_error error = overflow(x, &values[0]);
        if (error == CSS_OK)
            error = overflow(y, &values[1]);
        if (error == CSS_OK)
            error = css__declaration_values_join(values, 2, " ", out);
        for (unsigned i = 0; i < 2; i++) {
            if (values[i] != NULL)
                lwc_string_unref(values[i]);
        }
        return error;
    }
    default: return CSS_INVALID;
    }
}

/* CSSOM serialization of computed values. Licensed under the MIT License. */
#include <libcss/computed.h>
#include <string.h>
#include "parse/property.h"
#include "parse/declaration_value.h"
#include "select/computed.h"
#include "select/calc.h"

static css_error keyword(const char *text, lwc_string **out)
{
    return lwc_intern_string(text, strlen(text), out) == lwc_error_ok ? CSS_OK : CSS_NOMEM;
}

static css_error overflow(uint8_t value, lwc_string **out)
{
    switch (value) {
    case CSS_OVERFLOW_VISIBLE:
        return keyword("visible", out);
    case CSS_OVERFLOW_HIDDEN:
        return keyword("hidden", out);
    case CSS_OVERFLOW_SCROLL:
        return keyword("scroll", out);
    case CSS_OVERFLOW_AUTO:
        return keyword("auto", out);
    default:
        return CSS_INVALID;
    }
}

static css_error dimension(css_fixed value, const char *suffix, lwc_string **out)
{
    lwc_string *parts[2] = {NULL, NULL};
    css_error error = css__declaration_fixed_serialize(value, &parts[0]);
    if (error == CSS_OK)
        error = keyword(suffix, &parts[1]);
    if (error == CSS_OK)
        error = css__declaration_values_join(parts, 2, "", out);
    for (unsigned i = 0; i < 2; i++)
        if (parts[i] != NULL)
            lwc_string_unref(parts[i]);
    return error;
}

typedef uint8_t (*length_getter)(const css_computed_style *, css_fixed_or_calc *, css_unit *);

static css_error length_value(const css_computed_style *style, const struct css_computed_value_ctx *ctx,
    const char *name, length_getter getter, uint8_t set, bool used, lwc_string **out)
{
    css_fixed_or_calc value = {.value = 0};
    css_unit unit = CSS_UNIT_PX;
    if (used && ctx != NULL && ctx->used_length != NULL) {
        css_error error = ctx->used_length(ctx->pw, name, &value.value);
        if (error == CSS_OK)
            return dimension(value.value, "px", out);
        if (error != CSS_PROPERTY_NOT_SET)
            return error;
    }
    uint8_t type = getter(style, &value, &unit);
    if (type != set)
        return type == CSS_WIDTH_AUTO ? keyword("auto", out) : CSS_INVALID;
    if (unit == CSS_UNIT_CALC) {
        if (ctx == NULL || ctx->units == NULL)
            return CSS_INVALID;
        css_error error = css_calculator_calculate(style->calc, ctx->units, -1, value.calc, style, &unit, &value.value);
        if (error != CSS_OK)
            return error;
        /* The calculator returns CSS pixels. Percentage-bearing expressions
         * without a used value need computed math serialization, not a guess. */
        if (unit != CSS_UNIT_PX)
            return CSS_INVALID;
    }
    if (unit == CSS_UNIT_PCT)
        return dimension(value.value, "%", out);
    if (unit == CSS_UNIT_MIN_CONTENT)
        return keyword("min-content", out);
    if (unit == CSS_UNIT_MAX_CONTENT)
        return keyword("max-content", out);
    if (unit > CSS_UNIT_CQB)
        return CSS_INVALID;
    if (unit != CSS_UNIT_PX) {
        if (ctx == NULL || ctx->units == NULL)
            return CSS_INVALID;
        value.value = css_unit_len2css_px(style, ctx->units, value.value, unit);
    }
    return dimension(value.value, "px", out);
}

static css_error four_sides(const css_computed_style *style, const struct css_computed_value_ctx *ctx,
    const struct css_prop_entry *property, lwc_string **out)
{
    lwc_string *values[4] = {NULL, NULL, NULL, NULL};
    css_error error = CSS_OK;
    for (unsigned i = 0; i < 4 && error == CSS_OK; i++)
        error = css_computed_property_value(
            style, ctx, property->longhands[i], strlen(property->longhands[i]), &values[i]);
    if (error == CSS_OK) {
        unsigned count = 4;
        if (values[1] == values[3]) {
            count = 3;
            if (values[0] == values[2]) {
                count = 2;
                if (values[0] == values[1])
                    count = 1;
            }
        }
        error = css__declaration_values_join(values, count, " ", out);
    }
    for (unsigned i = 0; i < 4; i++)
        if (values[i] != NULL)
            lwc_string_unref(values[i]);
    return error;
}

css_error css_computed_property_value(const css_computed_style *style, const struct css_computed_value_ctx *ctx,
    const char *name, size_t length, lwc_string **out)
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
    case CSS_DECL_DISPLAY: {
        static const char *const names[] = {NULL, "inline", "block", "list-item", "run-in", "inline-block", "table",
            "inline-table", "table-row-group", "table-header-group", "table-footer-group", "table-row",
            "table-column-group", "table-column", "table-cell", "table-caption", "none", "flex", "inline-flex", "grid",
            "inline-grid", "flow-root"};
        uint8_t value = css_computed_display(style, ctx != NULL && ctx->blockify);
        return value > 0 && value < sizeof(names) / sizeof(names[0]) ? keyword(names[value], out) : CSS_INVALID;
    }
    case CSS_DECL_POSITION:
        switch (css_computed_position(style)) {
        case CSS_POSITION_STATIC:
            return keyword("static", out);
        case CSS_POSITION_RELATIVE:
            return keyword("relative", out);
        case CSS_POSITION_ABSOLUTE:
            return keyword("absolute", out);
        case CSS_POSITION_FIXED:
            return keyword("fixed", out);
        case CSS_POSITION_STICKY:
            return keyword("sticky", out);
        default:
            return CSS_INVALID;
        }
    case CSS_DECL_BOX_SIZING:
        return keyword(css_computed_box_sizing(style) == CSS_BOX_SIZING_BORDER_BOX ? "border-box" : "content-box", out);
    case CSS_DECL_DIRECTION:
        return keyword(css_computed_direction(style) == CSS_DIRECTION_RTL ? "rtl" : "ltr", out);
    case CSS_DECL_FLOAT:
        switch (css_computed_float(style)) {
        case CSS_FLOAT_LEFT:
            return keyword("left", out);
        case CSS_FLOAT_RIGHT:
            return keyword("right", out);
        case CSS_FLOAT_NONE:
            return keyword("none", out);
        default:
            return CSS_INVALID;
        }
    case CSS_DECL_TOP:
        return length_value(style, ctx, property->name, css_computed_top, CSS_TOP_SET, true, out);
    case CSS_DECL_RIGHT:
        return length_value(style, ctx, property->name, css_computed_right, CSS_RIGHT_SET, true, out);
    case CSS_DECL_BOTTOM:
        return length_value(style, ctx, property->name, css_computed_bottom, CSS_BOTTOM_SET, true, out);
    case CSS_DECL_LEFT:
        return length_value(style, ctx, property->name, css_computed_left, CSS_LEFT_SET, true, out);
    case CSS_DECL_WIDTH:
        return length_value(style, ctx, property->name, css_computed_width, CSS_WIDTH_SET, true, out);
    case CSS_DECL_HEIGHT:
        return length_value(style, ctx, property->name, css_computed_height, CSS_HEIGHT_SET, true, out);
    case CSS_DECL_MARGIN:
    case CSS_DECL_PADDING:
    case CSS_DECL_BORDER_WIDTH:
    case CSS_DECL_BORDER_STYLE:
        return four_sides(style, ctx, property, out);
    case CSS_DECL_MARGIN_TOP:
        return length_value(style, ctx, property->name, css_computed_margin_top, CSS_MARGIN_SET, true, out);
    case CSS_DECL_PADDING_TOP:
        return length_value(style, ctx, property->name, css_computed_padding_top, CSS_PADDING_SET, true, out);
    case CSS_DECL_BORDER_TOP_WIDTH:
        if (css_computed_border_top_style(style) <= CSS_BORDER_STYLE_HIDDEN)
            return keyword("0px", out);
        return length_value(
            style, ctx, property->name, css_computed_border_top_width, CSS_BORDER_WIDTH_WIDTH, false, out);
    case CSS_DECL_MARGIN_RIGHT:
        return length_value(style, ctx, property->name, css_computed_margin_right, CSS_MARGIN_SET, true, out);
    case CSS_DECL_PADDING_RIGHT:
        return length_value(style, ctx, property->name, css_computed_padding_right, CSS_PADDING_SET, true, out);
    case CSS_DECL_BORDER_RIGHT_WIDTH:
        if (css_computed_border_right_style(style) <= CSS_BORDER_STYLE_HIDDEN)
            return keyword("0px", out);
        return length_value(
            style, ctx, property->name, css_computed_border_right_width, CSS_BORDER_WIDTH_WIDTH, false, out);
    case CSS_DECL_MARGIN_BOTTOM:
        return length_value(style, ctx, property->name, css_computed_margin_bottom, CSS_MARGIN_SET, true, out);
    case CSS_DECL_PADDING_BOTTOM:
        return length_value(style, ctx, property->name, css_computed_padding_bottom, CSS_PADDING_SET, true, out);
    case CSS_DECL_BORDER_BOTTOM_WIDTH:
        if (css_computed_border_bottom_style(style) <= CSS_BORDER_STYLE_HIDDEN)
            return keyword("0px", out);
        return length_value(
            style, ctx, property->name, css_computed_border_bottom_width, CSS_BORDER_WIDTH_WIDTH, false, out);
    case CSS_DECL_MARGIN_LEFT:
        return length_value(style, ctx, property->name, css_computed_margin_left, CSS_MARGIN_SET, true, out);
    case CSS_DECL_PADDING_LEFT:
        return length_value(style, ctx, property->name, css_computed_padding_left, CSS_PADDING_SET, true, out);
    case CSS_DECL_BORDER_LEFT_WIDTH:
        if (css_computed_border_left_style(style) <= CSS_BORDER_STYLE_HIDDEN)
            return keyword("0px", out);
        return length_value(
            style, ctx, property->name, css_computed_border_left_width, CSS_BORDER_WIDTH_WIDTH, false, out);
    case CSS_DECL_BORDER_TOP_STYLE:
    case CSS_DECL_BORDER_RIGHT_STYLE:
    case CSS_DECL_BORDER_BOTTOM_STYLE:
    case CSS_DECL_BORDER_LEFT_STYLE: {
        static const char *const names[] = {
            NULL, "none", "hidden", "dotted", "dashed", "solid", "double", "groove", "ridge", "inset", "outset"};
        uint8_t value = property->id == CSS_DECL_BORDER_TOP_STYLE ? css_computed_border_top_style(style)
            : property->id == CSS_DECL_BORDER_RIGHT_STYLE         ? css_computed_border_right_style(style)
            : property->id == CSS_DECL_BORDER_BOTTOM_STYLE        ? css_computed_border_bottom_style(style)
                                                                  : css_computed_border_left_style(style);
        return value > 0 && value < sizeof(names) / sizeof(names[0]) ? keyword(names[value], out) : CSS_INVALID;
    }
    case CSS_DECL_OPACITY: {
        css_fixed opacity;
        if (css_computed_opacity(style, &opacity) != CSS_OPACITY_SET)
            return CSS_INVALID;
        return css__declaration_fixed_serialize(opacity, out);
    }
    case CSS_DECL_VISIBILITY:
        switch (css_computed_visibility(style)) {
        case CSS_VISIBILITY_VISIBLE:
            return keyword("visible", out);
        case CSS_VISIBILITY_HIDDEN:
            return keyword("hidden", out);
        case CSS_VISIBILITY_COLLAPSE:
            return keyword("collapse", out);
        default:
            return CSS_INVALID;
        }
    case CSS_DECL_OVERFLOW_X:
        return overflow(css_computed_overflow_x(style), out);
    case CSS_DECL_OVERFLOW_Y:
        return overflow(css_computed_overflow_y(style), out);
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
    default:
        return CSS_INVALID;
    }
}

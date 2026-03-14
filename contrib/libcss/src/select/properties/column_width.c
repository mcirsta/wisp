/*
 * This file is part of LibCSS
 * Licensed under the MIT License,
 *		  http://www.opensource.org/licenses/mit-license.php
 * Copyright 2012 Michael Drake <tlsa@netsurf-browser.org>
 */

#include "utils/css_utils.h"
#include "bytecode/bytecode.h"
#include "bytecode/opcodes.h"
#include "select/propget.h"
#include "select/propset.h"

#include "select/properties/helpers.h"
#include "select/properties/properties.h"

css_error css__cascade_column_width(uint32_t opv, css_style *style, css_select_state *state)
{
    return css__cascade_length_normal_calc(opv, style, state, set_column_width);
}

css_error css__set_column_width_from_hint(const css_hint *hint, css_computed_style *style)
{
    css_fixed_or_calc length = {.value = hint->data.length.value};
    return set_column_width(style, hint->status, length, hint->data.length.unit);
}

css_error css__initial_column_width(css_select_state *state)
{
    return set_column_width(state->computed, CSS_COLUMN_WIDTH_AUTO, (css_fixed_or_calc)INTTOFIX(1), CSS_UNIT_EM);
}

css_error css__copy_column_width(const css_computed_style *from, css_computed_style *to)
{
    css_fixed_or_calc length = (css_fixed_or_calc)0;
    css_unit unit = CSS_UNIT_PX;
    uint8_t type = get_column_width(from, &length, &unit);

    if (from == to) {
        return CSS_OK;
    }

    return set_column_width(to, type, length, unit);
}

css_error
css__compose_column_width(const css_computed_style *parent, const css_computed_style *child, css_computed_style *result)
{
    css_fixed_or_calc length = (css_fixed_or_calc)0;
    css_unit unit = CSS_UNIT_PX;
    uint8_t type = get_column_width(child, &length, &unit);

    return css__copy_column_width(type == CSS_COLUMN_WIDTH_INHERIT ? parent : child, result);
}

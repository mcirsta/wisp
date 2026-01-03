/*
 * This file is part of LibCSS
 * Licensed under the MIT License,
 *		  http://www.opensource.org/licenses/mit-license.php
 * Copyright 2017 Lucas Neves <lcneves@gmail.com>
 */

#include "utils/utils.h"
#include "bytecode/bytecode.h"
#include "bytecode/opcodes.h"
#include "select/propget.h"
#include "select/propset.h"

#include "select/properties/helpers.h"
#include "select/properties/properties.h"

css_error css__cascade_flex_basis(uint32_t opv, css_style *style, css_select_state *state)
{
    uint16_t value = CSS_FLEX_BASIS_INHERIT;
    css_fixed_or_calc length = (css_fixed_or_calc)0;
    uint32_t unit = CSS_UNIT_PX;
    uint32_t snum = 0;

    if (hasFlagValue(opv) == false) {
        switch (getValue(opv)) {
        case FLEX_BASIS_AUTO:
            value = CSS_FLEX_BASIS_AUTO;
            break;
        case FLEX_BASIS_CONTENT:
            value = CSS_FLEX_BASIS_CONTENT;
            break;
        case FLEX_BASIS_SET:
            value = CSS_FLEX_BASIS_SET;
            length.value = *((css_fixed *)style->bytecode);
            advance_bytecode(style, sizeof(length.value));
            unit = css__to_css_unit(*((uint32_t *)style->bytecode));
            advance_bytecode(style, sizeof(unit));
            break;
        case FLEX_BASIS_CALC:
            value = CSS_FLEX_BASIS_SET;
            advance_bytecode(style, sizeof(unit)); // Skip unit
            snum = *((uint32_t *)style->bytecode);
            advance_bytecode(style, sizeof(snum));
            unit = CSS_UNIT_CALC;
            css__stylesheet_string_get(style->sheet, snum, &length.calc);
            break;
        default:
            assert(0 && "Invalid value");
            break;
        }
    }

    if (css__outranks_existing(getOpcode(opv), isImportant(opv), state, getFlagValue(opv))) {
        return set_flex_basis(state->computed, value, length, unit);
    }

    return CSS_OK;
}

css_error css__set_flex_basis_from_hint(const css_hint *hint, css_computed_style *style)
{
    return set_flex_basis(style, hint->status, (css_fixed_or_calc)(hint->data.length.value), hint->data.length.unit);
}

css_error css__initial_flex_basis(css_select_state *state)
{
    return set_flex_basis(state->computed, CSS_FLEX_BASIS_AUTO, (css_fixed_or_calc)0, CSS_UNIT_PX);
}

css_error css__copy_flex_basis(const css_computed_style *from, css_computed_style *to)
{
    css_fixed_or_calc length = (css_fixed_or_calc)0;
    css_unit unit = CSS_UNIT_PX;
    uint8_t type = get_flex_basis(from, &length, &unit);

    if (from == to) {
        return CSS_OK;
    }

    return set_flex_basis(to, type, length, unit);
}

css_error
css__compose_flex_basis(const css_computed_style *parent, const css_computed_style *child, css_computed_style *result)
{
    css_fixed_or_calc length = (css_fixed_or_calc)0;
    css_unit unit = CSS_UNIT_PX;
    uint8_t type = get_flex_basis(child, &length, &unit);

    return css__copy_flex_basis(type == CSS_FLEX_BASIS_INHERIT ? parent : child, result);
}

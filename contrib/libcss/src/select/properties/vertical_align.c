/*
 * This file is part of LibCSS
 * Licensed under the MIT License,
 *		  http://www.opensource.org/licenses/mit-license.php
 * Copyright 2009 John-Mark Bell <jmb@netsurf-browser.org>
 */

#include "utils/css_utils.h"
#include "bytecode/bytecode.h"
#include "bytecode/opcodes.h"
#include "select/propget.h"
#include "select/propset.h"

#include "select/properties/helpers.h"
#include "select/properties/properties.h"

css_error css__cascade_vertical_align(uint32_t opv, css_style *style, css_select_state *state)
{
    uint16_t value = CSS_VERTICAL_ALIGN_INHERIT;
    css_fixed_or_calc length = (css_fixed_or_calc)0;
    uint32_t unit = UNIT_PX;
    uint32_t snum = 0;

    if (hasFlagValue(opv) == false) {
        switch (getValue(opv)) {
        case VERTICAL_ALIGN_SET:
            value = CSS_VERTICAL_ALIGN_SET;

            length.value = *((css_fixed *)style->bytecode);
            advance_bytecode(style, sizeof(length.value));
            unit = *((uint32_t *)style->bytecode);
            advance_bytecode(style, sizeof(unit));
            break;
        case VERTICAL_ALIGN_BASELINE:
            value = CSS_VERTICAL_ALIGN_BASELINE;
            break;
        case VERTICAL_ALIGN_SUB:
            value = CSS_VERTICAL_ALIGN_SUB;
            break;
        case VERTICAL_ALIGN_SUPER:
            value = CSS_VERTICAL_ALIGN_SUPER;
            break;
        case VERTICAL_ALIGN_TOP:
            value = CSS_VERTICAL_ALIGN_TOP;
            break;
        case VERTICAL_ALIGN_TEXT_TOP:
            value = CSS_VERTICAL_ALIGN_TEXT_TOP;
            break;
        case VERTICAL_ALIGN_MIDDLE:
            value = CSS_VERTICAL_ALIGN_MIDDLE;
            break;
        case VERTICAL_ALIGN_BOTTOM:
            value = CSS_VERTICAL_ALIGN_BOTTOM;
            break;
        case VERTICAL_ALIGN_TEXT_BOTTOM:
            value = CSS_VERTICAL_ALIGN_TEXT_BOTTOM;
            break;
        case VERTICAL_ALIGN_CALC:
            value = CSS_VERTICAL_ALIGN_SET;
            advance_bytecode(style, sizeof(unit)); /* skip unit */
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

    unit = css__to_css_unit(unit);

    if (css__outranks_existing(getOpcode(opv), isImportant(opv), state, getFlagValue(opv))) {
        return set_vertical_align(state->computed, value, length, unit);
    }

    return CSS_OK;
}

css_error css__set_vertical_align_from_hint(const css_hint *hint, css_computed_style *style)
{
    css_fixed_or_calc value = {.value = hint->data.length.value};
    return set_vertical_align(style, hint->status, value, hint->data.length.unit);
}

css_error css__initial_vertical_align(css_select_state *state)
{
    return set_vertical_align(state->computed, CSS_VERTICAL_ALIGN_BASELINE, (css_fixed_or_calc)0, CSS_UNIT_PX);
}

css_error css__copy_vertical_align(const css_computed_style *from, css_computed_style *to)
{
    css_fixed_or_calc length = (css_fixed_or_calc)0;
    css_unit unit = CSS_UNIT_PX;
    uint8_t type = get_vertical_align(from, &length, &unit);

    if (from == to) {
        return CSS_OK;
    }

    return set_vertical_align(to, type, length, unit);
}

css_error css__compose_vertical_align(
    const css_computed_style *parent, const css_computed_style *child, css_computed_style *result)
{
    css_fixed_or_calc length = (css_fixed_or_calc)0;
    css_unit unit = CSS_UNIT_PX;
    uint8_t type = get_vertical_align(child, &length, &unit);

    return css__copy_vertical_align(type == CSS_VERTICAL_ALIGN_INHERIT ? parent : child, result);
}

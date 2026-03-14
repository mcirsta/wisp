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

css_error css__cascade_line_height(uint32_t opv, css_style *style, css_select_state *state)
{
    uint16_t value = CSS_LINE_HEIGHT_INHERIT;
    css_fixed_or_calc val = (css_fixed_or_calc)0;
    uint32_t unit = UNIT_PX;
    uint32_t snum = 0;

    if (hasFlagValue(opv) == false) {
        switch (getValue(opv)) {
        case LINE_HEIGHT_NUMBER:
            value = CSS_LINE_HEIGHT_NUMBER;
            val.value = *((css_fixed *)style->bytecode);
            advance_bytecode(style, sizeof(val.value));
            break;
        case LINE_HEIGHT_DIMENSION:
            value = CSS_LINE_HEIGHT_DIMENSION;
            val.value = *((css_fixed *)style->bytecode);
            advance_bytecode(style, sizeof(val.value));
            unit = *((uint32_t *)style->bytecode);
            advance_bytecode(style, sizeof(unit));
            break;
        case LINE_HEIGHT_NORMAL:
            value = CSS_LINE_HEIGHT_NORMAL;
            break;
        case LINE_HEIGHT_CALC:
            value = CSS_LINE_HEIGHT_DIMENSION;
            advance_bytecode(style, sizeof(unit)); /* skip unit */
            snum = *((uint32_t *)style->bytecode);
            advance_bytecode(style, sizeof(snum));
            unit = CSS_UNIT_CALC;
            css__stylesheet_string_get(style->sheet, snum, &val.calc);
            break;
        default:
            assert(0 && "Invalid value");
            break;
        }
    }

    unit = css__to_css_unit(unit);

    if (css__outranks_existing(getOpcode(opv), isImportant(opv), state, getFlagValue(opv))) {
        return set_line_height(state->computed, value, val, unit);
    }

    return CSS_OK;
}

css_error css__set_line_height_from_hint(const css_hint *hint, css_computed_style *style)
{
    css_fixed_or_calc length = {.value = hint->data.length.value};
    return set_line_height(style, hint->status, length, hint->data.length.unit);
}

css_error css__initial_line_height(css_select_state *state)
{
    return set_line_height(state->computed, CSS_LINE_HEIGHT_NORMAL, (css_fixed_or_calc)0, CSS_UNIT_PX);
}

css_error css__copy_line_height(const css_computed_style *from, css_computed_style *to)
{
    css_fixed_or_calc length = (css_fixed_or_calc)0;
    css_unit unit = CSS_UNIT_PX;
    uint8_t type = get_line_height(from, &length, &unit);

    if (from == to) {
        return CSS_OK;
    }

    return set_line_height(to, type, length, unit);
}

css_error
css__compose_line_height(const css_computed_style *parent, const css_computed_style *child, css_computed_style *result)
{
    css_fixed_or_calc length = (css_fixed_or_calc)0;
    css_unit unit = CSS_UNIT_PX;
    uint8_t type = get_line_height(child, &length, &unit);

    return css__copy_line_height(type == CSS_LINE_HEIGHT_INHERIT ? parent : child, result);
}

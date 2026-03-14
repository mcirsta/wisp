/*
 * This file is part of LibCSS
 * Licensed under the MIT License,
 *		  http://www.opensource.org/licenses/mit-license.php
 * Copyright 2024 Marius
 */

#include "utils/css_utils.h"
#include "bytecode/bytecode.h"
#include "bytecode/opcodes.h"
#include "select/propget.h"
#include "select/propset.h"

#include "select/properties/helpers.h"
#include "select/properties/properties.h"

css_error css__cascade_object_position(uint32_t opv, css_style *style, css_select_state *state)
{
    uint16_t value = CSS_OBJECT_POSITION_INHERIT;
    css_fixed_or_calc hlength = (css_fixed_or_calc)0;
    css_fixed_or_calc vlength = (css_fixed_or_calc)0;
    uint32_t hunit = UNIT_PX;
    uint32_t vunit = UNIT_PX;

    if (hasFlagValue(opv) == false) {
        value = CSS_OBJECT_POSITION_SET;

        switch (getValue(opv) & 0xf0) {
        case OBJECT_POSITION_HORZ_SET:
            hlength.value = *((css_fixed *)style->bytecode);
            advance_bytecode(style, sizeof(hlength.value));
            hunit = *((uint32_t *)style->bytecode);
            advance_bytecode(style, sizeof(hunit));
            break;
        case OBJECT_POSITION_HORZ_CENTER:
            hlength.value = INTTOFIX(50);
            hunit = UNIT_PCT;
            break;
        case OBJECT_POSITION_HORZ_RIGHT:
            hlength.value = INTTOFIX(100);
            hunit = UNIT_PCT;
            break;
        case OBJECT_POSITION_HORZ_LEFT:
            hlength.value = INTTOFIX(0);
            hunit = UNIT_PCT;
            break;
        }

        switch (getValue(opv) & 0x0f) {
        case OBJECT_POSITION_VERT_SET:
            vlength.value = *((css_fixed *)style->bytecode);
            advance_bytecode(style, sizeof(vlength.value));
            vunit = *((uint32_t *)style->bytecode);
            advance_bytecode(style, sizeof(vunit));
            break;
        case OBJECT_POSITION_VERT_CENTER:
            vlength.value = INTTOFIX(50);
            vunit = UNIT_PCT;
            break;
        case OBJECT_POSITION_VERT_BOTTOM:
            vlength.value = INTTOFIX(100);
            vunit = UNIT_PCT;
            break;
        case OBJECT_POSITION_VERT_TOP:
            vlength.value = INTTOFIX(0);
            vunit = UNIT_PCT;
            break;
        }
    }

    hunit = css__to_css_unit(hunit);
    vunit = css__to_css_unit(vunit);

    if (css__outranks_existing(getOpcode(opv), isImportant(opv), state, getFlagValue(opv))) {
        return set_object_position(state->computed, value, hlength, hunit, vlength, vunit);
    }

    return CSS_OK;
}

css_error css__set_object_position_from_hint(const css_hint *hint, css_computed_style *style)
{
    css_fixed_or_calc hlength = {.value = hint->data.position.h.value};
    css_fixed_or_calc vlength = {.value = hint->data.position.v.value};
    return set_object_position(
        style, hint->status, hlength, hint->data.position.h.unit, vlength, hint->data.position.v.unit);
}

css_error css__initial_object_position(css_select_state *state)
{
    /* Default is 50% 50% (center center) */
    return set_object_position(
        state->computed, CSS_OBJECT_POSITION_SET, (css_fixed_or_calc)INTTOFIX(50), CSS_UNIT_PCT,
        (css_fixed_or_calc)INTTOFIX(50), CSS_UNIT_PCT);
}

css_error css__copy_object_position(const css_computed_style *from, css_computed_style *to)
{
    css_fixed_or_calc hlength = (css_fixed_or_calc)0, vlength = (css_fixed_or_calc)0;
    css_unit hunit = CSS_UNIT_PX, vunit = CSS_UNIT_PX;
    uint8_t type = get_object_position(from, &hlength, &hunit, &vlength, &vunit);

    if (from == to) {
        return CSS_OK;
    }

    return set_object_position(to, type, hlength, hunit, vlength, vunit);
}

css_error css__compose_object_position(
    const css_computed_style *parent, const css_computed_style *child, css_computed_style *result)
{
    css_fixed_or_calc hlength = (css_fixed_or_calc)0, vlength = (css_fixed_or_calc)0;
    css_unit hunit = CSS_UNIT_PX, vunit = CSS_UNIT_PX;
    uint8_t type = get_object_position(child, &hlength, &hunit, &vlength, &vunit);

    return css__copy_object_position(type == CSS_OBJECT_POSITION_INHERIT ? parent : child, result);
}

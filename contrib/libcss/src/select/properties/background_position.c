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
#include "select/background.h"
#include "select/propset.h"

#include "select/properties/helpers.h"
#include "select/properties/properties.h"

css_error css__cascade_background_position(uint32_t opv, css_style *style, css_select_state *state)
{
    return css__background_cascade(opv, style, state, CSS_BG_POSITION);
}

css_error css__set_background_position_from_hint(const css_hint *hint, css_computed_style *style)
{
    css__background_clear(style, CSS_BG_POSITION);
    css_fixed_or_calc hlength = {.value = hint->data.position.h.value};
    css_fixed_or_calc vlength = {.value = hint->data.position.v.value};
    return set_background_position(style, hint->status, hlength, hint->data.position.h.unit, vlength,
        hint->data.position.v.unit);
}

css_error css__initial_background_position(css_select_state *state)
{
    css__background_clear(state->computed, CSS_BG_POSITION);
    return set_background_position(
        state->computed, CSS_BACKGROUND_POSITION_SET, (css_fixed_or_calc)0, CSS_UNIT_PCT, (css_fixed_or_calc)0,
        CSS_UNIT_PCT);
}

css_error css__copy_background_position(const css_computed_style *from, css_computed_style *to)
{
    css_fixed_or_calc hlength = (css_fixed_or_calc)0, vlength = (css_fixed_or_calc)0;
    css_unit hunit = CSS_UNIT_PX, vunit = CSS_UNIT_PX;
    uint8_t type = get_background_position(from, &hlength, &hunit, &vlength, &vunit);

    if (from == to) {
        return CSS_OK;
    }
    css_error list_error = css__background_copy(from, to, CSS_BG_POSITION);
    if (list_error != CSS_OK)
        return list_error;

    return set_background_position(to, type, hlength, hunit, vlength, vunit);
}

css_error css__compose_background_position(
    const css_computed_style *parent, const css_computed_style *child, css_computed_style *result)
{
    css_fixed_or_calc hlength = (css_fixed_or_calc)0, vlength = (css_fixed_or_calc)0;
    css_unit hunit = CSS_UNIT_PX, vunit = CSS_UNIT_PX;
    uint8_t type = get_background_position(child, &hlength, &hunit, &vlength, &vunit);

    return css__copy_background_position(type == CSS_BACKGROUND_POSITION_INHERIT ? parent : child, result);
}

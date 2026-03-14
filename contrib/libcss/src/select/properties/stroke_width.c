/*
 * This file is part of LibCSS
 * Licensed under the MIT License,
 *		  http://www.opensource.org/licenses/mit-license.php
 */

#include "utils/css_utils.h"
#include "bytecode/bytecode.h"
#include "bytecode/opcodes.h"
#include "select/propget.h"
#include "select/propset.h"

#include "select/properties/helpers.h"
#include "select/properties/properties.h"

css_error css__cascade_stroke_width(uint32_t opv, css_style *style, css_select_state *state)
{
    uint16_t value = CSS_STROKE_WIDTH_INHERIT;
    css_fixed_or_calc length = (css_fixed_or_calc)0;
    uint32_t unit = UNIT_PX;

    if (hasFlagValue(opv) == false) {
        value = CSS_STROKE_WIDTH_SET;

        length.value = *((css_fixed *)style->bytecode);
        advance_bytecode(style, sizeof(length.value));
        unit = *((uint32_t *)style->bytecode);
        advance_bytecode(style, sizeof(unit));
    }

    unit = css__to_css_unit(unit);

    if (css__outranks_existing(getOpcode(opv), isImportant(opv), state, getFlagValue(opv))) {
        return set_stroke_width(state->computed, value, length, unit);
    }

    return CSS_OK;
}

css_error css__set_stroke_width_from_hint(const css_hint *hint, css_computed_style *style)
{
    css_fixed_or_calc length = {.value = hint->data.length.value};
    return set_stroke_width(style, hint->status, length, hint->data.length.unit);
}

css_error css__initial_stroke_width(css_select_state *state)
{
    /* SVG spec: initial value of stroke-width is 1 (px) */
    return set_stroke_width(state->computed, CSS_STROKE_WIDTH_SET, (css_fixed_or_calc)INTTOFIX(1), CSS_UNIT_PX);
}

css_error css__copy_stroke_width(const css_computed_style *from, css_computed_style *to)
{
    css_fixed_or_calc length = (css_fixed_or_calc)0;
    css_unit unit = CSS_UNIT_PX;
    uint8_t type = get_stroke_width(from, &length, &unit);

    if (from == to) {
        return CSS_OK;
    }

    return set_stroke_width(to, type, length, unit);
}

css_error
css__compose_stroke_width(const css_computed_style *parent, const css_computed_style *child, css_computed_style *result)
{
    css_fixed_or_calc length = (css_fixed_or_calc)0;
    css_unit unit = CSS_UNIT_PX;
    uint8_t type = get_stroke_width(child, &length, &unit);

    return css__copy_stroke_width(type == CSS_STROKE_WIDTH_INHERIT ? parent : child, result);
}

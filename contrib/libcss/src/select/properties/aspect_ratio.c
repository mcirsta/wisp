/* This file is part of LibCSS. Licensed under the MIT License. */
#include "bytecode/bytecode.h"
#include "bytecode/opcodes.h"
#include "select/propget.h"
#include "select/propset.h"
#include "select/properties/helpers.h"
#include "select/properties/properties.h"

css_error css__cascade_aspect_ratio(uint32_t opv, css_style *style, css_select_state *state)
{
    uint8_t type = CSS_ASPECT_RATIO_INHERIT;
    css_fixed width = 0, height = 0;
    if (!hasFlagValue(opv)) {
        type = CSS_ASPECT_RATIO_AUTO;
        if (getValue(opv) != ASPECT_RATIO_AUTO) {
            type = getValue(opv) == ASPECT_RATIO_RATIO ? CSS_ASPECT_RATIO_RATIO : CSS_ASPECT_RATIO_AUTO_RATIO;
            width = *((css_fixed *)style->bytecode);
            advance_bytecode(style, sizeof(width));
            height = *((css_fixed *)style->bytecode);
            advance_bytecode(style, sizeof(height));
        }
    }
    if (css__outranks_existing(getOpcode(opv), isImportant(opv), state, getFlagValue(opv))) {
        return set_aspect_ratio(state->computed, type, width, height);
    }
    return CSS_OK;
}

css_error css__set_aspect_ratio_from_hint(const css_hint *hint, css_computed_style *style)
{
    return set_aspect_ratio(style, hint->status, hint->data.position.h.value, hint->data.position.v.value);
}

css_error css__initial_aspect_ratio(css_select_state *state)
{
    return set_aspect_ratio(state->computed, CSS_ASPECT_RATIO_AUTO, 0, 0);
}

css_error css__copy_aspect_ratio(const css_computed_style *from, css_computed_style *to)
{
    css_fixed width = 0, height = 0;
    uint8_t type = get_aspect_ratio(from, &width, &height);
    return from == to ? CSS_OK : set_aspect_ratio(to, type, width, height);
}

css_error
css__compose_aspect_ratio(const css_computed_style *parent, const css_computed_style *child, css_computed_style *result)
{
    css_fixed width = 0, height = 0;
    uint8_t type = get_aspect_ratio(child, &width, &height);
    return css__copy_aspect_ratio(type == CSS_ASPECT_RATIO_INHERIT ? parent : child, result);
}

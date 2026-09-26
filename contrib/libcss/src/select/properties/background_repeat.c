/*
 * Background list cascade. Licensed under the MIT License.
 */

#include "utils/css_utils.h"
#include "bytecode/bytecode.h"
#include "bytecode/opcodes.h"
#include "select/propget.h"
#include "select/background.h"
#include "select/propset.h"

#include "select/properties/helpers.h"
#include "select/properties/properties.h"

css_error css__cascade_background_repeat(uint32_t opv, css_style *style, css_select_state *state)
{
    return css__background_cascade(opv, style, state, CSS_BG_REPEAT);
}

css_error css__set_background_repeat_from_hint(const css_hint *hint, css_computed_style *style)
{
    css__background_clear(style, CSS_BG_REPEAT);
    return set_background_repeat(style, hint->status);
}

css_error css__initial_background_repeat(css_select_state *state)
{
    css__background_clear(state->computed, CSS_BG_REPEAT);
    return set_background_repeat(state->computed, CSS_BACKGROUND_REPEAT_REPEAT);
}

css_error css__copy_background_repeat(const css_computed_style *from, css_computed_style *to)
{
    if (from == to) {
        return CSS_OK;
    }
    css_error list_error = css__background_copy(from, to, CSS_BG_REPEAT);
    if (list_error != CSS_OK)
        return list_error;

    return set_background_repeat(to, get_background_repeat(from));
}

css_error css__compose_background_repeat(
    const css_computed_style *parent, const css_computed_style *child, css_computed_style *result)
{
    uint8_t type = get_background_repeat(child);

    return css__copy_background_repeat(type == CSS_BACKGROUND_REPEAT_INHERIT ? parent : child, result);
}

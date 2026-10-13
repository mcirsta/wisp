/* Shared scalar calc algebra. Licensed under the MIT License. */
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "parse/calc_value.h"
#include "bytecode/calc.h"
#include "utils/css_utils.h"

/* CSS Values 4 10.13: number, percentage, then ASCII-sorted dimension units. */
const struct css_calc_unit css__calc_units[CSS_CALC_UNIT_COUNT] = {
    {UNIT_CALC_NUMBER, ""}, {UNIT_PCT, "%"}, {UNIT_CH, "ch"}, {UNIT_CQB, "cqb"}, {UNIT_CQH, "cqh"},
    {UNIT_CQI, "cqi"}, {UNIT_CQW, "cqw"}, {UNIT_DEG, "deg"}, {UNIT_DPPX, "dppx"}, {UNIT_EM, "em"},
    {UNIT_EX, "ex"}, {UNIT_HZ, "hz"}, {UNIT_IC, "ic"}, {UNIT_LH, "lh"}, {UNIT_PX, "px"}, {UNIT_REM, "rem"},
    {UNIT_S, "s"}, {UNIT_VB, "vb"}, {UNIT_VH, "vh"}, {UNIT_VI, "vi"}, {UNIT_VMAX, "vmax"},
    {UNIT_VMIN, "vmin"}, {UNIT_VW, "vw"},
};

/* Only context-independent conversions are possible at specified-value time. */
static size_t canonical_unit(uint32_t unit, double *factor)
{
    *factor = 1;
    switch (unit) {
    case UNIT_IN: *factor = 96; unit = UNIT_PX; break;
    case UNIT_CM: *factor = 96 / 2.54; unit = UNIT_PX; break;
    case UNIT_MM: *factor = 96 / 25.4; unit = UNIT_PX; break;
    case UNIT_Q: *factor = 96 / 101.6; unit = UNIT_PX; break;
    case UNIT_PT: *factor = 4.0 / 3; unit = UNIT_PX; break;
    case UNIT_PC: *factor = 16; unit = UNIT_PX; break;
    case UNIT_MS: *factor = .001; unit = UNIT_S; break;
    case UNIT_KHZ: *factor = 1000; unit = UNIT_HZ; break;
    case UNIT_GRAD: *factor = .9; unit = UNIT_DEG; break;
    case UNIT_RAD: *factor = 180 / acos(-1); unit = UNIT_DEG; break;
    case UNIT_TURN: *factor = 360; unit = UNIT_DEG; break;
    case UNIT_DPI: *factor = 1.0 / 96; unit = UNIT_DPPX; break;
    case UNIT_DPCM: *factor = 2.54 / 96; unit = UNIT_DPPX; break;
    }
    for (size_t i = 0; i < N_ELEMENTS(css__calc_units); i++) {
        if (unit == css__calc_units[i].unit)
            return i;
    }
    return N_ELEMENTS(css__calc_units);
}

static css_error push(struct css_calc_stack *stack, lwc_string *text, uint32_t unit)
{
    double factor;
    size_t index = canonical_unit(unit, &factor);
    if (index == N_ELEMENTS(css__calc_units))
        return CSS_INVALID;
    double value = css__calc_number(text, NULL) * factor;
    if (stack->used == stack->allocated) {
        size_t count = stack->allocated == 0 ? 4 : stack->allocated * 2;
        if (count < stack->allocated || count > SIZE_MAX / sizeof(*stack->items))
            return CSS_NOMEM;
        struct css_calc_sum *items = realloc(stack->items, count * sizeof(*items));
        if (items == NULL)
            return CSS_NOMEM;
        stack->items = items;
        stack->allocated = count;
    }
    struct css_calc_sum *sum = &stack->items[stack->used++];
    memset(sum, 0, sizeof(*sum));
    sum->present = 1u << index;
    sum->exact = true;
    sum->values[index] = value;
    return CSS_OK;
}

css_error css__calc_simplify(void *pw, css_code_t op, lwc_string *text, uint32_t unit)
{
    struct css_calc_stack *stack = pw;
    if (op == CALC_PUSH_NUMBER || op == CALC_PUSH_VALUE)
        return push(stack, text, unit);
    if (stack->used < 2)
        return CSS_INVALID;
    struct css_calc_sum *left = &stack->items[stack->used - 2], *right = left + 1;
    bool exact = left->exact && right->exact;
    if (op == CALC_ADD || op == CALC_SUBTRACT) {
        for (size_t i = 0; i < N_ELEMENTS(css__calc_units); i++) {
            if (right->present & (1u << i)) {
                if (left->present & (1u << i)) {
                    double value = css__calc_binary(op, left->values[i], right->values[i]);
                    exact &= css__calc_is_exact(op, left->values[i], right->values[i], value);
                    left->values[i] = value;
                } else {
                    left->values[i] = op == CALC_ADD ? right->values[i] : -right->values[i];
                }
            }
        }
        left->present |= right->present;
    } else {
        /* Unit zero is the number slot, never a unitless length. */
        bool left_number = left->present == 1, right_number = right->present == 1;
        if ((op != CALC_MULTIPLY && op != CALC_DIVIDE) ||
            (op == CALC_MULTIPLY ? !left_number && !right_number : !right_number))
            return CSS_INVALID;
        double factor;
        if (op == CALC_MULTIPLY && left_number && !right_number) {
            factor = left->values[0];
            *left = *right;
        } else {
            factor = right->values[0];
        }
        for (size_t i = 0; i < N_ELEMENTS(css__calc_units); i++) {
            if (left->present & (1u << i)) {
                double value = css__calc_binary(op, left->values[i], factor);
                exact &= css__calc_is_exact(op, left->values[i], factor, value);
                left->values[i] = value;
            }
        }
    }
    left->exact = exact;
    stack->used--;
    return CSS_OK;
}


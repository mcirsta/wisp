/*
 * Specified calc() simplification. Licensed under the MIT License.
 *
 * The accepted grammar has scalar products/divisors only. Every finite result
 * therefore simplifies to a sum of numeric coefficients, one per unit. Keep
 * presence separately: 0% and 0em cannot be erased from a specified value.
 * No document, computed metrics or fixed-point rendering operands are used.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "parse/calc.h"
#include "utils/css_utils.h"

/* CSS Values 4 10.13: number, percentage, then ASCII-sorted dimension units. */
static const struct {
    uint32_t unit;
    const char *name;
} units[] = {
    {UNIT_CALC_NUMBER, ""}, {UNIT_PCT, "%"}, {UNIT_CH, "ch"}, {UNIT_CQB, "cqb"}, {UNIT_CQH, "cqh"},
    {UNIT_CQI, "cqi"}, {UNIT_CQW, "cqw"}, {UNIT_DEG, "deg"}, {UNIT_DPPX, "dppx"}, {UNIT_EM, "em"},
    {UNIT_EX, "ex"}, {UNIT_HZ, "hz"}, {UNIT_IC, "ic"}, {UNIT_LH, "lh"}, {UNIT_PX, "px"}, {UNIT_REM, "rem"},
    {UNIT_S, "s"}, {UNIT_VB, "vb"}, {UNIT_VH, "vh"}, {UNIT_VI, "vi"}, {UNIT_VMAX, "vmax"},
    {UNIT_VMIN, "vmin"}, {UNIT_VW, "vw"},
};

struct calc_sum {
    uint32_t present;
    /* Match the native calculator: wider readback arithmetic could change
     * a large sum when its serialized value is reparsed. */
    double values[N_ELEMENTS(units)];
};

struct calc_stack {
    struct calc_sum *items;
    size_t used, allocated;
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
    for (size_t i = 0; i < N_ELEMENTS(units); i++) {
        if (unit == units[i].unit)
            return i;
    }
    return N_ELEMENTS(units);
}

static css_error push(struct calc_stack *stack, lwc_string *text, uint32_t unit)
{
    double factor;
    size_t index = canonical_unit(unit, &factor);
    if (index == N_ELEMENTS(units))
        return CSS_INVALID;
    double value = css__calc_number(text, NULL) * factor;
    if (!isfinite(value))
        return CSS_INVALID;
    if (stack->used == stack->allocated) {
        size_t count = stack->allocated == 0 ? 4 : stack->allocated * 2;
        if (count < stack->allocated || count > SIZE_MAX / sizeof(*stack->items))
            return CSS_NOMEM;
        struct calc_sum *items = realloc(stack->items, count * sizeof(*items));
        if (items == NULL)
            return CSS_NOMEM;
        stack->items = items;
        stack->allocated = count;
    }
    struct calc_sum *sum = &stack->items[stack->used++];
    memset(sum, 0, sizeof(*sum));
    sum->present = 1u << index;
    sum->values[index] = value;
    return CSS_OK;
}

static css_error operation(void *pw, css_code_t op, lwc_string *text, uint32_t unit)
{
    struct calc_stack *stack = pw;
    if (op == CALC_PUSH_NUMBER || op == CALC_PUSH_VALUE)
        return push(stack, text, unit);
    if (stack->used < 2)
        return CSS_INVALID;
    struct calc_sum *left = &stack->items[stack->used - 2], *right = left + 1;
    if (op == CALC_ADD || op == CALC_SUBTRACT) {
        for (size_t i = 0; i < N_ELEMENTS(units); i++) {
            if (right->present & (1u << i))
                left->values[i] += op == CALC_ADD ? right->values[i] : -right->values[i];
        }
        left->present |= right->present;
    } else {
        /* Unit zero is the number slot, never a unitless length. */
        bool left_number = left->present == 1, right_number = right->present == 1;
        if ((op != CALC_MULTIPLY && op != CALC_DIVIDE) ||
            (op == CALC_MULTIPLY ? !left_number && !right_number : !right_number || right->values[0] == 0))
            return CSS_INVALID;
        double factor;
        if (op == CALC_MULTIPLY && left_number && !right_number) {
            factor = left->values[0];
            *left = *right;
        } else {
            factor = right->values[0];
        }
        for (size_t i = 0; i < N_ELEMENTS(units); i++) {
            if (left->present & (1u << i)) {
                if (op == CALC_MULTIPLY)
                    left->values[i] *= factor;
                else
                    left->values[i] /= factor;
            }
        }
    }
    for (size_t i = 0; i < N_ELEMENTS(units); i++) {
        if ((left->present & (1u << i)) && !isfinite(left->values[i]))
            return CSS_INVALID;
    }
    stack->used--;
    return CSS_OK;
}

/* Six fractional digits without changing or consulting the process locale.
 * Integer formatting has no decimal separator; insert the literal '.' here.
 * Allocate once for the full result, using a sizing pass through this helper. */
static size_t write_number(char *out, long double value)
{
    bool negative = value < 0;
    value = fabsl(value);
    long double whole = floorl(value);
    unsigned fraction = (unsigned)floorl((value - whole) * 1000000 + .5L);
    if (fraction == 1000000) {
        whole += 1;
        fraction = 0;
    }
    negative &= whole != 0 || fraction != 0;
    size_t used = negative ? 1 : 0;
    if (out != NULL && negative)
        out[0] = '-';
    /* A finite long double needs at most LDBL_MAX_10_EXP + 1 integer digits. */
    used += out != NULL ? (size_t)sprintf(out + used, "%.0Lf", whole) :
        (size_t)snprintf(NULL, 0, "%.0Lf", whole);
    if (fraction != 0) {
        char digits[8];
        size_t length = (size_t)sprintf(digits, ".%06u", fraction);
        while (digits[length - 1] == '0')
            length--;
        if (out != NULL)
            memcpy(out + used, digits, length);
        used += length;
    }
    return used;
}

static css_error serialize(const struct calc_sum *sum, lwc_string **out)
{
    size_t length = 6; /* calc( and ) */
    for (size_t i = 0, count = 0; i < N_ELEMENTS(units); i++) {
        if (!(sum->present & (1u << i)))
            continue;
        long double value = sum->values[i];
        length += write_number(NULL, count == 0 ? value : fabsl(value)) + strlen(units[i].name);
        if (count++ != 0)
            length += 3;
    }
    char *buffer = malloc(length + 1);
    if (buffer == NULL)
        return CSS_NOMEM;
    memcpy(buffer, "calc(", 5);
    size_t used = 5;
    for (size_t i = 0, count = 0; i < N_ELEMENTS(units); i++) {
        if (!(sum->present & (1u << i)))
            continue;
        long double value = sum->values[i];
        if (count++ != 0) {
            memcpy(buffer + used, value < 0 ? " - " : " + ", 3);
            used += 3;
            value = fabsl(value);
        }
        used += write_number(buffer + used, value);
        size_t n = strlen(units[i].name);
        memcpy(buffer + used, units[i].name, n);
        used += n;
    }
    buffer[used++] = ')';
    css_error error = lwc_intern_string(buffer, used, out) == lwc_error_ok ? CSS_OK : CSS_NOMEM;
    free(buffer);
    return error;
}

css_error css__declaration_calc_serialize(const css_declaration_token *tokens, size_t count,
    lwc_string **out)
{
    if (out == NULL)
        return CSS_BADPARM;
    *out = NULL;
    struct calc_stack stack = {0};
    css_error error = css__calc_walk(tokens, count, operation, &stack);
    if (error == CSS_OK && stack.used != 1)
        error = CSS_INVALID;
    if (error == CSS_OK) {
        error = serialize(&stack.items[0], out);
    }
    free(stack.items);
    return error;
}

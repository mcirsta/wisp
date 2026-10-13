/*
 * Specified calc() simplification. Licensed under the MIT License.
 *
 * The accepted grammar has scalar products/divisors only. Every result
 * therefore simplifies to a sum of numeric coefficients, one per unit. Keep
 * presence separately: 0% and 0em cannot be erased from a specified value.
 * No document, computed metrics or fixed-point rendering operands are used.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "parse/calc_value.h"
#include "utils/css_utils.h"

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

/* Non-finite dimensions have no CSS numeric-token spelling. Use the standard
 * keyword times one unit; parentheses keep such a term intact inside a sum. */
static size_t write_term(char *out, double value, size_t index, bool nested)
{
    const char *unit = css__calc_units[index].name;
    if (isfinite(value)) {
        size_t used = write_number(out, value);
        if (out != NULL)
            memcpy(out + used, unit, strlen(unit));
        return used + strlen(unit);
    }
    const char *keyword = isnan(value) ? "NaN" : value < 0 ? "-infinity" : "infinity";
    if (index == 0)
        return out != NULL ? (size_t)sprintf(out, "%s", keyword) : strlen(keyword);
    const char *format = nested ? "(%s * 1%s)" : "%s * 1%s";
    return out != NULL ? (size_t)sprintf(out, format, keyword, unit) :
        (size_t)snprintf(NULL, 0, format, keyword, unit);
}

static css_error serialize(const struct css_calc_sum *sum, lwc_string **out)
{
    bool nested = (sum->present & (sum->present - 1)) != 0;
    size_t length = 6; /* calc( and ) */
    for (size_t i = 0, count = 0; i < N_ELEMENTS(css__calc_units); i++) {
        if (!(sum->present & (1u << i)))
            continue;
        double value = sum->values[i];
        length += write_term(NULL, count == 0 ? value : fabs(value), i, nested);
        if (count++ != 0)
            length += 3;
    }
    char *buffer = malloc(length + 1);
    if (buffer == NULL)
        return CSS_NOMEM;
    memcpy(buffer, "calc(", 5);
    size_t used = 5;
    for (size_t i = 0, count = 0; i < N_ELEMENTS(css__calc_units); i++) {
        if (!(sum->present & (1u << i)))
            continue;
        double value = sum->values[i];
        if (count++ != 0) {
            memcpy(buffer + used, value < 0 ? " - " : " + ", 3);
            used += 3;
            value = fabs(value);
        }
        used += write_term(buffer + used, value, i, nested);
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
    struct css_calc_stack stack = {0};
    css_error error = css__calc_walk(tokens, count, css__calc_simplify, &stack);
    if (error == CSS_OK && stack.used != 1)
        error = CSS_INVALID;
    if (error == CSS_OK) {
        error = serialize(&stack.items[0], out);
    }
    free(stack.items);
    return error;
}

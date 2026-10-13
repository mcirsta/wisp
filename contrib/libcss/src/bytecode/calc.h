/* Private calculator operand encoding. Licensed under the MIT License. */
#ifndef css_bytecode_calc_h_
#define css_bytecode_calc_h_

#include <string.h>
#include <math.h>
#include "bytecode/bytecode.h"

/* Calc blobs are process-local, immutable strings, not a persisted format.
 * Ordinary property bytecode still uses css_fixed. Only calculator operands
 * need a wider range/precision until the complete expression is evaluated.
 * Use memcpy: operands start on code-word, not necessarily double, boundaries. */
#define CSS_CALC_NUMBER_WORDS 2
_Static_assert(sizeof(double) == CSS_CALC_NUMBER_WORDS * sizeof(css_code_t), "64-bit calc operands");

static inline double css__calc_read_number(const css_code_t **code)
{
    double value;
    memcpy(&value, *code, sizeof(value));
    *code += CSS_CALC_NUMBER_WORDS;
    return value;
}

static inline void css__calc_write_number(css_code_t *code, double value)
{
    memcpy(code, &value, sizeof(value));
}

/* IEEE-754 division, also on builds which diagnose floating division by zero. */
static inline double css__calc_binary(css_code_t op, double left, double right)
{
    switch (op) {
    case CALC_ADD: return left + right;
    case CALC_SUBTRACT: return left - right;
    case CALC_MULTIPLY: return left * right;
    case CALC_DIVIDE:
        if (right == 0)
            return left == 0 || isnan(left) ? NAN :
                copysign(INFINITY, signbit(left) != signbit(right) ? -1 : 1);
        return left / right;
    default: return NAN;
    }
}

/* Conservative zero proofs must not rely on a term lost to rounding. */
static inline bool css__calc_is_exact(css_code_t op, double left, double right, double result)
{
    if (!isfinite(result))
        return false;
    switch (op) {
    case CALC_SUBTRACT:
        right = -right;
        /* fall through */
    case CALC_ADD: {
        double part = result - left;
        return (left - (result - part)) + (right - part) == 0;
    }
    case CALC_MULTIPLY:
        return !(result == 0 && left != 0 && right != 0) && fma(left, right, -result) == 0;
    case CALC_DIVIDE:
        return right != 0 && !(result == 0 && left != 0) && fma(result, right, -left) == 0;
    default: return false;
    }
}

#endif

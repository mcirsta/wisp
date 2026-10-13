/* Private calculator operand encoding. Licensed under the MIT License. */
#ifndef css_bytecode_calc_h_
#define css_bytecode_calc_h_

#include <string.h>
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

#endif

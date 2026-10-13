/* Shared scalar calc simplification. Licensed under the MIT License. */
#ifndef css_parse_calc_value_h_
#define css_parse_calc_value_h_

#include "parse/calc.h"

#define CSS_CALC_UNIT_COUNT 23
#define CSS_CALC_PERCENT_BIT (1u << 1)

struct css_calc_unit {
    uint32_t unit;
    const char *name;
};
extern const struct css_calc_unit css__calc_units[CSS_CALC_UNIT_COUNT];

struct css_calc_sum {
    uint32_t present;
    bool exact;
    double values[CSS_CALC_UNIT_COUNT];
};

struct css_calc_stack {
    struct css_calc_sum *items;
    size_t used, allocated;
};

/* Postfix visitor shared by native bytecode compilation and CSSOM readback.
 * Units with zero coefficients stay present; no document metrics are used. */
css_error css__calc_simplify(void *pw, css_code_t op, lwc_string *text, uint32_t unit);

#endif

/*
 * This file is part of LibCSS.
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 * Copyright 2024 Daniel Silverstone <dsilvers@netsurf-browser.org>
 */

#include <string.h>
#include <math.h>

#include "bytecode/calc.h"

#include "libcss/hint.h"

#include "select/calc.h"
#include "select/helpers.h"
#include "select/unit.h"

#include "utils/css_utils.h"

/** The default number of entries on a calculator stack */
#define DEFAULT_STACK_SIZE 32

#ifndef NDEBUG
#define CANARY "A css_calculator has leaked"
#define CANARY_SLEN SLEN(CANARY)
#endif

/****************************** Allocation **********************************/

/* Exported function, documented in calc.h */
css_error css_calculator_create(css_calculator **out)
{
    *out = calloc(1, sizeof(css_calculator));
    if (*out == NULL) {
        return CSS_NOMEM;
    }

#ifndef NDEBUG
    if (lwc_intern_string(CANARY, CANARY_SLEN, &((*out)->canary)) != lwc_error_ok) {
        free(*out);
        *out = NULL;
        return CSS_NOMEM;
    }
#endif

    (*out)->stack = calloc(DEFAULT_STACK_SIZE, sizeof(css_calculator_stack_entry));
    if ((*out)->stack == NULL) {
#ifndef NDEBUG
        lwc_string_unref((*out)->canary);
#endif
        free(*out);
        *out = NULL;
        return CSS_NOMEM;
    }

    (*out)->refcount = 1;
    (*out)->stack_alloc = DEFAULT_STACK_SIZE;
    (*out)->stack_ptr = 0;

    return CSS_OK;
}

/* Exported function, documented in calc.h */
css_calculator *css_calculator_ref(css_calculator *calc)
{
    calc->refcount += 1;
    return calc;
}

/* Exported function, documented in calc.h */
void css_calculator_unref(css_calculator *calc)
{
    calc->refcount -= 1;
    if (calc->refcount == 0) {
#ifndef NDEBUG
        lwc_string_unref(calc->canary);
#endif
        free(calc->stack);
        free(calc);
    }
}

/****************************** Helpers ************************************/

static css_error css__calculator_push(css_calculator *calc, unit unit, double value, double percent)
{
    if (calc->stack_ptr == calc->stack_alloc) {
        css_calculator_stack_entry *newstack = realloc(
            calc->stack, sizeof(css_calculator_stack_entry) * 2 * calc->stack_alloc);
        if (newstack == NULL) {
            return CSS_NOMEM;
        }
        calc->stack = newstack;
        calc->stack_alloc *= 2;
    }
    calc->stack[calc->stack_ptr].unit = unit;
    calc->stack[calc->stack_ptr].value = value;
    calc->stack[calc->stack_ptr].percent = percent;
    calc->stack_ptr += 1;

    return CSS_OK;
}

static css_error css__calculator_pop(css_calculator *calc, unit *unit, double *value, double *percent)
{
    if (calc->stack_ptr == 0) {
        return CSS_INVALID;
    }

    calc->stack_ptr -= 1;
    *unit = calc->stack[calc->stack_ptr].unit;
    *value = calc->stack[calc->stack_ptr].value;
    *percent = calc->stack[calc->stack_ptr].percent;

    return CSS_OK;
}

#define CALC_PUSH(unit, value, percent)                                                                                \
    do {                                                                                                               \
        ret = css__calculator_push(calc, unit, value, percent);                                                        \
        if (ret != CSS_OK) {                                                                                           \
            return ret;                                                                                                \
        }                                                                                                              \
    } while (0)

#define CALC_POP(unit, value, percent)                                                                                 \
    do {                                                                                                               \
        ret = css__calculator_pop(calc, &unit, &value, &percent);                                                      \
        if (ret != CSS_OK) {                                                                                           \
            return ret;                                                                                                \
        }                                                                                                              \
    } while (0)

/* Normalise:
 *  - lengths to pixels
 *  - angles to degrees
 *  - time to ms
 *  - freq to hz
 *  - resolution to dpi
 */
static css_error css__normalise_unit(
    const css_unit_ctx *unit_ctx, const css_computed_style *style, int32_t available, unit *u, double *v)
{
    if (*u & UNIT_LENGTH) {
        *v = css_unit_len2css_px_calc(style, unit_ctx, *v, css__to_css_unit(*u));
        *u = UNIT_PX;
        return CSS_OK;
    } else if (*u & UNIT_ANGLE) {
        if (*u == UNIT_GRAD)
            *v *= .9;
        else if (*u == UNIT_RAD)
            *v *= 180 / acos(-1);
        else if (*u == UNIT_TURN)
            *v *= 360;
        *u = UNIT_DEG;
        return CSS_OK;
    } else if (*u & UNIT_TIME) {
        if (*u == UNIT_S) {
            *v *= 1000;
        }
        *u = UNIT_MS;
        return CSS_OK;
    } else if (*u & UNIT_FREQ) {
        if (*u == UNIT_KHZ) {
            *v *= 1000;
        }
        *u = UNIT_HZ;
        return CSS_OK;
    } else if (*u & UNIT_RESOLUTION) {
        if (*u == UNIT_DPCM) {
            *v *= 2.54;
        } else if (*u == UNIT_DPPX) {
            *v *= 96;
        }
        *u = UNIT_DPI;
        return CSS_OK;
    } else if (*u == UNIT_PCT) {
        if (available < 0) {
            return CSS_INVALID;
        }
        *v *= (double)available * 96 * F_1 / unit_ctx->device_dpi / 100;
        *u = UNIT_PX;
        return CSS_OK;
    } else if (*u == UNIT_CALC_NUMBER) {
        /* Nothing to do to normalise numbers */
        return CSS_OK;
    }
    return CSS_INVALID;
}

/****************************** Compute ************************************/

/* Analysis must not infer cancellation from rounded arithmetic. TwoSum and
 * fused residuals detect inexact operations without an arbitrary epsilon.
 * A failed proof simply leaves the edge classified as potentially nonzero. */
static css_error css__calculator_exact_op(css_code_t op, double left, double right, double *out)
{
    double result, error;
    switch (op) {
    case CALC_SUBTRACT:
        right = -right;
        /* fall through */
    case CALC_ADD: {
        result = left + right;
        double part = result - left;
        error = (left - (result - part)) + (right - part);
        break;
    }
    case CALC_MULTIPLY:
        result = left * right;
        error = fma(left, right, -result);
        if (result == 0 && left != 0 && right != 0)
            return CSS_INVALID;
        break;
    case CALC_DIVIDE:
        if (right == 0)
            return CSS_INVALID;
        result = left / right;
        error = fma(result, right, -left);
        if (result == 0 && left != 0)
            return CSS_INVALID;
        break;
    default:
        return CSS_INVALID;
    }
    if (!isfinite(result) || error != 0)
        return CSS_INVALID;
    *out = result;
    return CSS_OK;
}

/* A non-NULL percent_out preserves the percentage coefficient for analysis.
 * Ordinary used-value evaluation still resolves each percentage as before. */
static css_error css__calculator_calculate(css_calculator *calc, const css_unit_ctx *unit_ctx, int32_t available,
    lwc_string *expr, const css_computed_style *style, css_unit *unit_out, double *value_out,
    double *percent_out)
{
    css_error ret = CSS_OK;
    const css_code_t *codeptr = (const css_code_t *)(const void *)lwc_string_data(expr);

    /* Reset the stack before we begin, just in case */
    calc->stack_ptr = 0;

    /* We are trusting that the bytecode is sane */
    while (*codeptr != CALC_FINISH) {
        css_code_t op = *codeptr++;
        switch (op) {
        case CALC_PUSH_VALUE: {
            double v = css__calc_read_number(&codeptr);
            unit u = (unit)(*codeptr++);
            double percent = 0;
            if (percent_out != NULL && u == UNIT_PCT) {
                percent = v;
                v = 0;
                u = UNIT_PX;
            } else {
                ret = css__normalise_unit(unit_ctx, style, available, &u, &v);
                if (ret != CSS_OK) {
                    return ret;
                }
            }
            CALC_PUSH(u, v, percent);
            break;
        }
        case CALC_PUSH_NUMBER: {
            double v = css__calc_read_number(&codeptr);
            CALC_PUSH(UNIT_CALC_NUMBER, v, 0);
            break;
        }
        case CALC_ADD: /* fallthrough */
        case CALC_SUBTRACT: {
            unit u_left, u_right;
            double v_left, v_right, p_left, p_right;
            CALC_POP(u_right, v_right, p_right);
            CALC_POP(u_left, v_left, p_left);
            if (percent_out != NULL) {
                if (u_left != u_right ||
                    css__calculator_exact_op(op, v_left, v_right, &v_left) != CSS_OK ||
                    css__calculator_exact_op(op, p_left, p_right, &p_left) != CSS_OK)
                    return CSS_INVALID;
            } else if (op == CALC_ADD) {
                v_left += v_right;
            } else {
                v_left -= v_right;
            }
            CALC_PUSH(u_left, v_left, p_left);
            break;
        }
        case CALC_MULTIPLY: /* fallthrough */
        case CALC_DIVIDE: {
            unit u_left, u_right;
            double v_left, v_right, p_left, p_right;
            CALC_POP(u_right, v_right, p_right);
            CALC_POP(u_left, v_left, p_left);

            if (op == CALC_MULTIPLY && u_left == UNIT_CALC_NUMBER) {
                unit u_tmp = u_left;
                double v_tmp = v_left;
                u_left = u_right;
                v_left = v_right;
                u_right = u_tmp;
                v_right = v_tmp;
                double p_tmp = p_left;
                p_left = p_right;
                p_right = p_tmp;
            }

            if (u_right != UNIT_CALC_NUMBER) {
                return CSS_INVALID;
            }

            if (op == CALC_DIVIDE && v_right == 0)
                return CSS_INVALID;

            if (percent_out != NULL) {
                if (css__calculator_exact_op(op, v_left, v_right, &v_left) != CSS_OK ||
                    css__calculator_exact_op(op, p_left, v_right, &p_left) != CSS_OK)
                    return CSS_INVALID;
            } else if (op == CALC_MULTIPLY) {
                v_left *= v_right;
            } else {
                v_left /= v_right;
            }

            CALC_PUSH(u_left, v_left, p_left);
            break;
        }
        case CALC_FINISH: /* Should not happen */
        default:
            return CSS_INVALID;
        }
    }

    if (calc->stack_ptr != 1) {
        return CSS_INVALID;
    }

    *unit_out = css__to_css_unit(calc->stack[0].unit);
    *value_out = calc->stack[0].value;
    if (percent_out != NULL)
        *percent_out = calc->stack[0].percent;

    return CSS_OK;
}

/* Exported functions, documented in calc.h. */
css_error css_calculator_calculate(css_calculator *calc, const css_unit_ctx *unit_ctx, int32_t available,
    lwc_string *expr, const css_computed_style *style, css_unit *unit_out, css_fixed *value_out)
{
    double value;
    css_error error = css__calculator_calculate(calc, unit_ctx, available, expr, style, unit_out, &value, NULL);
    if (error != CSS_OK)
        return error;
    /* Conversion to the existing computed-value API is the sole fixed-point
     * boundary. Check range before conversion to avoid undefined casts. */
    if (!isfinite(value))
        return CSS_INVALID;
    value = round(value * F_1);
    *value_out = value >= INT32_MAX ? INT32_MAX : value <= INT32_MIN ? INT32_MIN : (css_fixed)value;
    return CSS_OK;
}

static bool line_height_needs_layout(css_unit unit)
{
    return unit != CSS_UNIT_PX && unit != CSS_UNIT_IN && unit != CSS_UNIT_CM && unit != CSS_UNIT_MM &&
        unit != CSS_UNIT_Q && unit != CSS_UNIT_PT && unit != CSS_UNIT_PC;
}

css_error css_calculator_resolve_line_height(lwc_string *expr, const css_computed_style *style,
    const css_unit_ctx *unit_ctx, lwc_string **result, bool *needs_layout)
{
    const css_code_t *code = (const css_code_t *)(const void *)lwc_string_data(expr);
    css_code_t *resolved = NULL;
    size_t bytes = lwc_string_length(expr);
    *needs_layout = false;

    /* Expressions are immutable interned bytecode. Copy only when a font-relative
     * operand needs to be frozen; inherited, already-resolved expressions share
     * their original storage. Keep viewport/container operands for reflow. */
    for (size_t i = 0; code[i] != CALC_FINISH;) {
        if (code[i] == CALC_PUSH_NUMBER) {
            i += 1 + CSS_CALC_NUMBER_WORDS;
        } else if (code[i] == CALC_PUSH_VALUE) {
            const css_code_t *operand = code + i + 1;
            double value = css__calc_read_number(&operand);
            size_t unit_index = i + 1 + CSS_CALC_NUMBER_WORDS;
            css_unit unit = css__to_css_unit(code[unit_index]);
            switch (unit) {
            case CSS_UNIT_PCT:
            case CSS_UNIT_EM:
            case CSS_UNIT_EX:
            case CSS_UNIT_CH:
            case CSS_UNIT_IC:
            case CSS_UNIT_REM:
                css_unit_compute_line_height_calc(style, unit_ctx, &value, &unit);
                if (line_height_needs_layout(unit))
                    *needs_layout = true;
                break;
            default:
                /* Absolute lengths and numbers can be folded immediately. */
                if ((code[unit_index] & UNIT_LENGTH) && line_height_needs_layout(unit))
                    *needs_layout = true;
                i += 2 + CSS_CALC_NUMBER_WORDS;
                continue;
            }
            if (resolved == NULL) {
                resolved = malloc(bytes);
                if (resolved == NULL)
                    return CSS_NOMEM;
                memcpy(resolved, code, bytes);
            }
            css__calc_write_number(resolved + i + 1, value);
            /* Length units share their bytecode/public-enum offsets. */
            resolved[unit_index] = UNIT_LENGTH + unit;
            assert(css__to_css_unit(resolved[unit_index]) == unit);
            i += 2 + CSS_CALC_NUMBER_WORDS;
        } else {
            i++;
        }
    }
    if (resolved == NULL) {
        *result = lwc_string_ref(expr);
        return CSS_OK;
    }
    lwc_error error = lwc_intern_string((const char *)resolved, bytes, result);
    free(resolved);
    return error == lwc_error_ok ? CSS_OK : CSS_NOMEM;
}

bool css_calculator_length_is_zero(css_calculator *calc, const css_unit_ctx *unit_ctx,
    lwc_string *expr, const css_computed_style *style)
{
    double value, percent;
    css_unit unit;
    /* Test the unrounded result, never a value quantized to zero at the public
     * fixed-point boundary. Percentages still need separate coefficient proof. */
    if (css__calculator_calculate(calc, unit_ctx, -1, expr, style, &unit, &value, NULL) == CSS_OK)
        return value == 0;
    return css__calculator_calculate(calc, unit_ctx, -1, expr, style, &unit, &value, &percent) == CSS_OK &&
        unit == CSS_UNIT_PX && value == 0 && percent == 0;
}

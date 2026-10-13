/*
 * This file is part of LibCSS. Licensed under the MIT License.
 * Copyright 2024 Daniel Silverstone <dsilvers@netsurf-browser.org>
 *
 * Shared native calc() grammar. Check types and simplify before compiling a
 * declaration's postfix calculator bytecode. Operands retain floating-point
 * precision until final computed-value conversion.
 */

#include "parse/properties/utils.h"
#include "parse/calc_value.h"
#include "bytecode/calc.h"
#include "utils/parserutilserror.h"

/* CSS Values 4 10.8 requires at least 32 levels. Bound C recursion while
 * allowing substantially more than that; flat sums/products remain iterative. */
#define CALC_MAX_DEPTH 128

struct calc_parser {
    const parserutils_vector *tokens;
    bool check_types;
    uint32_t unit_mask;
    unsigned depth;
    css_calc_emit emit;
    void *pw;
};

/* Decimal literals normalize signed zero; arithmetic can subsequently produce
 * negative zero. The three non-finite keywords are numbers only inside calc. */
double css__calc_number(lwc_string *text, size_t *consumed)
{
    if (css__keyword_equal(text, "infinity") || css__keyword_equal(text, "-infinity") ||
        css__keyword_equal(text, "nan")) {
        if (consumed != NULL)
            *consumed = lwc_string_length(text);
        return css__keyword_equal(text, "nan") ? NAN :
            css__keyword_equal(text, "-infinity") ? -INFINITY : INFINITY;
    }
    const char *data = lwc_string_data(text);
    size_t length = lwc_string_length(text), pos = 0;
    bool negative = length > 0 && data[pos] == '-';
    if (length > 0 && (negative || data[pos] == '+'))
        pos++;
    long double value = 0;
    while (pos < length && data[pos] >= '0' && data[pos] <= '9')
        value = value * 10 + (data[pos++] - '0');
    if (pos < length && data[pos] == '.') {
        pos++;
        long double place = .1L;
        while (pos < length && data[pos] >= '0' && data[pos] <= '9') {
            value += (data[pos++] - '0') * place;
            place /= 10;
        }
    }
    if (consumed != NULL)
        *consumed = pos;
    return value == 0 ? 0 : (double)(negative ? -value : value);
}

static css_error calc_sum(struct calc_parser *parser, int *ctx, uint32_t *type);

static css_error calc_operator(struct calc_parser *parser, css_code_t op)
{
    return parser->emit(parser->pw, op, NULL, 0);
}

static uint32_t calc_unit_type(uint32_t unit)
{
    return unit & (UNIT_LENGTH | UNIT_PCT | UNIT_ANGLE | UNIT_TIME | UNIT_FREQ | UNIT_RESOLUTION);
}

/* Numbers are always numbers, including zero and line-height multipliers.
 * A percentage can combine only with its property's dimension type. */
static uint32_t calc_percentage_type(uint32_t mask)
{
    if (mask & UNIT_LENGTH)
        return UNIT_LENGTH;
    if (mask & UNIT_TIME)
        return UNIT_TIME;
    return UNIT_PCT;
}

static css_error calc_value(struct calc_parser *parser, int *ctx, uint32_t *type)
{
    const css_token *token = parserutils_vector_peek(parser->tokens, *ctx);
    if (token == NULL)
        return CSS_INVALID;
    if (tokenIsChar(token, '(') ||
        (token->type == CSS_TOKEN_FUNCTION && css__keyword_equal(token->idata, "calc"))) {
        if (parser->depth == CALC_MAX_DEPTH)
            return CSS_INVALID;
        parser->depth++;
        parserutils_vector_iterate(parser->tokens, ctx);
        consumeWhitespace(parser->tokens, ctx);
        css_error error = calc_sum(parser, ctx, type);
        parser->depth--;
        if (error != CSS_OK)
            return error;
        token = parserutils_vector_iterate(parser->tokens, ctx);
        return tokenIsChar(token, ')') ? CSS_OK : CSS_INVALID;
    }
    bool keyword = token->type == CSS_TOKEN_IDENT &&
        (css__keyword_equal(token->idata, "infinity") || css__keyword_equal(token->idata, "-infinity") ||
            css__keyword_equal(token->idata, "nan"));
    if (token->type == CSS_TOKEN_NUMBER || keyword) {
        size_t consumed;
        css__calc_number(token->idata, &consumed);
        if (consumed != lwc_string_length(token->idata))
            return CSS_INVALID;
        parserutils_vector_iterate(parser->tokens, ctx);
        *type = UNIT_CALC_NUMBER;
        const css_code_t push = CALC_PUSH_NUMBER;
        return parser->emit(parser->pw, push, token->idata, UNIT_CALC_NUMBER);
    }
    if (token->type != CSS_TOKEN_DIMENSION && token->type != CSS_TOKEN_PERCENTAGE)
        return CSS_INVALID;
    size_t consumed;
    css__calc_number(token->idata, &consumed);
    uint32_t unit = UNIT_PCT;
    if (token->type == CSS_TOKEN_DIMENSION) {
        css_error error = css__parse_unit_keyword(lwc_string_data(token->idata) + consumed,
            lwc_string_length(token->idata) - consumed, &unit);
        if (error != CSS_OK)
            return error;
    } else if (consumed != lwc_string_length(token->idata)) {
        return CSS_INVALID;
    }
    parserutils_vector_iterate(parser->tokens, ctx);
    /* fr shares the bytecode length bit, but is not a CSS length. */
    if (unit == UNIT_FR || (parser->check_types && !(calc_unit_type(unit) & parser->unit_mask)))
        return CSS_INVALID;
    *type = unit == UNIT_PCT ? calc_percentage_type(parser->unit_mask) : calc_unit_type(unit);
    const css_code_t push = CALC_PUSH_VALUE;
    return parser->emit(parser->pw, push, token->idata, unit);
}

static css_error calc_product(struct calc_parser *parser, int *ctx, uint32_t *type)
{
    css_error error = calc_value(parser, ctx, type);
    if (error != CSS_OK)
        return error;
    for (;;) {
        consumeWhitespace(parser->tokens, ctx);
        const css_token *token = parserutils_vector_peek(parser->tokens, *ctx);
        if (tokenIsChar(token, ')') || tokenIsChar(token, '+') || tokenIsChar(token, '-'))
            return CSS_OK;
        css_code_t op;
        if (tokenIsChar(token, '*'))
            op = CALC_MULTIPLY;
        else if (tokenIsChar(token, '/'))
            op = CALC_DIVIDE;
        else
            return CSS_INVALID;
        parserutils_vector_iterate(parser->tokens, ctx);
        consumeWhitespace(parser->tokens, ctx);
        uint32_t right;
        error = calc_value(parser, ctx, &right);
        if (error != CSS_OK)
            return error;
        /* This calculator supports scalar multiplication/division. CSS Values
         * 4 dimensional products and cancellation need an extended evaluator;
         * reject them here instead of publishing unusable bytecode. */
        if (op == CALC_MULTIPLY && *type == UNIT_CALC_NUMBER)
            *type = right;
        else if (right != UNIT_CALC_NUMBER)
            return CSS_INVALID;
        error = calc_operator(parser, op);
        if (error != CSS_OK)
            return error;
    }
}

static css_error calc_sum(struct calc_parser *parser, int *ctx, uint32_t *type)
{
    css_error error = calc_product(parser, ctx, type);
    if (error != CSS_OK)
        return error;
    for (;;) {
        const css_token *token = parserutils_vector_peek(parser->tokens, *ctx);
        if (tokenIsChar(token, ')'))
            return CSS_OK;
        css_code_t op;
        if (tokenIsChar(token, '+'))
            op = CALC_ADD;
        else if (tokenIsChar(token, '-'))
            op = CALC_SUBTRACT;
        else
            return CSS_INVALID;
        const css_token *before = parserutils_vector_peek(parser->tokens, *ctx - 1);
        parserutils_vector_iterate(parser->tokens, ctx);
        const css_token *after = parserutils_vector_peek(parser->tokens, *ctx);
        if (before == NULL || before->type != CSS_TOKEN_S || after == NULL || after->type != CSS_TOKEN_S)
            return CSS_INVALID;
        consumeWhitespace(parser->tokens, ctx);
        uint32_t right;
        error = calc_product(parser, ctx, &right);
        if (error != CSS_OK)
            return error;
        if (parser->check_types && *type != right)
            return CSS_INVALID;
        error = calc_operator(parser, op);
        if (error != CSS_OK)
            return error;
    }
}

/* Documented in properties/utils.h. The generated caller has consumed calc(. */
css_error css__parse_calc(css_language *c, const parserutils_vector *vector, int *ctx, css_style *result,
    css_code_t OPV, const struct css_calc_parse_options *options)
{
    uint32_t unit = options->unit, unit_mask = options->unit_mask;
    int orig_ctx = *ctx;
    uint32_t orig_used = result->used;
    struct css_calc_stack stack = {0};
    struct calc_parser parser = {.tokens = vector, .unit_mask = unit_mask, .depth = 1, .check_types = true,
        .emit = css__calc_simplify, .pw = &stack};
    css_error error;
    consumeWhitespace(vector, ctx);
    uint32_t type;
    error = calc_sum(&parser, ctx, &type);
    if (error != CSS_OK)
        goto done;
    bool number_allowed = unit == UNIT_CALC_NUMBER || unit == UNIT_CALC_ANY;
    if (type == UNIT_CALC_NUMBER ? !number_allowed : !(type & unit_mask)) {
        error = CSS_INVALID;
        goto done;
    }
    /* calc_sum stops at its required closing parenthesis. */
    parserutils_vector_iterate(vector, ctx);
    if (stack.used != 1) {
        error = CSS_INVALID;
        goto done;
    }
    /* Compile the specified-value sum, before document unit resolution. This
     * is also what CSSOM reads back. Scalar calc needs no persistent AST, and
     * long flat expressions become a bounded, small used-value program. */
    const struct css_calc_sum *sum = &stack.items[0];
    css_code_t code[CSS_CALC_UNIT_COUNT * (3 + CSS_CALC_NUMBER_WORDS) + 3 + 2 * CSS_CALC_NUMBER_WORDS];
    size_t used = 0, terms = 0;
    if (!sum->exact && (sum->present & CSS_CALC_PERCENT_BIT))
        code[used++] = CALC_INEXACT;
    for (size_t i = 0; i < CSS_CALC_UNIT_COUNT; i++) {
        if (!(sum->present & (1u << i)))
            continue;
        code[used++] = i == 0 ? CALC_PUSH_NUMBER : CALC_PUSH_VALUE;
        css__calc_write_number(code + used, sum->values[i]);
        used += CSS_CALC_NUMBER_WORDS;
        if (i != 0)
            code[used++] = css__calc_units[i].unit;
        if (terms++ != 0)
            code[used++] = CALC_ADD;
    }
    if (isfinite(options->minimum) || isfinite(options->maximum)) {
        code[used++] = CALC_CLAMP;
        css__calc_write_number(code + used, options->minimum);
        used += CSS_CALC_NUMBER_WORDS;
        css__calc_write_number(code + used, options->maximum);
        used += CSS_CALC_NUMBER_WORDS;
    }
    code[used++] = CALC_FINISH;
    lwc_string *expression;
    error = css_error_from_lwc_error(lwc_intern_string((const char *)code, used * sizeof(*code), &expression));
    if (error != CSS_OK)
        goto done;
    uint32_t index;
    /* The sheet takes ownership even if adding the string fails. */
    error = css__stylesheet_string_add(c->sheet, expression, &index);
    if (error == CSS_OK)
        error = css__stylesheet_style_vappend(result, 3, OPV, unit, index);
done:
    free(stack.items);
    if (error != CSS_OK) {
        *ctx = orig_ctx;
        result->used = orig_used;
    }
    return error;
}

css_error css__calc_walk(const css_declaration_token *tokens, size_t count, css_calc_emit emit, void *pw)
{
    if (tokens == NULL || emit == NULL || count == 0 || count > INT32_MAX)
        return CSS_BADPARM;
    if (tokens[0].type != CSS_TOKEN_FUNCTION || !css__keyword_equal(tokens[0].text, "calc"))
        return CSS_INVALID;
    if (count > SIZE_MAX / sizeof(css_token))
        return CSS_NOMEM;
    parserutils_vector *vector;
    css_error error = css_error_from_parserutils_error(parserutils_vector_create(sizeof(css_token), count, &vector));
    if (error != CSS_OK)
        return error;
    for (size_t i = 0; i < count && error == CSS_OK; i++) {
        css_token token = {.type = tokens[i].type, .idata = tokens[i].text};
        error = css_error_from_parserutils_error(parserutils_vector_append(vector, &token));
    }
    if (error == CSS_OK) {
        struct calc_parser parser = {.tokens = vector, .depth = 1, .emit = emit, .pw = pw};
        int ctx = 1;
        uint32_t type;
        consumeWhitespace(vector, &ctx);
        error = calc_sum(&parser, &ctx, &type);
        if (error == CSS_OK && (size_t)ctx + 1 != count)
            error = CSS_INVALID;
    }
    parserutils_vector_destroy(vector);
    return error;
}

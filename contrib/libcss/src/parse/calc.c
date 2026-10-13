/*
 * This file is part of LibCSS. Licensed under the MIT License.
 * Copyright 2024 Daniel Silverstone <dsilvers@netsurf-browser.org>
 *
 * Shared native calc() grammar. Emit the existing postfix calculator bytecode
 * while checking types, before a declaration can enter the cascade. Numeric
 * readback must use the specified tokens, not these fixed-point operands.
 */

#include "parse/properties/utils.h"
#include "utils/parserutilserror.h"

/* CSS Values 4 10.8 requires at least 32 levels. Bound C recursion while
 * allowing substantially more than that; flat sums/products remain iterative. */
#define CALC_MAX_DEPTH 128

struct calc_parser {
    css_language *language;
    const parserutils_vector *tokens;
    parserutils_buffer *code;
    uint32_t unit_mask;
    unsigned depth;
};

static css_error calc_sum(struct calc_parser *parser, int *ctx, uint32_t *type);

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
    if (token->type == CSS_TOKEN_NUMBER) {
        size_t consumed;
        css_fixed number = css__number_from_lwc_string(token->idata, false, &consumed);
        if (consumed != lwc_string_length(token->idata))
            return CSS_INVALID;
        parserutils_vector_iterate(parser->tokens, ctx);
        *type = UNIT_CALC_NUMBER;
        const css_code_t push = CALC_PUSH_NUMBER;
        return css_error_from_parserutils_error(
            parserutils_buffer_appendv(parser->code, 2, &push, sizeof(push), &number, sizeof(number)));
    }
    if (token->type != CSS_TOKEN_DIMENSION && token->type != CSS_TOKEN_PERCENTAGE)
        return CSS_INVALID;
    css_fixed number;
    uint32_t unit;
    css_error error = css__parse_unit_specifier(
        parser->language, parser->tokens, ctx, UNIT_CALC_NUMBER, &number, &unit);
    if (error != CSS_OK)
        return error;
    /* fr shares the bytecode length bit, but is not a CSS length. */
    if (unit == UNIT_FR || !(calc_unit_type(unit) & parser->unit_mask))
        return CSS_INVALID;
    *type = unit == UNIT_PCT ? calc_percentage_type(parser->unit_mask) : calc_unit_type(unit);
    const css_code_t push = CALC_PUSH_VALUE;
    return css_error_from_parserutils_error(parserutils_buffer_appendv(
        parser->code, 3, &push, sizeof(push), &number, sizeof(number), &unit, sizeof(unit)));
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
        error = css_error_from_parserutils_error(
            parserutils_buffer_append(parser->code, (const uint8_t *)&op, sizeof(op)));
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
        if (*type != right)
            return CSS_INVALID;
        error = css_error_from_parserutils_error(
            parserutils_buffer_append(parser->code, (const uint8_t *)&op, sizeof(op)));
        if (error != CSS_OK)
            return error;
    }
}

/* Documented in properties/utils.h. The generated caller has consumed calc(. */
css_error css__parse_calc(css_language *c, const parserutils_vector *vector, int *ctx, css_style *result,
    css_code_t OPV, uint32_t unit, uint32_t unit_mask)
{
    int orig_ctx = *ctx;
    uint32_t orig_used = result->used;
    struct calc_parser parser = {.language = c, .tokens = vector, .unit_mask = unit_mask, .depth = 1};
    css_error error = css_error_from_parserutils_error(parserutils_buffer_create(&parser.code));
    if (error != CSS_OK)
        return error;
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
    const css_code_t finish = CALC_FINISH;
    error = css_error_from_parserutils_error(
        parserutils_buffer_append(parser.code, (const uint8_t *)&finish, sizeof(finish)));
    if (error != CSS_OK)
        goto done;
    lwc_string *expression;
    error = css_error_from_lwc_error(
        lwc_intern_string((const char *)parser.code->data, parser.code->length, &expression));
    if (error != CSS_OK)
        goto done;
    uint32_t index;
    /* The sheet takes ownership even if adding the string fails. */
    error = css__stylesheet_string_add(c->sheet, expression, &index);
    if (error == CSS_OK)
        error = css__stylesheet_style_vappend(result, 3, OPV, unit, index);
done:
    parserutils_buffer_destroy(parser.code);
    if (error != CSS_OK) {
        *ctx = orig_ctx;
        result->used = orig_used;
    }
    return error;
}

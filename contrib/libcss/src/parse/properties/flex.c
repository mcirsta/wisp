/*
 * This file is part of LibCSS.
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 * Copyright 2017 Lucas Neves <lcneves@gmail.com>
 */

#include "bytecode/bytecode.h"
#include "bytecode/opcodes.h"
#include "parse/properties/properties.h"
#include "parse/properties/utils.h"

/* The factor pair is one grammar group: basis may precede or follow it, but
 * cannot split it. Parsing numbers first also implements Flexbox §7.1's rule
 * that a bare zero is a factor unless two factors have already been supplied. */
static css_error parse_factors(css_language *c, const parserutils_vector *vector, int32_t *ctx,
    css_style *grow, css_style *shrink)
{
    css_error error = css__parse_flex_grow_value(c, vector, ctx, grow);
    if (error != CSS_OK)
        return error;
    consumeWhitespace(vector, ctx);
    error = css__parse_flex_shrink_value(c, vector, ctx, shrink);
    if (error != CSS_OK && error != CSS_INVALID)
        return error;
    consumeWhitespace(vector, ctx);
    return CSS_OK;
}

/* Parse none | [ <flex-grow> <flex-shrink>? || <flex-basis> ]. Keep both
 * bytecode and token position unchanged on failure, including allocation errors. */
css_error css__parse_flex(css_language *c, const parserutils_vector *vector, int32_t *ctx, css_style *result)
{
    int32_t orig_ctx = *ctx;
    uint32_t orig_used = result->used;
    const css_token *token = parserutils_vector_peek(vector, *ctx);
    if (token == NULL)
        return CSS_INVALID;
    css_style *parts[3] = {0};
    const enum css_properties_e properties[] = {CSS_PROP_FLEX_GROW, CSS_PROP_FLEX_SHRINK, CSS_PROP_FLEX_BASIS};
    css_error error;
    enum flag_value flag = get_css_flag_value(c, token);
    if (flag != FLAG_VALUE__NONE) {
        for (size_t i = 0; i < 3; i++) {
            error = css_stylesheet_style_flag_value(result, flag, properties[i]);
            if (error != CSS_OK)
                goto done;
        }
        parserutils_vector_iterate(vector, ctx);
        goto done;
    }
    for (size_t i = 0; i < 3; i++) {
        error = css__stylesheet_style_create(c->sheet, &parts[i]);
        if (error != CSS_OK)
            goto done;
    }
    bool none = token->type == CSS_TOKEN_IDENT && css__keyword_equal(token->idata, "none");
    if (none) {
        parserutils_vector_iterate(vector, ctx);
    } else {
        error = parse_factors(c, vector, ctx, parts[0], parts[1]);
        if (error == CSS_OK) {
            error = css__parse_flex_basis_value(c, vector, ctx, parts[2]);
            if (error != CSS_OK && error != CSS_INVALID)
                goto done;
        } else if (error == CSS_INVALID) {
            error = css__parse_flex_basis_value(c, vector, ctx, parts[2]);
            if (error != CSS_OK)
                goto done;
            consumeWhitespace(vector, ctx);
            error = parse_factors(c, vector, ctx, parts[0], parts[1]);
            if (error != CSS_OK && error != CSS_INVALID)
                goto done;
        } else {
            goto done;
        }
    }
    /* Shorthand defaults differ from initial longhand values: 1 1 0px.
     * auto is a basis value (also valid before factors); none is 0 0 auto. */
    for (size_t i = 0; i < 2; i++) {
        if (parts[i]->used != 0)
            continue;
        error = css__stylesheet_style_vappend(parts[i], 2,
            buildOPV(properties[i], 0, i == 0 ? FLEX_GROW_SET : FLEX_SHRINK_SET), none ? 0 : INTTOFIX(1));
        if (error != CSS_OK)
            goto done;
    }
    if (parts[2]->used == 0) {
        error = css__stylesheet_style_appendOPV(parts[2], CSS_PROP_FLEX_BASIS, 0,
            none ? FLEX_BASIS_AUTO : FLEX_BASIS_SET);
        if (error != CSS_OK)
            goto done;
        if (!none) {
            error = css__stylesheet_style_vappend(parts[2], 2, 0, UNIT_PX);
            if (error != CSS_OK)
                goto done;
        }
    }
    for (size_t i = 0; i < 3; i++) {
        error = css__stylesheet_merge_style(result, parts[i]);
        if (error != CSS_OK)
            goto done;
    }
done:
    for (size_t i = 0; i < 3; i++)
        css__stylesheet_style_destroy(parts[i]);
    if (error != CSS_OK) {
        *ctx = orig_ctx;
        result->used = orig_used;
    }
    return error;
}

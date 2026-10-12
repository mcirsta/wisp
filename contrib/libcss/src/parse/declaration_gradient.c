/*
 * This file is part of LibCSS. Licensed under the MIT License.
 * CSS Images 3 section 7: specified gradient serialization.
 */
#include <stdlib.h>
#include <string.h>

#include "parse/declaration_gradient.h"
#include "parse/declaration_value.h"
#include "parse/colour.h"
#include "parse/property.h"
#include <libcss/gradient.h>

static const css_declaration_token *peek(const css_declaration *source, size_t *cursor)
{
    while (*cursor < source->token_count && source->tokens[*cursor].type == CSS_TOKEN_S)
        (*cursor)++;
    return *cursor < source->token_count ? &source->tokens[*cursor] : NULL;
}

static bool character(const css_declaration_token *token, char ch)
{
    return token != NULL && token->type == CSS_TOKEN_CHAR && lwc_string_length(token->text) == 1 &&
        lwc_string_data(token->text)[0] == ch;
}

/* The native grammar currently accepts a degree angle, one cardinal side, or
 * a radial shape. Omit defaults, but do not normalize authored angles to the
 * painter's approximate direction. This is extraction, not a second grammar. */
static css_error prelude(const css_declaration *source, size_t *cursor, bool radial, lwc_string **out)
{
    const css_declaration_token *token = peek(source, cursor);
    css_error error;
    if (!radial && token != NULL && token->type == CSS_TOKEN_DIMENSION) {
        error = css__declaration_numeric_serialize(token, false, out);
        if (error != CSS_OK)
            return error;
        if (css__keyword_equal(*out, "180deg")) {
            lwc_string_unref(*out);
            *out = NULL;
        }
    } else if (!radial && token != NULL && token->type == CSS_TOKEN_IDENT && css__keyword_equal(token->text, "to")) {
        (*cursor)++;
        token = peek(source, cursor);
        if (token == NULL || token->type != CSS_TOKEN_IDENT)
            return CSS_INVALID;
        const char *text = NULL;
        if (css__keyword_equal(token->text, "top"))
            text = "to top";
        else if (css__keyword_equal(token->text, "left"))
            text = "to left";
        else if (css__keyword_equal(token->text, "right"))
            text = "to right";
        else if (!css__keyword_equal(token->text, "bottom"))
            return CSS_INVALID;
        if (text != NULL && lwc_intern_string(text, strlen(text), out) != lwc_error_ok)
            return CSS_NOMEM;
    } else if (radial && token != NULL && token->type == CSS_TOKEN_IDENT &&
        (css__keyword_equal(token->text, "circle") || css__keyword_equal(token->text, "ellipse"))) {
        if (css__keyword_equal(token->text, "circle") && lwc_intern_string("circle", 6, out) != lwc_error_ok)
            return CSS_NOMEM;
    } else {
        return CSS_OK;
    }
    (*cursor)++;
    if (!character(peek(source, cursor), ','))
        return CSS_INVALID;
    (*cursor)++;
    return CSS_OK;
}

static css_error color_stop(const css_declaration *source, size_t *cursor, bool first, lwc_string **out)
{
    if (peek(source, cursor) == NULL)
        return CSS_INVALID;
    size_t end = css__declaration_component_end(source, *cursor);
    if (end == 0)
        return CSS_INVALID;
    css_declaration color = *source;
    color.tokens += *cursor;
    color.token_count = end - *cursor;
    color.property = css_prop_lookup("color", 5);
    const css_declaration_contribution contribution = {.source = &color};
    lwc_string *parts[2] = {0};
    css_error error = css__declaration_colour_serialize(&contribution, &parts[0]);
    if (error != CSS_OK)
        return error;
    *cursor = end;
    const css_declaration_token *position = peek(source, cursor);
    size_t count = 1;
    if (position != NULL &&
        (position->type == CSS_TOKEN_DIMENSION || position->type == CSS_TOKEN_PERCENTAGE ||
            position->type == CSS_TOKEN_NUMBER)) {
        error = css__declaration_numeric_serialize(position, true, &parts[1]);
        (*cursor)++;
        const css_declaration_token *separator = peek(source, cursor);
        if (error == CSS_OK) {
            /* First zero and last 100% are the stop-fixup defaults. Other
             * authored positions stay specified; never run used-value fixup. */
            bool omitted = (first && css__number_sign(parts[1]) == 0) ||
                (character(separator, ')') && css__keyword_equal(parts[1], "100%"));
            count = omitted ? 1 : 2;
        }
    }
    if (error == CSS_OK)
        error = css__declaration_values_join(parts, count, " ", out);
    for (size_t i = 0; i < 2; i++) {
        if (parts[i] != NULL)
            lwc_string_unref(parts[i]);
    }
    return error;
}

css_error css__declaration_gradient_serialize(const css_declaration *source, lwc_string **out)
{
    bool radial = css__keyword_equal(source->tokens[0].text, "radial-gradient");
    if (!radial && !css__keyword_equal(source->tokens[0].text, "linear-gradient"))
        return CSS_INVALID;
    const char *prefix = radial ? "radial-gradient(" : "linear-gradient(";
    /* Match the native parser's existing bound; this adds no rendering limit. */
    lwc_string *parts[1 + CSS_LINEAR_GRADIENT_MAX_STOPS] = {0};
    size_t cursor = 1, count = 0, stops = 0;
    css_error error = prelude(source, &cursor, radial, &parts[0]);
    if (error != CSS_OK)
        goto done;
    count = parts[0] != NULL ? 1 : 0;
    while (stops < CSS_LINEAR_GRADIENT_MAX_STOPS) {
        error = color_stop(source, &cursor, stops == 0, &parts[count]);
        if (error != CSS_OK)
            goto done;
        stops++;
        count++;
        const css_declaration_token *separator = peek(source, &cursor);
        bool last = character(separator, ')');
        if (!last && !character(separator, ',')) {
            error = CSS_INVALID;
            goto done;
        }
        cursor++;
        if (last)
            break;
    }
    if (stops < 2 || peek(source, &cursor) != NULL || !character(&source->tokens[cursor - 1], ')')) {
        error = CSS_INVALID;
        goto done;
    }
    lwc_string *body = NULL;
    error = css__declaration_values_join(parts, count, ", ", &body);
    if (error == CSS_OK) {
        size_t prefix_length = strlen(prefix), length = lwc_string_length(body);
        char *text = length <= SIZE_MAX - prefix_length - 1 ? malloc(prefix_length + length + 1) : NULL;
        if (text == NULL) {
            error = CSS_NOMEM;
        } else {
            memcpy(text, prefix, prefix_length);
            memcpy(text + prefix_length, lwc_string_data(body), length);
            text[prefix_length + length] = ')';
            if (lwc_intern_string(text, prefix_length + length + 1, out) != lwc_error_ok)
                error = CSS_NOMEM;
            free(text);
        }
        lwc_string_unref(body);
    }
done:
    for (size_t i = 0; i < N_ELEMENTS(parts); i++) {
        if (parts[i] != NULL)
            lwc_string_unref(parts[i]);
    }
    return error;
}

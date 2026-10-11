/*
 * This file is part of LibCSS.
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 */

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "parse/colour.h"
#include "parse/property.h"
#include "parse/properties/utils.h"

/* A bounded, borrowed view shared by the normal parser and captured CSSOM
 * tokens. No reparsing of source strings and no temporary token-vector allocation. */
struct colour_token {
    css_token_type type;
    lwc_string *text;
};

struct rgb_colour {
    double channels[4]; /* RGB 0..255, alpha 0..1, before rendering quantization */
    unsigned missing;
};

static bool character(const struct colour_token *token, char ch)
{
    return token->type == CSS_TOKEN_CHAR && token->text != NULL && lwc_string_length(token->text) == 1 &&
        lwc_string_data(token->text)[0] == ch;
}

/* Colour components are clamped at parse time. Read decimal tokens without
 * locale-sensitive strtod or fixed-point overflow/rounding before scaling.
 * Capping the integral part above the permitted range is safe even for huge
 * tokens; continue scanning to validate the entire spelling. */
static bool component_number(lwc_string *text, double maximum, double *out)
{
    const char *data = lwc_string_data(text);
    size_t length = lwc_string_length(text), pos = 0;
    bool negative = pos < length && data[pos] == '-';
    if (pos < length && (data[pos] == '+' || data[pos] == '-'))
        pos++;
    bool digits = false;
    double value = 0;
    while (pos < length && data[pos] >= '0' && data[pos] <= '9') {
        digits = true;
        value = fmin(maximum + 1, value * 10 + data[pos++] - '0');
    }
    if (pos < length && data[pos] == '.') {
        pos++;
        double place = 0.1;
        while (pos < length && data[pos] >= '0' && data[pos] <= '9') {
            digits = true;
            value += (data[pos++] - '0') * place;
            place *= 0.1;
        }
    }
    if (!digits || pos != length)
        return false;
    *out = negative ? 0 : fmin(value, maximum);
    return true;
}

/* CSS Color 4 §5.1: both names accept either syntax. Legacy syntax requires
 * matching RGB component types and excludes none; modern syntax allows mixing
 * percentages/numbers and uses '/' before alpha. Math/relative forms need their
 * own typed representation and are not accepted here. */
static bool rgb_value(const struct colour_token *tokens, size_t count, struct rgb_colour *out)
{
    if (count < 4)
        return false;
    bool legacy = character(&tokens[1], ',');
    *out = (struct rgb_colour){{0, 0, 0, 1}, 0};
    size_t pos = 0;
    for (size_t i = 0; i < 4; i++) {
        if (pos >= count)
            return false;
        const struct colour_token *token = &tokens[pos++];
        if (!legacy && token->type == CSS_TOKEN_IDENT && css__keyword_equal(token->text, "none")) {
            out->missing |= 1u << i;
            out->channels[i] = 0;
        } else {
            if (token->type != CSS_TOKEN_NUMBER && token->type != CSS_TOKEN_PERCENTAGE)
                return false;
            if (legacy && i < 3 && token->type != tokens[0].type)
                return false;
            bool percentage = token->type == CSS_TOKEN_PERCENTAGE;
            double maximum = percentage ? 100 : i == 3 ? 1 : 255;
            if (!component_number(token->text, maximum, &out->channels[i]))
                return false;
            if (percentage)
                out->channels[i] = out->channels[i] / 100 * (i == 3 ? 1 : 255);
        }
        if (pos >= count)
            return false;
        if (i >= 2 && character(&tokens[pos], ')'))
            return pos + 1 == count;
        if (i == 3)
            return false;
        if (legacy || i == 2) {
            if (!character(&tokens[pos++], legacy ? ',' : '/'))
                return false;
        }
    }
    return false;
}

bool css__parse_rgb_colour(const parserutils_vector *vector, int32_t *ctx, uint32_t *result)
{
    /* Four components, three separators, closing parenthesis. */
    struct colour_token tokens[8];
    size_t count = 0;
    while (count < N_ELEMENTS(tokens)) {
        consumeWhitespace(vector, ctx);
        const css_token *token = parserutils_vector_iterate(vector, ctx);
        if (token == NULL)
            return false;
        tokens[count++] = (struct colour_token){token->type, token->idata};
        if (tokenIsChar(token, ')'))
            break;
    }
    struct rgb_colour colour;
    if (!rgb_value(tokens, count, &colour))
        return false;
    uint32_t red = (uint32_t)floor(colour.channels[0] + 0.5);
    uint32_t green = (uint32_t)floor(colour.channels[1] + 0.5);
    uint32_t blue = (uint32_t)floor(colour.channels[2] + 0.5);
    uint32_t alpha = (uint32_t)floor(colour.channels[3] * 255 + 0.5);
    *result = alpha << 24 | red << 16 | green << 8 | blue;
    return true;
}

/* Values are bounded by 255. Integer formatting keeps the decimal separator
 * independent of LC_NUMERIC; CSSOM permits at most six fractional digits. */
static size_t number(char *out, double value)
{
    unsigned scaled = (unsigned)floor(value * 1000000 + 0.5);
    size_t used = (size_t)sprintf(out, "%u", scaled / 1000000);
    if (scaled % 1000000 != 0) {
        used += (size_t)sprintf(out + used, ".%06u", scaled % 1000000);
        while (out[used - 1] == '0')
            used--;
    }
    return used;
}

static css_error serialize_rgb(const struct rgb_colour *colour, lwc_string **out)
{
    /* Missing components must retain their identity in CSSOM. Their color(srgb)
     * serialization needs proper color() support, not a conversion to zero. */
    if (colour->missing != 0)
        return CSS_INVALID;
    bool opaque = floor(colour->channels[3] * 1000000 + 0.5) == 1000000;
    char buffer[80]; /* Four bounded numbers, separators and function delimiters. */
    size_t used = (size_t)sprintf(buffer, "%s(", opaque ? "rgb" : "rgba");
    for (size_t i = 0; i < (opaque ? 3u : 4u); i++) {
        if (i != 0) {
            buffer[used++] = ',';
            buffer[used++] = ' ';
        }
        used += number(buffer + used, colour->channels[i]);
    }
    buffer[used++] = ')';
    return lwc_intern_string(buffer, used, out) == lwc_error_ok ? CSS_OK : CSS_NOMEM;
}

/* Extract one complete colour value, treating a function as one component.
 * Property grammar was already validated; nested functions remain unsupported
 * by this serializer, but must not shift another border side's component. */
static const css_declaration_token *colour_component(const css_declaration_contribution *value, size_t *end)
{
    const css_declaration *source = value->source;
    size_t starts[4], ends[4], count = 0, pos = 0;
    while (pos < source->token_count) {
        if (source->tokens[pos].type == CSS_TOKEN_S) {
            pos++;
            continue;
        }
        if (count == N_ELEMENTS(starts))
            return NULL;
        starts[count] = pos;
        unsigned depth = source->tokens[pos++].type == CSS_TOKEN_FUNCTION ? 1 : 0;
        while (pos < source->token_count && depth != 0) {
            const css_declaration_token *token = &source->tokens[pos++];
            if (token->type == CSS_TOKEN_FUNCTION)
                depth++;
            else if (token->type == CSS_TOKEN_CHAR && lwc_string_length(token->text) == 1) {
                if (lwc_string_data(token->text)[0] == '(')
                    depth++;
                else if (lwc_string_data(token->text)[0] == ')')
                    depth--;
            }
        }
        if (depth != 0)
            return NULL;
        ends[count++] = pos;
    }
    size_t arity = source->property->value_kind == CSS_DECL_VALUE_QUAD_COLOUR ? 4 : 1;
    if (count == 0 || count > arity)
        return NULL;
    size_t component = value->component;
    if (component >= count)
        component = count == 1 ? 0 : component == 2 ? 0 : 1;
    *end = ends[component];
    return &source->tokens[starts[component]];
}

css_error css__declaration_colour_serialize(const css_declaration_contribution *value, lwc_string **out)
{
    size_t end;
    const css_declaration_token *token = colour_component(value, &end);
    if (token == NULL)
        return CSS_INVALID;
    struct rgb_colour colour = {{0, 0, 0, 1}, 0};
    if (token->type == CSS_TOKEN_HASH || token->type == CSS_TOKEN_IDENT || token->type == CSS_TOKEN_NUMBER ||
        token->type == CSS_TOKEN_DIMENSION) {
        uint32_t packed;
        css_error error = css__parse_hash_colour(token->text, &packed);
        if (error != CSS_OK) {
            if (token->type == CSS_TOKEN_IDENT)
                return lwc_string_tolower(token->text, out) == lwc_error_ok ? CSS_OK : CSS_NOMEM;
            return error;
        }
        /* Accepted hashless hex tokens in quirks mode still serialize as RGB,
         * not as a made-up named colour or an untyped number. */
        colour.channels[0] = (packed >> 16) & 255;
        colour.channels[1] = (packed >> 8) & 255;
        colour.channels[2] = packed & 255;
        /* CSS Color 4 §16.1.1: hex alpha is exactly eight bits. Prefer the
         * integer percentage that round-trips, otherwise use three decimals. */
        unsigned alpha = packed >> 24;
        unsigned percent = (alpha * 100 + 127) / 255;
        colour.channels[3] = (percent * 255 + 50) / 100 == alpha ? percent / 100.0
                                                                 : ((alpha * 1000 + 127) / 255) / 1000.0;
    } else if (token->type == CSS_TOKEN_FUNCTION &&
        (css__keyword_equal(token->text, "rgb") || css__keyword_equal(token->text, "rgba"))) {
        struct colour_token tokens[8];
        size_t count = 0;
        for (token++; token < value->source->tokens + end; token++) {
            if (token->type == CSS_TOKEN_S)
                continue;
            if (count == N_ELEMENTS(tokens))
                return CSS_INVALID;
            tokens[count++] = (struct colour_token){token->type, token->text};
        }
        if (!rgb_value(tokens, count, &colour))
            return CSS_INVALID;
    } else {
        return CSS_INVALID;
    }
    return serialize_rgb(&colour, out);
}

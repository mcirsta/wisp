/*
 * This file is part of LibCSS.
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 */

#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bytecode/bytecode.h"
#include "parse/colour.h"
#include "parse/declaration_value.h"
#include "parse/property.h"
#include "parse/properties/utils.h"

/* A bounded, borrowed view shared by the normal parser and captured CSSOM
 * tokens. No reparsing of source strings and no temporary token-vector allocation. */
struct colour_token {
    css_token_type type;
    lwc_string *text;
};

struct colour_value {
    /* Parsed coordinates remain in their original RGB/HSL/HWB space, alpha
     * 0..1. Convert only at the rendering or legacy RGB serialization boundary. */
    double channels[4];
    unsigned missing;
};

static bool character(const struct colour_token *token, char ch)
{
    return token->type == CSS_TOKEN_CHAR && token->text != NULL && lwc_string_length(token->text) == 1 &&
        lwc_string_data(token->text)[0] == ch;
}

/* Locale-independent decimal tokens. Saturate before scaling rather than
 * overflowing the old fixed-point representation. Return the numeric prefix
 * length so angle dimensions use the same reader as numbers/percentages. */
static size_t decimal_number(lwc_string *text, double maximum, double *out)
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
        value = fmin(maximum, value * 10 + data[pos++] - '0');
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
    *out = negative ? -value : value;
    return digits ? pos : 0;
}

static bool component_number(lwc_string *text, double maximum, double *out)
{
    size_t consumed = decimal_number(text, maximum, out);
    if (consumed == 0 || consumed != lwc_string_length(text))
        return false;
    *out = fmax(0, fmin(*out, maximum));
    return true;
}

/* CSS Color 4 §4.3: normalize in the authored angle unit before converting to
 * degrees. This avoids overflow when a large number of turns is multiplied. */
static bool hue_number(const struct colour_token *token, double *out)
{
    if (token->type != CSS_TOKEN_NUMBER && token->type != CSS_TOKEN_DIMENSION)
        return false;
    size_t consumed = decimal_number(token->text, DBL_MAX, out);
    if (consumed == 0)
        return false;
    double period = 360;
    size_t length = lwc_string_length(token->text);
    if (token->type == CSS_TOKEN_DIMENSION) {
        uint32_t unit;
        if (css__parse_unit_keyword(lwc_string_data(token->text) + consumed, length - consumed, &unit) != CSS_OK)
            return false;
        switch (unit) {
        case UNIT_DEG: break;
        case UNIT_GRAD: period = 400; break;
        case UNIT_TURN: period = 1; break;
        case UNIT_RAD: period = 6.28318530717958647693; break;
        default: return false;
        }
    } else if (consumed != length) {
        return false;
    }
    *out = fmod(*out, period) / period * 360;
    if (*out < 0)
        *out += 360;
    return true;
}

/* CSS Color 4 §5.1: both names accept either syntax. Legacy syntax requires
 * matching RGB component types and excludes none; modern syntax allows mixing
 * percentages/numbers and uses '/' before alpha. Math/relative forms need their
 * own typed representation and are not accepted here. */
static bool rgb_value(const struct colour_token *tokens, size_t count, struct colour_value *out)
{
    if (count < 4)
        return false;
    bool legacy = character(&tokens[1], ',');
    *out = (struct colour_value){{0, 0, 0, 1}, 0};
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

enum colour_function {
    COLOUR_RGB,
    COLOUR_HSL,
    COLOUR_HWB,
    COLOUR_UNSUPPORTED
};

static enum colour_function colour_function(lwc_string *name)
{
    if (css__keyword_equal(name, "rgb") || css__keyword_equal(name, "rgba"))
        return COLOUR_RGB;
    if (css__keyword_equal(name, "hsl") || css__keyword_equal(name, "hsla"))
        return COLOUR_HSL;
    if (css__keyword_equal(name, "hwb"))
        return COLOUR_HWB;
    return COLOUR_UNSUPPORTED;
}

/* CSS Color 4 §7.1 and §8.1. Keep fractional components until the rendering
 * boundary. In particular HWB white/black normalize together, not individually. */
static void polar_to_rgb(enum colour_function function, struct colour_value *colour)
{
    double hue = colour->channels[0];
    double first = colour->channels[1] / 100;
    double second = colour->channels[2] / 100;
    if (function == COLOUR_HWB && first + second >= 1) {
        double gray = first / (first + second) * 255;
        colour->channels[0] = colour->channels[1] = colour->channels[2] = gray;
        return;
    }
    double light = function == COLOUR_HSL ? second : 0.5;
    double amplitude = function == COLOUR_HSL ? first * fmin(light, 1 - light) : 0.5;
    /* Express the hue as a sector and its fractional offset. Unlike adding
     * large channel offsets to hue/30, this preserves precision near the
     * channel's half-byte rounding boundaries. */
    double sector = hue / 60;
    unsigned sextant = (unsigned)sector % 6;
    double fraction = sector - floor(sector);
    double levels[] = {0, sextant & 1 ? 1 - fraction : fraction, 1};
    static const unsigned order[6][3] = {{2, 1, 0}, {1, 2, 0}, {0, 2, 1},
        {0, 1, 2}, {1, 0, 2}, {2, 0, 1}};
    for (size_t i = 0; i < 3; i++) {
        double pure = levels[order[sextant][i]];
        double channel;
        if (function == COLOUR_HWB) {
            channel = pure * (1 - first - second) + first;
        } else {
            double slope = 2 * pure - 1;
            /* A huge amplitude can overflow; zero slope still means lightness. */
            channel = slope == 0 ? light : light + amplitude * slope;
        }
        colour->channels[i] = channel * 255;
    }
}

static bool polar_value(enum colour_function function, const struct colour_token *tokens, size_t count,
    struct colour_value *out)
{
    if (count < 4)
        return false;
    bool legacy = character(&tokens[1], ',');
    if (legacy && function == COLOUR_HWB)
        return false;
    *out = (struct colour_value){{0, 0, 0, 1}, 0};
    size_t pos = 0;
    for (size_t i = 0; i < 4; i++) {
        if (pos >= count)
            return false;
        const struct colour_token *token = &tokens[pos++];
        if (!legacy && token->type == CSS_TOKEN_IDENT && css__keyword_equal(token->text, "none")) {
            out->missing |= 1u << i;
            out->channels[i] = 0;
        } else if (i == 0) {
            if (!hue_number(token, &out->channels[i]))
                return false;
        } else {
            if (token->type != CSS_TOKEN_NUMBER && token->type != CSS_TOKEN_PERCENTAGE)
                return false;
            if (legacy && i < 3 && token->type != CSS_TOKEN_PERCENTAGE)
                return false;
            if (i == 3) {
                bool percentage = token->type == CSS_TOKEN_PERCENTAGE;
                if (!component_number(token->text, percentage ? 100 : 1, &out->channels[i]))
                    return false;
                if (percentage)
                    out->channels[i] /= 100;
            } else {
                size_t consumed = decimal_number(token->text, DBL_MAX, &out->channels[i]);
                if (consumed == 0 || consumed != lwc_string_length(token->text))
                    return false;
                /* Only negative HSL saturation is clamped at parse time. */
                if (function == COLOUR_HSL && i == 1)
                    out->channels[i] = fmax(0, out->channels[i]);
            }
        }
        if (pos >= count)
            return false;
        if (i >= 2 && character(&tokens[pos], ')')) {
            if (pos + 1 != count)
                return false;
            return true;
        }
        if (i == 3)
            return false;
        if (legacy || i == 2) {
            if (!character(&tokens[pos++], legacy ? ',' : '/'))
                return false;
        }
    }
    return false;
}

static bool function_value(enum colour_function function, const struct colour_token *tokens, size_t count,
    struct colour_value *out)
{
    if (function == COLOUR_RGB)
        return rgb_value(tokens, count, out);
    if (function == COLOUR_HSL || function == COLOUR_HWB)
        return polar_value(function, tokens, count, out);
    return false;
}

static uint32_t channel_byte(double value)
{
    /* The existing painter/bytecode is ARGB8. Wider gamut mapping needs a
     * separate representation; CSSOM must never read back this clipped value. */
    return (uint32_t)floor(fmax(0, fmin(value, 255)) + 0.5);
}

bool css__parse_colour_function(lwc_string *name, const parserutils_vector *vector, int32_t *ctx, uint32_t *result)
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
    enum colour_function function = colour_function(name);
    struct colour_value colour;
    if (!function_value(function, tokens, count, &colour))
        return false;
    if (function == COLOUR_HSL || function == COLOUR_HWB)
        polar_to_rgb(function, &colour);
    uint32_t red = channel_byte(colour.channels[0]);
    uint32_t green = channel_byte(colour.channels[1]);
    uint32_t blue = channel_byte(colour.channels[2]);
    uint32_t alpha = channel_byte(colour.channels[3] * 255);
    *result = alpha << 24 | red << 16 | green << 8 | blue;
    return true;
}

/* Values are bounded by 360. Integer formatting keeps the decimal separator
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

/* Assemble modern color syntax after serializing its individual components.
 * A missing alpha is a real fourth component; an omitted alpha is NULL. */
static css_error serialize_modern(const char *name, lwc_string *const parts[4], lwc_string **out)
{
    size_t count = parts[3] == NULL ? 3 : 4;
    size_t length = strlen(name) + (count == 3 ? 4 : 7); /* Delimiters and separators. */
    for (size_t i = 0; i < count; i++) {
        size_t n = lwc_string_length(parts[i]);
        if (n > SIZE_MAX - length)
            return CSS_NOMEM;
        length += n;
    }
    char *buffer = malloc(length);
    if (buffer == NULL)
        return CSS_NOMEM;
    size_t used = strlen(name);
    memcpy(buffer, name, used);
    buffer[used++] = '(';
    for (size_t i = 0; i < count; i++) {
        if (i != 0)
            buffer[used++] = ' ';
        if (i == 3) {
            buffer[used++] = '/';
            buffer[used++] = ' ';
        }
        size_t n = lwc_string_length(parts[i]);
        memcpy(buffer + used, lwc_string_data(parts[i]), n);
        used += n;
    }
    buffer[used++] = ')';
    css_error error = lwc_intern_string(buffer, used, out) == lwc_error_ok ? CSS_OK : CSS_NOMEM;
    free(buffer);
    return error;
}

/* CSS Color 4 section 16.2.2 preserves HSL/HWB none in the original space.
 * The parser has validated modern syntax, so the first three tokens are the
 * coordinates. Unbounded percentages use the shared decimal-token serializer,
 * not a double or a rendering integer; only hue/alpha need normalized numbers. */
static css_error serialize_missing_polar(enum colour_function function, const struct colour_token *tokens,
    const struct colour_value *colour, lwc_string **out)
{
    lwc_string *parts[4] = {0};
    css_error error = CSS_OK;
    for (size_t i = 0; i < 4; i++) {
        if (colour->missing & (1u << i)) {
            error = lwc_intern_string("none", 4, &parts[i]) == lwc_error_ok ? CSS_OK : CSS_NOMEM;
        } else if (i == 0 || i == 3) {
            char buffer[16]; /* Bounded hue/alpha, at most six fractional digits. */
            double value = colour->channels[i];
            if (i == 0 && floor(value * 1000000 + 0.5) == 360000000)
                value = 0; /* Rounding must not produce a noncanonical full turn. */
            size_t length = number(buffer, value);
            if (i == 3 && length == 1 && buffer[0] == '1')
                continue;
            error = lwc_intern_string(buffer, length, &parts[i]) == lwc_error_ok ? CSS_OK : CSS_NOMEM;
        } else if (function == COLOUR_HSL && i == 1 && css__number_sign(tokens[i].text) < 0) {
            error = lwc_intern_string("0%", 2, &parts[i]) == lwc_error_ok ? CSS_OK : CSS_NOMEM;
        } else {
            css_declaration_token percentage = {.type = CSS_TOKEN_PERCENTAGE, .text = tokens[i].text};
            error = css__declaration_numeric_serialize(&percentage, false, &parts[i]);
        }
        if (error != CSS_OK)
            break;
    }
    if (error == CSS_OK)
        error = serialize_modern(function == COLOUR_HSL ? "hsl" : "hwb", parts, out);
    for (size_t i = 0; i < N_ELEMENTS(parts); i++) {
        if (parts[i] != NULL)
            lwc_string_unref(parts[i]);
    }
    return error;
}

static css_error serialize_rgb(const struct colour_value *colour, lwc_string **out)
{
    /* Missing RGB components need color(srgb ...) support before readback can
     * round-trip. HSL/HWB missing values take their own modern syntax path. */
    if (colour->missing != 0)
        return CSS_INVALID;
    for (size_t i = 0; i < 3; i++) {
        if (!isfinite(colour->channels[i]) || colour->channels[i] < 0 || colour->channels[i] > 255)
            return CSS_INVALID; /* Do not freeze the painter's out-of-gamut clipping into CSSOM. */
    }
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
        pos = css__declaration_component_end(source, pos);
        if (pos == 0)
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
    struct colour_value colour = {{0, 0, 0, 1}, 0};
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
    } else if (token->type == CSS_TOKEN_FUNCTION) {
        enum colour_function function = colour_function(token->text);
        struct colour_token tokens[8];
        size_t count = 0;
        for (token++; token < value->source->tokens + end; token++) {
            if (token->type == CSS_TOKEN_S)
                continue;
            if (count == N_ELEMENTS(tokens))
                return CSS_INVALID;
            tokens[count++] = (struct colour_token){token->type, token->text};
        }
        if (!function_value(function, tokens, count, &colour))
            return CSS_INVALID;
        if (function == COLOUR_HSL || function == COLOUR_HWB) {
            if (colour.missing != 0)
                return serialize_missing_polar(function, tokens, &colour, out);
            polar_to_rgb(function, &colour);
        }
    } else {
        return CSS_INVALID;
    }
    return serialize_rgb(&colour, out);
}

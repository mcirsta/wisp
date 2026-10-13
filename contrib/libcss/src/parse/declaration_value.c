/*
 * This file is part of LibCSS.
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 */

#include <stdlib.h>
#include <string.h>

#include "parse/declaration_value.h"
#include "parse/declaration_background.h"
#include "parse/declaration_font.h"
#include "parse/calc.h"
#include "parse/colour.h"
#include "parse/declaration_text.h"
#include "parse/property.h"
#include "parse/transform.h"
#include "parse/white_space.h"
#include "parse/properties/utils.h"
#include "utils/css_utils.h"

static css_error intern(const char *text, size_t length, lwc_string **out)
{
    return lwc_intern_string(text, length, out) == lwc_error_ok ? CSS_OK : CSS_NOMEM;
}

static const char *wide_keyword(const css_declaration *source)
{
    if (source->token_count != 1 || source->tokens[0].type != CSS_TOKEN_IDENT)
        return NULL;
    static const char *const keywords[] = {"inherit", "initial", "unset", "revert", "revert-layer"};
    for (size_t i = 0; i < sizeof(keywords) / sizeof(keywords[0]); i++) {
        if (css__keyword_equal(source->tokens[0].text, keywords[i]))
            return keywords[i];
    }
    return NULL;
}

/* CSS Variables 1 §4.1: retain spelling; an empty custom value serializes
 * as one space so passing it to setProperty does not remove the property. */
static css_error serialize_unparsed(const css_declaration *source, lwc_string **out)
{
    if (source->value_start == source->value_end)
        return intern(" ", 1, out);
    return intern(
        lwc_string_data(source->source_value) + source->value_start, source->value_end - source->value_start, out);
}

static bool digit(char ch)
{
    return ch >= '0' && ch <= '9';
}

/* CSSOM §6.7.2 numbers use decimal notation and at most six fractional digits. Work
 * directly on the parsed token's decimal spelling: rendering bytecode has only
 * ten fractional bits, and strtod/snprintf would introduce locale and binary
 * rounding differences. The current property parser does not accept exponents.
 * This is serialization after validation, not another property parser. */
static css_error serialize_numeric(
    const css_declaration_token *token, const char *default_unit, bool ties_up, lwc_string **out)
{
    const char *text = lwc_string_data(token->text);
    size_t length = lwc_string_length(token->text);
    if (length > SIZE_MAX - 10)
        return CSS_NOMEM;
    char *buffer = malloc(length + 10);
    if (buffer == NULL)
        return CSS_NOMEM;

    size_t pos = 0;
    bool negative = text[pos] == '-';
    if (negative || text[pos] == '+')
        pos++;
    size_t first = pos;
    while (pos < length && digit(text[pos]))
        pos++;
    size_t whole_end = pos;
    while (first < whole_end && text[first] == '0')
        first++;

    /* One spare leading digit absorbs a carry from 999.9999999. */
    size_t used = 1;
    if (first == whole_end) {
        buffer[used++] = '0';
    } else {
        memcpy(buffer + used, text + first, whole_end - first);
        used += whole_end - first;
    }
    size_t fraction = used;
    bool round_up = false, negative_tie = false;
    if (pos < length && text[pos] == '.') {
        buffer[used++] = '.';
        pos++;
        size_t start = pos;
        while (pos < length && digit(text[pos])) {
            size_t offset = pos - start;
            if (offset < 6)
                buffer[used++] = text[pos];
            else if (offset == 6) {
                negative_tie = ties_up && negative && text[pos] == '5';
                round_up = text[pos] >= '5' && !negative_tie;
            } else if (negative_tie && text[pos] != '0') {
                round_up = true;
            }
            pos++;
        }
    }
    size_t begin = 1;
    if (round_up) {
        size_t i = used;
        while (i > 1) {
            i--;
            if (buffer[i] == '.')
                continue;
            if (buffer[i] != '9') {
                buffer[i]++;
                round_up = false;
                break;
            }
            buffer[i] = '0';
        }
        if (round_up) {
            buffer[0] = '1';
            begin = 0;
        }
    }
    if (used > fraction) {
        while (used > fraction + 1 && buffer[used - 1] == '0')
            used--;
        if (used == fraction + 1)
            used = fraction;
    }
    if (used - begin == 1 && buffer[begin] == '0')
        negative = false;
    size_t number_length = used - begin;
    memmove(buffer + (negative ? 1 : 0), buffer + begin, number_length);
    if (negative)
        buffer[0] = '-';
    used = number_length + (negative ? 1 : 0);

    if (token->type == CSS_TOKEN_PERCENTAGE) {
        buffer[used++] = '%';
    } else if (token->type == CSS_TOKEN_DIMENSION) {
        /* A validated dimension has a decoded, case-insensitive CSS unit. */
        for (; pos < length; pos++) {
            char ch = text[pos];
            buffer[used++] = ch >= 'A' && ch <= 'Z' ? ch + ('a' - 'A') : ch;
        }
    } else if (default_unit != NULL) {
        size_t unit_length = strlen(default_unit);
        memcpy(buffer + used, default_unit, unit_length);
        used += unit_length;
    }
    css_error error = intern(buffer, used, out);
    free(buffer);
    return error;
}

css_error css__declaration_numeric_serialize(const css_declaration_token *token, bool length_value, lwc_string **out)
{
    return serialize_numeric(token, length_value ? "px" : NULL, false, out);
}

/* Scale the decimal coefficient as digits, then place its decimal point.
 * Multiplication and unit conversion precede rounding; large authored values
 * never pass through double or rendering fixed point. */
static css_error numeric_scaled(const css_declaration_token *token, unsigned multiplier, unsigned places,
    const char *unit, bool ties_up, lwc_string **out)
{
    const char *text = lwc_string_data(token->text);
    size_t length = lwc_string_length(token->text);
    if (length > SIZE_MAX - places - 16)
        return CSS_NOMEM;
    size_t capacity = length + places + 16;
    char *buffer = malloc(capacity);
    if (buffer == NULL)
        return CSS_NOMEM;
    size_t first = (text[0] == '+' || text[0] == '-') ? 1 : 0;
    size_t end = first, fraction = 0;
    while (end < length && digit(text[end]))
        end++;
    if (end < length && text[end] == '.') {
        size_t start = ++end;
        while (end < length && digit(text[end]))
            end++;
        fraction = end - start;
    }
    size_t coefficient = capacity;
    uint64_t carry = 0;
    for (size_t i = end; i > first;) {
        char ch = text[--i];
        if (ch == '.')
            continue;
        carry += (uint64_t)(ch - '0') * multiplier;
        buffer[--coefficient] = '0' + carry % 10;
        carry /= 10;
    }
    while (carry != 0) {
        buffer[--coefficient] = '0' + carry % 10;
        carry /= 10;
    }
    size_t digits = capacity - coefficient, decimals = fraction + places, used = first;
    if (first != 0)
        buffer[0] = text[0];
    size_t whole = digits > decimals ? digits - decimals : 0;
    if (whole != 0) {
        memmove(buffer + used, buffer + coefficient, whole);
        used += whole;
    } else {
        buffer[used++] = '0';
    }
    if (decimals != 0) {
        buffer[used++] = '.';
        for (size_t i = digits; i < decimals; i++)
            buffer[used++] = '0';
        memmove(buffer + used, buffer + coefficient + whole, digits - whole);
        used += digits - whole;
    }
    css_declaration_token number = {.type = CSS_TOKEN_NUMBER};
    css_error error = intern(buffer, used, &number.text);
    free(buffer);
    if (error == CSS_OK) {
        error = serialize_numeric(&number, unit, ties_up, out);
        lwc_string_unref(number.text);
    }
    return error;
}

css_error css__declaration_numeric_divide(
    const css_declaration_token *token, unsigned int places, const char *unit, lwc_string **out)
{
    return numeric_scaled(token, 1, places, unit, false, out);
}

css_error css__declaration_colour_number(
    const css_declaration_token *token, unsigned multiplier, unsigned places, lwc_string **out)
{
    return numeric_scaled(token, multiplier, places, NULL, true, out);
}

struct value_format {
    const char *prefix, *separator, *suffix;
};

/* Combine a list of already serialized components. Both shorthand and
 * structured longhand values use the same overflow-checked output path. */
static css_error join_values(lwc_string *const *values, size_t count, const struct value_format *format,
    lwc_string **out)
{
    size_t prefix = strlen(format->prefix), separator = strlen(format->separator), suffix = strlen(format->suffix);
    if (count == 1 && prefix == 0 && suffix == 0) {
        *out = lwc_string_ref(values[0]);
        return CSS_OK;
    }
    /* Formats are static; comma-separated values can have arbitrary length. */
    size_t length = prefix + suffix;
    if (separator != 0 && count - 1 > (SIZE_MAX - length) / separator)
        return CSS_NOMEM;
    length += (count - 1) * separator;
    for (size_t i = 0; i < count; i++) {
        size_t n = lwc_string_length(values[i]);
        if (n > SIZE_MAX - length)
            return CSS_NOMEM;
        length += n;
    }
    char *buffer = malloc(length);
    if (buffer == NULL)
        return CSS_NOMEM;
    memcpy(buffer, format->prefix, prefix);
    size_t pos = prefix;
    for (size_t i = 0; i < count; i++) {
        if (i != 0) {
            memcpy(buffer + pos, format->separator, separator);
            pos += separator;
        }
        size_t n = lwc_string_length(values[i]);
        memcpy(buffer + pos, lwc_string_data(values[i]), n);
        pos += n;
    }
    memcpy(buffer + pos, format->suffix, suffix);
    css_error error = intern(buffer, length, out);
    free(buffer);
    return error;
}

css_error css__declaration_values_join(lwc_string *const *values, size_t count, const char *separator, lwc_string **out)
{
    if (out == NULL)
        return CSS_BADPARM;
    *out = NULL;
    if (values == NULL || count == 0 || separator == NULL)
        return CSS_BADPARM;
    const struct value_format format = {"", separator, ""};
    return join_values(values, count, &format, out);
}

/* These declarations have already passed their property grammar. Extract the
 * scalar components only; unsupported functions must remain explicit errors,
 * not be serialized from a rounded rendering value. */
static css_error geometry_components(const css_declaration *source, const css_declaration_token **parts,
    size_t *count, bool *automatic)
{
    enum css_declaration_value_kind kind = source->property->value_kind;
    *count = 0;
    *automatic = false;
    for (size_t i = 0; i < source->token_count; i++) {
        const css_declaration_token *token = &source->tokens[i];
        if (token->type == CSS_TOKEN_S)
            continue;
        if (kind == CSS_DECL_VALUE_ASPECT_RATIO && token->type == CSS_TOKEN_IDENT &&
            css__keyword_equal(token->text, "auto")) {
            *automatic = true;
            continue;
        }
        if (kind == CSS_DECL_VALUE_CLIP && i == 0 && token->type == CSS_TOKEN_FUNCTION &&
            css__keyword_equal(token->text, "rect"))
            continue;
        if (token->type == CSS_TOKEN_CHAR && lwc_string_length(token->text) == 1) {
            char ch = lwc_string_data(token->text)[0];
            if ((kind == CSS_DECL_VALUE_ASPECT_RATIO && ch == '/') ||
                (kind == CSS_DECL_VALUE_CLIP && (ch == ',' || ch == ')')))
                continue;
        }
        if (*count == (kind == CSS_DECL_VALUE_CLIP ? 4u : 2u))
            return CSS_INVALID;
        if (token->type != CSS_TOKEN_NUMBER &&
            !(kind != CSS_DECL_VALUE_ASPECT_RATIO && token->type == CSS_TOKEN_DIMENSION) &&
            !(kind == CSS_DECL_VALUE_CLIP && token->type == CSS_TOKEN_IDENT &&
                css__keyword_equal(token->text, "auto")))
            return CSS_INVALID;
        parts[(*count)++] = token;
    }
    return CSS_OK;
}

static css_error serialize_geometry(const css_declaration *source, lwc_string **out)
{
    const css_declaration_token *parts[4] = {0};
    size_t count;
    bool automatic;
    css_error error = geometry_components(source, parts, &count, &automatic);
    if (error != CSS_OK)
        return error;
    enum css_declaration_value_kind kind = source->property->value_kind;
    bool ratio = kind == CSS_DECL_VALUE_ASPECT_RATIO;
    if (ratio && count == 0)
        return automatic ? intern("auto", 4, out) : CSS_INVALID;
    if (kind == CSS_DECL_VALUE_CLIP && count == 1 && parts[0]->type == CSS_TOKEN_IDENT)
        return intern("auto", 4, out);
    if (count == 0 || (kind == CSS_DECL_VALUE_CLIP && count != 4))
        return CSS_INVALID;
    /* CSS Values gives an omitted ratio denominator the value 1. CSSOM always
     * writes both components; it does not reduce ratios or replace 0/0 by auto. */
    if (ratio && count == 1)
        count = 2;
    lwc_string *values[4] = {0};
    for (size_t i = 0; i < count; i++) {
        if (parts[i] == NULL)
            error = intern("1", 1, &values[i]);
        else if (parts[i]->type == CSS_TOKEN_IDENT)
            error = intern("auto", 4, &values[i]);
        else
            error = css__declaration_numeric_serialize(parts[i], !ratio, &values[i]);
        if (error != CSS_OK)
            goto done;
    }
    size_t used = count;
    struct value_format format = {"", " ", ""};
    if (ratio) {
        format.prefix = automatic ? "auto " : "";
        format.separator = " / ";
    } else if (kind == CSS_DECL_VALUE_CLIP) {
        format = (struct value_format){"rect(", ", ", ")"};
    } else if (count == 2 && values[0] == values[1]) {
        used = 1;
    }
    error = join_values(values, used, &format, out);
done:
    for (size_t i = 0; i < count; i++) {
        if (values[i] != NULL)
            lwc_string_unref(values[i]);
    }
    return error;
}

static size_t scalar_arity(enum css_declaration_value_kind kind)
{
    switch (kind) {
    case CSS_DECL_VALUE_PAIR_LENGTH:
    case CSS_DECL_VALUE_PAIR_KEYWORD:
    case CSS_DECL_VALUE_PAIR_TIME:
    case CSS_DECL_VALUE_PAIR_URI:
        return 2;
    case CSS_DECL_VALUE_QUAD_LENGTH:
    case CSS_DECL_VALUE_QUAD_KEYWORD:
    case CSS_DECL_VALUE_QUAD_COLOUR:
        return 4;
    default:
        return 1;
    }
}

/* Locate whole scalar/math components, never splitting a function at its spaces. */
static bool scalar_component(const css_declaration_contribution *value, size_t arity, css_declaration *part)
{
    const css_declaration *source = value->source;
    size_t starts[4], lengths[4];
    size_t count = 0;
    for (size_t i = 0; i < source->token_count;) {
        const css_declaration_token *token = &source->tokens[i];
        if (token->type == CSS_TOKEN_S) {
            i++;
            continue;
        }
        if (count == arity ||
            (token->type != CSS_TOKEN_IDENT && token->type != CSS_TOKEN_NUMBER && token->type != CSS_TOKEN_PERCENTAGE &&
                token->type != CSS_TOKEN_DIMENSION && token->type != CSS_TOKEN_URI &&
                !(token->type == CSS_TOKEN_FUNCTION && css__keyword_equal(token->text, "calc"))))
            return false;
        size_t end = css__declaration_component_end(source, i);
        if (end == 0)
            return false;
        starts[count] = i;
        lengths[count++] = end - i;
        i = end;
    }
    if (count == 0)
        return false;
    size_t component = value->component;
    if (component >= count)
        component = count == 1 ? 0 : component == 2 ? 0 : 1;
    *part = *source;
    part->tokens += starts[component];
    part->token_count = lengths[component];
    return true;
}

static css_error serialize_white_space_component(const css_declaration_contribution *value, lwc_string **out)
{
    lwc_string *parts[2];
    size_t count = 0;
    const css_declaration *source = value->source;
    for (size_t i = 0; i < source->token_count; i++) {
        const css_declaration_token *token = &source->tokens[i];
        if (token->type == CSS_TOKEN_S)
            continue;
        if (count == 2 || token->type != CSS_TOKEN_IDENT)
            return CSS_INVALID;
        parts[count++] = token->text;
    }
    uint16_t values[2];
    if (value->component >= 2 || !css__white_space_values(parts, count, values))
        return CSS_INVALID;
    const char *text = css__white_space_component(value->component, values[value->component]);
    return intern(text, strlen(text), out);
}

/* Within the accepted scalar-product calc grammar a dimension/percentage can
 * never cancel its type. This identifies shorthand components without needing
 * to evaluate or serialize unrelated components (including non-finite math). */
static bool calc_number(const css_declaration_token *tokens, size_t count)
{
    for (size_t i = 0; i < count; i++) {
        if (tokens[i].type == CSS_TOKEN_DIMENSION || tokens[i].type == CSS_TOKEN_PERCENTAGE)
            return false;
    }
    return true;
}

/* Expand validated flex shorthands into their specified scalar components.
 * Numeric spellings retain CSSOM precision, independently of layout bytecode. */
static css_error serialize_flex_component(const css_declaration_contribution *value, lwc_string **out)
{
    const css_declaration *source = value->source;
    bool flow = source->property->value_kind == CSS_DECL_VALUE_FLEX_FLOW;
    const css_declaration_token *parts[3] = {0};
    size_t lengths[3] = {0};
    const char *defaults[3] = {"1", "1", "0px"};
    if (flow) {
        defaults[0] = "row";
        defaults[1] = "nowrap";
    }
    size_t factors = 0;
    for (size_t i = 0; i < source->token_count;) {
        const css_declaration_token *token = &source->tokens[i];
        if (token->type == CSS_TOKEN_S) {
            i++;
            continue;
        }
        size_t end = css__declaration_component_end(source, i);
        if (end == 0)
            return CSS_INVALID;
        size_t length = end - i;
        i = end;
        bool math = token->type == CSS_TOKEN_FUNCTION && css__keyword_equal(token->text, "calc");
        size_t component;
        if (flow) {
            if (token->type != CSS_TOKEN_IDENT)
                return CSS_INVALID;
            component = css__keyword_equal(token->text, "row") || css__keyword_equal(token->text, "row-reverse") ||
                css__keyword_equal(token->text, "column") || css__keyword_equal(token->text, "column-reverse") ? 0 : 1;
        } else if (token->type == CSS_TOKEN_IDENT && css__keyword_equal(token->text, "none")) {
            defaults[0] = defaults[1] = "0";
            defaults[2] = "auto";
            continue;
        } else if ((token->type == CSS_TOKEN_NUMBER || (math && calc_number(token, length))) && factors < 2) {
            component = factors++;
        } else if (token->type == CSS_TOKEN_NUMBER || token->type == CSS_TOKEN_DIMENSION ||
            token->type == CSS_TOKEN_PERCENTAGE || token->type == CSS_TOKEN_IDENT || math) {
            component = 2;
        } else {
            return CSS_INVALID;
        }
        if (parts[component] != NULL)
            return CSS_INVALID;
        parts[component] = token;
        lengths[component] = length;
    }
    size_t component = value->component;
    const css_declaration_token *token = parts[component];
    if (token == NULL)
        return intern(defaults[component], strlen(defaults[component]), out);
    if (token->type == CSS_TOKEN_IDENT)
        return lwc_string_tolower(token->text, out) == lwc_error_ok ? CSS_OK : CSS_NOMEM;
    if (token->type == CSS_TOKEN_FUNCTION)
        return css__declaration_calc_serialize(token, lengths[component], out);
    return css__declaration_numeric_serialize(token, component == 2, out);
}

/* Expand already validated unordered list/column values. Ambiguous none and
 * auto tokens supply defaults only after concrete components have been found. */
static css_error serialize_list_or_columns_component(const css_declaration_contribution *value, lwc_string **out)
{
    const css_declaration *source = value->source;
    bool columns = source->property->value_kind == CSS_DECL_VALUE_COLUMNS;
    const css_declaration_token *parts[3] = {0};
    size_t lengths[3] = {0};
    const char *defaults[3] = {"disc", "outside", "none"};
    if (columns)
        defaults[0] = defaults[1] = "auto";
    for (size_t i = 0; i < source->token_count;) {
        const css_declaration_token *token = &source->tokens[i];
        if (token->type == CSS_TOKEN_S) {
            i++;
            continue;
        }
        size_t end = css__declaration_component_end(source, i);
        if (end == 0)
            return CSS_INVALID;
        size_t length = end - i;
        i = end;
        size_t component;
        if (columns) {
            if (token->type == CSS_TOKEN_IDENT && css__keyword_equal(token->text, "auto"))
                continue;
            if (token->type == CSS_TOKEN_DIMENSION) {
                component = 0;
            } else if (token->type == CSS_TOKEN_NUMBER) {
                /* Unitless zero is a width; a positive integer is a count.
                 * Do not round a small nonzero token into a zero length. */
                component = css__number_sign(token->text) == 0 ? 0 : 1;
            } else if (token->type == CSS_TOKEN_FUNCTION && css__keyword_equal(token->text, "calc")) {
                component = calc_number(token, length) ? 1 : 0;
            } else {
                return CSS_INVALID;
            }
        } else if (token->type == CSS_TOKEN_URI) {
            component = 2;
        } else if (token->type == CSS_TOKEN_IDENT) {
            if (css__keyword_equal(token->text, "none")) {
                defaults[0] = "none";
                continue;
            }
            component = css__keyword_equal(token->text, "inside") || css__keyword_equal(token->text, "outside") ? 1 : 0;
        } else {
            return CSS_INVALID;
        }
        if (parts[component] != NULL)
            return CSS_INVALID;
        parts[component] = token;
        lengths[component] = length;
    }
    size_t component = value->component;
    const css_declaration_token *token = parts[component];
    if (token == NULL)
        return intern(defaults[component], strlen(defaults[component]), out);
    if (token->type == CSS_TOKEN_IDENT)
        return lwc_string_tolower(token->text, out) == lwc_error_ok ? CSS_OK : CSS_NOMEM;
    if (token->type == CSS_TOKEN_FUNCTION)
        return css__declaration_calc_serialize(token, lengths[component], out);
    if (token->type == CSS_TOKEN_URI) {
        css_declaration image = {.property = css_prop_lookup("list-style-image", 16), .tokens = token, .token_count = 1};
        return css__declaration_text_serialize(&image, out);
    }
    return css__declaration_numeric_serialize(token, component == 0, out);
}

/* The native border parser tries colour, style, width, in that order. The
 * grammars are otherwise disjoint; preserve its hashless-colour precedence in
 * quirks mode without reading specified values from rounded bytecode. */
static css_error serialize_border_component(const css_declaration_contribution *value, lwc_string **out)
{
    const css_declaration *source = value->source;
    enum css_declaration_value_kind kind = source->property->value_kind;
    size_t starts[3] = {0}, lengths[3] = {0};
    for (size_t pos = 0; pos < source->token_count;) {
        const css_declaration_token *token = &source->tokens[pos];
        if (token->type == CSS_TOKEN_S) {
            pos++;
            continue;
        }
        size_t end = css__declaration_component_end(source, pos);
        if (end == 0)
            return CSS_INVALID;
        size_t component = 2; /* Colour, unless identified as width or style. */
        if (token->type == CSS_TOKEN_NUMBER || token->type == CSS_TOKEN_DIMENSION) {
            uint32_t ignored;
            bool quirky_colour = source->quirks_allowed && lengths[2] == 0 &&
                css__parse_hash_colour(token->text, &ignored) == CSS_OK;
            component = quirky_colour ? 2 : 0;
        } else if (token->type == CSS_TOKEN_FUNCTION && css__keyword_equal(token->text, "calc")) {
            component = 0;
        } else if (token->type == CSS_TOKEN_IDENT) {
            static const char *const widths[] = {"thin", "medium", "thick"};
            static const char *const styles[] = {
                "none", "hidden", "dotted", "dashed", "solid", "double", "groove", "ridge", "inset", "outset"};
            for (size_t i = 0; i < sizeof(widths) / sizeof(widths[0]); i++) {
                if (css__keyword_equal(token->text, widths[i]))
                    component = 0;
            }
            for (size_t i = 0; i < sizeof(styles) / sizeof(styles[0]); i++) {
                if (css__keyword_equal(token->text, styles[i]))
                    component = 1;
            }
        }
        if (lengths[component] != 0)
            return CSS_INVALID;
        starts[component] = pos;
        lengths[component] = end - pos;
        pos = end;
    }
    size_t component = kind == CSS_DECL_VALUE_BORDER ? value->component / 4 : value->component;
    const char *defaults[] = {"medium", "none", kind == CSS_DECL_VALUE_OUTLINE ? "invert" : "currentcolor"};
    if (lengths[component] == 0)
        return intern(defaults[component], strlen(defaults[component]), out);
    const char *name = source->property->longhands[value->component];
    css_declaration part = *source;
    part.property = css_prop_lookup(name, strlen(name));
    part.tokens = source->tokens + starts[component];
    part.token_count = lengths[component];
    const css_declaration_contribution member = {.source = &part};
    return css__declaration_value_serialize(&member, out);
}

/* Numeric grid placement has one line value per slash-delimited component.
 * The shared native parser has validated each value and its integer range. */
static css_error serialize_grid_component(const css_declaration_contribution *value, lwc_string **out)
{
    const css_declaration *source = value->source;
    size_t component = 0;
    bool span = false, automatic = false;
    const css_declaration_token *number = NULL;
    for (size_t i = 0; i < source->token_count; i++) {
        const css_declaration_token *token = &source->tokens[i];
        if (token->type == CSS_TOKEN_S)
            continue;
        if (token->type == CSS_TOKEN_CHAR && lwc_string_length(token->text) == 1 &&
            lwc_string_data(token->text)[0] == '/') {
            if (component == value->component)
                break;
            component++;
        } else if (component == value->component) {
            if (token->type == CSS_TOKEN_NUMBER)
                number = token;
            else if (token->type == CSS_TOKEN_IDENT && css__keyword_equal(token->text, "span"))
                span = true;
            else if (token->type == CSS_TOKEN_IDENT && css__keyword_equal(token->text, "auto"))
                automatic = true;
            else
                return CSS_INVALID;
        }
    }
    if (component < value->component || automatic)
        return intern("auto", 4, out);
    if (number == NULL)
        return CSS_INVALID;
    lwc_string *integer;
    css_error error = css__declaration_numeric_serialize(number, false, &integer);
    if (error != CSS_OK)
        return error;
    const struct value_format format = {span ? "span " : "", "", ""};
    error = join_values(&integer, 1, &format, out);
    lwc_string_unref(integer);
    return error;
}

/* Legacy aural angles use deg for unitless zero. CSSOM's declared-value
 * azimuth example represents absolute directions as angles; leftwards and
 * rightwards must stay relative. Both properties passed their native grammar. */
static css_error serialize_angle(const css_declaration *source, lwc_string **out)
{
    const css_declaration_token *direction = NULL;
    bool behind = false;
    for (size_t i = 0; i < source->token_count; i++) {
        const css_declaration_token *token = &source->tokens[i];
        if (token->type == CSS_TOKEN_S)
            continue;
        if (token->type == CSS_TOKEN_IDENT && css__keyword_equal(token->text, "behind")) {
            behind = true;
        } else if (direction == NULL) {
            direction = token;
        } else {
            return CSS_INVALID;
        }
    }
    if (behind && direction == NULL)
        return intern("180deg", 6, out);
    if (direction == NULL)
        return CSS_INVALID;
    if (direction->type != CSS_TOKEN_IDENT) {
        if (behind || (direction->type != CSS_TOKEN_NUMBER && direction->type != CSS_TOKEN_DIMENSION))
            return CSS_INVALID;
        return serialize_numeric(direction, "deg", false, out);
    }
    if (source->property->id == CSS_DECL_AZIMUTH) {
        static const struct {
            const char *name, *front, *back;
        } directions[] = {
            {"left-side", "270deg", "270deg"}, {"far-left", "300deg", "240deg"}, {"left", "320deg", "220deg"},
            {"center-left", "340deg", "200deg"}, {"center", "0deg", "180deg"}, {"center-right", "20deg", "160deg"},
            {"right", "40deg", "140deg"}, {"far-right", "60deg", "120deg"}, {"right-side", "90deg", "90deg"},
        };
        for (size_t i = 0; i < sizeof(directions) / sizeof(directions[0]); i++) {
            if (css__keyword_equal(direction->text, directions[i].name)) {
                const char *value = behind ? directions[i].back : directions[i].front;
                return intern(value, strlen(value), out);
            }
        }
    }
    if (behind)
        return CSS_INVALID;
    return lwc_string_tolower(direction->text, out) == lwc_error_ok ? CSS_OK : CSS_NOMEM;
}

css_error css__declaration_value_serialize(const css_declaration_contribution *value, lwc_string **out)
{
    if (out == NULL)
        return CSS_BADPARM;
    *out = NULL;
    if (value == NULL || value->source == NULL)
        return CSS_BADPARM;
    const css_declaration *source = value->source;
    const struct css_prop_entry *property = source->property;
    size_t members = property != NULL && property->shorthand ? property->longhand_count : 1;
    if (value->component >= members)
        return CSS_BADPARM;
    if (property == NULL)
        return serialize_unparsed(source, out);
    if (source->deferred)
        return property->shorthand ? intern("", 0, out) : serialize_unparsed(source, out);
    const char *wide = wide_keyword(source);
    if (wide != NULL)
        return intern(wide, strlen(wide), out);
    enum css_declaration_value_kind kind = property->value_kind;
    if (kind == CSS_DECL_VALUE_NONE)
        return CSS_INVALID;
    if (kind == CSS_DECL_VALUE_SINGLE) {
        css_declaration part = *source;
        part.property = css_prop_lookup(property->longhands[0], strlen(property->longhands[0]));
        const css_declaration_contribution member = {.source = &part};
        return css__declaration_value_serialize(&member, out);
    }
    if (kind == CSS_DECL_VALUE_TRANSFORM)
        return css__declaration_transform_serialize(source, out);
    if (kind == CSS_DECL_VALUE_ANGLE)
        return serialize_angle(source, out);
    if (kind == CSS_DECL_VALUE_FONT)
        return css__declaration_font_component(value, out);
    if (kind == CSS_DECL_VALUE_WHITE_SPACE)
        return serialize_white_space_component(value, out);
    if (kind == CSS_DECL_VALUE_IMAGE_LIST)
        return css__declaration_background_image(source, out);
    if (kind == CSS_DECL_VALUE_BACKGROUND || kind == CSS_DECL_VALUE_REPEAT_LIST ||
        kind == CSS_DECL_VALUE_POSITION_LIST || kind == CSS_DECL_VALUE_POSITION_AXIS_LIST)
        return css__declaration_background_component(value, out);
    if (kind == CSS_DECL_VALUE_URI || kind == CSS_DECL_VALUE_CURSOR || kind == CSS_DECL_VALUE_KEYWORD_LIST ||
        kind == CSS_DECL_VALUE_GRID_TRACKS || kind == CSS_DECL_VALUE_GRID_FLOW ||
        kind == CSS_DECL_VALUE_CONTENT || kind == CSS_DECL_VALUE_STRING_LIST || kind == CSS_DECL_VALUE_COUNTER_LIST ||
        kind == CSS_DECL_VALUE_FONT_FAMILY || kind == CSS_DECL_VALUE_VOICE_FAMILY || kind == CSS_DECL_VALUE_PLAY_DURING ||
        kind == CSS_DECL_VALUE_TEXT_DECORATION)
        return css__declaration_text_serialize(source, out);
    if (kind == CSS_DECL_VALUE_ASPECT_RATIO || kind == CSS_DECL_VALUE_BORDER_SPACING || kind == CSS_DECL_VALUE_CLIP)
        return serialize_geometry(source, out);
    if (kind == CSS_DECL_VALUE_FLEX || kind == CSS_DECL_VALUE_FLEX_FLOW)
        return serialize_flex_component(value, out);
    if (kind == CSS_DECL_VALUE_LIST_STYLE || kind == CSS_DECL_VALUE_COLUMNS)
        return serialize_list_or_columns_component(value, out);
    if (kind == CSS_DECL_VALUE_BORDER || kind == CSS_DECL_VALUE_BORDER_SIDE || kind == CSS_DECL_VALUE_OUTLINE)
        return serialize_border_component(value, out);
    if (kind == CSS_DECL_VALUE_GRID_LINE || kind == CSS_DECL_VALUE_GRID_PAIR || kind == CSS_DECL_VALUE_GRID_AREA)
        return serialize_grid_component(value, out);
    if (kind == CSS_DECL_VALUE_POSITION)
        return css__declaration_object_position(source, out);
    if (kind == CSS_DECL_VALUE_COLOUR || kind == CSS_DECL_VALUE_QUAD_COLOUR)
        return css__declaration_colour_serialize(value, out);
    size_t arity = scalar_arity(kind);
    css_declaration part;
    if (!scalar_component(value, arity, &part))
        return CSS_INVALID;
    const css_declaration_token *token = part.tokens;
    if (token->type == CSS_TOKEN_IDENT)
        return lwc_string_tolower(token->text, out) == lwc_error_ok ? CSS_OK : CSS_NOMEM;
    if (kind == CSS_DECL_VALUE_KEYWORD || kind == CSS_DECL_VALUE_PAIR_KEYWORD || kind == CSS_DECL_VALUE_QUAD_KEYWORD)
        return CSS_INVALID;
    if (token->type == CSS_TOKEN_FUNCTION)
        return css__declaration_calc_serialize(part.tokens, part.token_count, out);
    if (kind == CSS_DECL_VALUE_PAIR_URI) {
        css_declaration component = {.property = css_prop_lookup("cue-before", 10), .tokens = token, .token_count = 1};
        return css__declaration_text_serialize(&component, out);
    }
    if ((kind == CSS_DECL_VALUE_TIME || kind == CSS_DECL_VALUE_PAIR_TIME) && token->type == CSS_TOKEN_DIMENSION) {
        const char *text = lwc_string_data(token->text);
        size_t length = lwc_string_length(token->text);
        if (length >= 2 && (text[length - 2] == 'm' || text[length - 2] == 'M'))
            return css__declaration_numeric_divide(token, 3, "s", out);
    }
    if (kind == CSS_DECL_VALUE_TIME || kind == CSS_DECL_VALUE_PAIR_TIME || kind == CSS_DECL_VALUE_FREQUENCY)
        return serialize_numeric(token, kind == CSS_DECL_VALUE_FREQUENCY ? "hz" : "s", false, out);
    return css__declaration_numeric_serialize(
        token, kind != CSS_DECL_VALUE_NUMBER && kind != CSS_DECL_VALUE_NUMBER_LENGTH, out);
}

static css_error serialize_border_shorthand(enum css_declaration_value_kind kind,
    const css_declaration_contribution *const *members, lwc_string **out)
{
    size_t sides = kind == CSS_DECL_VALUE_BORDER ? 4 : 1;
    lwc_string *values[3] = {0};
    css_error error = CSS_OK;
    for (size_t component = 0; component < 3; component++) {
        for (size_t side = 0; side < sides; side++) {
            lwc_string *value = NULL;
            error = css__declaration_value_serialize(members[component * sides + side], &value);
            if (error != CSS_OK)
                goto done;
            if (side == 0) {
                values[component] = value;
            } else {
                bool equal = values[component] == value;
                lwc_string_unref(value);
                if (!equal) {
                    error = intern("", 0, out); /* Unequal sides cannot be expressed by border. */
                    goto done;
                }
            }
        }
    }
    const char *defaults[] = {"medium", "none", kind == CSS_DECL_VALUE_OUTLINE ? "invert" : "currentcolor"};
    lwc_string *parts[3];
    size_t count = 0;
    for (size_t i = 0; i < 3; i++) {
        /* UI 3 outline order is colour/style/width; border and column-rule
         * order is width/style/colour. Omit only true specified defaults. */
        size_t component = kind == CSS_DECL_VALUE_OUTLINE ? 2 - i : i;
        if (!css__keyword_equal(values[component], defaults[component]))
            parts[count++] = values[component];
    }
    if (count == 0)
        parts[count++] = values[1]; /* none is the shortest spelling of the defaults. */
    const struct value_format format = {"", " ", ""};
    error = join_values(parts, count, &format, out);
done:
    for (size_t i = 0; i < 3; i++) {
        if (values[i] != NULL)
            lwc_string_unref(values[i]);
    }
    return error;
}

static css_error serialize_components(enum css_declaration_value_kind kind,
    const css_declaration_contribution *const *members, size_t count, lwc_string **out)
{
    lwc_string *values[4] = {0};
    css_error error = CSS_OK;
    for (size_t i = 0; i < count; i++) {
        error = css__declaration_value_serialize(members[i], &values[i]);
        if (error != CSS_OK)
            goto done;
    }
    size_t first = 0, used = count;
    lwc_string *ordered[3];
    if (kind == CSS_DECL_VALUE_GRID_PAIR || kind == CSS_DECL_VALUE_GRID_AREA) {
        /* Numeric placements default omitted members to auto. Named-line
         * support will need the custom-ident copying rules from Grid 2 8.4. */
        while (used > 1 && css__keyword_equal(values[used - 1], "auto"))
            used--;
        const struct value_format format = {"", " / ", ""};
        error = join_values(values, used, &format, out);
        goto done;
    } else if (kind == CSS_DECL_VALUE_LIST_STYLE) {
        /* CSS Lists 3 canonical order is position, image, type. Omit the
         * default image only when doing so preserves none's dual meaning. */
        used = 0;
        if (!css__keyword_equal(values[1], "outside"))
            ordered[used++] = values[1];
        if (!css__keyword_equal(values[2], "none"))
            ordered[used++] = values[2];
        if (!css__keyword_equal(values[0], "disc") || used == 0)
            ordered[used++] = values[0];
        const struct value_format format = {"", " ", ""};
        error = join_values(ordered, used, &format, out);
        goto done;
    } else if (kind == CSS_DECL_VALUE_COLUMNS) {
        if (css__keyword_equal(values[1], "auto"))
            used = 1;
        else if (css__keyword_equal(values[0], "auto")) {
            first = 1;
            used = 1;
        }
    } else if (kind == CSS_DECL_VALUE_WHITE_SPACE) {
        /* Prefer the established single-keyword forms where they express the
         * entire pair. The sixth supported combination has no legacy alias. */
        bool nowrap = css__keyword_equal(values[1], "nowrap");
        const char *keyword = NULL;
        if (css__keyword_equal(values[0], "collapse"))
            keyword = nowrap ? "nowrap" : "normal";
        else if (css__keyword_equal(values[0], "preserve"))
            keyword = nowrap ? "pre" : "pre-wrap";
        else if (!nowrap)
            keyword = "pre-line";
        if (keyword != NULL) {
            error = intern(keyword, strlen(keyword), out);
            goto done;
        }
    } else if (kind == CSS_DECL_VALUE_FLEX_FLOW) {
        if (css__keyword_equal(values[1], "nowrap"))
            used = 1;
        else if (css__keyword_equal(values[0], "row")) {
            first = 1;
            used = 1;
        }
    } else if (kind != CSS_DECL_VALUE_FLEX) {
        /* Interned equality permits the shortest equivalent pair/quad spelling. */
        if (used == 4 && values[3] == values[1])
            used--;
        if (used == 3 && values[2] == values[0])
            used--;
        if (used == 2 && values[1] == values[0])
            used--;
    }
    /* flex uses the established explicit grow/shrink/basis serialization. In
     * particular, a 0px basis must not become 0% (different for indefinite sizes). */
    const struct value_format format = {"", " ", ""};
    error = join_values(values + first, used, &format, out);
done:
    for (size_t i = 0; i < count; i++) {
        if (values[i] != NULL)
            lwc_string_unref(values[i]);
    }
    return error;
}

css_error css__declaration_shorthand_serialize(const struct css_prop_entry *property,
    const css_declaration_contribution *const *members, size_t count, lwc_string **out)
{
    if (out == NULL)
        return CSS_BADPARM;
    *out = NULL;
    if (property == NULL || !property->shorthand || members == NULL || count == 0 || count != property->longhand_count)
        return CSS_BADPARM;
    const css_declaration *first = NULL;
    const char *wide = NULL;
    bool any_wide = false, same_wide = true, any_deferred = false, same_source = true;
    for (size_t i = 0; i < count; i++) {
        if (members[i] == NULL)
            return intern("", 0, out);
        const css_declaration *source = members[i]->source;
        if (source == NULL || members[i]->name == NULL || strcmp(members[i]->name, property->longhands[i]) != 0)
            return CSS_BADPARM;
        if (i == 0) {
            first = source;
            wide = wide_keyword(source);
        }
        if (source->important != first->important)
            return intern("", 0, out);
        const char *keyword = wide_keyword(source);
        any_wide |= keyword != NULL;
        same_wide &= keyword == wide;
        any_deferred |= source->deferred;
        same_source &= source == first;
    }
    /* CSS Values 5, Substitution in Shorthand Properties: only the original
     * shorthand may recover its text when every member still comes from it. */
    if (any_deferred) {
        if (same_source && first->property == property)
            return serialize_unparsed(first, out);
        return intern("", 0, out);
    }
    if (any_wide)
        return same_wide ? intern(wide, strlen(wide), out) : intern("", 0, out);
    enum css_declaration_value_kind kind = property->value_kind;
    if (kind == CSS_DECL_VALUE_SINGLE)
        return css__declaration_value_serialize(members[0], out);
    if (kind == CSS_DECL_VALUE_FONT)
        return css__declaration_font_shorthand(members, out);
    if (kind == CSS_DECL_VALUE_BACKGROUND)
        return css__declaration_background_shorthand(members, out);
    if (kind == CSS_DECL_VALUE_REPEAT_LIST || kind == CSS_DECL_VALUE_POSITION_LIST)
        return css__declaration_background_pair_shorthand(kind, members, out);
    if (kind == CSS_DECL_VALUE_BORDER || kind == CSS_DECL_VALUE_BORDER_SIDE || kind == CSS_DECL_VALUE_OUTLINE)
        return serialize_border_shorthand(kind, members, out);
    if (scalar_arity(kind) == 1 && kind != CSS_DECL_VALUE_FLEX && kind != CSS_DECL_VALUE_FLEX_FLOW &&
        kind != CSS_DECL_VALUE_LIST_STYLE && kind != CSS_DECL_VALUE_COLUMNS && kind != CSS_DECL_VALUE_WHITE_SPACE &&
        kind != CSS_DECL_VALUE_GRID_PAIR && kind != CSS_DECL_VALUE_GRID_AREA)
        return CSS_INVALID;
    return serialize_components(kind, members, count, out);
}

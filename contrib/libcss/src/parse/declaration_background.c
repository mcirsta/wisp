/*
 * This file is part of LibCSS. Licensed under the MIT License.
 * Specified background layers retain their own list lengths and source tokens;
 * painting's list repetition and fixed-point values must not affect CSSOM.
 */
#include <stdlib.h>
#include <string.h>

#include "parse/declaration_background.h"
#include "parse/position.h"
#include "parse/property.h"
#include "parse/properties/utils.h"
#include "parse/repeat.h"

enum { BG_IMAGE, BG_POSITION, BG_REPEAT, BG_ATTACHMENT, BG_COLOUR, BG_COMPONENTS };
static const char *const names[BG_COMPONENTS] = {"background-image", "background-position", "background-repeat",
    "background-attachment", "background-color"};
static const char *const defaults[BG_COMPONENTS] = {"none", "0% 0%", "repeat", "scroll", "transparent"};

struct layer_values {
    size_t count;
    lwc_string **items;
    lwc_string *single;
};

static bool comma(const css_declaration_token *token)
{
    return token->type == CSS_TOKEN_CHAR && css__keyword_equal(token->text, ",");
}

/* Component boundaries, not text splitting: strings/URLs are single tokens
 * and commas inside functions belong to the function. */
static css_error next_layer(const css_declaration *source, size_t *cursor, css_declaration *layer)
{
    size_t start = *cursor, end = start;
    while (end < source->token_count && !comma(&source->tokens[end])) {
        end = css__declaration_component_end(source, end);
        if (end == 0)
            return CSS_INVALID;
    }
    *cursor = end < source->token_count ? end + 1 : end;
    while (start < end && source->tokens[start].type == CSS_TOKEN_S)
        start++;
    while (end > start && source->tokens[end - 1].type == CSS_TOKEN_S)
        end--;
    if (start == end)
        return CSS_INVALID;
    *layer = *source;
    layer->tokens += start;
    layer->token_count = end - start;
    return CSS_OK;
}

static css_error layer_count(const css_declaration *source, size_t *count)
{
    *count = 0;
    size_t cursor = 0;
    while (cursor < source->token_count) {
        css_declaration layer;
        css_error error = next_layer(source, &cursor, &layer);
        if (error != CSS_OK)
            return error;
        (*count)++;
    }
    return *count != 0 ? CSS_OK : CSS_INVALID;
}

static bool position_token(const css_declaration_token *token)
{
    return token->type == CSS_TOKEN_NUMBER || token->type == CSS_TOKEN_DIMENSION ||
        token->type == CSS_TOKEN_PERCENTAGE ||
        (token->type == CSS_TOKEN_IDENT && css__position_keyword(token->text) != CSS_POSITION_INVALID);
}

/* The source already passed background's grammar. Locate its disjoint value
 * groups, using the same keyword helpers as native position/repeat parsing.
 * In quirks mode colour precedes position, but a second position token belongs
 * to that position even when it could have been a hashless colour on its own. */
static css_error background_component(const css_declaration *layer, size_t wanted, css_declaration *part)
{
    size_t starts[BG_COMPONENTS] = {0}, lengths[BG_COMPONENTS] = {0};
    for (size_t pos = 0; pos < layer->token_count;) {
        const css_declaration_token *token = &layer->tokens[pos];
        if (token->type == CSS_TOKEN_S) {
            pos++;
            continue;
        }
        size_t end = css__declaration_component_end(layer, pos);
        if (end == 0)
            return CSS_INVALID;
        size_t component = BG_COLOUR;
        uint32_t ignored;
        bool quirky_colour = layer->quirks_allowed && lengths[BG_COLOUR] == 0 &&
            (token->type == CSS_TOKEN_NUMBER || token->type == CSS_TOKEN_DIMENSION) &&
            css__parse_hash_colour(token->text, &ignored) == CSS_OK;
        if (token->type == CSS_TOKEN_URI ||
            (token->type == CSS_TOKEN_IDENT && css__keyword_equal(token->text, "none")) ||
            (token->type == CSS_TOKEN_FUNCTION && (css__keyword_equal(token->text, "linear-gradient") ||
                css__keyword_equal(token->text, "radial-gradient")))) {
            component = BG_IMAGE;
        } else if (token->type == CSS_TOKEN_IDENT &&
            (css__keyword_equal(token->text, "scroll") || css__keyword_equal(token->text, "fixed"))) {
            component = BG_ATTACHMENT;
        } else if (token->type == CSS_TOKEN_IDENT && css__repeat_keyword(token->text) >= 0) {
            component = BG_REPEAT;
        } else if (!quirky_colour && position_token(token)) {
            component = BG_POSITION;
        }
        if (lengths[component] != 0)
            return CSS_INVALID;
        if (component == BG_POSITION || component == BG_REPEAT) {
            size_t second = end;
            while (second < layer->token_count && layer->tokens[second].type == CSS_TOKEN_S)
                second++;
            if (second < layer->token_count) {
                const css_declaration_token *next = &layer->tokens[second];
                bool pair = component == BG_POSITION ? position_token(next) :
                    css__repeat_axis(css__repeat_keyword(token->text)) && next->type == CSS_TOKEN_IDENT &&
                        css__repeat_axis(css__repeat_keyword(next->text));
                if (pair)
                    end = second + 1;
            }
        }
        starts[component] = pos;
        lengths[component] = end - pos;
        pos = end;
    }
    *part = *layer;
    part->property = css_prop_lookup(names[wanted], strlen(names[wanted]));
    part->tokens += starts[wanted];
    part->token_count = lengths[wanted];
    return CSS_OK;
}

static void destroy_values(struct layer_values *values)
{
    for (size_t i = 0; i < values->count; i++) {
        if (values->items[i] != NULL)
            lwc_string_unref(values->items[i]);
    }
    if (values->items != &values->single)
        free(values->items);
}

static css_error collect_values(const css_declaration_contribution *value, size_t component, struct layer_values *values)
{
    const css_declaration *source = value->source;
    size_t count;
    css_error error = layer_count(source, &count);
    if (error != CSS_OK)
        return error;
    size_t stored = component == BG_COLOUR ? 1 : count;
    if (stored > SIZE_MAX / sizeof(*values->items))
        return CSS_NOMEM;
    values->items = stored == 1 ? &values->single : calloc(stored, sizeof(*values->items));
    if (values->items == NULL)
        return CSS_NOMEM;
    values->count = stored;
    size_t cursor = 0;
    for (size_t i = 0; i < count; i++) {
        css_declaration part;
        error = next_layer(source, &cursor, &part);
        if (error != CSS_OK)
            return error;
        if (component == BG_COLOUR && i + 1 != count)
            continue; /* Only the final layer can supply the colour. */
        if (source->property->value_kind == CSS_DECL_VALUE_BACKGROUND) {
            css_declaration layer = part;
            error = background_component(&layer, component, &part);
            if (error != CSS_OK)
                return error;
        }
        lwc_string **out = &values->items[component == BG_COLOUR ? 0 : i];
        if (part.token_count == 0) {
            error = lwc_intern_string(defaults[component], strlen(defaults[component]), out) == lwc_error_ok ?
                CSS_OK : CSS_NOMEM;
        } else {
            const css_declaration_contribution member = {.source = &part};
            error = css__declaration_value_serialize(&member, out);
        }
        if (error != CSS_OK)
            return error;
    }
    return CSS_OK;
}

css_error css__declaration_background_component(const css_declaration_contribution *value, lwc_string **out)
{
    struct layer_values values = {0};
    css_error error = collect_values(value, value->component, &values);
    if (error == CSS_OK)
        error = css__declaration_values_join(values.items, values.count, ", ", out);
    destroy_values(&values);
    return error;
}

css_error css__declaration_background_shorthand(const css_declaration_contribution *const *members, lwc_string **out)
{
    /* CSSOM requires exact specified-value representation: repeating or
     * truncating lists as painting does would alter the independent longhands. */
    size_t count = 0;
    for (size_t i = 0; i < BG_COLOUR; i++) {
        size_t length;
        css_error error = layer_count(members[i]->source, &length);
        if (error != CSS_OK)
            return error;
        if (i != 0 && length != count)
            return lwc_intern_string("", 0, out) == lwc_error_ok ? CSS_OK : CSS_NOMEM;
        count = length;
    }
    struct layer_values values[BG_COMPONENTS] = {0};
    lwc_string *single = NULL;
    if (count > SIZE_MAX / sizeof(lwc_string *))
        return CSS_NOMEM;
    lwc_string **layers = count == 1 ? &single : calloc(count, sizeof(*layers));
    if (layers == NULL)
        return CSS_NOMEM;
    css_error error = CSS_OK;
    for (size_t i = 0; i < BG_COMPONENTS; i++) {
        error = collect_values(members[i], i, &values[i]);
        if (error != CSS_OK)
            goto done;
    }
    for (size_t layer = 0; layer < count; layer++) {
        lwc_string *parts[BG_COMPONENTS];
        size_t used = 0;
        for (size_t i = 0; i < BG_COMPONENTS; i++) {
            if (i == BG_COLOUR && layer + 1 != count)
                continue;
            lwc_string *part = values[i].items[i == BG_COLOUR ? 0 : layer];
            bool initial = css__keyword_equal(part, defaults[i]) ||
                (i == BG_COLOUR && css__keyword_equal(part, "rgba(0, 0, 0, 0)"));
            if (!initial)
                parts[used++] = part;
        }
        if (used == 0) {
            error = lwc_intern_string("none", 4, &layers[layer]) == lwc_error_ok ? CSS_OK : CSS_NOMEM;
        } else {
            error = css__declaration_values_join(parts, used, " ", &layers[layer]);
        }
        if (error != CSS_OK)
            goto done;
    }
    error = css__declaration_values_join(layers, count, ", ", out);
done:
    for (size_t i = 0; i < BG_COMPONENTS; i++)
        destroy_values(&values[i]);
    for (size_t i = 0; i < count; i++) {
        if (layers[i] != NULL)
            lwc_string_unref(layers[i]);
    }
    if (layers != &single)
        free(layers);
    return error;
}

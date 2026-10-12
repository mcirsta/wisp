/*
 * This file is part of LibCSS. Licensed under the MIT License.
 * Font readback uses specified tokens, never rounded native font metrics.
 */
#include <string.h>

#include "parse/declaration_font.h"
#include "parse/font.h"
#include "parse/property.h"

enum {
    FONT_PART_STYLE,
    FONT_PART_VARIANT,
    FONT_PART_WEIGHT,
    FONT_PART_SIZE,
    FONT_PART_LINE_HEIGHT,
    FONT_PART_FAMILY,
    FONT_PART_COMPONENTS
};
static const char *const names[FONT_PART_COMPONENTS] = {
    "font-style", "font-variant", "font-weight", "font-size", "line-height", "font-family"};

static void skip_space(const css_declaration *source, size_t *pos)
{
    while (*pos < source->token_count && source->tokens[*pos].type == CSS_TOKEN_S)
        (*pos)++;
}

/* The parser has already resolved ambiguity and validated the entire value.
 * Locate the size and optional line-height as components (a calc() may contain
 * spaces or slashes); the remaining tokens are the complete family list. */
static css_error font_component(const css_declaration *source, size_t wanted, css_declaration *part)
{
    size_t starts[FONT_PART_COMPONENTS] = {0}, lengths[FONT_PART_COMPONENTS] = {0};
    size_t pos = 0;
    skip_space(source, &pos);
    while (pos < source->token_count) {
        const css_declaration_token *token = &source->tokens[pos];
        uint16_t ignored;
        enum css_font_prefix prefix = css__font_prefix(token->type, token->text, &ignored);
        if (prefix == CSS_FONT_PREFIX_END)
            break;
        if (prefix != CSS_FONT_PREFIX_NORMAL) {
            starts[prefix] = pos;
            lengths[prefix] = 1;
        }
        pos++;
        skip_space(source, &pos);
    }
    starts[FONT_PART_SIZE] = pos;
    size_t end = css__declaration_component_end(source, pos);
    if (end == 0)
        return CSS_INVALID;
    lengths[FONT_PART_SIZE] = end - pos;
    pos = end;
    skip_space(source, &pos);
    if (pos < source->token_count && source->tokens[pos].type == CSS_TOKEN_CHAR &&
        css__keyword_equal(source->tokens[pos].text, "/")) {
        pos++;
        skip_space(source, &pos);
        starts[FONT_PART_LINE_HEIGHT] = pos;
        end = css__declaration_component_end(source, pos);
        if (end == 0)
            return CSS_INVALID;
        lengths[FONT_PART_LINE_HEIGHT] = end - pos;
        pos = end;
        skip_space(source, &pos);
    }
    /* A client-resolved system font has no explicit size/family components.
     * Leave it explicitly unsupported until system-font CSSOM state is added. */
    if (pos == source->token_count)
        return CSS_INVALID;
    starts[FONT_PART_FAMILY] = pos;
    lengths[FONT_PART_FAMILY] = source->token_count - pos;
    *part = *source;
    part->property = css_prop_lookup(names[wanted], strlen(names[wanted]));
    part->tokens += starts[wanted];
    part->token_count = lengths[wanted];
    return CSS_OK;
}

css_error css__declaration_font_component(const css_declaration_contribution *value, lwc_string **out)
{
    css_declaration part;
    css_error error = font_component(value->source, value->component, &part);
    if (error != CSS_OK)
        return error;
    if (part.token_count == 0)
        return lwc_intern_string("normal", SLEN("normal"), out) == lwc_error_ok ? CSS_OK : CSS_NOMEM;
    const css_declaration_contribution member = {.source = &part};
    return css__declaration_value_serialize(&member, out);
}

css_error css__declaration_font_shorthand(const css_declaration_contribution *const *members, lwc_string **out)
{
    lwc_string *values[FONT_PART_COMPONENTS] = {0}, *size = NULL, *ordered[FONT_PART_COMPONENTS];
    css_error error = CSS_OK;
    for (size_t i = 0; i < FONT_PART_COMPONENTS; i++) {
        error = css__declaration_value_serialize(members[i], &values[i]);
        if (error != CSS_OK)
            goto done;
    }
    /* CSSOM canonical order, omitting only defaults that parse identically.
     * Numeric 400/700 stay numbers rather than being rewritten as keywords. */
    size_t used = 0;
    for (size_t i = FONT_PART_STYLE; i <= FONT_PART_WEIGHT; i++) {
        if (!css__keyword_equal(values[i], "normal"))
            ordered[used++] = values[i];
    }
    if (css__keyword_equal(values[FONT_PART_LINE_HEIGHT], "normal")) {
        size = lwc_string_ref(values[FONT_PART_SIZE]);
    } else {
        error = css__declaration_values_join(values + FONT_PART_SIZE, 2, " / ", &size);
        if (error != CSS_OK)
            goto done;
    }
    ordered[used++] = size;
    ordered[used++] = values[FONT_PART_FAMILY];
    error = css__declaration_values_join(ordered, used, " ", out);
done:
    if (size != NULL)
        lwc_string_unref(size);
    for (size_t i = 0; i < FONT_PART_COMPONENTS; i++) {
        if (values[i] != NULL)
            lwc_string_unref(values[i]);
    }
    return error;
}

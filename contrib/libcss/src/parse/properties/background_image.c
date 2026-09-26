/*
 * This file is part of LibCSS.
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 * Copyright 2024 The NetSurf Browser Project.
 */

#include <assert.h>
#include <string.h>

#include "bytecode/bytecode.h"
#include "bytecode/opcodes.h"
#include "parse/properties/background.h"
#include "parse/properties/utils.h"

#include <libcss/gradient.h>

#define MAX_GRADIENT_STOPS CSS_LINEAR_GRADIENT_MAX_STOPS

/**
 * Parse linear gradient direction keywords
 *
 * \param c       Parsing context
 * \param vector  Vector of tokens
 * \param ctx     Token context
 * \param direction  Output: gradient direction
 * \return CSS_OK if direction parsed, CSS_INVALID otherwise
 */
static css_error
parse_gradient_direction(css_language *c, const parserutils_vector *vector, int32_t *ctx, uint32_t *direction)
{
    const css_token *token;
    bool match;
    lwc_string *keyword = NULL;

    /* Expect "to" keyword */
    token = parserutils_vector_peek(vector, *ctx);
    if (token == NULL || token->type != CSS_TOKEN_IDENT)
        return CSS_INVALID;

    if (lwc_intern_string("to", 2, &keyword) != lwc_error_ok)
        return CSS_INVALID;

    if (lwc_string_caseless_isequal(token->idata, keyword, &match) != lwc_error_ok) {
        lwc_string_unref(keyword);
        return CSS_INVALID;
    }
    lwc_string_unref(keyword);

    if (!match)
        return CSS_INVALID;

    parserutils_vector_iterate(vector, ctx); /* Consume "to" */
    consumeWhitespace(vector, ctx); /* Skip whitespace after "to" */

    /* Get direction keyword */
    token = parserutils_vector_iterate(vector, ctx);
    if (token == NULL || token->type != CSS_TOKEN_IDENT)
        return CSS_INVALID;

    /* Check top */
    if (lwc_intern_string("top", 3, &keyword) == lwc_error_ok) {
        if (lwc_string_caseless_isequal(token->idata, keyword, &match) == lwc_error_ok && match) {
            lwc_string_unref(keyword);
            *direction = 1;
            return CSS_OK;
        }
        lwc_string_unref(keyword);
    }

    /* Check bottom */
    if (lwc_intern_string("bottom", 6, &keyword) == lwc_error_ok) {
        if (lwc_string_caseless_isequal(token->idata, keyword, &match) == lwc_error_ok && match) {
            lwc_string_unref(keyword);
            *direction = 0;
            return CSS_OK;
        }
        lwc_string_unref(keyword);
    }

    /* Check left */
    if (lwc_intern_string("left", 4, &keyword) == lwc_error_ok) {
        if (lwc_string_caseless_isequal(token->idata, keyword, &match) == lwc_error_ok && match) {
            lwc_string_unref(keyword);
            *direction = 2;
            return CSS_OK;
        }
        lwc_string_unref(keyword);
    }

    /* Check right */
    if (lwc_intern_string("right", 5, &keyword) == lwc_error_ok) {
        if (lwc_string_caseless_isequal(token->idata, keyword, &match) == lwc_error_ok && match) {
            lwc_string_unref(keyword);
            *direction = 3;
            return CSS_OK;
        }
        lwc_string_unref(keyword);
    }

    return CSS_INVALID;
}

/** Parse a bounded color-stop list, retaining units until used-value resolution.
 * Bytecode: direction/shape, count, then (color, offset, unit) per stop.
 * Unit zero denotes an omitted position; negative positions are valid lengths.
 */
static css_error parse_gradient_stops(
    css_language *c, const parserutils_vector *vector, int32_t *ctx, css_style *result, uint16_t kind, uint32_t header)
{
    struct {
        uint32_t color;
        css_fixed offset;
        uint32_t unit;
    } stops[MAX_GRADIENT_STOPS] = {0};
    uint32_t count = 0;
    css_error error;

    while (true) {
        if (count == MAX_GRADIENT_STOPS)
            return CSS_INVALID;
        consumeWhitespace(vector, ctx);
        uint16_t color_type;
        error = css__parse_colour_specifier(c, vector, ctx, &color_type, &stops[count].color);
        if (error != CSS_OK)
            return error;
        if (color_type == COLOR_TRANSPARENT)
            stops[count].color = 0;
        else if (color_type != COLOR_SET)
            return CSS_INVALID; /* currentColor needs a computed-color reference. */

        consumeWhitespace(vector, ctx);
        const css_token *token = parserutils_vector_peek(vector, *ctx);
        if (token != NULL &&
            (token->type == CSS_TOKEN_DIMENSION || token->type == CSS_TOKEN_NUMBER ||
                token->type == CSS_TOKEN_PERCENTAGE)) {
            error = css__parse_unit_specifier(c, vector, ctx, UNIT_PX, &stops[count].offset, &stops[count].unit);
            if (error != CSS_OK)
                return error;
            if (stops[count].unit != UNIT_PCT && (!(stops[count].unit & UNIT_LENGTH) || stops[count].unit == UNIT_FR))
                return CSS_INVALID;
            /* Gradient functions do not accept unitless nonzero lengths, even
             * in a quirks-mode sheet. */
            if (token->type == CSS_TOKEN_NUMBER && stops[count].offset != 0)
                return CSS_INVALID;
        }
        count++;
        consumeWhitespace(vector, ctx);
        token = parserutils_vector_iterate(vector, ctx);
        if (token == NULL)
            return CSS_INVALID;
        if (tokenIsChar(token, ')'))
            break;
        if (!tokenIsChar(token, ','))
            return CSS_INVALID;
    }
    if (count < 2)
        return CSS_INVALID;

    error = css__stylesheet_style_appendOPV(result, CSS_PROP_BACKGROUND_IMAGE, 0, kind);
    if (error == CSS_OK)
        error = css__stylesheet_style_append(result, header);
    if (error == CSS_OK)
        error = css__stylesheet_style_append(result, count);
    for (uint32_t i = 0; i < count && error == CSS_OK; i++) {
        error = css__stylesheet_style_append(result, stops[i].color);
        if (error == CSS_OK)
            error = css__stylesheet_style_append(result, stops[i].offset);
        if (error == CSS_OK)
            error = css__stylesheet_style_append(result, stops[i].unit);
    }
    return error;
}

/**
 * Parse a linear gradient
 *
 * \param c       Parsing context
 * \param vector  Vector of tokens
 * \param ctx     Token context
 * \param result  Output style
 * \return CSS_OK on success
 */
static css_error
parse_linear_gradient(css_language *c, const parserutils_vector *vector, int32_t *ctx, css_style *result)
{
    int32_t orig_ctx = *ctx;
    css_error error = CSS_OK;
    const css_token *token;
    uint32_t direction = 0; /* Default: to bottom */
    bool has_direction = false;

    /* Try to parse direction - first try angle, then try keyword */
    int32_t checkpoint = *ctx;

    /* Try angle first (e.g., 45deg) */
    token = parserutils_vector_peek(vector, *ctx);
    if (token && token->type == CSS_TOKEN_DIMENSION) {
        size_t len = lwc_string_length(token->idata);
        const char *data = lwc_string_data(token->idata);

        /* Check if it ends with "deg" */
        if (len > 3 && strncasecmp(data + len - 3, "deg", 3) == 0) {
            /* Parse the angle value */
            size_t consumed = 0;
            css_fixed angle = css__number_from_string((const uint8_t *)data, len - 3, false, &consumed);
            int angle_deg = FIXTOINT(angle);

            /* Convert angle to direction enum - approximate to nearest cardinal */
            /* CSS angles: 0deg = to top, 90deg = to right, 180deg = to bottom, 270deg = to left */
            angle_deg = ((angle_deg % 360) + 360) % 360; /* Normalize to 0-359 */

            if (angle_deg >= 315 || angle_deg < 45) {
                direction = 1; /* to top */
            } else if (angle_deg >= 45 && angle_deg < 135) {
                direction = 3; /* to right */
            } else if (angle_deg >= 135 && angle_deg < 225) {
                direction = 0; /* to bottom */
            } else {
                direction = 2; /* to left */
            }

            has_direction = true;
            parserutils_vector_iterate(vector, ctx); /* Consume angle token */
            consumeWhitespace(vector, ctx);

            /* Expect comma after direction */
            token = parserutils_vector_iterate(vector, ctx);
            if (token == NULL || token->type != CSS_TOKEN_CHAR || lwc_string_length(token->idata) != 1 ||
                lwc_string_data(token->idata)[0] != ',') {
                *ctx = orig_ctx;
                return CSS_INVALID;
            }
            consumeWhitespace(vector, ctx);
        }
    }

    /* If no angle, try keyword direction (to top, to bottom, etc.) */
    if (!has_direction && parse_gradient_direction(c, vector, ctx, &direction) == CSS_OK) {
        has_direction = true;
        consumeWhitespace(vector, ctx);

        /* Expect comma after direction */
        token = parserutils_vector_iterate(vector, ctx);
        if (token == NULL || token->type != CSS_TOKEN_CHAR || lwc_string_length(token->idata) != 1 ||
            lwc_string_data(token->idata)[0] != ',') {
            *ctx = orig_ctx;
            return CSS_INVALID;
        }
        consumeWhitespace(vector, ctx);
    } else if (!has_direction) {
        /* No direction, restore checkpoint */
        *ctx = checkpoint;
    }

    error = parse_gradient_stops(c, vector, ctx, result, BACKGROUND_IMAGE_LINEAR_GRADIENT, direction);
    if (error != CSS_OK)
        *ctx = orig_ctx;
    return error;
}

/**
 * Parse a radial gradient
 *
 * \param c       Parsing context
 * \param vector  Vector of tokens
 * \param ctx     Token context
 * \param result  Output style
 * \return CSS_OK on success
 */
static css_error
parse_radial_gradient(css_language *c, const parserutils_vector *vector, int32_t *ctx, css_style *result)
{
    int32_t orig_ctx = *ctx;
    css_error error = CSS_OK;
    const css_token *token;
    uint32_t shape = 0; /* Default: circle */
    bool has_shape = false;
    bool match;
    lwc_string *keyword = NULL;

    /* Try to parse shape keyword (circle or ellipse) */
    token = parserutils_vector_peek(vector, *ctx);
    if (token && token->type == CSS_TOKEN_IDENT) {
        /* Check for "circle" */
        if (lwc_intern_string("circle", 6, &keyword) == lwc_error_ok) {
            if (lwc_string_caseless_isequal(token->idata, keyword, &match) == lwc_error_ok && match) {
                shape = 0; /* circle */
                has_shape = true;
                parserutils_vector_iterate(vector, ctx);
                consumeWhitespace(vector, ctx);
            }
            lwc_string_unref(keyword);
        }

        if (!has_shape && lwc_intern_string("ellipse", 7, &keyword) == lwc_error_ok) {
            if (lwc_string_caseless_isequal(token->idata, keyword, &match) == lwc_error_ok && match) {
                shape = 1; /* ellipse */
                has_shape = true;
                parserutils_vector_iterate(vector, ctx);
                consumeWhitespace(vector, ctx);
            }
            lwc_string_unref(keyword);
        }
    }

    /* If shape was specified, expect comma */
    if (has_shape) {
        token = parserutils_vector_peek(vector, *ctx);
        if (token && token->type == CSS_TOKEN_CHAR && lwc_string_length(token->idata) == 1 &&
            lwc_string_data(token->idata)[0] == ',') {
            parserutils_vector_iterate(vector, ctx);
            consumeWhitespace(vector, ctx);
        }
    }

    error = parse_gradient_stops(c, vector, ctx, result, BACKGROUND_IMAGE_RADIAL_GRADIENT, shape);
    if (error != CSS_OK)
        *ctx = orig_ctx;
    return error;
}

/**
 * Parse background-image
 *
 * Supports: none, inherit, initial, revert, unset, url(), linear-gradient(), radial-gradient()
 */
css_error
css__parse_background_image_item(css_language *c, const parserutils_vector *vector, int32_t *ctx, css_style *result)
{
    int32_t orig_ctx = *ctx;
    css_error error;
    const css_token *token;
    bool match;

    token = parserutils_vector_iterate(vector, ctx);
    if (token == NULL) {
        *ctx = orig_ctx;
        return CSS_INVALID;
    }

    /* Handle IDENT tokens (none, inherit, initial, revert, unset) */
    if (token->type == CSS_TOKEN_IDENT) {
        if ((lwc_string_caseless_isequal(token->idata, c->strings[INHERIT], &match) == lwc_error_ok && match)) {
            error = css_stylesheet_style_inherit(result, CSS_PROP_BACKGROUND_IMAGE);
        } else if ((lwc_string_caseless_isequal(token->idata, c->strings[NONE], &match) == lwc_error_ok && match)) {
            error = css__stylesheet_style_appendOPV(result, CSS_PROP_BACKGROUND_IMAGE, 0, BACKGROUND_IMAGE_NONE);
        } else if ((lwc_string_caseless_isequal(token->idata, c->strings[INITIAL], &match) == lwc_error_ok && match)) {
            error = css_stylesheet_style_initial(result, CSS_PROP_BACKGROUND_IMAGE);
        } else if ((lwc_string_caseless_isequal(token->idata, c->strings[REVERT], &match) == lwc_error_ok && match)) {
            error = css_stylesheet_style_revert(result, CSS_PROP_BACKGROUND_IMAGE);
        } else if ((lwc_string_caseless_isequal(token->idata, c->strings[UNSET], &match) == lwc_error_ok && match)) {
            error = css_stylesheet_style_unset(result, CSS_PROP_BACKGROUND_IMAGE);
        } else {
            error = CSS_INVALID;
        }

        if (error != CSS_OK)
            *ctx = orig_ctx;
        return error;
    }

    /* Handle URI tokens */
    if (token->type == CSS_TOKEN_URI) {
        lwc_string *uri = NULL;
        uint32_t uri_snumber;

        error = c->sheet->resolve(c->sheet->resolve_pw, c->sheet->url, token->idata, &uri);
        if (error != CSS_OK) {
            *ctx = orig_ctx;
            return error;
        }

        error = css__stylesheet_string_add(c->sheet, uri, &uri_snumber);
        if (error != CSS_OK) {
            *ctx = orig_ctx;
            return error;
        }

        error = css__stylesheet_style_appendOPV(result, CSS_PROP_BACKGROUND_IMAGE, 0, BACKGROUND_IMAGE_URI);
        if (error != CSS_OK) {
            *ctx = orig_ctx;
            return error;
        }

        error = css__stylesheet_style_append(result, uri_snumber);
        if (error != CSS_OK)
            *ctx = orig_ctx;
        return error;
    }

    /* Handle gradient functions */
    if (token->type == CSS_TOKEN_FUNCTION) {
        if ((lwc_string_caseless_isequal(token->idata, c->strings[LINEAR_GRADIENT], &match) == lwc_error_ok && match)) {
            return parse_linear_gradient(c, vector, ctx, result);
        }

        /* Radial gradient */
        if ((lwc_string_caseless_isequal(token->idata, c->strings[RADIAL_GRADIENT], &match) == lwc_error_ok && match)) {
            return parse_radial_gradient(c, vector, ctx, result);
        }
    }

    *ctx = orig_ctx;
    return CSS_INVALID;
}

css_error css__parse_background_image(css_language *c, const parserutils_vector *v, int32_t *ctx, css_style *result)
{
    return css__parse_background_list(c, v, ctx, result, CSS_PROP_BACKGROUND_IMAGE, css__parse_background_image_item);
}

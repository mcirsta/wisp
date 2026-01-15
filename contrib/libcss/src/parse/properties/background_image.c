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
#include "parse/properties/properties.h"
#include "parse/properties/utils.h"

#define MAX_GRADIENT_STOPS 10

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
    uint32_t stop_count = 0;
    struct {
        uint32_t color;
        css_fixed offset;
    } stops[MAX_GRADIENT_STOPS];
    uint16_t color_val;
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

    /* Parse color stops */
    while (stop_count < MAX_GRADIENT_STOPS) {
        consumeWhitespace(vector, ctx);

        /* Parse color */
        token = parserutils_vector_peek(vector, *ctx);
        error = css__parse_colour_specifier(c, vector, ctx, &color_val, &stops[stop_count].color);
        if (error != CSS_OK) {
            *ctx = orig_ctx;
            return error;
        }

        consumeWhitespace(vector, ctx);

        /* Check for optional offset (percentage) */
        token = parserutils_vector_peek(vector, *ctx);
        if (token && token->type == CSS_TOKEN_PERCENTAGE) {
            size_t consumed = 0;

            /* PERCENTAGE token idata does NOT include the '%' character */
            stops[stop_count].offset = css__number_from_lwc_string(token->idata, false, &consumed);

            parserutils_vector_iterate(vector, ctx); /* Consume percentage */
            consumeWhitespace(vector, ctx);
        } else {
            /* Auto-distribute stops evenly if no offset specified */
            /* We'll calculate this after all stops are parsed */
            stops[stop_count].offset = INTTOFIX(-1); /* Marker for auto */
        }

        stop_count++;

        /* Check for end of gradient or comma */
        token = parserutils_vector_peek(vector, *ctx);
        if (token == NULL) {
            *ctx = orig_ctx;
            return CSS_INVALID;
        }

        if (token->type == CSS_TOKEN_CHAR && lwc_string_length(token->idata) == 1 &&
            lwc_string_data(token->idata)[0] == ')') {
            /* End of gradient */
            parserutils_vector_iterate(vector, ctx); /* Consume ')' */
            break;
        }

        if (token->type == CSS_TOKEN_CHAR && lwc_string_length(token->idata) == 1 &&
            lwc_string_data(token->idata)[0] == ',') {
            parserutils_vector_iterate(vector, ctx); /* Consume ',' */
            continue;
        }

        /* Unexpected token */
        *ctx = orig_ctx;
        return CSS_INVALID;
    }

    /* Auto-distribute stops without explicit offsets */
    if (stop_count > 0) {
        int auto_count = 0;
        for (uint32_t i = 0; i < stop_count; i++) {
            if (stops[i].offset == INTTOFIX(-1))
                auto_count++;
        }

        if (auto_count > 0) {
            /* Simple distribution: first=0%, last=100%, others evenly spaced */
            for (uint32_t i = 0; i < stop_count; i++) {
                if (stops[i].offset == INTTOFIX(-1)) {
                    stops[i].offset = FDIV(INTTOFIX(i * 100), INTTOFIX(stop_count - 1));
                }
            }
        }
    }

    error = css__stylesheet_style_appendOPV(result, CSS_PROP_BACKGROUND_IMAGE, 0, BACKGROUND_IMAGE_LINEAR_GRADIENT);
    if (error != CSS_OK) {
        *ctx = orig_ctx;
        return error;
    }

    error = css__stylesheet_style_append(result, direction);
    if (error != CSS_OK) {
        *ctx = orig_ctx;
        return error;
    }

    error = css__stylesheet_style_append(result, stop_count);
    if (error != CSS_OK) {
        *ctx = orig_ctx;
        return error;
    }

    for (uint32_t i = 0; i < stop_count; i++) {
        error = css__stylesheet_style_append(result, stops[i].color);
        if (error != CSS_OK) {
            *ctx = orig_ctx;
            return error;
        }

        error = css__stylesheet_style_append(result, stops[i].offset);
        if (error != CSS_OK) {
            *ctx = orig_ctx;
            return error;
        }
    }

    return CSS_OK;
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
    uint32_t stop_count = 0;
    struct {
        uint32_t color;
        css_fixed offset;
    } stops[MAX_GRADIENT_STOPS];
    uint16_t color_val;
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

    /* Parse color stops */
    while (stop_count < MAX_GRADIENT_STOPS) {
        consumeWhitespace(vector, ctx);

        /* Parse color */
        token = parserutils_vector_peek(vector, *ctx);
        if (token == NULL) {
            break;
        }
        error = css__parse_colour_specifier(c, vector, ctx, &color_val, &stops[stop_count].color);
        if (error != CSS_OK) {
            *ctx = orig_ctx;
            return error;
        }

        consumeWhitespace(vector, ctx);

        /* Check for optional offset (percentage) */
        token = parserutils_vector_peek(vector, *ctx);
        if (token && token->type == CSS_TOKEN_PERCENTAGE) {
            size_t consumed = 0;
            stops[stop_count].offset = css__number_from_lwc_string(token->idata, false, &consumed);
            parserutils_vector_iterate(vector, ctx);
            consumeWhitespace(vector, ctx);
        } else {
            stops[stop_count].offset = INTTOFIX(-1); /* Marker for auto */
        }

        stop_count++;

        /* Check for end of gradient or comma */
        token = parserutils_vector_peek(vector, *ctx);
        if (token == NULL) {
            *ctx = orig_ctx;
            return CSS_INVALID;
        }

        if (token->type == CSS_TOKEN_CHAR && lwc_string_length(token->idata) == 1 &&
            lwc_string_data(token->idata)[0] == ')') {
            parserutils_vector_iterate(vector, ctx);
            break;
        }

        if (token->type == CSS_TOKEN_CHAR && lwc_string_length(token->idata) == 1 &&
            lwc_string_data(token->idata)[0] == ',') {
            parserutils_vector_iterate(vector, ctx);
            continue;
        }

        *ctx = orig_ctx;
        return CSS_INVALID;
    }

    /* Auto-distribute stops without explicit offsets */
    if (stop_count > 0) {
        for (uint32_t i = 0; i < stop_count; i++) {
            if (stops[i].offset == INTTOFIX(-1)) {
                stops[i].offset = FDIV(INTTOFIX(i * 100), INTTOFIX(stop_count - 1));
            }
        }
    }

    /* Write to bytecode */
    error = css__stylesheet_style_appendOPV(result, CSS_PROP_BACKGROUND_IMAGE, 0, BACKGROUND_IMAGE_RADIAL_GRADIENT);
    if (error != CSS_OK) {
        *ctx = orig_ctx;
        return error;
    }

    error = css__stylesheet_style_append(result, shape);
    if (error != CSS_OK) {
        *ctx = orig_ctx;
        return error;
    }

    error = css__stylesheet_style_append(result, stop_count);
    if (error != CSS_OK) {
        *ctx = orig_ctx;
        return error;
    }

    for (uint32_t i = 0; i < stop_count; i++) {
        error = css__stylesheet_style_append(result, stops[i].color);
        if (error != CSS_OK) {
            *ctx = orig_ctx;
            return error;
        }

        error = css__stylesheet_style_append(result, stops[i].offset);
        if (error != CSS_OK) {
            *ctx = orig_ctx;
            return error;
        }
    }

    return CSS_OK;
}

/**
 * Parse background-image
 *
 * Supports: none, inherit, initial, revert, unset, url(), linear-gradient(), radial-gradient()
 */
css_error
css__parse_background_image(css_language *c, const parserutils_vector *vector, int32_t *ctx, css_style *result)
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

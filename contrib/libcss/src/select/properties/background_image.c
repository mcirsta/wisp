/*
 * This file is part of LibCSS
 * Licensed under the MIT License,
 *		  http://www.opensource.org/licenses/mit-license.php
 * Copyright 2009 John-Mark Bell <jmb@netsurf-browser.org>
 */

#include <libcss/gradient.h>
#include "utils/utils.h"
#include "bytecode/bytecode.h"
#include "bytecode/opcodes.h"
#include "select/propget.h"
#include "select/propset.h"
#include <stdlib.h>

#include "select/properties/helpers.h"
#include "select/properties/properties.h"

css_error css__cascade_background_image(uint32_t opv, css_style *style, css_select_state *state)
{
    uint16_t value = CSS_BACKGROUND_IMAGE_INHERIT;
    lwc_string *uri = NULL;
    css_linear_gradient *gradient = NULL;

    if (hasFlagValue(opv) == false) {
        switch (getValue(opv)) {
        case BACKGROUND_IMAGE_NONE:
            value = CSS_BACKGROUND_IMAGE_NONE;
            break;
        case BACKGROUND_IMAGE_URI:
            value = CSS_BACKGROUND_IMAGE_IMAGE;
            css__stylesheet_string_get(style->sheet, *((css_code_t *)style->bytecode), &uri);
            advance_bytecode(style, sizeof(css_code_t));
            break;
        case BACKGROUND_IMAGE_LINEAR_GRADIENT:
            value = CSS_BACKGROUND_IMAGE_LINEAR_GRADIENT;
            /* Read gradient data from bytecode */
            {
                /* First 32-bit word: direction */
                uint32_t direction = *((css_code_t *)style->bytecode);
                advance_bytecode(style, sizeof(css_code_t));

                /* Second 32-bit word: stop count */
                uint32_t stop_count = *((css_code_t *)style->bytecode);
                advance_bytecode(style, sizeof(css_code_t));

                /* Allocate gradient structure */
                gradient = malloc(sizeof(css_linear_gradient));
                if (gradient == NULL) {
                    return CSS_NOMEM;
                }

                gradient->direction = (css_gradient_direction)direction;
                gradient->stop_count = (stop_count < CSS_LINEAR_GRADIENT_MAX_STOPS) ? stop_count
                                                                                    : CSS_LINEAR_GRADIENT_MAX_STOPS;

                /* Read color stops */
                for (uint32_t i = 0; i < stop_count && i < CSS_LINEAR_GRADIENT_MAX_STOPS; i++) {
                    /* Each stop: color (4 bytes) + offset (4 bytes fixed point) */
                    gradient->stops[i].color = *((css_code_t *)style->bytecode);
                    advance_bytecode(style, sizeof(css_code_t));

                    gradient->stops[i].offset = *((css_fixed *)style->bytecode);
                    advance_bytecode(style, sizeof(css_fixed));
                }
            }
            break;
        case BACKGROUND_IMAGE_RADIAL_GRADIENT:
            value = CSS_BACKGROUND_IMAGE_RADIAL_GRADIENT;
            /* Read radial gradient data from bytecode */
            {
                /* First 32-bit word: shape */
                uint32_t shape = *((css_code_t *)style->bytecode);
                advance_bytecode(style, sizeof(css_code_t));

                /* Second 32-bit word: stop count */
                uint32_t stop_count = *((css_code_t *)style->bytecode);
                advance_bytecode(style, sizeof(css_code_t));

                /* Allocate radial gradient structure */
                css_radial_gradient *radial = malloc(sizeof(css_radial_gradient));
                if (radial == NULL) {
                    return CSS_NOMEM;
                }

                radial->shape = (css_radial_gradient_shape)shape;
                radial->stop_count = (stop_count < CSS_RADIAL_GRADIENT_MAX_STOPS) ? stop_count
                                                                                  : CSS_RADIAL_GRADIENT_MAX_STOPS;

                /* Read color stops */
                for (uint32_t i = 0; i < stop_count && i < CSS_RADIAL_GRADIENT_MAX_STOPS; i++) {
                    radial->stops[i].color = *((css_code_t *)style->bytecode);
                    advance_bytecode(style, sizeof(css_code_t));

                    radial->stops[i].offset = *((css_fixed *)style->bytecode);
                    advance_bytecode(style, sizeof(css_fixed));
                }

                /* Store in state for later use */
                if (css__outranks_existing(getOpcode(opv), isImportant(opv), state, getFlagValue(opv))) {
                    css_error error = set_background_image(state->computed, value, NULL);
                    if (error != CSS_OK) {
                        free(radial);
                        return error;
                    }
                    if (state->computed->background_radial_gradient != NULL) {
                        free(state->computed->background_radial_gradient);
                    }
                    state->computed->background_radial_gradient = radial;
                } else {
                    free(radial);
                }
                return CSS_OK;
            }
        }
    }

    if (css__outranks_existing(getOpcode(opv), isImportant(opv), state, getFlagValue(opv))) {
        css_error error = set_background_image(state->computed, value, uri);
        if (error != CSS_OK) {
            if (gradient != NULL)
                free(gradient);
            return error;
        }

        /* Store gradient if present */
        if (value == CSS_BACKGROUND_IMAGE_LINEAR_GRADIENT) {
            /* Free any existing gradient */
            if (state->computed->background_gradient != NULL) {
                free(state->computed->background_gradient);
            }
            state->computed->background_gradient = gradient;
        }
    } else {
        /* Not cascading, free any allocated gradient */
        if (gradient != NULL)
            free(gradient);
    }

    return CSS_OK;
}

css_error css__set_background_image_from_hint(const css_hint *hint, css_computed_style *style)
{
    css_error error;

    error = set_background_image(style, hint->status, hint->data.string);

    lwc_string_unref(hint->data.string);

    return error;
}

css_error css__initial_background_image(css_select_state *state)
{
    /* Free any existing gradient */
    if (state->computed->background_gradient != NULL) {
        free(state->computed->background_gradient);
        state->computed->background_gradient = NULL;
    }
    return set_background_image(state->computed, CSS_BACKGROUND_IMAGE_NONE, NULL);
}

css_error css__copy_background_image(const css_computed_style *from, css_computed_style *to)
{
    lwc_string *url;
    uint8_t type = get_background_image(from, &url);

    if (from == to) {
        return CSS_OK;
    }

    css_error error = set_background_image(to, type, url);
    if (error != CSS_OK)
        return error;

    /* Copy linear gradient if present */
    if (type == CSS_BACKGROUND_IMAGE_LINEAR_GRADIENT && from->background_gradient != NULL) {
        /* Allocate and copy gradient */
        css_linear_gradient *new_gradient = malloc(sizeof(css_linear_gradient));
        if (new_gradient == NULL) {
            return CSS_NOMEM;
        }
        *new_gradient = *(from->background_gradient);

        /* Free any existing gradient in destination */
        if (to->background_gradient != NULL) {
            free(to->background_gradient);
        }
        to->background_gradient = new_gradient;
    } else {
        /* Free any existing linear gradient in destination */
        if (to->background_gradient != NULL) {
            free(to->background_gradient);
            to->background_gradient = NULL;
        }
    }

    /* Copy radial gradient if present */
    if (type == CSS_BACKGROUND_IMAGE_RADIAL_GRADIENT && from->background_radial_gradient != NULL) {
        /* Allocate and copy radial gradient */
        css_radial_gradient *new_radial = malloc(sizeof(css_radial_gradient));
        if (new_radial == NULL) {
            return CSS_NOMEM;
        }
        *new_radial = *(from->background_radial_gradient);

        /* Free any existing radial gradient in destination */
        if (to->background_radial_gradient != NULL) {
            free(to->background_radial_gradient);
        }
        to->background_radial_gradient = new_radial;
    } else {
        /* Free any existing radial gradient in destination */
        if (to->background_radial_gradient != NULL) {
            free(to->background_radial_gradient);
            to->background_radial_gradient = NULL;
        }
    }

    return CSS_OK;
}

css_error css__compose_background_image(
    const css_computed_style *parent, const css_computed_style *child, css_computed_style *result)
{
    lwc_string *url;
    uint8_t type = get_background_image(child, &url);
    const css_computed_style *from = (type == CSS_BACKGROUND_IMAGE_INHERIT) ? parent : child;

    return css__copy_background_image(from, result);
}

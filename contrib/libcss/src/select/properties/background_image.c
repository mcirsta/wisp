/*
 * This file is part of LibCSS
 * Licensed under the MIT License,
 *		  http://www.opensource.org/licenses/mit-license.php
 * Copyright 2009 John-Mark Bell <jmb@netsurf-browser.org>
 */

#include <libcss/gradient.h>
#include "utils/css_utils.h"
#include "bytecode/bytecode.h"
#include "bytecode/opcodes.h"
#include "select/propget.h"
#include "select/background.h"
#include "select/propset.h"
#include <stdlib.h>

#include "select/properties/helpers.h"
#include "select/properties/properties.h"

css_error css__cascade_background_image(uint32_t opv, css_style *style, css_select_state *state)
{
    return css__background_cascade(opv, style, state, CSS_BG_IMAGE);
}

css_error css__set_background_image_from_hint(const css_hint *hint, css_computed_style *style)
{
    css__background_clear(style, CSS_BG_IMAGE);
    free(style->background_gradient);
    free(style->background_radial_gradient);
    style->background_gradient = NULL;
    style->background_radial_gradient = NULL;
    css_error error;

    error = set_background_image(style, hint->status, hint->data.string);

    lwc_string_unref(hint->data.string);

    return error;
}

css_error css__initial_background_image(css_select_state *state)
{
    css__background_clear(state->computed, CSS_BG_IMAGE);
    /* Free any existing gradient */
    if (state->computed->background_gradient != NULL) {
        free(state->computed->background_gradient);
        state->computed->background_gradient = NULL;
    }
    free(state->computed->background_radial_gradient);
    state->computed->background_radial_gradient = NULL;
    return set_background_image(state->computed, CSS_BACKGROUND_IMAGE_NONE, NULL);
}

css_error css__copy_background_image(const css_computed_style *from, css_computed_style *to)
{
    lwc_string *url;
    uint8_t type = get_background_image(from, &url);

    if (from == to) {
        return CSS_OK;
    }
    css_error list_error = css__background_copy(from, to, CSS_BG_IMAGE);
    if (list_error != CSS_OK)
        return list_error;

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

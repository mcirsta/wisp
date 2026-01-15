/*
 * This file is part of LibCSS.
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 * Copyright 2024 The NetSurf Browser Project.
 */

#ifndef libcss_gradient_h_
#define libcss_gradient_h_

#ifdef __cplusplus
extern "C" {
#endif

#include <libcss/fpmath.h>
#include <libcss/types.h>

/**
 * Gradient direction for linear gradients
 */
typedef enum css_gradient_direction {
    CSS_GRADIENT_TO_BOTTOM = 0,
    CSS_GRADIENT_TO_TOP = 1,
    CSS_GRADIENT_TO_LEFT = 2,
    CSS_GRADIENT_TO_RIGHT = 3
} css_gradient_direction;

/**
 * Color stop for gradients
 */
typedef struct css_gradient_stop {
    css_color color; /* AARRGGBB color value */
    css_fixed offset; /* Position as percentage (0-100 as fixed point) */
} css_gradient_stop;

/**
 * Linear gradient definition
 */
#define CSS_LINEAR_GRADIENT_MAX_STOPS 10

typedef struct css_linear_gradient {
    css_gradient_direction direction;
    uint8_t stop_count;
    css_gradient_stop stops[CSS_LINEAR_GRADIENT_MAX_STOPS];
} css_linear_gradient;

/**
 * Radial gradient shape
 */
typedef enum css_radial_gradient_shape {
    CSS_RADIAL_SHAPE_CIRCLE = 0,
    CSS_RADIAL_SHAPE_ELLIPSE = 1
} css_radial_gradient_shape;

/**
 * Radial gradient definition
 */
#define CSS_RADIAL_GRADIENT_MAX_STOPS 10

typedef struct css_radial_gradient {
    css_radial_gradient_shape shape;
    uint8_t stop_count;
    css_gradient_stop stops[CSS_RADIAL_GRADIENT_MAX_STOPS];
} css_radial_gradient;

#ifdef __cplusplus
}
#endif

#endif

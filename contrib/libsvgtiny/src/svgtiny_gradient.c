/*
 * This file is part of Libsvgtiny
 * Licensed under the MIT License,
 *                http://opensource.org/licenses/mit-license.php
 * Copyright 2008 James Bursa <james@semichrome.net>
 */

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "svgtiny.h"
#include "svgtiny_internal.h"

#undef GRADIENT_DEBUG
/* set to add vector shape in output */
#undef GRADIENT_DEBUG_VECTOR

/* Forward declaration for triangle fallback (used when native gradients disabled) */
#ifndef NEOSURF_USE_NATIVE_GRADIENTS
static svgtiny_code svgtiny_gradient_add_path_triangles(
    float *p, unsigned int n, struct svgtiny_parse_state *state, bool has_fill_gradient, bool has_stroke_gradient);
#endif

/**
 * Get the bounding box of path.
 */
static void svgtiny_path_bbox(float *p, unsigned int n, float *x0, float *y0, float *x1, float *y1)
{
    unsigned int j;

    *x0 = *x1 = p[1];
    *y0 = *y1 = p[2];

    for (j = 0; j != n;) {
        unsigned int points = 0;
        unsigned int k;
        switch ((int)p[j]) {
        case svgtiny_PATH_MOVE:
        case svgtiny_PATH_LINE:
            points = 1;
            break;
        case svgtiny_PATH_CLOSE:
            points = 0;
            break;
        case svgtiny_PATH_BEZIER:
            points = 3;
            break;
        default:
            assert(0);
        }
        j++;
        for (k = 0; k != points; k++) {
            float x = p[j], y = p[j + 1];
            if (x < *x0)
                *x0 = x;
            else if (*x1 < x)
                *x1 = x;
            if (y < *y0)
                *y0 = y;
            else if (*y1 < y)
                *y1 = y;
            j += 2;
        }
    }
}


/**
 * Invert a transformation matrix.
 *
 * svg transform matrix
 * | a c e |
 * | b d f |
 * | 0 0 1 |
 */
static inline void
svgtiny_invert_matrix(const struct svgtiny_transformation_matrix *m, struct svgtiny_transformation_matrix *inv)
{
    float determinant = m->a * m->d - m->b * m->c;
    inv->a = m->d / determinant;
    inv->b = -m->b / determinant;
    inv->c = -m->c / determinant;
    inv->d = m->a / determinant;
    inv->e = (m->c * m->f - m->d * m->e) / determinant;
    inv->f = (m->b * m->e - m->a * m->f) / determinant;
}


static svgtiny_code
parse_gradient_stops(dom_element *linear, struct svgtiny_parse_state_gradient *grad, struct svgtiny_parse_state *state)
{
    unsigned int i = 0;
    dom_exception exc;
    dom_nodelist *stops;
    uint32_t listlen, stopnr;

    exc = dom_element_get_elements_by_tag_name(linear, state->interned_stop, &stops);
    if (exc != DOM_NO_ERR) {
        return svgtiny_LIBDOM_ERROR;
    }
    if (stops == NULL) {
        /* no stops */
        return svgtiny_OK;
    }

    exc = dom_nodelist_get_length(stops, &listlen);
    if (exc != DOM_NO_ERR) {
        dom_nodelist_unref(stops);
        return svgtiny_LIBDOM_ERROR;
    }

    for (stopnr = 0; stopnr < listlen; ++stopnr) {
        dom_element *stop;
        float offset = -1;
        svgtiny_colour color = svgtiny_TRANSPARENT;
        svgtiny_code res;
        struct svgtiny_parse_internal_operation ops[] = {
            {state->interned_stop_color, SVGTIOP_COLOR, NULL, &color, NULL},
            {state->interned_offset, SVGTIOP_OFFSET, NULL, &offset, NULL},
            {NULL, SVGTIOP_NONE, NULL, NULL},
        };


        exc = dom_nodelist_item(stops, stopnr, (dom_node **)(void *)&stop);
        if (exc != DOM_NO_ERR)
            continue;

        res = svgtiny_parse_attributes(stop, state, ops);
        if (res != svgtiny_OK) {
            /* stop attributes produced error, skip stop */
            continue;
        }
        ops[1].key = NULL; /* offset is not a style */
        svgtiny_parse_inline_style(stop, state, ops);

        if (offset != -1 && color != svgtiny_TRANSPARENT) {
#ifdef GRADIENT_DEBUG
            fprintf(stderr, "stop %g %x\n", offset, color);
#endif
            grad->gradient_stop[i].offset = offset;
            grad->gradient_stop[i].color = color;
            i++;
        }
        dom_node_unref(stop);
        if (i == svgtiny_MAX_STOPS)
            break;
    }

    dom_nodelist_unref(stops);

    if (i > 0) {
        grad->linear_gradient_stop_count = i;
    }

    return svgtiny_OK;
}


/**
 * Parse a <linearGradient> element node.
 *
 * http://www.w3.org/TR/SVG11/pservers#LinearGradients
 */
static svgtiny_code svgtiny_parse_linear_gradient(
    dom_element *linear, struct svgtiny_parse_state_gradient *grad, struct svgtiny_parse_state *state)
{
    dom_string *attr;
    dom_exception exc;
    svgtiny_code res;
    dom_element *ref; /* referenced element */

    res = svgtiny_parse_element_from_href(linear, state, &ref);
    if (res == svgtiny_OK && ref != NULL) {
        svgtiny_update_gradient(ref, state, grad);
        dom_node_unref(ref);
    }

    exc = dom_element_get_attribute(linear, state->interned_x1, &attr);
    if (exc == DOM_NO_ERR && attr != NULL) {
        dom_string_unref(grad->gradient_x1);
        grad->gradient_x1 = attr;
        attr = NULL;
    }

    exc = dom_element_get_attribute(linear, state->interned_y1, &attr);
    if (exc == DOM_NO_ERR && attr != NULL) {
        dom_string_unref(grad->gradient_y1);
        grad->gradient_y1 = attr;
        attr = NULL;
    }

    exc = dom_element_get_attribute(linear, state->interned_x2, &attr);
    if (exc == DOM_NO_ERR && attr != NULL) {
        dom_string_unref(grad->gradient_x2);
        grad->gradient_x2 = attr;
        attr = NULL;
    }

    exc = dom_element_get_attribute(linear, state->interned_y2, &attr);
    if (exc == DOM_NO_ERR && attr != NULL) {
        dom_string_unref(grad->gradient_y2);
        grad->gradient_y2 = attr;
        attr = NULL;
    }

    exc = dom_element_get_attribute(linear, state->interned_gradientUnits, &attr);
    if (exc == DOM_NO_ERR && attr != NULL) {
        grad->gradient_user_space_on_use = dom_string_isequal(attr, state->interned_userSpaceOnUse);
        dom_string_unref(attr);
    }

    exc = dom_element_get_attribute(linear, state->interned_gradientTransform, &attr);
    if (exc == DOM_NO_ERR && attr != NULL) {
        struct svgtiny_transformation_matrix tm = {.a = 1, .b = 0, .c = 0, .d = 1, .e = 0, .f = 0};
        svgtiny_parse_transform(dom_string_data(attr), dom_string_byte_length(attr), &tm);

#ifdef GRADIENT_DEBUG
        fprintf(stderr, "transform %g %g %g %g %g %g\n", tm.a, tm.b, tm.c, tm.d, tm.e, tm.f);
#endif
        grad->gradient_transform.a = tm.a;
        grad->gradient_transform.b = tm.b;
        grad->gradient_transform.c = tm.c;
        grad->gradient_transform.d = tm.d;
        grad->gradient_transform.e = tm.e;
        grad->gradient_transform.f = tm.f;
        dom_string_unref(attr);
    }

    return parse_gradient_stops(linear, grad, state);
}


/**
 * Parse a <radialGradient> element node.
 *
 * http://www.w3.org/TR/SVG11/pservers#RadialGradients
 *
 * Note: This is a simplified implementation that only parses gradient stops.
 * The actual radial gradient interpolation is not implemented - instead,
 * shapes with radial gradients will be filled with the first stop color
 * as a fallback.
 */
static svgtiny_code svgtiny_parse_radial_gradient(
    dom_element *radial, struct svgtiny_parse_state_gradient *grad, struct svgtiny_parse_state *state)
{
    svgtiny_code res;
    dom_element *ref; /* referenced element */

    /* Follow href reference to get stops from referenced gradient */
    res = svgtiny_parse_element_from_href(radial, state, &ref);
    if (res == svgtiny_OK && ref != NULL) {
        svgtiny_update_gradient(ref, state, grad);
        dom_node_unref(ref);
    }

    /* Parse stops from this element (may override referenced stops) */
    return parse_gradient_stops(radial, grad, state);
}

struct grad_point {
    float x, y, r;
};

struct grad_vector {
    float x0;
    float y0;
    float x1;
    float y1;
};


static void compute_gradient_vector(float *p, unsigned int n, struct svgtiny_parse_state *state,
    struct svgtiny_parse_state_gradient *grad, struct grad_vector *vector)
{
    float object_x0, object_y0, object_x1, object_y1;

    /* determine object bounding box */
    svgtiny_path_bbox(p, n, &object_x0, &object_y0, &object_x1, &object_y1);
#ifdef GRADIENT_DEBUG_VECTOR
    fprintf(stderr, "object bbox: (%g %g) (%g %g)\n", object_x0, object_y0, object_x1, object_y1);
#endif

    if (!grad->gradient_user_space_on_use) {
        svgtiny_parse_length(dom_string_data(grad->gradient_x1), dom_string_byte_length(grad->gradient_x1),
            object_x1 - object_x0, &vector->x0);

        svgtiny_parse_length(dom_string_data(grad->gradient_y1), dom_string_byte_length(grad->gradient_y1),
            object_y1 - object_y0, &vector->y0);

        svgtiny_parse_length(dom_string_data(grad->gradient_x2), dom_string_byte_length(grad->gradient_x2),
            object_x1 - object_x0, &vector->x1);

        svgtiny_parse_length(dom_string_data(grad->gradient_y2), dom_string_byte_length(grad->gradient_y2),
            object_y1 - object_y0, &vector->y1);

        vector->x0 += object_x0;
        vector->y0 += object_y0;
        vector->x1 += object_x0;
        vector->y1 += object_y0;
    } else {
        svgtiny_parse_length(dom_string_data(grad->gradient_x1), dom_string_byte_length(grad->gradient_x1),
            state->viewport_width, &vector->x0);
        svgtiny_parse_length(dom_string_data(grad->gradient_y1), dom_string_byte_length(grad->gradient_y1),
            state->viewport_height, &vector->y0);
        svgtiny_parse_length(dom_string_data(grad->gradient_x2), dom_string_byte_length(grad->gradient_x2),
            state->viewport_width, &vector->x1);
        svgtiny_parse_length(dom_string_data(grad->gradient_y2), dom_string_byte_length(grad->gradient_y2),
            state->viewport_height, &vector->y1);
    }

#ifdef GRADIENT_DEBUG_VECTOR
    fprintf(stderr, "gradient vector: (%g %g) => (%g %g)\n", vector->x0, vector->y0, vector->x1, vector->y1);
#endif
}


/* compute points on the path */
static svgtiny_code compute_grad_points(float *p, unsigned int n, struct svgtiny_transformation_matrix *trans,
    struct grad_vector *vector, struct svgtiny_list *pts, unsigned int *min_pt)
{
    float gradient_norm_squared;
    float gradient_dx;
    float gradient_dy;
    unsigned int j;
    float min_r = 1000;
    float x0 = 0, y0 = 0, x0_trans, y0_trans, r0; /* segment start point */
    float x1, y1, x1_trans, y1_trans, r1; /* segment end point */
    float c0x = 0, c0y = 0, c1x = 0, c1y = 0; /* segment control points (beziers only) */
    unsigned int steps = 10;

    gradient_dx = vector->x1 - vector->x0;
    gradient_dy = vector->y1 - vector->y0;

    gradient_norm_squared = gradient_dx * gradient_dx + gradient_dy * gradient_dy;

    /* r, r0, r1 are distance along gradient vector */
    for (j = 0; j != n;) {
        int segment_type = (int)p[j];
        struct grad_point *point;
        unsigned int z;

        if (segment_type == svgtiny_PATH_MOVE) {
            x0 = p[j + 1];
            y0 = p[j + 2];
            j += 3;
            continue;
        }

        assert(segment_type == svgtiny_PATH_CLOSE || segment_type == svgtiny_PATH_LINE ||
            segment_type == svgtiny_PATH_BEZIER);

        /* start point (x0, y0) */
        x0_trans = trans->a * x0 + trans->c * y0 + trans->e;
        y0_trans = trans->b * x0 + trans->d * y0 + trans->f;
        r0 = ((x0_trans - vector->x0) * gradient_dx + (y0_trans - vector->y0) * gradient_dy) / gradient_norm_squared;
        point = svgtiny_list_push(pts);
        if (!point) {
            return svgtiny_OUT_OF_MEMORY;
        }
        point->x = x0;
        point->y = y0;
        point->r = r0;
        if (r0 < min_r) {
            min_r = r0;
            *min_pt = svgtiny_list_size(pts) - 1;
        }

        /* end point (x1, y1) */
        if (segment_type == svgtiny_PATH_LINE) {
            x1 = p[j + 1];
            y1 = p[j + 2];
            j += 3;
        } else if (segment_type == svgtiny_PATH_CLOSE) {
            x1 = p[1];
            y1 = p[2];
            j++;
        } else /* svgtiny_PATH_BEZIER */ {
            c0x = p[j + 1];
            c0y = p[j + 2];
            c1x = p[j + 3];
            c1y = p[j + 4];
            x1 = p[j + 5];
            y1 = p[j + 6];
            j += 7;
        }
        x1_trans = trans->a * x1 + trans->c * y1 + trans->e;
        y1_trans = trans->b * x1 + trans->d * y1 + trans->f;
        r1 = ((x1_trans - vector->x0) * gradient_dx + (y1_trans - vector->y0) * gradient_dy) / gradient_norm_squared;

        /* determine steps from change in r */

        if (isnan(r0) || isnan(r1)) {
            steps = 1;
        } else {
            steps = ceilf(fabsf(r1 - r0) / 0.05);
        }

        if (steps == 0)
            steps = 1;
#ifdef GRADIENT_DEBUG
        fprintf(stderr, "r0 %g, r1 %g, steps %i\n", r0, r1, steps);
#endif

        /* loop through intermediate points */
        for (z = 1; z != steps; z++) {
            float t, x, y, x_trans, y_trans, r;
            struct grad_point *point;
            t = (float)z / (float)steps;
            if (segment_type == svgtiny_PATH_BEZIER) {
                x = (1 - t) * (1 - t) * (1 - t) * x0 + 3 * t * (1 - t) * (1 - t) * c0x + 3 * t * t * (1 - t) * c1x +
                    t * t * t * x1;
                y = (1 - t) * (1 - t) * (1 - t) * y0 + 3 * t * (1 - t) * (1 - t) * c0y + 3 * t * t * (1 - t) * c1y +
                    t * t * t * y1;
            } else {
                x = (1 - t) * x0 + t * x1;
                y = (1 - t) * y0 + t * y1;
            }
            x_trans = trans->a * x + trans->c * y + trans->e;
            y_trans = trans->b * x + trans->d * y + trans->f;
            r = ((x_trans - vector->x0) * gradient_dx + (y_trans - vector->y0) * gradient_dy) / gradient_norm_squared;
#ifdef GRADIENT_DEBUG
            fprintf(stderr, "(%g %g [%g]) ", x, y, r);
#endif
            point = svgtiny_list_push(pts);
            if (!point) {
                return svgtiny_OUT_OF_MEMORY;
            }
            point->x = x;
            point->y = y;
            point->r = r;
            if (r < min_r) {
                min_r = r;
                *min_pt = svgtiny_list_size(pts) - 1;
            }
        }
#ifdef GRADIENT_DEBUG
        fprintf(stderr, "\n");
#endif

        /* next segment start point is this segment end point */
        x0 = x1;
        y0 = y1;
    }
#ifdef GRADIENT_DEBUG
    fprintf(stderr, "pts size %i, min_pt %i, min_r %.3f\n", svgtiny_list_size(pts), *min_pt, min_r);
#endif
    return svgtiny_OK;
}


#ifdef GRADIENT_DEBUG
/**
 * show theoretical gradient strips for debugging
 */
static svgtiny_code add_debug_gradient_strips(struct svgtiny_parse_state *state, struct grad_vector *vector)
{
    float gradient_dx = vector->x1 - vector->x0;
    float gradient_dy = vector->y1 - vector->y0;
    unsigned int strips = 10;
    for (unsigned int z = 0; z != strips; z++) {
        float f0, fd, strip_x0, strip_y0, strip_dx, strip_dy;
        f0 = (float)z / (float)strips;
        fd = (float)1 / (float)strips;
        strip_x0 = vector->x0 + f0 * gradient_dx;
        strip_y0 = vector->y0 + f0 * gradient_dy;
        strip_dx = fd * gradient_dx;
        strip_dy = fd * gradient_dy;
        fprintf(stderr, "strip %i vector: (%g %g) + (%g %g)\n", z, strip_x0, strip_y0, strip_dx, strip_dy);

        float *p = malloc(13 * sizeof p[0]);
        if (!p)
            return svgtiny_OUT_OF_MEMORY;
        p[0] = svgtiny_PATH_MOVE;
        p[1] = strip_x0 + (strip_dy * 3);
        p[2] = strip_y0 - (strip_dx * 3);
        p[3] = svgtiny_PATH_LINE;
        p[4] = p[1] + strip_dx;
        p[5] = p[2] + strip_dy;
        p[6] = svgtiny_PATH_LINE;
        p[7] = p[4] - (strip_dy * 6);
        p[8] = p[5] + (strip_dx * 6);
        p[9] = svgtiny_PATH_LINE;
        p[10] = p[7] - strip_dx;
        p[11] = p[8] - strip_dy;
        p[12] = svgtiny_PATH_CLOSE;
        svgtiny_transform_path(p, 13, state);
        struct svgtiny_shape *shape = svgtiny_add_shape(state);
        if (!shape) {
            return svgtiny_OUT_OF_MEMORY;
        }
        shape->path = p;
        shape->path_length = 13;
        shape->fill = svgtiny_TRANSPARENT;
        shape->stroke = svgtiny_RGB(0, 0xff, 0);
        state->diagram->shape_count++;
    }
    return svgtiny_OK;
}


/**
 * render triangle vertices with r values for debugging
 */
static svgtiny_code add_debug_gradient_vertices(struct svgtiny_parse_state *state, struct svgtiny_list *pts)
{
    unsigned int i;

    for (i = 0; i != svgtiny_list_size(pts); i++) {
        struct grad_point *point = svgtiny_list_get(pts, i);
        struct svgtiny_shape *shape = svgtiny_add_shape(state);
        if (!shape)
            return svgtiny_OUT_OF_MEMORY;
        char *text = malloc(20);
        if (!text)
            return svgtiny_OUT_OF_MEMORY;
        sprintf(text, "%i=%.3f", i, point->r);
        shape->text = text;
        shape->text_x = state->ctm.a * point->x + state->ctm.c * point->y + state->ctm.e;
        shape->text_y = state->ctm.b * point->x + state->ctm.d * point->y + state->ctm.f;
        shape->fill = svgtiny_RGB(0, 0, 0);
        shape->stroke = svgtiny_TRANSPARENT;
        state->diagram->shape_count++;
    }
    return svgtiny_OK;
}

#endif


#ifdef GRADIENT_DEBUG_VECTOR
/**
 * render gradient vector for debugging
 */
static svgtiny_code add_debug_gradient_vector(struct svgtiny_parse_state *state, struct grad_vector *vector)
{
    float *p = malloc(7 * sizeof p[0]);
    if (!p)
        return svgtiny_OUT_OF_MEMORY;
    p[0] = svgtiny_PATH_MOVE;
    p[1] = vector->x0;
    p[2] = vector->y0;
    p[3] = svgtiny_PATH_LINE;
    p[4] = vector->x1;
    p[5] = vector->y1;
    p[6] = svgtiny_PATH_CLOSE;
    svgtiny_transform_path(p, 7, state);
    struct svgtiny_shape *shape = svgtiny_add_shape(state);
    if (!shape) {
        free(p);
        return svgtiny_OUT_OF_MEMORY;
    }
    shape->path = p;
    shape->path_length = 7;
    shape->fill = svgtiny_TRANSPARENT;
    shape->stroke = svgtiny_RGB(0xff, 0, 0);
    state->diagram->shape_count++;
    return svgtiny_OK;
}
#endif


struct grad_color {
    struct svgtiny_gradient_stop *stops;
    unsigned int stop_count;
    unsigned int current_stop;
    float last_stop_r;
    float current_stop_r;
    int red0;
    int green0;
    int blue0;
    int red1;
    int green1;
    int blue1;
};

/**
 * initalise gradiant colour state to first stop
 */
static inline void init_grad_color(struct svgtiny_parse_state_gradient *grad, struct grad_color *gc)
{
    assert(2 <= grad->linear_gradient_stop_count);

    gc->stops = grad->gradient_stop;
    gc->stop_count = grad->linear_gradient_stop_count;
    gc->current_stop = 0;
    gc->last_stop_r = 0;
    gc->current_stop_r = grad->gradient_stop[0].offset;
    gc->red0 = gc->red1 = svgtiny_RED(grad->gradient_stop[0].color);
    gc->green0 = gc->green1 = svgtiny_GREEN(grad->gradient_stop[0].color);
    gc->blue0 = gc->blue1 = svgtiny_BLUE(grad->gradient_stop[0].color);
}

/**
 * advance through stops to get to a target r value
 *
 * \param gc The gradient colour
 * \param tgt_t distance along gradient vector to advance to
 */
static inline svgtiny_colour advance_grad_color(struct grad_color *gc, float tgt_r)
{
    svgtiny_colour current_color = 0;

    while ((gc->current_stop != gc->stop_count) && (gc->current_stop_r < tgt_r)) {
        gc->current_stop++;
        if (gc->current_stop == gc->stop_count) {
            /* no more stops to try */
            break;
        }
        gc->red0 = gc->red1;
        gc->green0 = gc->green1;
        gc->blue0 = gc->blue1;

        gc->red1 = svgtiny_RED(gc->stops[gc->current_stop].color);
        gc->green1 = svgtiny_GREEN(gc->stops[gc->current_stop].color);
        gc->blue1 = svgtiny_BLUE(gc->stops[gc->current_stop].color);

        gc->last_stop_r = gc->current_stop_r;
        gc->current_stop_r = gc->stops[gc->current_stop].offset;
    }

    /* compute the colour for the target r */
    if (gc->current_stop == 0) {
        current_color = gc->stops[0].color;
    } else if (gc->current_stop == gc->stop_count) {
        current_color = gc->stops[gc->stop_count - 1].color;
    } else {
        float stop_r = (tgt_r - gc->last_stop_r) / (gc->current_stop_r - gc->last_stop_r);
        current_color = svgtiny_RGB((int)((1 - stop_r) * gc->red0 + stop_r * gc->red1),
            (int)((1 - stop_r) * gc->green0 + stop_r * gc->green1),
            (int)((1 - stop_r) * gc->blue0 + stop_r * gc->blue1));
    }
    return current_color;
}


/**
 * add triangles to fill a gradient
 */
static svgtiny_code add_gradient_triangles(struct svgtiny_parse_state *state, struct svgtiny_parse_state_gradient *grad,
    struct svgtiny_list *pts, unsigned int min_pt)
{
    unsigned int t, a, b;
    struct grad_color gc;

    init_grad_color(grad, &gc);

    t = min_pt;
    a = (min_pt + 1) % svgtiny_list_size(pts);
    b = min_pt == 0 ? svgtiny_list_size(pts) - 1 : min_pt - 1;
    while (a != b) {
        struct grad_point *point_t = svgtiny_list_get(pts, t);
        struct grad_point *point_a = svgtiny_list_get(pts, a);
        struct grad_point *point_b = svgtiny_list_get(pts, b);
        float mean_r = (point_t->r + point_a->r + point_b->r) / 3;
        float *p;
        struct svgtiny_shape *shape;
#ifdef GRADIENT_DEBUG
        fprintf(stderr,
            "triangle: t %i %.3f a %i %.3f b %i %.3f "
            "mean_r %.3f\n",
            t, pts[t].r, a, pts[a].r, b, pts[b].r, mean_r);
#endif
        p = malloc(10 * sizeof p[0]);
        if (!p)
            return svgtiny_OUT_OF_MEMORY;
        p[0] = svgtiny_PATH_MOVE;
        p[1] = point_t->x;
        p[2] = point_t->y;
        p[3] = svgtiny_PATH_LINE;
        p[4] = point_a->x;
        p[5] = point_a->y;
        p[6] = svgtiny_PATH_LINE;
        p[7] = point_b->x;
        p[8] = point_b->y;
        p[9] = svgtiny_PATH_CLOSE;
        svgtiny_transform_path(p, 10, state);
        shape = svgtiny_add_shape(state);
        if (!shape) {
            free(p);
            return svgtiny_OUT_OF_MEMORY;
        }
        shape->path = p;
        shape->path_length = 10;
        shape->fill = advance_grad_color(&gc, mean_r);
        shape->stroke = svgtiny_TRANSPARENT;
#ifdef GRADIENT_DEBUG
        shape->stroke = svgtiny_RGB(0, 0, 0xff);
#endif
        state->diagram->shape_count++;

        if (point_a->r < point_b->r) {
            t = a;
            a = (a + 1) % svgtiny_list_size(pts);
        } else {
            t = b;
            b = b == 0 ? svgtiny_list_size(pts) - 1 : b - 1;
        }
    }
    return svgtiny_OK;
}


/**
 * add lines to stroke a gradient
 */
static svgtiny_code add_gradient_lines(struct svgtiny_parse_state *state, struct svgtiny_parse_state_gradient *grad,
    struct svgtiny_list *pts, unsigned int min_pt)
{
    struct grad_color gc;
    unsigned int b;
    struct grad_point *point_a;
    struct grad_point *point_b;
    int dir;

    for (dir = 0; dir < 2; dir++) {
        init_grad_color(grad, &gc);

        point_a = svgtiny_list_get(pts, min_pt);
        if (dir == 0) {
            b = (min_pt + 1) % svgtiny_list_size(pts);
        } else {
            b = min_pt == 0 ? svgtiny_list_size(pts) - 1 : min_pt - 1;
        }
        point_b = svgtiny_list_get(pts, b);
        while (point_a->r <= point_b->r) {
            float mean_r = (point_a->r + point_b->r) / 2;
            float *p;
            struct svgtiny_shape *shape;

#ifdef GRADIENT_DEBUG
            fprintf(stderr,
                "line: a (x:%.3f y:%.3f r:%.3f) "
                "b (i:%i x:%.3f y:%.3f r:%.3f) "
                "mean_r %.3f\n",
                point_a->x, point_a->y, point_a->r, b, point_b->x, point_b->y, point_b->r, mean_r);
#endif

            p = malloc(7 * sizeof p[0]);
            if (!p) {
                return svgtiny_OUT_OF_MEMORY;
            }
            p[0] = svgtiny_PATH_MOVE;
            p[1] = point_a->x;
            p[2] = point_a->y;
            p[3] = svgtiny_PATH_LINE;
            p[4] = point_b->x;
            p[5] = point_b->y;
            p[6] = svgtiny_PATH_CLOSE;
            svgtiny_transform_path(p, 7, state);
            shape = svgtiny_add_shape(state);
            if (!shape) {
                free(p);
                return svgtiny_OUT_OF_MEMORY;
            }
            shape->path = p;
            shape->path_length = 7;
            shape->fill = svgtiny_TRANSPARENT;
            shape->stroke = advance_grad_color(&gc, mean_r);

            state->diagram->shape_count++;

            point_a = point_b;
            if (dir == 0) {
                b = (b + 1) % svgtiny_list_size(pts);
            } else {
                b = b == 0 ? svgtiny_list_size(pts) - 1 : b - 1;
            }
            point_b = svgtiny_list_get(pts, b);
        }
    }

    return svgtiny_OK;
}

/**
 * update a gradient from a dom element
 */
svgtiny_code svgtiny_update_gradient(
    dom_element *grad_element, struct svgtiny_parse_state *state, struct svgtiny_parse_state_gradient *grad)
{
    dom_string *name;
    dom_exception exc;
    svgtiny_code res = svgtiny_OK;

    grad->linear_gradient_stop_count = 0;
    if (grad->gradient_x1 != NULL)
        dom_string_unref(grad->gradient_x1);
    if (grad->gradient_y1 != NULL)
        dom_string_unref(grad->gradient_y1);
    if (grad->gradient_x2 != NULL)
        dom_string_unref(grad->gradient_x2);
    if (grad->gradient_y2 != NULL)
        dom_string_unref(grad->gradient_y2);
    grad->gradient_x1 = dom_string_ref(state->interned_zero_percent);
    grad->gradient_y1 = dom_string_ref(state->interned_zero_percent);
    grad->gradient_x2 = dom_string_ref(state->interned_hundred_percent);
    grad->gradient_y2 = dom_string_ref(state->interned_zero_percent);
    grad->gradient_user_space_on_use = false;
    grad->gradient_transform.a = 1;
    grad->gradient_transform.b = 0;
    grad->gradient_transform.c = 0;
    grad->gradient_transform.d = 1;
    grad->gradient_transform.e = 0;
    grad->gradient_transform.f = 0;

    exc = dom_node_get_node_name(grad_element, &name);
    if (exc != DOM_NO_ERR) {
        return svgtiny_SVG_ERROR;
    }

    /* ensure element is a linear or radial gradient */
    if (dom_string_isequal(name, state->interned_linearGradient)) {
        res = svgtiny_parse_linear_gradient(grad_element, grad, state);
    } else if (dom_string_isequal(name, state->interned_radialGradient)) {
        res = svgtiny_parse_radial_gradient(grad_element, grad, state);
    }

    dom_string_unref(name);

#ifdef GRADIENT_DEBUG
    fprintf(stderr, "linear_gradient_stop_count %i\n", grad->linear_gradient_stop_count);
#endif

    return res;
}


/**
 * Add a path with a linear gradient stroke to the diagram.
 *
 */
svgtiny_code svgtiny_gradient_add_stroke_path(float *p, unsigned int n, struct svgtiny_parse_state *state)
{
    struct grad_vector vector; /* gradient vector */
    struct svgtiny_transformation_matrix trans;
    struct svgtiny_list *pts;
    unsigned int min_pt = 0;
    struct svgtiny_parse_state_gradient *grad;
    svgtiny_code res;

    assert(state->stroke == svgtiny_LINEAR_GRADIENT);
    /* original path should not be stroked as a shape is added here */
    state->stroke = svgtiny_TRANSPARENT;

    grad = &state->stroke_grad;

    /* at least two stops are required to form a valid gradient */
    if (grad->linear_gradient_stop_count < 2) {
        if (grad->linear_gradient_stop_count == 1) {
            /* stroke the shape with first stop colour */
            state->stroke = grad->gradient_stop[0].color;
        }
        return svgtiny_OK;
    }

    compute_gradient_vector(p, n, state, grad, &vector);

    svgtiny_invert_matrix(&grad->gradient_transform, &trans);

    /* compute points on the path for vertices */
    pts = svgtiny_list_create(sizeof(struct grad_point));
    if (!pts) {
        return svgtiny_OUT_OF_MEMORY;
    }
    res = compute_grad_points(p, n, &trans, &vector, pts, &min_pt);
    if (res != svgtiny_OK) {
        svgtiny_list_free(pts);
        return res;
    }

    /* There must be at least a single point for the gradient */
    if (svgtiny_list_size(pts) == 0) {
        svgtiny_list_free(pts);
        return svgtiny_OK;
    }

    /* render lines */
    res = add_gradient_lines(state, grad, pts, min_pt);

    svgtiny_list_free(pts);

    return res;
}

/**
 * Add a path with gradient fill and/or stroke to the diagram.
 *
 * This unified function creates ONE shape with both fill and stroke gradient
 * info. The caller continues to own the original path. The renderer (svg.c)
 * decides at render time whether to use native gradient APIs or fallback.
 *
 * \param p      Path data (not consumed - caller keeps ownership)
 * \param n      Path length
 * \param state  Parse state with fill/stroke gradient info
 * \return svgtiny_OK on success
 */
svgtiny_code svgtiny_gradient_add_path(float *p, unsigned int n, struct svgtiny_parse_state *state)
{
    bool has_fill_gradient = (state->fill == svgtiny_LINEAR_GRADIENT);
    bool has_stroke_gradient = (state->stroke == svgtiny_LINEAR_GRADIENT);

    /* At least one gradient must be present */
    if (!has_fill_gradient && !has_stroke_gradient) {
        return svgtiny_OK; /* Nothing to do */
    }

    /* Handle degenerate fill gradient (< 2 stops) */
    if (has_fill_gradient && state->fill_grad.linear_gradient_stop_count < 2) {
        if (state->fill_grad.linear_gradient_stop_count == 1) {
            state->fill = state->fill_grad.gradient_stop[0].color;
        } else {
            state->fill = svgtiny_TRANSPARENT;
        }
        has_fill_gradient = false;
    }

    /* Handle degenerate stroke gradient (< 2 stops) */
    if (has_stroke_gradient && state->stroke_grad.linear_gradient_stop_count < 2) {
        if (state->stroke_grad.linear_gradient_stop_count == 1) {
            state->stroke = state->stroke_grad.gradient_stop[0].color;
        } else {
            state->stroke = svgtiny_TRANSPARENT;
        }
        has_stroke_gradient = false;
    }

    /* If both gradients were degenerate, nothing more to do */
    if (!has_fill_gradient && !has_stroke_gradient) {
        return svgtiny_OK;
    }

#ifdef NEOSURF_USE_NATIVE_GRADIENTS
    /*
     * NATIVE GRADIENT PATH: Store gradient info in shape struct.
     * The renderer (svg.c) will call native plotter APIs to render.
     */
    struct svgtiny_shape *shape;
    struct grad_vector fill_vector, stroke_vector;

    /* Compute gradient vectors */
    if (has_fill_gradient) {
        compute_gradient_vector(p, n, state, &state->fill_grad, &fill_vector);
    }
    if (has_stroke_gradient) {
        compute_gradient_vector(p, n, state, &state->stroke_grad, &stroke_vector);
    }

    /* Create shape with gradient info */
    shape = svgtiny_add_shape(state);
    if (!shape) {
        return svgtiny_OUT_OF_MEMORY;
    }

    /* Store a COPY of the path (caller continues to own original p) */
    float *path_copy = malloc(n * sizeof(float));
    if (!path_copy) {
        return svgtiny_OUT_OF_MEMORY;
    }
    memcpy(path_copy, p, n * sizeof(float));
    svgtiny_transform_path(path_copy, n, state);

    shape->path = path_copy;
    shape->path_length = n;
    shape->fill = has_fill_gradient ? svgtiny_TRANSPARENT : state->fill;
    shape->stroke = has_stroke_gradient ? svgtiny_TRANSPARENT : state->stroke;
    shape->stroke_width = state->stroke_width;

    /* Store fill gradient info */
    if (has_fill_gradient) {
        struct svgtiny_parse_state_gradient *grad = &state->fill_grad;

        shape->fill_gradient_type = svgtiny_GRADIENT_LINEAR;
        shape->fill_grad_x1 = fill_vector.x0;
        shape->fill_grad_y1 = fill_vector.y0;
        shape->fill_grad_x2 = fill_vector.x1;
        shape->fill_grad_y2 = fill_vector.y1;

        shape->fill_grad_stop_count = grad->linear_gradient_stop_count;
        shape->fill_grad_stops = malloc(shape->fill_grad_stop_count * sizeof(struct svgtiny_gradient_stop_public));
        if (!shape->fill_grad_stops) {
            shape->fill_gradient_type = svgtiny_GRADIENT_NONE;
            shape->fill_grad_stop_count = 0;
            return svgtiny_OUT_OF_MEMORY;
        }
        for (unsigned int i = 0; i < shape->fill_grad_stop_count; i++) {
            shape->fill_grad_stops[i].offset = grad->gradient_stop[i].offset;
            shape->fill_grad_stops[i].color = grad->gradient_stop[i].color;
        }

        shape->fill_grad_transform[0] = grad->gradient_transform.a;
        shape->fill_grad_transform[1] = grad->gradient_transform.b;
        shape->fill_grad_transform[2] = grad->gradient_transform.c;
        shape->fill_grad_transform[3] = grad->gradient_transform.d;
        shape->fill_grad_transform[4] = grad->gradient_transform.e;
        shape->fill_grad_transform[5] = grad->gradient_transform.f;
    }

    /* Store stroke gradient info */
    if (has_stroke_gradient) {
        struct svgtiny_parse_state_gradient *grad = &state->stroke_grad;

        shape->stroke_gradient_type = svgtiny_GRADIENT_LINEAR;
        shape->stroke_grad_x1 = stroke_vector.x0;
        shape->stroke_grad_y1 = stroke_vector.y0;
        shape->stroke_grad_x2 = stroke_vector.x1;
        shape->stroke_grad_y2 = stroke_vector.y1;

        shape->stroke_grad_stop_count = grad->linear_gradient_stop_count;
        shape->stroke_grad_stops = malloc(shape->stroke_grad_stop_count * sizeof(struct svgtiny_gradient_stop_public));
        if (!shape->stroke_grad_stops) {
            shape->stroke_gradient_type = svgtiny_GRADIENT_NONE;
            shape->stroke_grad_stop_count = 0;
            return svgtiny_OUT_OF_MEMORY;
        }
        for (unsigned int i = 0; i < shape->stroke_grad_stop_count; i++) {
            shape->stroke_grad_stops[i].offset = grad->gradient_stop[i].offset;
            shape->stroke_grad_stops[i].color = grad->gradient_stop[i].color;
        }

        shape->stroke_grad_transform[0] = grad->gradient_transform.a;
        shape->stroke_grad_transform[1] = grad->gradient_transform.b;
        shape->stroke_grad_transform[2] = grad->gradient_transform.c;
        shape->stroke_grad_transform[3] = grad->gradient_transform.d;
        shape->stroke_grad_transform[4] = grad->gradient_transform.e;
        shape->stroke_grad_transform[5] = grad->gradient_transform.f;
    }

    state->diagram->shape_count++;

    /* Set fill/stroke to TRANSPARENT so caller (svgtiny_add_path) doesn't
     * create another shape on top. The gradients are handled by our shape. */
    if (has_fill_gradient) {
        state->fill = svgtiny_TRANSPARENT;
    }
    if (has_stroke_gradient) {
        state->stroke = svgtiny_TRANSPARENT;
    }

    return svgtiny_OK;

#else /* !NEOSURF_USE_NATIVE_GRADIENTS */
    /*
     * TRIANGLE FALLBACK PATH: Generate triangle/line shapes.
     * No native plotter needed - shapes are rendered directly.
     */
    return svgtiny_gradient_add_path_triangles(p, n, state, has_fill_gradient, has_stroke_gradient);
#endif /* NEOSURF_USE_NATIVE_GRADIENTS */
}


#ifndef NEOSURF_USE_NATIVE_GRADIENTS
/**
 * Triangle-based gradient fallback implementation.
 *
 * This function generates triangle/line shapes to approximate gradients
 * when native gradient rendering is not available. Used for platforms
 * without hardware gradient support (e.g., GDI on Windows).
 *
 * \param p                  Path data
 * \param n                  Path length
 * \param state              Parse state
 * \param has_fill_gradient  True if fill needs gradient
 * \param has_stroke_gradient True if stroke needs gradient
 * \return svgtiny_OK on success
 */
static svgtiny_code svgtiny_gradient_add_path_triangles(
    float *p, unsigned int n, struct svgtiny_parse_state *state, bool has_fill_gradient, bool has_stroke_gradient)
{
    svgtiny_code res = svgtiny_OK;

    /* Handle fill gradient with triangles */
    if (has_fill_gradient) {
        struct grad_vector vector;
        struct svgtiny_transformation_matrix trans;
        struct svgtiny_list *pts;
        unsigned int min_pt = 0;
        struct svgtiny_parse_state_gradient *grad = &state->fill_grad;

        /* Mark fill as transparent so normal path doesn't overpaint */
        state->fill = svgtiny_TRANSPARENT;

        compute_gradient_vector(p, n, state, grad, &vector);
        svgtiny_invert_matrix(&grad->gradient_transform, &trans);

        pts = svgtiny_list_create(sizeof(struct grad_point));
        if (!pts) {
            return svgtiny_OUT_OF_MEMORY;
        }
        res = compute_grad_points(p, n, &trans, &vector, pts, &min_pt);
        if (res != svgtiny_OK) {
            svgtiny_list_free(pts);
            return res;
        }

        if (svgtiny_list_size(pts) > 0) {
            res = add_gradient_triangles(state, grad, pts, min_pt);
        }
        svgtiny_list_free(pts);

        if (res != svgtiny_OK) {
            return res;
        }
    }

    /* Handle stroke gradient with lines */
    if (has_stroke_gradient) {
        struct grad_vector vector;
        struct svgtiny_transformation_matrix trans;
        struct svgtiny_list *pts;
        unsigned int min_pt = 0;
        struct svgtiny_parse_state_gradient *grad = &state->stroke_grad;

        /* Mark stroke as transparent so normal path doesn't overpaint */
        state->stroke = svgtiny_TRANSPARENT;

        compute_gradient_vector(p, n, state, grad, &vector);
        svgtiny_invert_matrix(&grad->gradient_transform, &trans);

        pts = svgtiny_list_create(sizeof(struct grad_point));
        if (!pts) {
            return svgtiny_OUT_OF_MEMORY;
        }
        res = compute_grad_points(p, n, &trans, &vector, pts, &min_pt);
        if (res != svgtiny_OK) {
            svgtiny_list_free(pts);
            return res;
        }

        if (svgtiny_list_size(pts) > 0) {
            res = add_gradient_lines(state, grad, pts, min_pt);
        }
        svgtiny_list_free(pts);

        if (res != svgtiny_OK) {
            return res;
        }
    }

    return res;
}
#endif /* !NEOSURF_USE_NATIVE_GRADIENTS */

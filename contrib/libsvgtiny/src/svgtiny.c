/*
 * This file is part of Libsvgtiny
 * Licensed under the MIT License,
 *                http://opensource.org/licenses/mit-license.php
 * Copyright 2008-2009 James Bursa <james@semichrome.net>
 * Copyright 2012 Daniel Silverstone <dsilvers@netsurf-browser.org>
 * Copyright 2024 Vincent Sanders <vince@netsurf-browser.org>
 */

#include <assert.h>
#include <math.h>
#include <setjmp.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <dom/dom.h>
#include <xml/xmlparser.h>

#include "svgtiny.h"
#include "svgtiny_internal.h"


/* circles are approximated with four bezier curves
 *
 * The optimal distance to the control points is the constant (4/3)*tan(pi/(2n))
 * (where n is 4)
 */
#define KAPPA 0.5522847498

/* debug flag which enables printing of libdom parse messages to stderr */
#undef PRINT_XML_PARSE_MSG

#if (defined(_GNU_SOURCE) && !defined(__APPLE__) || defined(__amigaos4__) || defined(__HAIKU__) ||                     \
    (defined(_POSIX_C_SOURCE) && ((_POSIX_C_SOURCE - 0) >= 200809L)))
#define HAVE_STRNDUP
#else
#undef HAVE_STRNDUP
char *svgtiny_strndup(const char *s, size_t n);
#define strndup svgtiny_strndup
#endif

static svgtiny_code parse_element(dom_element *element, struct svgtiny_parse_state *state);

#ifndef HAVE_STRNDUP
char *svgtiny_strndup(const char *s, size_t n)
{
    size_t len;
    char *s2;

    for (len = 0; len != n && s[len]; len++)
        continue;

    s2 = malloc(len + 1);
    if (s2 == NULL)
        return NULL;

    memcpy(s2, s, len);
    s2[len] = '\0';

    return s2;
}
#endif


/**
 * Parse x, y, width, and height attributes, if present.
 */
static void svgtiny_parse_position_attributes(
    dom_element *node, struct svgtiny_parse_state state, float *x, float *y, float *width, float *height)
{
    struct svgtiny_parse_internal_operation styles[] = {
        {/* x */
            state.interned_x, SVGTIOP_LENGTH, &state.viewport_width, x},
        {/* y */
            state.interned_y, SVGTIOP_LENGTH, &state.viewport_height, y},
        {/* width */
            state.interned_width, SVGTIOP_LENGTH, &state.viewport_width, width},
        {/* height */
            state.interned_height, SVGTIOP_LENGTH, &state.viewport_height, height},
        {NULL, SVGTIOP_NONE, NULL, NULL},
    };

    *x = 0;
    *y = 0;
    *width = state.viewport_width;
    *height = state.viewport_height;

    svgtiny_parse_attributes(node, &state, styles);
}


/**
 * Call this to ref the strings in a gradient state.
 */
static void svgtiny_grad_string_ref(struct svgtiny_parse_state_gradient *grad)
{
    if (grad->gradient_x1 != NULL) {
        dom_string_ref(grad->gradient_x1);
    }
    if (grad->gradient_y1 != NULL) {
        dom_string_ref(grad->gradient_y1);
    }
    if (grad->gradient_x2 != NULL) {
        dom_string_ref(grad->gradient_x2);
    }
    if (grad->gradient_y2 != NULL) {
        dom_string_ref(grad->gradient_y2);
    }
}


/**
 * Call this to clean up the strings in a gradient state.
 */
static void svgtiny_grad_string_cleanup(struct svgtiny_parse_state_gradient *grad)
{
    if (grad->gradient_x1 != NULL) {
        dom_string_unref(grad->gradient_x1);
        grad->gradient_x1 = NULL;
    }
    if (grad->gradient_y1 != NULL) {
        dom_string_unref(grad->gradient_y1);
        grad->gradient_y1 = NULL;
    }
    if (grad->gradient_x2 != NULL) {
        dom_string_unref(grad->gradient_x2);
        grad->gradient_x2 = NULL;
    }
    if (grad->gradient_y2 != NULL) {
        dom_string_unref(grad->gradient_y2);
        grad->gradient_y2 = NULL;
    }
}


/**
 * Set the local externally-stored parts of a parse state.
 * Call this in functions that made a new state on the stack.
 * Doesn't make own copy of global state, such as the interned string list.
 */
static void svgtiny_setup_state_local(struct svgtiny_parse_state *state)
{
    svgtiny_grad_string_ref(&(state->fill_grad));
    svgtiny_grad_string_ref(&(state->stroke_grad));
}


/**
 * Cleanup the local externally-stored parts of a parse state.
 * Call this in functions that made a new state on the stack.
 * Doesn't cleanup global state, such as the interned string list.
 */
static void svgtiny_cleanup_state_local(struct svgtiny_parse_state *state)
{
    svgtiny_grad_string_cleanup(&(state->fill_grad));
    svgtiny_grad_string_cleanup(&(state->stroke_grad));
    if (state->stroke_dasharray != NULL) {
        free(state->stroke_dasharray);
        state->stroke_dasharray = NULL;
        state->stroke_dasharray_count = 0;
        state->stroke_dasharray_set = false;
    }
}


static void ignore_msg(uint32_t severity, void *ctx, const char *msg, ...)
{
#ifdef PRINT_XML_PARSE_MSG
#include <stdarg.h>
    va_list l;

    UNUSED(ctx);

    va_start(l, msg);

    fprintf(stderr, "%" PRIu32 ": ", severity);
    vfprintf(stderr, msg, l);
    fprintf(stderr, "\n");

    va_end(l);
#else
    UNUSED(severity);
    UNUSED(ctx);
    UNUSED(msg);
#endif
}


/**
 * Parse paint attributes, if present.
 */
static void svgtiny_parse_paint_attributes(dom_element *node, struct svgtiny_parse_state *state)
{
    const struct svgtiny_keyword_map fill_rule_map[] = {
        {"nonzero", svgtiny_FILL_NONZERO}, {"evenodd", svgtiny_FILL_EVENODD}, {NULL, 0}};
    const struct svgtiny_keyword_map linecap_map[] = {
        {"butt", svgtiny_CAP_BUTT}, {"round", svgtiny_CAP_ROUND}, {"square", svgtiny_CAP_SQUARE}, {NULL, 0}};
    const struct svgtiny_keyword_map linejoin_map[] = {
        {"miter", svgtiny_JOIN_MITER}, {"round", svgtiny_JOIN_ROUND}, {"bevel", svgtiny_JOIN_BEVEL}, {NULL, 0}};
    struct svgtiny_dasharray_dest dashdest = {&state->stroke_dasharray, &state->stroke_dasharray_count};
    struct svgtiny_parse_internal_operation ops[] = {
        {/* fill color */
            state->interned_fill, SVGTIOP_PAINT, &state->fill_grad, &state->fill},
        {/* stroke color */
            state->interned_stroke, SVGTIOP_PAINT, &state->stroke_grad, &state->stroke},
        {/* stroke width */
            state->interned_stroke_width, SVGTIOP_INTLENGTH, &state->viewport_width, &state->stroke_width},
        {/* fill-opacity */
            state->interned_fill_opacity, SVGTIOP_OFFSET, NULL, &state->fill_opacity, &state->fill_opacity_set},
        {/* stroke-opacity */
            state->interned_stroke_opacity, SVGTIOP_OFFSET, NULL, &state->stroke_opacity, &state->stroke_opacity_set},
        {/* fill-rule */
            state->interned_fill_rule, SVGTIOP_KEYWORD, (void *)fill_rule_map, &state->fill_rule,
            &state->fill_rule_set},
        {/* stroke-linecap */
            state->interned_stroke_linecap, SVGTIOP_KEYWORD, (void *)linecap_map, &state->stroke_linecap,
            &state->stroke_linecap_set},
        {/* stroke-linejoin */
            state->interned_stroke_linejoin, SVGTIOP_KEYWORD, (void *)linejoin_map, &state->stroke_linejoin,
            &state->stroke_linejoin_set},
        {/* stroke-miterlimit */
            state->interned_stroke_miterlimit, SVGTIOP_NUMBER, NULL, &state->stroke_miterlimit,
            &state->stroke_miterlimit_set},
        {/* stroke-dasharray */
            state->interned_stroke_dasharray, SVGTIOP_DASHARRAY, NULL, &dashdest, &state->stroke_dasharray_set},
        {/* stroke-dashoffset */
            state->interned_stroke_dashoffset, SVGTIOP_NUMBER, NULL, &state->stroke_dashoffset,
            &state->stroke_dashoffset_set},
        {NULL, SVGTIOP_NONE, NULL, NULL},
    };

    svgtiny_parse_attributes(node, state, ops);
    svgtiny_parse_inline_style(node, state, ops);
}


/**
 * Parse font attributes, if present.
 */
static void svgtiny_parse_font_attributes(dom_element *node, struct svgtiny_parse_state *state)
{
    struct svgtiny_parse_internal_operation ops[] = {
        {/* font-size */
            state->interned_font_size, SVGTIOP_LENGTH, &state->viewport_height, /* use viewport height as reference */
            &state->font_size},
        {NULL, SVGTIOP_NONE, NULL, NULL},
    };
    dom_string *attr;
    dom_exception exc;

    svgtiny_parse_attributes(node, state, ops);
    svgtiny_parse_inline_style(node, state, ops);

    /* Parse text-anchor (start, middle, end) */
    exc = dom_element_get_attribute(node, state->interned_text_anchor, &attr);
    if (exc == DOM_NO_ERR && attr != NULL) {
        const char *data = dom_string_data(attr);
        if (strncmp(data, "middle", 6) == 0) {
            state->text_anchor = svgtiny_TEXT_ANCHOR_MIDDLE;
        } else if (strncmp(data, "end", 3) == 0) {
            state->text_anchor = svgtiny_TEXT_ANCHOR_END;
        } else {
            state->text_anchor = svgtiny_TEXT_ANCHOR_START;
        }
        dom_string_unref(attr);
    }

    /* Parse font-weight (bold, normal) */
    exc = dom_element_get_attribute(node, state->interned_font_weight, &attr);
    if (exc == DOM_NO_ERR && attr != NULL) {
        const char *data = dom_string_data(attr);
        if (strncmp(data, "bold", 4) == 0 || strncmp(data, "700", 3) == 0 || strncmp(data, "800", 3) == 0 ||
            strncmp(data, "900", 3) == 0) {
            state->font_weight_bold = true;
        } else {
            state->font_weight_bold = false;
        }
        dom_string_unref(attr);
    }
}


/**
 * Parse transform attributes, if present.
 *
 * http://www.w3.org/TR/SVG11/coords#TransformAttribute
 */
static void svgtiny_parse_transform_attributes(dom_element *node, struct svgtiny_parse_state *state)
{
    dom_string *attr;
    dom_exception exc;

    exc = dom_element_get_attribute(node, state->interned_transform, &attr);
    if (exc == DOM_NO_ERR && attr != NULL) {
        svgtiny_parse_transform(dom_string_data(attr), dom_string_byte_length(attr), &state->ctm);

        dom_string_unref(attr);
    }
}


/**
 * Add a path to the svgtiny_diagram.
 */
static svgtiny_code svgtiny_add_path(float *p, unsigned int n, struct svgtiny_parse_state *state)
{
    struct svgtiny_shape *shape;
    svgtiny_code res = svgtiny_OK;

    /* Handle gradient fills and/or strokes with unified function */
    if (state->fill == svgtiny_LINEAR_GRADIENT || state->stroke == svgtiny_LINEAR_GRADIENT) {
        res = svgtiny_gradient_add_path(p, n, state);
        if (res != svgtiny_OK) {
            free(p);
            return res;
        }
    }

    /* if stroke and fill are transparent do not add a shape */
    if ((state->fill == svgtiny_TRANSPARENT) && (state->stroke == svgtiny_TRANSPARENT)) {
        free(p);
        return res;
    }

    svgtiny_transform_path(p, n, state);

    shape = svgtiny_add_shape(state);
    if (shape == NULL) {
        free(p);
        return svgtiny_OUT_OF_MEMORY;
    }
    shape->path = p;
    shape->path_length = n;
    state->diagram->shape_count++;


    return svgtiny_OK;
}


/**
 * return svgtiny_OK if source is an ancestor of target else
 * svgtiny_LIBDOM_ERROR
 */
static svgtiny_code is_ancestor_node(dom_node *source, dom_node *target)
{
    dom_node *parent;
    dom_exception exc;

    parent = dom_node_ref(target);
    while (parent != NULL) {
        dom_node *next = NULL;
        if (parent == source) {
            dom_node_unref(parent);
            return svgtiny_OK;
        }
        exc = dom_node_get_parent_node(parent, &next);
        dom_node_unref(parent);
        if (exc != DOM_NO_ERR) {
            break;
        }

        parent = next;
    }
    return svgtiny_LIBDOM_ERROR;
}


/**
 * Parse a <path> element node.
 *
 * https://svgwg.org/svg2-draft/paths.html#PathElement
 */
static svgtiny_code svgtiny_parse_path(dom_element *path, struct svgtiny_parse_state state)
{
    svgtiny_code res;
    dom_string *path_d_str;
    dom_exception exc;
    float *p; /* path elemets */
    unsigned int i;

    svgtiny_setup_state_local(&state);

    svgtiny_parse_paint_attributes(path, &state);
    svgtiny_parse_transform_attributes(path, &state);

    /* read d attribute */
    exc = dom_element_get_attribute(path, state.interned_d, &path_d_str);
    if (exc != DOM_NO_ERR) {
        state.diagram->error_line = -1; /* path->line; */
        state.diagram->error_message = "path: error retrieving d attribute";
        svgtiny_cleanup_state_local(&state);
        return svgtiny_SVG_ERROR;
    }

    if (path_d_str == NULL) {
        state.diagram->error_line = -1; /* path->line; */
        state.diagram->error_message = "path: missing d attribute";
        svgtiny_cleanup_state_local(&state);
        return svgtiny_SVG_ERROR;
    }

    res = svgtiny_parse_path_data(dom_string_data(path_d_str), dom_string_byte_length(path_d_str), &p, &i);
    if (res != svgtiny_OK) {
        svgtiny_cleanup_state_local(&state);
        return res;
    }

    if (i <= 4) {
        /* insufficient segments in path treated as none */
        if (i > 0) {
            free(p);
        }
        res = svgtiny_OK;
    } else {
        res = svgtiny_add_path(p, i, &state);
    }

    dom_string_unref(path_d_str);
    svgtiny_cleanup_state_local(&state);

    return res;
}


/**
 * Parse a <rect> element node.
 *
 * http://www.w3.org/TR/SVG11/shapes#RectElement
 */
static svgtiny_code svgtiny_parse_rect(dom_element *rect, struct svgtiny_parse_state state)
{
    svgtiny_code err;
    float x, y, width, height;
    float *p;

    svgtiny_setup_state_local(&state);

    svgtiny_parse_position_attributes(rect, state, &x, &y, &width, &height);
    svgtiny_parse_paint_attributes(rect, &state);
    svgtiny_parse_transform_attributes(rect, &state);

    p = malloc(13 * sizeof p[0]);
    if (!p) {
        svgtiny_cleanup_state_local(&state);
        return svgtiny_OUT_OF_MEMORY;
    }

    p[0] = svgtiny_PATH_MOVE;
    p[1] = x;
    p[2] = y;
    p[3] = svgtiny_PATH_LINE;
    p[4] = x + width;
    p[5] = y;
    p[6] = svgtiny_PATH_LINE;
    p[7] = x + width;
    p[8] = y + height;
    p[9] = svgtiny_PATH_LINE;
    p[10] = x;
    p[11] = y + height;
    p[12] = svgtiny_PATH_CLOSE;

    err = svgtiny_add_path(p, 13, &state);

    svgtiny_cleanup_state_local(&state);

    return err;
}


/**
 * Parse a <circle> element node.
 */
static svgtiny_code svgtiny_parse_circle(dom_element *circle, struct svgtiny_parse_state state)
{
    svgtiny_code err;
    float x = 0, y = 0, r = -1;
    float *p;
    struct svgtiny_parse_internal_operation ops[] = {
        {state.interned_cx, SVGTIOP_LENGTH, &state.viewport_width, &x},
        {state.interned_cy, SVGTIOP_LENGTH, &state.viewport_height, &y},
        {state.interned_r, SVGTIOP_LENGTH, &state.viewport_width, &r},
        {NULL, SVGTIOP_NONE, NULL, NULL},
    };

    svgtiny_setup_state_local(&state);

    err = svgtiny_parse_attributes(circle, &state, ops);
    if (err != svgtiny_OK) {
        svgtiny_cleanup_state_local(&state);
        return err;
    }

    svgtiny_parse_paint_attributes(circle, &state);
    svgtiny_parse_transform_attributes(circle, &state);

    if (r < 0) {
        state.diagram->error_line = -1; /* circle->line; */
        state.diagram->error_message = "circle: r missing or negative";
        svgtiny_cleanup_state_local(&state);
        return svgtiny_SVG_ERROR;
    }
    if (r == 0) {
        svgtiny_cleanup_state_local(&state);
        return svgtiny_OK;
    }

    p = malloc(32 * sizeof p[0]);
    if (!p) {
        svgtiny_cleanup_state_local(&state);
        return svgtiny_OUT_OF_MEMORY;
    }

    p[0] = svgtiny_PATH_MOVE;
    p[1] = x + r;
    p[2] = y;
    p[3] = svgtiny_PATH_BEZIER;
    p[4] = x + r;
    p[5] = y + r * KAPPA;
    p[6] = x + r * KAPPA;
    p[7] = y + r;
    p[8] = x;
    p[9] = y + r;
    p[10] = svgtiny_PATH_BEZIER;
    p[11] = x - r * KAPPA;
    p[12] = y + r;
    p[13] = x - r;
    p[14] = y + r * KAPPA;
    p[15] = x - r;
    p[16] = y;
    p[17] = svgtiny_PATH_BEZIER;
    p[18] = x - r;
    p[19] = y - r * KAPPA;
    p[20] = x - r * KAPPA;
    p[21] = y - r;
    p[22] = x;
    p[23] = y - r;
    p[24] = svgtiny_PATH_BEZIER;
    p[25] = x + r * KAPPA;
    p[26] = y - r;
    p[27] = x + r;
    p[28] = y - r * KAPPA;
    p[29] = x + r;
    p[30] = y;
    p[31] = svgtiny_PATH_CLOSE;

    err = svgtiny_add_path(p, 32, &state);

    svgtiny_cleanup_state_local(&state);

    return err;
}


/**
 * Parse an <ellipse> element node.
 */
static svgtiny_code svgtiny_parse_ellipse(dom_element *ellipse, struct svgtiny_parse_state state)
{
    svgtiny_code err;
    float x = 0, y = 0, rx = -1, ry = -1;
    float *p;
    struct svgtiny_parse_internal_operation ops[] = {
        {state.interned_cx, SVGTIOP_LENGTH, &state.viewport_width, &x},
        {state.interned_cy, SVGTIOP_LENGTH, &state.viewport_height, &y},
        {state.interned_rx, SVGTIOP_LENGTH, &state.viewport_width, &rx},
        {state.interned_ry, SVGTIOP_LENGTH, &state.viewport_height, &ry},
        {NULL, SVGTIOP_NONE, NULL, NULL},
    };

    svgtiny_setup_state_local(&state);

    err = svgtiny_parse_attributes(ellipse, &state, ops);
    if (err != svgtiny_OK) {
        svgtiny_cleanup_state_local(&state);
        return err;
    }

    svgtiny_parse_paint_attributes(ellipse, &state);
    svgtiny_parse_transform_attributes(ellipse, &state);

    if (rx < 0 || ry < 0) {
        state.diagram->error_line = -1; /* ellipse->line; */
        state.diagram->error_message = "ellipse: rx or ry missing "
                                       "or negative";
        svgtiny_cleanup_state_local(&state);
        return svgtiny_SVG_ERROR;
    }
    if (rx == 0 || ry == 0) {
        svgtiny_cleanup_state_local(&state);
        return svgtiny_OK;
    }

    p = malloc(32 * sizeof p[0]);
    if (!p) {
        svgtiny_cleanup_state_local(&state);
        return svgtiny_OUT_OF_MEMORY;
    }

    p[0] = svgtiny_PATH_MOVE;
    p[1] = x + rx;
    p[2] = y;
    p[3] = svgtiny_PATH_BEZIER;
    p[4] = x + rx;
    p[5] = y + ry * KAPPA;
    p[6] = x + rx * KAPPA;
    p[7] = y + ry;
    p[8] = x;
    p[9] = y + ry;
    p[10] = svgtiny_PATH_BEZIER;
    p[11] = x - rx * KAPPA;
    p[12] = y + ry;
    p[13] = x - rx;
    p[14] = y + ry * KAPPA;
    p[15] = x - rx;
    p[16] = y;
    p[17] = svgtiny_PATH_BEZIER;
    p[18] = x - rx;
    p[19] = y - ry * KAPPA;
    p[20] = x - rx * KAPPA;
    p[21] = y - ry;
    p[22] = x;
    p[23] = y - ry;
    p[24] = svgtiny_PATH_BEZIER;
    p[25] = x + rx * KAPPA;
    p[26] = y - ry;
    p[27] = x + rx;
    p[28] = y - ry * KAPPA;
    p[29] = x + rx;
    p[30] = y;
    p[31] = svgtiny_PATH_CLOSE;

    err = svgtiny_add_path(p, 32, &state);

    svgtiny_cleanup_state_local(&state);

    return err;
}


/**
 * Parse a <line> element node.
 */
static svgtiny_code svgtiny_parse_line(dom_element *line, struct svgtiny_parse_state state)
{
    svgtiny_code err;
    float x1 = 0, y1 = 0, x2 = 0, y2 = 0;
    float *p;
    struct svgtiny_parse_internal_operation ops[] = {
        {state.interned_x1, SVGTIOP_LENGTH, &state.viewport_width, &x1},
        {state.interned_y1, SVGTIOP_LENGTH, &state.viewport_height, &y1},
        {state.interned_x2, SVGTIOP_LENGTH, &state.viewport_width, &x2},
        {state.interned_y2, SVGTIOP_LENGTH, &state.viewport_height, &y2},
        {NULL, SVGTIOP_NONE, NULL, NULL},
    };

    svgtiny_setup_state_local(&state);

    err = svgtiny_parse_attributes(line, &state, ops);
    if (err != svgtiny_OK) {
        svgtiny_cleanup_state_local(&state);
        return err;
    }

    svgtiny_parse_paint_attributes(line, &state);
    svgtiny_parse_transform_attributes(line, &state);

    /* Lines are stroke-only elements in SVG - they have no fill area.
     * Force fill to transparent regardless of any inherited or explicit fill. */
    state.fill = svgtiny_TRANSPARENT;

    p = malloc(7 * sizeof p[0]);
    if (!p) {
        svgtiny_cleanup_state_local(&state);
        return svgtiny_OUT_OF_MEMORY;
    }

    p[0] = svgtiny_PATH_MOVE;
    p[1] = x1;
    p[2] = y1;
    p[3] = svgtiny_PATH_LINE;
    p[4] = x2;
    p[5] = y2;
    p[6] = svgtiny_PATH_CLOSE;

    err = svgtiny_add_path(p, 7, &state);

    svgtiny_cleanup_state_local(&state);

    return err;
}


/**
 * Parse a <polyline> or <polygon> element node.
 *
 * http://www.w3.org/TR/SVG11/shapes#PolylineElement
 * http://www.w3.org/TR/SVG11/shapes#PolygonElement
 */
static svgtiny_code svgtiny_parse_poly(dom_element *poly, struct svgtiny_parse_state state, bool polygon)
{
    svgtiny_code err;
    dom_string *points_str;
    dom_exception exc;
    float *pointv;
    unsigned int pointc;

    svgtiny_setup_state_local(&state);

    svgtiny_parse_paint_attributes(poly, &state);
    svgtiny_parse_transform_attributes(poly, &state);

    exc = dom_element_get_attribute(poly, state.interned_points, &points_str);
    if (exc != DOM_NO_ERR) {
        svgtiny_cleanup_state_local(&state);
        return svgtiny_LIBDOM_ERROR;
    }

    if (points_str == NULL) {
        state.diagram->error_line = -1; /* poly->line; */
        state.diagram->error_message = "polyline/polygon: missing points attribute";
        svgtiny_cleanup_state_local(&state);
        return svgtiny_SVG_ERROR;
    }

    /* allocate space for path: it will never have more elements than bytes
     * in the string.
     */
    pointc = dom_string_byte_length(points_str);
    pointv = malloc(sizeof pointv[0] * pointc);
    if (pointv == NULL) {
        svgtiny_cleanup_state_local(&state);
        return svgtiny_OUT_OF_MEMORY;
    }

    err = svgtiny_parse_poly_points(dom_string_data(points_str), dom_string_byte_length(points_str), pointv, &pointc);
    dom_string_unref(points_str);
    if (err != svgtiny_OK) {
        free(pointv);
        state.diagram->error_line = -1; /* poly->line; */
        state.diagram->error_message = "polyline/polygon: failed to parse points";
    } else {
        if (pointc > 0) {
            pointv[0] = svgtiny_PATH_MOVE;
        }
        if (polygon) {
            pointv[pointc++] = svgtiny_PATH_CLOSE;
        }

        err = svgtiny_add_path(pointv, pointc, &state);
    }
    svgtiny_cleanup_state_local(&state);

    return err;
}


/**
 * Parse a <text> or <tspan> element node.
 */
static svgtiny_code svgtiny_parse_text(dom_element *text, struct svgtiny_parse_state state)
{
    float x, y, width, height;
    float dx = 0, dy = 0;
    float px, py;
    dom_node *child;
    dom_exception exc;
    dom_string *attr;
    bool x_set = false, y_set = false;

    svgtiny_setup_state_local(&state);

    /* Check if x attribute is explicitly set */
    exc = dom_element_get_attribute(text, state.interned_x, &attr);
    if (exc == DOM_NO_ERR && attr != NULL) {
        x_set = true;
        dom_string_unref(attr);
    }
    /* Check if y attribute is explicitly set */
    exc = dom_element_get_attribute(text, state.interned_y, &attr);
    if (exc == DOM_NO_ERR && attr != NULL) {
        y_set = true;
        dom_string_unref(attr);
    }

    /* Parse dx attribute (relative x offset) */
    exc = dom_element_get_attribute(text, state.interned_dx, &attr);
    if (exc == DOM_NO_ERR && attr != NULL) {
        svgtiny_parse_length(
            dom_string_data(attr), dom_string_byte_length(attr), (int)state.viewport_width, state.font_size, &dx);
        dom_string_unref(attr);
    }
    /* Parse dy attribute (relative y offset) */
    exc = dom_element_get_attribute(text, state.interned_dy, &attr);
    if (exc == DOM_NO_ERR && attr != NULL) {
        svgtiny_parse_length(
            dom_string_data(attr), dom_string_byte_length(attr), (int)state.viewport_height, state.font_size, &dy);
        dom_string_unref(attr);
    }

    /* Parse position attributes (fills with defaults if not present) */
    svgtiny_parse_position_attributes(text, state, &x, &y, &width, &height);
    svgtiny_parse_paint_attributes(text, &state);
    svgtiny_parse_font_attributes(text, &state);
    svgtiny_parse_transform_attributes(text, &state);

    /* If x or y were explicitly set, use them; otherwise inherit from
     * parent's text position */
    if (x_set) {
        px = state.ctm.a * x + state.ctm.c * y + state.ctm.e;
        state.text_x = px;
        state.text_x_set = true;
    } else if (state.text_x_set) {
        /* Inherit from parent */
        px = state.text_x;
    } else {
        /* First text element, use default (0,0 transformed) */
        px = state.ctm.a * x + state.ctm.c * y + state.ctm.e;
        state.text_x = px;
    }

    if (y_set) {
        py = state.ctm.b * x + state.ctm.d * y + state.ctm.f;
        state.text_y = py;
        state.text_y_set = true;
    } else if (state.text_y_set) {
        /* Inherit from parent */
        py = state.text_y;
    } else {
        /* First text element, use default (0,0 transformed) */
        py = state.ctm.b * x + state.ctm.d * y + state.ctm.f;
        state.text_y = py;
    }

    /* Apply dx/dy offsets (scale by CTM) */
    px += state.ctm.a * dx;
    py += state.ctm.d * dy;

    exc = dom_node_get_first_child(text, &child);
    if (exc != DOM_NO_ERR) {
        return svgtiny_LIBDOM_ERROR;
        svgtiny_cleanup_state_local(&state);
    }
    while (child != NULL) {
        dom_node *next;
        dom_node_type nodetype;
        svgtiny_code code = svgtiny_OK;

        exc = dom_node_get_node_type(child, &nodetype);
        if (exc != DOM_NO_ERR) {
            dom_node_unref(child);
            svgtiny_cleanup_state_local(&state);
            return svgtiny_LIBDOM_ERROR;
        }
        if (nodetype == DOM_ELEMENT_NODE) {
            dom_string *nodename;
            exc = dom_node_get_node_name(child, &nodename);
            if (exc != DOM_NO_ERR) {
                dom_node_unref(child);
                svgtiny_cleanup_state_local(&state);
                return svgtiny_LIBDOM_ERROR;
            }
            if (dom_string_caseless_isequal(nodename, state.interned_tspan))
                code = svgtiny_parse_text((dom_element *)child, state);
            dom_string_unref(nodename);
        } else if (nodetype == DOM_TEXT_NODE) {
            struct svgtiny_shape *shape = svgtiny_add_shape(&state);
            dom_string *content;
            if (shape == NULL) {
                dom_node_unref(child);
                svgtiny_cleanup_state_local(&state);
                return svgtiny_OUT_OF_MEMORY;
            }
            exc = dom_text_get_whole_text(child, &content);
            if (exc != DOM_NO_ERR) {
                dom_node_unref(child);
                svgtiny_cleanup_state_local(&state);
                return svgtiny_LIBDOM_ERROR;
            }
            if (content != NULL) {
                shape->text = strndup(dom_string_data(content), dom_string_byte_length(content));
                dom_string_unref(content);
            } else {
                shape->text = strdup("");
            }
            shape->text_x = px;
            shape->text_y = py;
            state.diagram->shape_count++;
        }

        if (code != svgtiny_OK) {
            dom_node_unref(child);
            svgtiny_cleanup_state_local(&state);
            return code;
        }
        exc = dom_node_get_next_sibling(child, &next);
        dom_node_unref(child);
        if (exc != DOM_NO_ERR) {
            svgtiny_cleanup_state_local(&state);
            return svgtiny_LIBDOM_ERROR;
        }
        child = next;
    }

    svgtiny_cleanup_state_local(&state);

    return svgtiny_OK;
}


/**
 * Parse a <use> element node.
 *
 * https://www.w3.org/TR/SVG2/struct.html#UseElement
 */
static svgtiny_code svgtiny_parse_use(dom_element *use, struct svgtiny_parse_state state)
{
    svgtiny_code res;
    dom_element *ref; /* referenced element */

    svgtiny_setup_state_local(&state);

    res = svgtiny_parse_element_from_href(use, &state, &ref);
    if (res != svgtiny_OK) {
        svgtiny_cleanup_state_local(&state);
        return res;
    }

    if (ref != NULL) {
        /* found the reference */

        /**
         * If the referenced element is a ancestor of the ‘use’ element,
         * then this is an invalid circular reference and the ‘use’
         * element is in error.
         */
        res = is_ancestor_node((dom_node *)ref, (dom_node *)use);
        if (res != svgtiny_OK) {
            res = parse_element(ref, &state);
        }
        dom_node_unref(ref);
    }

    svgtiny_cleanup_state_local(&state);

    return svgtiny_OK;
}


/**
 * Parse a <svg> or <g> element node.
 */
static svgtiny_code svgtiny_parse_svg(dom_element *svg, struct svgtiny_parse_state state)
{
    float x, y, width, height;
    dom_string *view_box;
    dom_element *child;
    dom_exception exc;

    svgtiny_setup_state_local(&state);

    svgtiny_parse_position_attributes(svg, state, &x, &y, &width, &height);
    svgtiny_parse_paint_attributes(svg, &state);
    svgtiny_parse_font_attributes(svg, &state);

    state.aspect_ratio_align = svgtiny_ASPECT_RATIO_XMID_YMID;
    state.aspect_ratio_mos = svgtiny_ASPECT_RATIO_MEET;

    exc = dom_element_get_attribute(svg, state.interned_preserveAspectRatio, &view_box);
    if (exc == DOM_NO_ERR && view_box != NULL) {
        svgtiny_parse_preserveAspectRatio(dom_string_data(view_box), dom_string_byte_length(view_box), &state);
        dom_string_unref(view_box);
    }

    exc = dom_element_get_attribute(svg, state.interned_viewBox, &view_box);
    if (exc != DOM_NO_ERR) {
        svgtiny_cleanup_state_local(&state);
        return svgtiny_LIBDOM_ERROR;
    }

    if (view_box) {
        svgtiny_parse_viewbox(dom_string_data(view_box), dom_string_byte_length(view_box), &state);
        dom_string_unref(view_box);
    }

    svgtiny_parse_transform_attributes(svg, &state);

    exc = dom_node_get_first_child(svg, (dom_node **)(void *)&child);
    if (exc != DOM_NO_ERR) {
        svgtiny_cleanup_state_local(&state);
        return svgtiny_LIBDOM_ERROR;
    }
    while (child != NULL) {
        dom_element *next;
        dom_node_type nodetype;
        svgtiny_code code = svgtiny_OK;

        exc = dom_node_get_node_type(child, &nodetype);
        if (exc != DOM_NO_ERR) {
            dom_node_unref(child);
            return svgtiny_LIBDOM_ERROR;
        }
        if (nodetype == DOM_ELEMENT_NODE) {
            code = parse_element(child, &state);
        }
        if (code != svgtiny_OK) {
            dom_node_unref(child);
            svgtiny_cleanup_state_local(&state);
            return code;
        }
        exc = dom_node_get_next_sibling(child, (dom_node **)(void *)&next);
        dom_node_unref(child);
        if (exc != DOM_NO_ERR) {
            svgtiny_cleanup_state_local(&state);
            return svgtiny_LIBDOM_ERROR;
        }
        child = next;
    }

    svgtiny_cleanup_state_local(&state);
    return svgtiny_OK;
}


static svgtiny_code parse_element(dom_element *element, struct svgtiny_parse_state *state)
{
    dom_exception exc;
    dom_string *nodename;
    svgtiny_code code = svgtiny_OK;
    ;

    exc = dom_node_get_node_name(element, &nodename);
    if (exc != DOM_NO_ERR) {
        return svgtiny_LIBDOM_ERROR;
    }

    if (dom_string_caseless_isequal(state->interned_svg, nodename)) {
        code = svgtiny_parse_svg(element, *state);
    } else if (dom_string_caseless_isequal(state->interned_g, nodename)) {
        code = svgtiny_parse_svg(element, *state);
    } else if (dom_string_caseless_isequal(state->interned_a, nodename)) {
        code = svgtiny_parse_svg(element, *state);
    } else if (dom_string_caseless_isequal(state->interned_path, nodename)) {
        code = svgtiny_parse_path(element, *state);
    } else if (dom_string_caseless_isequal(state->interned_rect, nodename)) {
        code = svgtiny_parse_rect(element, *state);
    } else if (dom_string_caseless_isequal(state->interned_circle, nodename)) {
        code = svgtiny_parse_circle(element, *state);
    } else if (dom_string_caseless_isequal(state->interned_ellipse, nodename)) {
        code = svgtiny_parse_ellipse(element, *state);
    } else if (dom_string_caseless_isequal(state->interned_line, nodename)) {
        code = svgtiny_parse_line(element, *state);
    } else if (dom_string_caseless_isequal(state->interned_polyline, nodename)) {
        code = svgtiny_parse_poly(element, *state, false);
    } else if (dom_string_caseless_isequal(state->interned_polygon, nodename)) {
        code = svgtiny_parse_poly(element, *state, true);
    } else if (dom_string_caseless_isequal(state->interned_text, nodename)) {
        code = svgtiny_parse_text(element, *state);
    } else if (dom_string_caseless_isequal(state->interned_use, nodename)) {
        code = svgtiny_parse_use(element, *state);
    }
    dom_string_unref(nodename);
    return code;
}


static svgtiny_code initialise_parse_state(struct svgtiny_parse_state *state, struct svgtiny_diagram *diagram,
    dom_document *document, dom_element *svg, int viewport_width, int viewport_height)
{
    float x, y, width, height;

    memset(state, 0, sizeof(*state));

    state->diagram = diagram;
    state->document = document;

#define SVGTINY_STRING_ACTION2(s, n)                                                                                   \
    if (dom_string_create_interned((const uint8_t *)#n, strlen(#n), &state->interned_##s) != DOM_NO_ERR) {             \
        return svgtiny_LIBDOM_ERROR;                                                                                   \
    }
/* SVGTINY_STRING_ACTION3 takes quoted string literal directly */
#define SVGTINY_STRING_ACTION3(s, n)                                                                                   \
    if (dom_string_create_interned((const uint8_t *)n, sizeof(n) - 1, &state->interned_##s) != DOM_NO_ERR) {           \
        return svgtiny_LIBDOM_ERROR;                                                                                   \
    }
#include "svgtiny_strings.h"
#undef SVGTINY_STRING_ACTION2
#undef SVGTINY_STRING_ACTION3

    /* get graphic dimensions */
    state->viewport_width = viewport_width;
    state->viewport_height = viewport_height;
    svgtiny_parse_position_attributes(svg, *state, &x, &y, &width, &height);

    /* Fallback if width or height are missing/invalid */
    if (width <= 0 || height <= 0) {
        dom_string *view_box;
        dom_exception exc;

        exc = dom_element_get_attribute(svg, state->interned_viewBox, &view_box);
        if (exc == DOM_NO_ERR && view_box != NULL) {
            struct svgtiny_transformation_matrix tm;
            float viewbox_width = state->viewport_width;
            float viewbox_height = state->viewport_height;

            /* Use temporary viewport dimensions to simulate parsing
             */
            if (viewbox_width <= 0)
                viewbox_width = 100;
            if (viewbox_height <= 0)
                viewbox_height = 100;

            /* We only need the dimensions, so we parse with dummy
             * viewport args merely to extract the width/height
             * scaling behavior if needed, but actually
             * svgtiny_parse_viewbox doesn't return the raw wh
             * values easily. Wait, svgtiny_parse_viewbox calculates
             * a matrix (tm). We actually need to parse the string
             * manually here or reuse the parsing logic to get the
             * raw numbers.
             */

            /* Direct parsing of viewBox string for fallback
             * dimensions */
            const char *text = dom_string_data(view_box);
            size_t textlen = dom_string_byte_length(view_box);
            const char *cursor = text;
            const char *textend = text + textlen;
            const char *paramend;
            float paramv[4];
            int paramidx;
            svgtiny_code res;

            /* advance cursor past optional whitespace */
            while (cursor < textend && (*cursor == ' ' || *cursor == '\t' || *cursor == '\n' || *cursor == '\r'))
                cursor++;

            bool parse_success = true;
            for (paramidx = 0; paramidx < 4; paramidx++) {
                paramend = textend;
                res = svgtiny_parse_number(cursor, &paramend, &paramv[paramidx]);
                if (res != svgtiny_OK) {
                    parse_success = false;
                    break;
                }
                cursor = paramend;
                /* advance past comma/whitespace */
                while (cursor < textend &&
                    (*cursor == ' ' || *cursor == '\t' || *cursor == '\n' || *cursor == '\r' || *cursor == ','))
                    cursor++;
            }

            if (parse_success) {
                if (width <= 0)
                    width = paramv[2];
                if (height <= 0)
                    height = paramv[3];
            }

            dom_string_unref(view_box);
        }
    }

    /* Final safety fallback */
    if (width <= 0 && height <= 0) {
        fprintf(stderr, "svgtiny: missing width/height/viewBox, using defaults 300x150\n");
        width = 300;
        height = 150;
    } else if (width <= 0) {
        /* Only width missing - fallback to 300 if height gives no clue,
           or maybe keep aspect ratio? For now, simple fallback. */
        width = 300;
    } else if (height <= 0) {
        height = 150;
    }

    /* Use ceilf to ensure we don't clip content when viewBox has fractional dimensions */
    diagram->width = (int)ceilf(width);
    diagram->height = (int)ceilf(height);

    /* set up parsing state */
    state->viewport_width = width;
    state->viewport_height = height;
    /* CTM is identity - we parse in SVG user units.
     * Scaling to display size happens at render time. */
    state->ctm.a = 1;
    state->ctm.b = 0;
    state->ctm.c = 0;
    state->ctm.d = 1;
    state->ctm.e = 0;
    state->ctm.f = 0;
    /*state->style = css_base_style;
      state->style.font_size.value.length.value = option_font_size * 0.1;*/
    state->fill = 0x000000;
    state->stroke = svgtiny_TRANSPARENT;
    state->stroke_width = 1;
    state->fill_opacity = 1.0f;
    state->fill_opacity_set = false;
    state->stroke_opacity = 1.0f;
    state->stroke_opacity_set = false;
    state->fill_rule = svgtiny_FILL_NONZERO;
    state->fill_rule_set = false;
    state->stroke_linecap = svgtiny_CAP_BUTT;
    state->stroke_linecap_set = false;
    state->stroke_linejoin = svgtiny_JOIN_MITER;
    state->stroke_linejoin_set = false;
    state->stroke_miterlimit = 4.0f;
    state->stroke_miterlimit_set = false;
    state->stroke_dasharray = NULL;
    state->stroke_dasharray_count = 0;
    state->stroke_dasharray_set = false;
    state->stroke_dashoffset = 0.0f;
    state->stroke_dashoffset_set = false;
    state->font_size = 12.0f; /* default font size in px */
    state->text_anchor = svgtiny_TEXT_ANCHOR_START; /* default left align */
    state->font_weight_bold = false;
    state->text_x = 0.0f;
    state->text_y = 0.0f;
    state->text_x_set = false;
    state->text_y_set = false;
    return svgtiny_OK;
}


static svgtiny_code finalise_parse_state(struct svgtiny_parse_state *state)
{
    svgtiny_cleanup_state_local(state);

#define SVGTINY_STRING_ACTION2(s, n)                                                                                   \
    if (state->interned_##s != NULL)                                                                                   \
        dom_string_unref(state->interned_##s);
#include "svgtiny_strings.h"
#undef SVGTINY_STRING_ACTION2
    return svgtiny_OK;
}


static svgtiny_code get_svg_element(dom_document *document, dom_element **svg)
{
    dom_exception exc;
    dom_string *svg_name;
    lwc_string *svg_name_lwc;

    /* find root <svg> element */
    exc = dom_document_get_document_element(document, svg);
    if (exc != DOM_NO_ERR) {
        return svgtiny_LIBDOM_ERROR;
    }
    if (*svg == NULL) {
        /* no root element */
        return svgtiny_SVG_ERROR;
    }

    /* ensure root element is <svg> */
    exc = dom_node_get_node_name(*svg, &svg_name);
    if (exc != DOM_NO_ERR) {
        dom_node_unref(*svg);
        return svgtiny_LIBDOM_ERROR;
    }
    if (lwc_intern_string("svg", 3 /* SLEN("svg") */, &svg_name_lwc) != lwc_error_ok) {
        dom_string_unref(svg_name);
        dom_node_unref(*svg);
        return svgtiny_LIBDOM_ERROR;
    }
    if (!dom_string_caseless_lwc_isequal(svg_name, svg_name_lwc)) {
        lwc_string_unref(svg_name_lwc);
        dom_string_unref(svg_name);
        dom_node_unref(*svg);
        return svgtiny_NOT_SVG;
    }

    lwc_string_unref(svg_name_lwc);
    dom_string_unref(svg_name);

    return svgtiny_OK;
}


static svgtiny_code svg_document_from_buffer(uint8_t *buffer, size_t size, dom_document **document)
{
    dom_xml_parser *parser;
    dom_xml_error err;

    parser = dom_xml_parser_create(NULL, NULL, ignore_msg, NULL, document);

    if (parser == NULL)
        return svgtiny_LIBDOM_ERROR;

    err = dom_xml_parser_parse_chunk(parser, buffer, size);
    if (err != DOM_XML_OK) {
        dom_node_unref(*document);
        dom_xml_parser_destroy(parser);
        return svgtiny_LIBDOM_ERROR;
    }

    err = dom_xml_parser_completed(parser);
    if (err != DOM_XML_OK) {
        dom_node_unref(*document);
        dom_xml_parser_destroy(parser);
        return svgtiny_LIBDOM_ERROR;
    }

    /* We're done parsing, drop the parser.
     * We now own the document entirely.
     */
    dom_xml_parser_destroy(parser);

    return svgtiny_OK;
}


/**
 * Add a svgtiny_shape to the svgtiny_diagram.
 *
 * library internal interface
 */
struct svgtiny_shape *svgtiny_add_shape(struct svgtiny_parse_state *state)
{
    struct svgtiny_shape *shape;

    shape = realloc(state->diagram->shape, (state->diagram->shape_count + 1) * sizeof(state->diagram->shape[0]));
    if (shape != NULL) {
        state->diagram->shape = shape;

        shape += state->diagram->shape_count;
        shape->path = 0;
        shape->path_length = 0;
        shape->text = 0;
        /* Scale font size by CTM so it grows/shrinks with the SVG viewport */
        shape->font_size = state->font_size * (state->ctm.a + state->ctm.d) / 2.0f;
        shape->text_anchor = state->text_anchor;
        shape->font_weight_bold = state->font_weight_bold;

        shape->fill = state->fill;
        shape->stroke = state->stroke;
        shape->stroke_width = lroundf((float)state->stroke_width * (state->ctm.a + state->ctm.d) / 2.0);
        if (0 < state->stroke_width && shape->stroke_width == 0)
            shape->stroke_width = 1;
        shape->fill_opacity = state->fill_opacity;
        shape->fill_opacity_set = state->fill_opacity_set;
        shape->stroke_opacity = state->stroke_opacity;
        shape->stroke_opacity_set = state->stroke_opacity_set;
        shape->fill_rule = state->fill_rule;
        shape->fill_rule_set = state->fill_rule_set;
        shape->stroke_linecap = state->stroke_linecap;
        shape->stroke_linecap_set = state->stroke_linecap_set;
        shape->stroke_linejoin = state->stroke_linejoin;
        shape->stroke_linejoin_set = state->stroke_linejoin_set;
        shape->stroke_miterlimit = state->stroke_miterlimit;
        shape->stroke_miterlimit_set = state->stroke_miterlimit_set;
        shape->stroke_dashoffset = state->stroke_dashoffset;
        shape->stroke_dashoffset_set = state->stroke_dashoffset_set;
        shape->stroke_dasharray = NULL;
        shape->stroke_dasharray_count = 0;
        shape->stroke_dasharray_set = state->stroke_dasharray_set;
        if (state->stroke_dasharray_set && state->stroke_dasharray_count > 0 && state->stroke_dasharray != NULL) {
            unsigned int c = state->stroke_dasharray_count;
            float *arr = malloc(c * sizeof(float));
            if (arr != NULL) {
                /* Apply CTM scaling to dasharray values, same as stroke_width */
                float ctm_scale = (state->ctm.a + state->ctm.d) / 2.0f;
                for (unsigned int j = 0; j < c; j++) {
                    arr[j] = state->stroke_dasharray[j] * ctm_scale;
                }
                shape->stroke_dasharray = arr;
                shape->stroke_dasharray_count = c;
            } else {
                shape->stroke_dasharray_set = false;
                shape->stroke_dasharray_count = 0;
            }
            free(state->stroke_dasharray);
            state->stroke_dasharray = NULL;
            state->stroke_dasharray_count = 0;
        }

        /* Initialize fill gradient fields */
        shape->fill_gradient_type = svgtiny_GRADIENT_NONE;
        shape->fill_grad_x1 = 0;
        shape->fill_grad_y1 = 0;
        shape->fill_grad_x2 = 0;
        shape->fill_grad_y2 = 0;
        shape->fill_grad_stops = NULL;
        shape->fill_grad_stop_count = 0;
        shape->fill_grad_transform[0] = 1; /* identity matrix */
        shape->fill_grad_transform[1] = 0;
        shape->fill_grad_transform[2] = 0;
        shape->fill_grad_transform[3] = 1;
        shape->fill_grad_transform[4] = 0;
        shape->fill_grad_transform[5] = 0;

        /* Initialize stroke gradient fields */
        shape->stroke_gradient_type = svgtiny_GRADIENT_NONE;
        shape->stroke_grad_x1 = 0;
        shape->stroke_grad_y1 = 0;
        shape->stroke_grad_x2 = 0;
        shape->stroke_grad_y2 = 0;
        shape->stroke_grad_stops = NULL;
        shape->stroke_grad_stop_count = 0;
        shape->stroke_grad_transform[0] = 1;
        shape->stroke_grad_transform[1] = 0;
        shape->stroke_grad_transform[2] = 0;
        shape->stroke_grad_transform[3] = 1;
        shape->stroke_grad_transform[4] = 0;
        shape->stroke_grad_transform[5] = 0;
    }
    return shape;
}


/**
 * Apply the current transformation matrix to a path.
 *
 * library internal interface
 */
void svgtiny_transform_path(float *p, unsigned int n, struct svgtiny_parse_state *state)
{
    unsigned int j;

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
            float x0 = p[j], y0 = p[j + 1];
            float x = state->ctm.a * x0 + state->ctm.c * y0 + state->ctm.e;
            float y = state->ctm.b * x0 + state->ctm.d * y0 + state->ctm.f;
            p[j] = x;
            p[j + 1] = y;
            j += 2;
        }
    }
}


/**
 * Create a new svgtiny_diagram structure.
 */
struct svgtiny_diagram *svgtiny_create(void)
{
    struct svgtiny_diagram *diagram;

    diagram = calloc(1, sizeof(*diagram));

    return diagram;
}


/**
 * Parse a block of memory into a svgtiny_diagram.
 */
svgtiny_code svgtiny_parse(struct svgtiny_diagram *diagram, const char *buffer, size_t size, const char *url,
    int viewport_width, int viewport_height)
{
    dom_document *document;
    dom_element *svg;
    struct svgtiny_parse_state state;
    svgtiny_code code;

    assert(diagram);
    assert(buffer);
    assert(url);

    UNUSED(url);

    /* Clear any shapes from a previous parse so we don't accumulate
     * stale data when svg_reformat re-parses at a new viewport size. */
    if (diagram->shape_count > 0) {
        for (unsigned int i = 0; i < diagram->shape_count; i++) {
            free(diagram->shape[i].path);
            free(diagram->shape[i].text);
            free(diagram->shape[i].stroke_dasharray);
            free(diagram->shape[i].fill_grad_stops);
            free(diagram->shape[i].stroke_grad_stops);
        }
        free(diagram->shape);
        diagram->shape = NULL;
        diagram->shape_count = 0;
    }

    code = svg_document_from_buffer((uint8_t *)buffer, size, &document);
    if (code == svgtiny_OK) {
        code = get_svg_element(document, &svg);
        if (code == svgtiny_OK) {
            code = initialise_parse_state(&state, diagram, document, svg, viewport_width, viewport_height);
            if (code == svgtiny_OK) {
                code = svgtiny_parse_svg(svg, state);
            }

            finalise_parse_state(&state);

            dom_node_unref(svg);
        }
        dom_node_unref(document);
    }

    return code;
}


/**
 * Parse SVG to extract only the intrinsic dimensions (width/height).
 *
 * This is a lightweight function that extracts the SVG's intrinsic dimensions
 * from its width, height, and viewBox attributes without processing any shapes.
 * Use this for early layout calculations before the full parse.
 *
 * \param buffer    Source data
 * \param size      Size of source data
 * \param width     Pointer to store extracted width
 * \param height    Pointer to store extracted height
 * \param source    Pointer to store where dimensions came from (may be NULL)
 * \return svgtiny_OK on success, error code on failure
 */
svgtiny_code
svgtiny_parse_dimensions(const char *buffer, size_t size, int *width, int *height, svgtiny_dimension_source *source)
{
    dom_document *document;
    dom_element *svg;
    dom_string *attr;
    dom_string *str_width, *str_height, *str_viewBox;
    dom_exception exc;
    svgtiny_code code;
    float w = 0, h = 0;
    bool has_explicit_width = false;
    bool has_explicit_height = false;
    bool has_viewbox = false;

    assert(buffer);
    assert(width);
    assert(height);

    *width = 0;
    *height = 0;
    if (source)
        *source = svgtiny_DIMS_DEFAULT;

    code = svg_document_from_buffer((uint8_t *)buffer, size, &document);
    if (code != svgtiny_OK) {
        return code;
    }

    code = get_svg_element(document, &svg);
    if (code != svgtiny_OK) {
        dom_node_unref(document);
        return code;
    }

    /* Create interned strings for attribute names */
    if (dom_string_create_interned((const uint8_t *)"width", 5, &str_width) != DOM_NO_ERR ||
        dom_string_create_interned((const uint8_t *)"height", 6, &str_height) != DOM_NO_ERR ||
        dom_string_create_interned((const uint8_t *)"viewBox", 7, &str_viewBox) != DOM_NO_ERR) {
        dom_node_unref(svg);
        dom_node_unref(document);
        return svgtiny_LIBDOM_ERROR;
    }

    /* Try to get width attribute */
    exc = dom_element_get_attribute(svg, str_width, &attr);
    if (exc == DOM_NO_ERR && attr != NULL) {
        const char *data = dom_string_data(attr);
        /* Skip percentage values - they don't give intrinsic dimensions */
        if (strchr(data, '%') == NULL) {
            w = strtof(data, NULL);
            if (w > 0)
                has_explicit_width = true;
        }
        dom_string_unref(attr);
    }

    /* Try to get height attribute */
    exc = dom_element_get_attribute(svg, str_height, &attr);
    if (exc == DOM_NO_ERR && attr != NULL) {
        const char *data = dom_string_data(attr);
        /* Skip percentage values */
        if (strchr(data, '%') == NULL) {
            h = strtof(data, NULL);
            if (h > 0)
                has_explicit_height = true;
        }
        dom_string_unref(attr);
    }

    /* If width/height missing, try viewBox */
    if (w <= 0 || h <= 0) {
        exc = dom_element_get_attribute(svg, str_viewBox, &attr);
        if (exc == DOM_NO_ERR && attr != NULL) {
            const char *text = dom_string_data(attr);
            float vb[4] = {0, 0, 0, 0};
            int i = 0;

            /* Parse "minX minY width height" from viewBox */
            while (*text && i < 4) {
                /* Skip whitespace and commas */
                while (*text && (*text == ' ' || *text == '\t' || *text == '\n' || *text == '\r' || *text == ','))
                    text++;
                if (*text) {
                    char *end;
                    vb[i] = strtof(text, &end);
                    if (end > text) {
                        text = end;
                        i++;
                    } else {
                        break;
                    }
                }
            }

            if (i == 4) {
                has_viewbox = true;
                if (w <= 0)
                    w = vb[2];
                if (h <= 0)
                    h = vb[3];
            }

            dom_string_unref(attr);
        }
    }

    /* Clean up interned strings */
    dom_string_unref(str_width);
    dom_string_unref(str_height);
    dom_string_unref(str_viewBox);

    /* Fallback defaults if still no dimensions */
    if (w <= 0 && h <= 0) {
        w = 300;
        h = 150;
    } else if (w <= 0) {
        w = 300;
    } else if (h <= 0) {
        h = 150;
    }

    *width = (int)ceilf(w);
    *height = (int)ceilf(h);

    /* Report dimension source */
    if (source) {
        if (has_explicit_width && has_explicit_height) {
            *source = svgtiny_DIMS_EXPLICIT;
        } else if (has_viewbox) {
            *source = svgtiny_DIMS_VIEWBOX;
        } else {
            *source = svgtiny_DIMS_DEFAULT;
        }
    }

    dom_node_unref(svg);
    dom_node_unref(document);

    return svgtiny_OK;
}


/**
 * Free all memory used by a diagram.
 */
void svgtiny_free(struct svgtiny_diagram *svg)
{
    unsigned int i;
    assert(svg);

    for (i = 0; i != svg->shape_count; i++) {
        free(svg->shape[i].path);
        free(svg->shape[i].text);
        if (svg->shape[i].stroke_dasharray != NULL) {
            free(svg->shape[i].stroke_dasharray);
        }
        if (svg->shape[i].fill_grad_stops != NULL) {
            free(svg->shape[i].fill_grad_stops);
        }
        if (svg->shape[i].stroke_grad_stops != NULL) {
            free(svg->shape[i].stroke_grad_stops);
        }
    }

    free(svg->shape);

    free(svg);
}

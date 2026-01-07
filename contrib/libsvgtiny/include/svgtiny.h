/*
 * This file is part of Libsvgtiny
 * Licensed under the MIT License,
 *                http://opensource.org/licenses/mit-license.php
 * Copyright 2008 James Bursa <james@semichrome.net>
 */

#ifndef SVGTINY_H
#define SVGTINY_H

#include <stdbool.h>
#include <stddef.h>

typedef int svgtiny_colour;
#define svgtiny_TRANSPARENT 0x1000000
#define svgtiny_CURRENT_COLOR 0x3000000
#ifdef __riscos__
#define svgtiny_RGB(r, g, b) ((b) << 16 | (g) << 8 | (r))
#define svgtiny_RED(c) ((c) & 0xff)
#define svgtiny_GREEN(c) (((c) >> 8) & 0xff)
#define svgtiny_BLUE(c) (((c) >> 16) & 0xff)
#else
#define svgtiny_RGB(r, g, b) ((r) << 16 | (g) << 8 | (b))
#define svgtiny_RED(c) (((c) >> 16) & 0xff)
#define svgtiny_GREEN(c) (((c) >> 8) & 0xff)
#define svgtiny_BLUE(c) ((c) & 0xff)
#endif

typedef enum { svgtiny_FILL_NONZERO, svgtiny_FILL_EVENODD } svgtiny_fill_rule;

typedef enum { svgtiny_CAP_BUTT, svgtiny_CAP_ROUND, svgtiny_CAP_SQUARE } svgtiny_line_cap;

typedef enum { svgtiny_JOIN_MITER, svgtiny_JOIN_ROUND, svgtiny_JOIN_BEVEL } svgtiny_line_join;

typedef enum { svgtiny_TEXT_ANCHOR_START, svgtiny_TEXT_ANCHOR_MIDDLE, svgtiny_TEXT_ANCHOR_END } svgtiny_text_anchor;

struct svgtiny_shape {
    float *path;
    unsigned int path_length;
    char *text;
    float text_x, text_y;
    float font_size;
    svgtiny_text_anchor text_anchor;
    bool font_weight_bold;
    svgtiny_colour fill;
    svgtiny_colour stroke;
    int stroke_width;
    float fill_opacity;
    bool fill_opacity_set;
    float stroke_opacity;
    bool stroke_opacity_set;
    svgtiny_fill_rule fill_rule;
    bool fill_rule_set;
    svgtiny_line_cap stroke_linecap;
    bool stroke_linecap_set;
    svgtiny_line_join stroke_linejoin;
    bool stroke_linejoin_set;
    float stroke_miterlimit;
    bool stroke_miterlimit_set;
    float *stroke_dasharray;
    unsigned int stroke_dasharray_count;
    bool stroke_dasharray_set;
    float stroke_dashoffset;
    bool stroke_dashoffset_set;
};

struct svgtiny_diagram {
    int width, height;

    struct svgtiny_shape *shape;
    unsigned int shape_count;

    unsigned short error_line;
    const char *error_message;
};

typedef enum {
    svgtiny_OK,
    svgtiny_OUT_OF_MEMORY,
    svgtiny_LIBDOM_ERROR,
    svgtiny_NOT_SVG,
    svgtiny_SVG_ERROR
} svgtiny_code;

enum { svgtiny_PATH_MOVE, svgtiny_PATH_CLOSE, svgtiny_PATH_LINE, svgtiny_PATH_BEZIER };

struct svgtiny_named_color {
    const char *name;
    svgtiny_colour color;
};


struct svgtiny_diagram *svgtiny_create(void);
svgtiny_code
svgtiny_parse(struct svgtiny_diagram *diagram, const char *buffer, size_t size, const char *url, int width, int height);
void svgtiny_free(struct svgtiny_diagram *svg);

#endif

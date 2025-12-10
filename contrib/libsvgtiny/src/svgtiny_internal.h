/*
 * This file is part of Libsvgtiny
 * Licensed under the MIT License,
 *                http://opensource.org/licenses/mit-license.php
 * Copyright 2008 James Bursa <james@semichrome.net>
 */

#ifndef SVGTINY_INTERNAL_H
#define SVGTINY_INTERNAL_H

#include <stdbool.h>

#include <dom/dom.h>

#ifndef UNUSED
#define UNUSED(x) ((void) (x))
#endif

struct svgtiny_gradient_stop {
	float offset;
	svgtiny_colour color;
};

#define svgtiny_MAX_STOPS 10
#define svgtiny_LINEAR_GRADIENT 0x2000000

/**
 * svg transform matrix
 * | a c e |
 * | b d f |
 * | 0 0 1 |
 */
struct svgtiny_transformation_matrix {
	float a, b, c, d, e, f;
};

struct svgtiny_parse_state_gradient {
	unsigned int linear_gradient_stop_count;
	dom_string *gradient_x1, *gradient_y1, *gradient_x2, *gradient_y2;
	struct svgtiny_gradient_stop gradient_stop[svgtiny_MAX_STOPS];
	bool gradient_user_space_on_use;
	struct svgtiny_transformation_matrix gradient_transform;
};


struct svgtiny_parse_state {
    struct svgtiny_diagram *diagram;
    dom_document *document;

	float viewport_width;
	float viewport_height;

	/* current transformation matrix */
	struct svgtiny_transformation_matrix ctm;

	/*struct css_style style;*/

    /* paint attributes */
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

	/* gradients */
	struct svgtiny_parse_state_gradient fill_grad;
	struct svgtiny_parse_state_gradient stroke_grad;

	/* Interned strings */
#define SVGTINY_STRING_ACTION2(n,nn) dom_string *interned_##n;
#include "svgtiny_strings.h"
#undef SVGTINY_STRING_ACTION2

};

struct svgtiny_list;

/**
 * control structure for inline style parse
 */
struct svgtiny_parse_internal_operation {
    dom_string *key;
    enum {
        SVGTIOP_NONE,
        SVGTIOP_PAINT,
        SVGTIOP_COLOR,
        SVGTIOP_LENGTH,
        SVGTIOP_INTLENGTH,
        SVGTIOP_OFFSET,
        SVGTIOP_NUMBER,
        SVGTIOP_KEYWORD,
        SVGTIOP_DASHARRAY,
    } operation;
    void *param;
    void *value;
    bool *mark;
};

struct svgtiny_keyword_map {
    const char *keyword;
    int value;
};

struct svgtiny_dasharray_dest {
    float **arrayp;
    unsigned int *countp;
};

/* svgtiny.c */
struct svgtiny_shape *svgtiny_add_shape(struct svgtiny_parse_state *state);
void svgtiny_transform_path(float *p, unsigned int n,
		struct svgtiny_parse_state *state);

/* svgtiny_parse.c */
svgtiny_code svgtiny_parse_inline_style(dom_element *node,
		struct svgtiny_parse_state *state,
		struct svgtiny_parse_internal_operation *ops);
svgtiny_code svgtiny_parse_attributes(dom_element *node,
		struct svgtiny_parse_state *state,
		struct svgtiny_parse_internal_operation *ops);

svgtiny_code svgtiny_parse_element_from_href(dom_element *node,
		struct svgtiny_parse_state *state, dom_element **element);
svgtiny_code svgtiny_parse_poly_points(const char *data, size_t datalen,
		float *pointv, unsigned int *pointc);
svgtiny_code svgtiny_parse_length(const char *text, size_t textlen,
		int viewport_size, float *length);
svgtiny_code svgtiny_parse_transform(const char *text, size_t textlen,
		struct svgtiny_transformation_matrix *tm);
svgtiny_code svgtiny_parse_color(const char *text, size_t textlen,
		svgtiny_colour *c);
svgtiny_code svgtiny_parse_viewbox(const char *text, size_t textlen,
		float viewport_width, float viewport_height,
		struct svgtiny_transformation_matrix *tm);
svgtiny_code svgtiny_parse_none(const char *cursor, const char *textend);
svgtiny_code svgtiny_parse_number(const char *text, const char **textend,
		float *value);

/* svgtiny_path.c */
svgtiny_code svgtiny_parse_path_data(const char *text, size_t textlen,
		float **pointv, unsigned int *pointc);

/* svgtiny_gradient.c */
svgtiny_code svgtiny_update_gradient(dom_element *grad_element,
		struct svgtiny_parse_state *state,
		struct svgtiny_parse_state_gradient *grad);
svgtiny_code svgtiny_gradient_add_fill_path(float *p, unsigned int n,
		struct svgtiny_parse_state *state);
svgtiny_code svgtiny_gradient_add_stroke_path(float *p, unsigned int n,
		struct svgtiny_parse_state *state);

/* svgtiny_list.c */
struct svgtiny_list *svgtiny_list_create(size_t item_size);
unsigned int svgtiny_list_size(struct svgtiny_list *list);
svgtiny_code svgtiny_list_resize(struct svgtiny_list *list,
		unsigned int new_size);
void *svgtiny_list_get(struct svgtiny_list *list,
		unsigned int i);
void *svgtiny_list_push(struct svgtiny_list *list);
void svgtiny_list_free(struct svgtiny_list *list);

#endif

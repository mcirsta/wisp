/* Typed optional storage for background lists. Licensed under the MIT License. */
#ifndef CSS_SELECT_BACKGROUND_H
#define CSS_SELECT_BACKGROUND_H
#include "select/computed.h"
#include "select/select.h"
#include "stylesheet.h"

enum css_background_property {
    CSS_BG_IMAGE, CSS_BG_POSITION, CSS_BG_REPEAT_X, CSS_BG_REPEAT_Y, CSS_BG_ATTACHMENT, CSS_BG_COUNT
};

typedef struct css_background_image {
    uint8_t type;
    union {
        lwc_string *url;
        css_linear_gradient *linear;
        css_radial_gradient *radial;
    } data;
} css_background_image;

typedef struct css_background_position {
    css_fixed_or_calc x, y;
    css_unit xunit, yunit;
} css_background_position;

typedef union css_background_value {
    css_background_image image;
    css_background_position position;
    uint8_t keyword;
} css_background_value;

/* The first value stays in the compact scalar style fields. Only additional
 * values live here, and lists are independent (including unused extra values).
 * Gradients/URLs/calc strings have the same ownership as their scalar peers. */
struct css_background_list {
    uint32_t count;
    css_background_value values[];
};
struct css_background_lists {
    struct css_background_list *list[CSS_BG_COUNT];
};

void css__background_clear(css_computed_style *, enum css_background_property);
void css__background_destroy(css_computed_style *);
css_error css__background_copy(const css_computed_style *, css_computed_style *, enum css_background_property);
bool css__background_equal(const css_computed_style *, const css_computed_style *);
uint32_t css__background_hash(const css_computed_style *);
css_error css__background_cascade(uint32_t, css_style *, css_select_state *, enum css_background_property);
#endif

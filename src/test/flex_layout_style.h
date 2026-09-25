#ifndef WISP_TEST_FLEX_LAYOUT_STYLE_H
#define WISP_TEST_FLEX_LAYOUT_STYLE_H

#include <stdbool.h>
#include <libcss/computed.h>

struct flex_test_style {
    bool container;
    bool reverse;
    bool border_box;
    int width; /* Negative means auto. */
    css_unit width_unit;
    int min_width; /* Negative means auto. */
    css_unit min_unit;
    int max_width; /* Negative means none. */
    int basis; /* Negative means auto. */
    int grow;
    uint8_t overflow;
    int padding;
    int margin;
};

css_computed_style *flex_test_style_create(const struct flex_test_style *values);
uint8_t flex_test_style_grow(const css_computed_style *style, css_fixed *grow);

#endif

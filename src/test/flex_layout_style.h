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
    int border;
    uint8_t position; /* Zero uses static. */
};

css_computed_style *flex_test_style_create(const struct flex_test_style *values);
uint8_t flex_test_style_grow(const css_computed_style *style, css_fixed *grow);

void flex_test_style_ratio(css_computed_style *style, int height, int numerator, int denominator);
void flex_test_style_alignment(css_computed_style *style, uint8_t direction, uint8_t justify, int height);
void flex_test_style_stretch(css_computed_style *style, int minimum, int maximum, bool auto_margin);
void flex_test_style_shrink(css_computed_style *style, int shrink);
void flex_test_style_cross(css_computed_style *style, uint8_t wrap, bool rtl, uint8_t align);
void flex_test_style_wrap(css_computed_style *style, uint8_t wrap);
void flex_test_style_baseline(css_computed_style *style, int top_margin, int bottom_margin);

#endif

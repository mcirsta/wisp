/* Computed styles for grid item sizing tests. */
#ifndef WISP_TEST_GRID_LAYOUT_STYLE_H
#define WISP_TEST_GRID_LAYOUT_STYLE_H

#include <stdbool.h>
#include <libcss/computed.h>

struct grid_item_test_style {
    uint8_t position; /* Zero defaults to static. */
    int width; /* INT_MIN means auto; otherwise pixels or percent. */
    bool percent_width;
    bool border_box;
    int margin_left; /* INT_MIN means auto. */
    int margin_right;
    int padding;
    int border;
    int min_width;
    int max_width; /* Negative means none. */
};

css_computed_style *grid_item_test_style_create(const struct grid_item_test_style *values);
void grid_test_set_horizontal_margins(css_computed_style *style, int left, int right);
void grid_test_set_justify_content(css_computed_style *style, uint8_t justify);

void grid_test_set_block_alignment(css_computed_style *style, uint8_t align, int height, int top, int bottom);

#endif

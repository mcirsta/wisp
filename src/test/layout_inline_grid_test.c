/*
 * Copyright 2025 Marius
 *
 * Test for layout_grid inline-grid width behavior.
 */

#include <dom/dom.h>
#include <check.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "content/handlers/html/layout.h"
#include "wisp/content/handlers/html/box.h"
#include "wisp/css.h"
#include "wisp/types.h"
#include "wisp/utils/errors.h"
#include "wisp/utils/log.h"

/* Forward declare html_content to satisfy layout_internal.h */
typedef struct html_content html_content;

#include "content/handlers/html/layout_internal.h"

/* Mock dependencies */
struct html_content {
    struct css_unit_ctx unit_len_ctx;
};

/* Mock content/layout functions */
bool layout_block_context(struct box *block, int viewport_height, html_content *content)
{
    return true;
}
bool layout_table(struct box *table, int available_width, html_content *content)
{
    return true;
}
bool layout_inline_container(
    struct box *inline_container, int width, struct box *container, int cx, int cy, html_content *content)
{
    return true;
}
bool layout_flex(struct box *flex, int available_width, html_content *content)
{
    return true;
}
bool layout_flex_redistribute_auto_margins_vertical(struct box *flex)
{
    return true;
}

/* Stub for corestring_dom_class */
dom_string *corestring_dom_class = NULL;

/* Stub mocks for style accessors */
const css_len_func margin_funcs[4] = {0};
const css_len_func padding_funcs[4] = {0};
const css_len_func border_width_funcs[4] = {0};
const css_border_style_func border_style_funcs[4] = {0};
const css_border_color_func border_color_funcs[4] = {0};

/* Mock style */
static uint8_t mock_style_inline_grid[4096];

/* Mock computed style functions */
uint8_t css_computed_display(const css_computed_style *style, bool root)
{
    return CSS_DISPLAY_INLINE_GRID;
}
uint8_t css_computed_position(const css_computed_style *style)
{
    return CSS_POSITION_STATIC;
}
uint8_t css_computed_float(const css_computed_style *style)
{
    return CSS_FLOAT_NONE;
}
uint8_t css_computed_clear(const css_computed_style *style)
{
    return CSS_CLEAR_NONE;
}
uint8_t css_computed_overflow_x(const css_computed_style *style)
{
    return CSS_OVERFLOW_VISIBLE;
}
uint8_t css_computed_overflow_y(const css_computed_style *style)
{
    return CSS_OVERFLOW_VISIBLE;
}
uint8_t css_computed_visibility(const css_computed_style *style)
{
    return CSS_VISIBILITY_VISIBLE;
}
uint8_t css_computed_box_sizing(const css_computed_style *style)
{
    return CSS_BOX_SIZING_CONTENT_BOX;
}
uint8_t css_computed_direction(const css_computed_style *style)
{
    return CSS_DIRECTION_LTR;
}

/* Grid specific mocks */
uint8_t
css_computed_grid_template_columns(const css_computed_style *style, int32_t *length, css_computed_grid_track **tracks)
{
    if (length)
        *length = 0;
    if (tracks)
        *tracks = NULL;
    return CSS_GRID_TEMPLATE_NONE;
}

uint8_t
css_computed_grid_template_rows(const css_computed_style *style, int32_t *length, css_computed_grid_track **tracks)
{
    if (length)
        *length = 0;
    if (tracks)
        *tracks = NULL;
    return CSS_GRID_TEMPLATE_NONE;
}

uint8_t css_computed_grid_column_start(const css_computed_style *style, int32_t *integer)
{
    return 0; /* AUTO */
}

uint8_t css_computed_grid_column_end(const css_computed_style *style, int32_t *integer)
{
    return 0; /* AUTO */
}

uint8_t css_computed_grid_row_start(const css_computed_style *style, int32_t *integer)
{
    return 0; /* AUTO */
}

uint8_t css_computed_grid_row_end(const css_computed_style *style, int32_t *integer)
{
    return 0; /* AUTO */
}

uint8_t css_computed_grid_auto_flow(const css_computed_style *style)
{
    return CSS_GRID_AUTO_FLOW_ROW;
}

uint8_t css_computed_align_items(const css_computed_style *style)
{
    return CSS_ALIGN_ITEMS_STRETCH;
}
uint8_t css_computed_align_self(const css_computed_style *style)
{
    return CSS_ALIGN_SELF_AUTO;
}
uint8_t css_computed_align_search_items(const css_computed_style *style)
{
    return CSS_ALIGN_ITEMS_STRETCH;
}
/* Removed unknown justify items */

uint8_t css_computed_column_gap(const css_computed_style *style, css_fixed *length, css_unit *unit)
{
    return CSS_COLUMN_GAP_NORMAL;
}
uint8_t css_computed_row_gap(const css_computed_style *style, css_fixed *length, css_unit *unit)
{
    return CSS_ROW_GAP_NORMAL;
}

/* Mocks for width/height - return AUTO */
uint8_t css_computed_width(const css_computed_style *style, css_fixed *length, css_unit *unit)
{
    return CSS_WIDTH_AUTO;
}
uint8_t css_computed_height(const css_computed_style *style, css_fixed *length, css_unit *unit)
{
    return CSS_HEIGHT_AUTO;
}
uint8_t css_computed_min_width(const css_computed_style *style, css_fixed *length, css_unit *unit)
{
    return CSS_MIN_WIDTH_AUTO;
}
uint8_t css_computed_max_width(const css_computed_style *style, css_fixed *length, css_unit *unit)
{
    return CSS_MAX_WIDTH_NONE;
}
uint8_t css_computed_min_height(const css_computed_style *style, css_fixed *length, css_unit *unit)
{
    return CSS_MIN_HEIGHT_AUTO;
}
uint8_t css_computed_max_height(const css_computed_style *style, css_fixed *length, css_unit *unit)
{
    return CSS_MAX_HEIGHT_NONE;
}


START_TEST(test_inline_grid_auto_width_fails_assert)
{
    /* Construct inline grid */
    struct box *grid = calloc(1, sizeof(struct box));
    grid->type = BOX_INLINE_GRID;
    grid->style = (css_computed_style *)mock_style_inline_grid;
    grid->width = UNKNOWN_WIDTH; /* AUTO */
    grid->height = UNKNOWN_WIDTH;

    struct html_content content = {0};

    /* Call layout_grid */
    /* This should calculate width.
       If it fails to set grid->width and hits the assert at the end, it crashes. */
    layout_grid(grid, 1000, &content);

    /* If we get here, width should be set */
    ck_assert_int_ne(grid->width, UNKNOWN_WIDTH);

    free(grid);
}
END_TEST

Suite *layout_inline_grid_suite(void)
{
    Suite *s = suite_create("layout_inline_grid");
    TCase *tc = tcase_create("core");
    tcase_add_test(tc, test_inline_grid_auto_width_fails_assert);
    suite_add_tcase(s, tc);
    return s;
}

int main(void)
{
    Suite *s = layout_inline_grid_suite();
    SRunner *sr = srunner_create(s);
    srunner_run_all(sr, CK_ENV);
    int failed = srunner_ntests_failed(sr);
    srunner_free(sr);
    return (failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}

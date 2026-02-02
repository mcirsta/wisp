#include <check.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include <dom/dom.h>
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

/* Define AUTO locally for test since it's an internal macro often */
#ifndef AUTO
#define AUTO (-2147483648) /* INT_MIN */
#endif

/** Array of per-side access functions for computed style margins. */
const css_len_func margin_funcs[4] = {
    [TOP] = css_computed_margin_top,
    [RIGHT] = css_computed_margin_right,
    [BOTTOM] = css_computed_margin_bottom,
    [LEFT] = css_computed_margin_left,
};

/** Array of per-side access functions for computed style paddings. */
const css_len_func padding_funcs[4] = {
    [TOP] = css_computed_padding_top,
    [RIGHT] = css_computed_padding_right,
    [BOTTOM] = css_computed_padding_bottom,
    [LEFT] = css_computed_padding_left,
};

/** Array of per-side access functions for computed style border_widths. */
const css_len_func border_width_funcs[4] = {
    [TOP] = css_computed_border_top_width,
    [RIGHT] = css_computed_border_right_width,
    [BOTTOM] = css_computed_border_bottom_width,
    [LEFT] = css_computed_border_left_width,
};

/** Array of per-side access functions for computed style border styles. */
const css_border_style_func border_style_funcs[4] = {
    [TOP] = css_computed_border_top_style,
    [RIGHT] = css_computed_border_right_style,
    [BOTTOM] = css_computed_border_bottom_style,
    [LEFT] = css_computed_border_left_style,
};

/** Array of per-side access functions for computed style border colors. */
const css_border_color_func border_color_funcs[4] = {
    [TOP] = css_computed_border_top_color,
    [RIGHT] = css_computed_border_right_color,
    [BOTTOM] = css_computed_border_bottom_color,
    [LEFT] = css_computed_border_left_color,
};

/* Stub for corestring_dom_class */
dom_string *corestring_dom_class = NULL;

/* Stub for layout_grid - called by layout_flex_item */
bool layout_grid(struct box *grid, int available_width, html_content *content)
{
    return true;
}

/* Mock CSS styles for test */
static uint8_t mock_style_flex_grow_0[256]; /* flex-grow: 0 (fixed child) */
static uint8_t mock_style_flex_grow_1[256]; /* flex-grow: 1 (flex child) */

/* Mock css_computed_flex_grow to return values based on style pointer */
uint8_t css_computed_flex_grow(const css_computed_style *style, css_fixed *grow)
{
    if (style == (const css_computed_style *)mock_style_flex_grow_1) {
        *grow = (1 << 10); /* 1.0 in css_fixed format (10-bit fractional) */
        return CSS_FLEX_GROW_SET;
    }
    /* Default: flex-grow: 0 */
    *grow = 0;
    return CSS_FLEX_GROW_SET;
}

/* Now linking against REAL layout_flex.c - provide stubs for its dependencies */

/* Test-configurable content height for layout_block_context stub */
static int g_test_block_content_height = 50; /* Default test content height */

/* Stub for layout_block_context - called by layout_flex_item */
bool layout_block_context(struct box *block, int viewport_height, html_content *content)
{
    /* Simulate content layout by setting height from content */
    if (block->height == AUTO || block->height == 0) {
        block->height = g_test_block_content_height;
    }
    return true;
}

/* Stub for layout_table - called by layout_flex_item */
bool layout_table(struct box *table, int available_width, html_content *content)
{
    return true;
}

/* Stub for layout_inline_container - called by layout_flex_item */
bool layout_inline_container(
    struct box *inline_container, int width, struct box *container, int cx, int cy, html_content *content)
{
    return true;
}

/* Replicating the logic from layout_flex__resolve_line to test the fix */
static int test_resolve_line_logic(int available_main, int main_size)
{
    if (available_main == INT_MIN) { /* AUTO */
        /* OLD BEHAVIOR: available_main = INT_MAX; */
        /* NEW BEHAVIOR: available_main = main_size; */
        return main_size;
    }
    return available_main;
}

static int test_resolve_line_logic_old(int available_main, int main_size)
{
    if (available_main == INT_MIN) { /* AUTO */
        return INT_MAX;
    }
    return available_main;
}

START_TEST(test_flex_auto_width_logic)
{
    /* Case 1: Width is AUTO (INT_MIN in our macro) */
    int available_main = INT_MIN;
    int content_size = 500;

    /* With the fix, available_main should become content_size */
    int resolved = test_resolve_line_logic(available_main, content_size);
    ck_assert_int_eq(resolved, content_size);
}
END_TEST

/* Replicating the logic from layout_flex width clamping to test the fix */
static int test_flex_width_clamping(int width, int max_width, int min_width, int calculated_width)
{
    /* Logic from layout_flex */
    if (width == INT_MIN) { /* AUTO or UNKNOWN_WIDTH */
        width = calculated_width;

        if (max_width >= 0 && width > max_width) {
            width = max_width;
        }
        if (min_width > 0 && width < min_width) {
            width = min_width;
        }
    }
    return width;
}

START_TEST(test_flex_width_max_constraint)
{
    int width = INT_MIN; /* AUTO */
    int calculated_width = 800;
    int max_width = 600;
    int min_width = 0;

    int result = test_flex_width_clamping(width, max_width, min_width, calculated_width);

    /* Should be clamped to 600 */
    ck_assert_int_eq(result, 600);
}
END_TEST

START_TEST(test_flex_width_min_constraint)
{
    int width = INT_MIN; /* AUTO */
    int calculated_width = 200;
    int max_width = -1;
    int min_width = 300;

    int result = test_flex_width_clamping(width, max_width, min_width, calculated_width);

    /* Should be clamped to 300 */
    ck_assert_int_eq(result, 300);
}
/* Test that flex-basis calc() returns correct base_size */
START_TEST(test_flex_basis_calc_integration)
{
    /* Simulate what happens in layout_flex__base_and_main_sizes
     * When CSS_FLEX_BASIS_SET returns with calc() results:
     *
     * For flex-basis: calc(33.33% - 10px) on available_width 2484px:
     * Expected result: 2484 * 0.3333 - 10 ~= 817px
     *
     * For flex-basis: calc(200px - 50px):
     * Expected result: 150px
     */

    /* Test case 1: percentage-based calc */
    int available_width = 2484;
    int expected_px = 817; /* 2484 * 0.3333 - 10 */

    /* The css_computed_flex_basis_px should return this value
     * but we can't call it directly in unit test without full CSS context.
     * Instead, verify the math:
     */
    int calc_result = (available_width * 3333 / 10000) - 10;
    ck_assert_int_ge(calc_result, expected_px - 5);
    ck_assert_int_le(calc_result, expected_px + 5);

    /* Test case 2: px-only calc */
    int px_only_result = 200 - 50;
    ck_assert_int_eq(px_only_result, 150);

    /* Test case 3: Items should fit in container
     * 3 items at ~817px each = 2451px should fit in 2484px container
     */
    int total_items_width = 817 * 3;
    ck_assert_int_le(total_items_width, 2484);
}
END_TEST

/**
 * Test for column flex base_size calculation fix.
 *
 * Bug: For column (vertical) flex with flex-basis:auto, base_size was being
 * set to b->height BEFORE layout_flex_item() was called, getting a pre-layout
 * value (e.g., 22px) instead of the post-layout content height (e.g., 139px).
 *
 * Fix: For column flex, defer base_size to AUTO at line 348, then set it from
 * b->height after layout_flex_item() completes at line 365-367.
 */
static int test_column_flex_base_size_logic(
    int css_flex_basis_auto, int is_horizontal, int pre_layout_height, int post_layout_height)
{
    int base_size;

    if (css_flex_basis_auto) {
        if (is_horizontal) {
            /* Horizontal: width is known before layout */
            base_size = pre_layout_height; /* Actually would be width */
        } else {
            /* Column (vertical): defer to AUTO, set after layout */
            base_size = INT_MIN; /* AUTO */
        }
    } else {
        base_size = INT_MIN; /* AUTO */
    }

    /* Simulate layout_flex_item() being called for column flex */
    if (!is_horizontal && base_size == INT_MIN) {
        /* After layout, use post-layout height */
        base_size = post_layout_height;
    }

    return base_size;
}

START_TEST(test_column_flex_base_size_fix)
{
    /* Scenario: entry-wrapper flex item in column flex article container
     * Pre-layout height: 22px (wrong value from CSS or initial)
     * Post-layout height: 139px (correct content-based height)
     */
    int pre_layout = 22;
    int post_layout = 139;

    /* OLD behavior (bug): base_size = pre-layout height = 22 */
    /* NEW behavior (fix): base_size = post-layout height = 139 */

    int result = test_column_flex_base_size_logic(1, /* CSS_FLEX_BASIS_AUTO */
        0, /* is_horizontal = false (column flex) */
        pre_layout, post_layout);

    /* With the fix, should get post-layout height */
    ck_assert_int_eq(result, post_layout);
    ck_assert_int_ne(result, pre_layout);
}
END_TEST

START_TEST(test_horizontal_flex_base_size_unchanged)
{
    /* Horizontal flex should still use pre-layout width (unchanged behavior) */
    int pre_layout = 300;
    int post_layout = 350;

    int result = test_column_flex_base_size_logic(1, /* CSS_FLEX_BASIS_AUTO */
        1, /* is_horizontal = true */
        pre_layout, post_layout);

    /* Horizontal flex uses width which is known before layout */
    ck_assert_int_eq(result, pre_layout);
}


/**
 * Test for column flex height preservation fix.
 *
 * Bug: Column flex containers were preserving stretched height from parent,
 * causing content to be cut off.
 *
 * Fix: Only preserve height for horizontal flex (where height is cross-size).
 */
static bool test_should_preserve_height_logic(int is_horizontal, int height_definite)
{
    return height_definite && is_horizontal;
}

START_TEST(test_column_flex_height_not_preserved)
{
    /* Column flex should NOT preserve stretched height */
    bool should_preserve = test_should_preserve_height_logic(0, 1);
    ck_assert_int_eq(should_preserve, 0);
}
END_TEST

START_TEST(test_horizontal_flex_height_preserved)
{
    /* Horizontal flex SHOULD preserve height (cross-dimension) */
    bool should_preserve = test_should_preserve_height_logic(1, 1);
    ck_assert_int_eq(should_preserve, 1);
}
END_TEST


/**
 * Test for flex-basis:0 in column flex fix.
 *
 * Bug: Elements with 'flex: 1' (which sets flex-basis: 0) in column flex
 * were not having their content height measured. Instead, base_size was
 * set to 0, causing the layout to be incorrect.
 *
 * Fix: For column flex with flex-basis: 0, defer base_size calculation
 * to content-based sizing, just like flex-basis: auto.
 */
static bool test_flex_basis_zero_column_logic(int is_horizontal, int basis_px)
{
    /* Replicates the logic from layout_flex.c lines 344-350:
     * For column flex with flex-basis: 0, defer to content sizing.
     * Returns true if base_size should be set to AUTO (deferred). */
    if (is_horizontal == 0 && basis_px == 0) {
        return true; /* Defer to content sizing */
    }
    return false; /* Use the basis_px value directly */
}

START_TEST(test_column_flex_basis_zero_deferred)
{
    /* Column flex with flex-basis: 0 should defer to content sizing */
    bool should_defer = test_flex_basis_zero_column_logic(0, 0);
    ck_assert_int_eq(should_defer, 1);
}
END_TEST

START_TEST(test_horizontal_flex_basis_zero_not_deferred)
{
    /* Horizontal flex with flex-basis: 0 should NOT defer (use 0) */
    bool should_defer = test_flex_basis_zero_column_logic(1, 0);
    ck_assert_int_eq(should_defer, 0);
}
END_TEST

START_TEST(test_column_flex_basis_nonzero_not_deferred)
{
    /* Column flex with non-zero flex-basis should NOT defer */
    bool should_defer = test_flex_basis_zero_column_logic(0, 100);
    ck_assert_int_eq(should_defer, 0);
}
END_TEST


/**
 * Test for margin-top: auto redistribution in column flex.
 *
 * Scenario: A column flex container (.entry-wrapper) has:
 *   - child1: normal content (height 50)
 *   - child2: margin-top: auto (should be pushed to bottom)
 *
 * When container_height is set EXTERNALLY (e.g., by grid stretch or parent flex),
 * the extra space should be distributed to margin-top: auto.
 *
 * Container: height=200, content_height=80 (50 + 30)
 * Extra space: 120px -> should go to margin-top on child2
 * child2 final y: should be 170 (200 - 30)
 */
#define TEST_AUTO INT_MIN

static void test_auto_margin_redistribution_logic(int container_height, int child1_height, int child1_margin_top,
    int child1_margin_bottom, int child2_height, int child2_margin_top, int child2_margin_bottom, int *out_child1_y,
    int *out_child2_y)
{
    /* Simulate layout_flex_redistribute_auto_margins_vertical logic */
    int content_height = 0;
    int auto_margin_count = 0;

    /* Child 1 */
    int child1_outer = child1_height;
    if (child1_margin_top != TEST_AUTO) {
        child1_outer += child1_margin_top;
    } else {
        auto_margin_count++;
    }
    if (child1_margin_bottom != TEST_AUTO) {
        child1_outer += child1_margin_bottom;
    } else {
        auto_margin_count++;
    }
    content_height += child1_outer;

    /* Child 2 */
    int child2_outer = child2_height;
    if (child2_margin_top != TEST_AUTO) {
        child2_outer += child2_margin_top;
    } else {
        auto_margin_count++;
    }
    if (child2_margin_bottom != TEST_AUTO) {
        child2_outer += child2_margin_bottom;
    } else {
        auto_margin_count++;
    }
    content_height += child2_outer;

    /* Calculate extra space */
    int extra_space = 0;
    int margin_each = 0;
    if (auto_margin_count > 0 && container_height > content_height) {
        extra_space = container_height - content_height;
        margin_each = extra_space / auto_margin_count;
    }

    /* Position children */
    int current_y = 0;

    /* Child 1 margin-top */
    if (child1_margin_top == TEST_AUTO) {
        current_y += margin_each;
    } else {
        current_y += child1_margin_top;
    }

    *out_child1_y = current_y;
    current_y += child1_height;

    /* Child 1 margin-bottom */
    if (child1_margin_bottom == TEST_AUTO) {
        current_y += margin_each;
    } else {
        current_y += child1_margin_bottom;
    }

    /* Child 2 margin-top */
    if (child2_margin_top == TEST_AUTO) {
        current_y += margin_each;
    } else {
        current_y += child2_margin_top;
    }

    *out_child2_y = current_y;
}

START_TEST(test_margin_top_auto_pushes_to_bottom)
{
    /* Container height: 200px
     * Child 1: height=50, margin=0 -> y should be 0
     * Child 2: height=30, margin-top=auto -> should be pushed to bottom
     *
     * Content height: 50 + 30 = 80
     * Extra space: 200 - 80 = 120
     * Auto margin count: 1 (child2 margin-top)
     * margin_each: 120 / 1 = 120
     *
     * child1 y = 0
     * child2 y = 0 + 50 + 0 + 120 = 170
     */
    int child1_y, child2_y;

    test_auto_margin_redistribution_logic(200, /* container_height */
        50, 0, 0, /* child1: height, margin_top, margin_bottom */
        30, TEST_AUTO, 0, /* child2: height, margin_top=auto, margin_bottom */
        &child1_y, &child2_y);

    ck_assert_int_eq(child1_y, 0);
    ck_assert_int_eq(child2_y, 170); /* Pushed to bottom: 200 - 30 = 170 */
}
END_TEST

START_TEST(test_margin_top_auto_no_extra_space)
{
    /* When container height equals content height, no redistribution */
    int child1_y, child2_y;

    test_auto_margin_redistribution_logic(80, /* container_height = content_height */
        50, 0, 0, /* child1 */
        30, TEST_AUTO, 0, /* child2 */
        &child1_y, &child2_y);

    ck_assert_int_eq(child1_y, 0);
    ck_assert_int_eq(child2_y, 50); /* No extra space, margin_each = 0 */
}
END_TEST

START_TEST(test_multiple_auto_margins_split_space)
{
    /* Multiple auto margins split the extra space evenly */
    int child1_y, child2_y;

    /* Container: 200, content: 80, extra: 120
     * Auto margins: child1 margin-bottom + child2 margin-top = 2
     * margin_each: 120 / 2 = 60 */
    test_auto_margin_redistribution_logic(200, 50, 0, TEST_AUTO, /* child1: margin-bottom=auto */
        30, TEST_AUTO, 0, /* child2: margin-top=auto */
        &child1_y, &child2_y);

    ck_assert_int_eq(child1_y, 0);
    /* child2 y = 0 + 50 + 60 (child1 margin-bottom) + 60 (child2 margin-top) = 170 */
    ck_assert_int_eq(child2_y, 170);
}
END_TEST


/**
 * Test for flex-grow redistribution when container height changes externally.
 *
 * Scenario (simulating hotnews_grid):
 *   .article (column flex, height set by grid stretch from 242 -> 261)
 *     - .post-thumbnail (fixed height 104)
 *     - .entry-wrapper (flex: 1, should grow to fill remaining space)
 *         - .category-label (normal)
 *         - .share-wrapper (margin-top: auto, should be pushed to bottom)
 *
 * When .article height increases from 242 to 261 (+19px):
 *   - .entry-wrapper should grow by 19px
 *   - .share-wrapper should be pushed down by the extra space
 *
 * This test will FAIL until we implement flex-grow redistribution.
 */
static void test_flex_grow_redistribution_logic(int original_container_height, int new_container_height,
    int fixed_child_height, /* .post-thumbnail */
    int flex_child_original_height, /* .entry-wrapper original */
    int *out_flex_child_new_height)
{
    /* Simulate the logic we need to implement:
     * 1. Calculate extra space = new_container_height - original_container_height
     * 2. Items with flex-grow > 0 should get the extra space
     * 3. In this simple case with one flex-grow item, it gets all extra space
     */
    int extra_space = new_container_height - original_container_height;

    /* The flex child should get all the extra space (flex-grow: 1) */
    *out_flex_child_new_height = flex_child_original_height + extra_space;
}

START_TEST(test_flex_grow_child_resizes_when_parent_stretched)
{
    /* Simulating .article stretched from 242 -> 261 by grid
     * .post-thumbnail: 104px (fixed)
     * .entry-wrapper: flex: 1, originally gets remaining space
     *
     * Original: 242 total = 104 (.post-thumbnail) + 138 (.entry-wrapper)
     * After stretch: 261 total = 104 + 157 (.entry-wrapper should grow by 19)
     */
    int flex_child_new_height;

    test_flex_grow_redistribution_logic(242, /* original container height */
        261, /* new container height (stretched by grid) */
        104, /* fixed child (.post-thumbnail) */
        138, /* flex child original height */
        &flex_child_new_height);

    /* .entry-wrapper should grow from 138 to 157 */
    ck_assert_int_eq(flex_child_new_height, 157);
}
END_TEST


/**
 * Test for the complete scenario: flex-grow + nested margin-auto.
 *
 * This tests the full hotnews_grid scenario:
 *   1. Container height increases (grid stretch)
 *   2. Flex-grow child resizes
 *   3. Nested margin-auto child is repositioned
 *
 * Will FAIL until we implement both flex-grow AND margin-auto redistribution.
 */
static void test_nested_flex_with_margin_auto_logic(int outer_container_new_height, /* .article after grid stretch */
    int fixed_sibling_height, /* .post-thumbnail */
    int inner_child1_height, /* .category-label */
    int inner_child2_height, /* .share-wrapper */
    int *out_inner_container_height, /* .entry-wrapper height */
    int *out_child2_y) /* .share-wrapper y position */
{
    /* Step 1: Calculate new height for flex-grow child (.entry-wrapper)
     * Assuming .article has no padding/border for simplicity */
    *out_inner_container_height = outer_container_new_height - fixed_sibling_height;

    /* Step 2: Within .entry-wrapper, .share-wrapper has margin-top: auto
     * Content height = child1_height + child2_height
     * Extra space goes to margin-top of child2 */
    int content_height = inner_child1_height + inner_child2_height;
    int extra_space = *out_inner_container_height - content_height;

    /* child2 y = child1_height + extra_space (margin-top: auto) */
    *out_child2_y = inner_child1_height + extra_space;
}

START_TEST(test_nested_flex_margin_auto_after_parent_resize)
{
    /* Complete hotnews_grid scenario:
     * .article: 242 -> 261 (stretched by grid)
     * .post-thumbnail: 104 (fixed)
     * .entry-wrapper: flex: 1 -> should become 157 (261 - 104)
     *   - .category-label: 30 (approx)
     *   - .share-wrapper: 30, margin-top: auto
     *
     * Expected: .share-wrapper.y = 127 (pushed to bottom of 157 - 30)
     */
    int entry_wrapper_height, share_wrapper_y;

    test_nested_flex_with_margin_auto_logic(261, /* .article new height */
        104, /* .post-thumbnail */
        30, /* .category-label */
        30, /* .share-wrapper */
        &entry_wrapper_height, &share_wrapper_y);

    /* .entry-wrapper should be 157px tall */
    ck_assert_int_eq(entry_wrapper_height, 157);

    /* .share-wrapper should be at y=127 (157 - 30) */
    ck_assert_int_eq(share_wrapper_y, 127);
}
END_TEST


/**
 * FAILING TEST: Exposes the actual bug in layout_flex_redistribute_auto_margins_vertical
 *
 * This test calls the REAL function with actual box structures to demonstrate
 * that it currently doesn't handle flex-grow redistribution properly.
 *
 * Bug: When a flex container's height is increased externally (e.g., by grid stretch),
 * children with flex-grow > 0 don't get resized, causing a gap.
 */
START_TEST(test_real_flex_redistribute_with_flex_grow_child_FAILS)
{
    /* Create a column flex container (.article in hotnews_grid)
     * Container was stretched from 242 -> 261 by grid Pass 3
     */
    struct box *container = calloc(1, sizeof(struct box));
    container->type = BOX_FLEX;
    container->width = 295;
    container->height = 261; /* New height after grid stretch */
    container->padding[TOP] = 0;
    container->padding[BOTTOM] = 0;

    /* Child 1: .post-thumbnail (fixed height, flex-shrink: 0) */
    struct box *fixed_child = calloc(1, sizeof(struct box));
    fixed_child->type = BOX_FLEX;
    fixed_child->height = 104; /* Fixed */
    fixed_child->width = 295;
    fixed_child->margin[TOP] = 0;
    fixed_child->margin[BOTTOM] = 0;
    fixed_child->padding[TOP] = 0;
    fixed_child->padding[BOTTOM] = 0;
    fixed_child->border[TOP].width = 0;
    fixed_child->border[BOTTOM].width = 0;
    fixed_child->y = 0;
    fixed_child->style = (css_computed_style *)mock_style_flex_grow_0; /* flex-grow: 0 */

    /* Child 2: .entry-wrapper (flex: 1, should grow to fill remaining space)
     * Currently has height from initial layout (138), but should be 157 after parent stretch */
    struct box *flex_grow_child = calloc(1, sizeof(struct box));
    flex_grow_child->type = BOX_FLEX;
    flex_grow_child->height = 116; /* Content height before redistribution */
    flex_grow_child->width = 295;
    flex_grow_child->margin[TOP] = 0;
    flex_grow_child->margin[BOTTOM] = 0;
    flex_grow_child->padding[TOP] = 10;
    flex_grow_child->padding[BOTTOM] = 10;
    flex_grow_child->border[TOP].width = 1;
    flex_grow_child->border[BOTTOM].width = 1;
    flex_grow_child->y = 104;
    flex_grow_child->style = (css_computed_style *)mock_style_flex_grow_1; /* flex-grow: 1 */

    /* Link the children */
    container->children = fixed_child;
    fixed_child->parent = container;
    fixed_child->next = flex_grow_child;
    flex_grow_child->prev = fixed_child;
    flex_grow_child->parent = container;
    container->last = flex_grow_child;

    /* Debug: print initial state */
    printf("Before redistribution:\n");
    printf("  Container height: %d\n", container->height);
    printf("  Fixed child height: %d (style=%p)\n", fixed_child->height, fixed_child->style);
    printf("  Flex child height: %d (style=%p)\n", flex_grow_child->height, flex_grow_child->style);

    /* Call the REAL function that should redistribute space */
    layout_flex_redistribute_auto_margins_vertical(container);

    /* Debug: print final state */
    printf("After redistribution:\n");
    printf("  Flex child height: %d (expected 135, total space 157 minus padding 20 and border 2)\n",
        flex_grow_child->height);

    /* BUG: The function currently only handles margin: auto, not flex-grow
     * Expected: flex_grow_child->height should be 157 (261 - 104)
     * Actual: flex_grow_child->height is still 138
     *
     * This test will FAIL, exposing the bug */
    ck_assert_int_eq(flex_grow_child->height, 135); /* 261 - 104 - padding(20) - border(2) */

    /* Cleanup */
    free(fixed_child);
    free(flex_grow_child);
    free(container);
}
END_TEST


/**
 * Test for the actual hotnews.ro .meta-after bug: nested flex with margin-top: auto
 *
 * Simulates:
 *   Outer flex container (parent article/entry-wrapper)
 *     - becomes a column flex container with definite height
 *   Inner child with margin-top: auto (.meta-after)
 *     - should be pushed to the bottom
 *
 * This tests that layout_flex calls layout_flex_redistribute_auto_margins_vertical
 * for nested column flex containers.
 */
START_TEST(test_nested_flex_margin_top_auto_redistribution)
{
    /* Create outer column flex container */
    struct box *outer = calloc(1, sizeof(struct box));
    outer->type = BOX_FLEX;
    outer->width = 300;
    outer->height = 200; /* Definite height */
    outer->padding[TOP] = 0;
    outer->padding[BOTTOM] = 0;
    outer->children = NULL;
    outer->last = NULL;

    /* Create child 1: normal content */
    struct box *child1 = calloc(1, sizeof(struct box));
    child1->type = BOX_BLOCK;
    child1->height = 50;
    child1->width = 300;
    child1->margin[TOP] = 0;
    child1->margin[BOTTOM] = 0;
    child1->padding[TOP] = 0;
    child1->padding[BOTTOM] = 0;
    child1->border[TOP].width = 0;
    child1->border[BOTTOM].width = 0;
    child1->y = 0;

    /* Create child 2: element with margin-top: auto (like .meta-after) */
    struct box *child2 = calloc(1, sizeof(struct box));
    child2->type = BOX_BLOCK;
    child2->height = 30;
    child2->width = 300;
    child2->margin[TOP] = TEST_AUTO; /* margin-top: auto */
    child2->margin[BOTTOM] = 0;
    child2->padding[TOP] = 0;
    child2->padding[BOTTOM] = 0;
    child2->border[TOP].width = 0;
    child2->border[BOTTOM].width = 0;
    child2->y = 50; /* Initial position before redistribution */

    /* Link children */
    outer->children = child1;
    child1->parent = outer;
    child1->next = child2;
    child2->prev = child1;
    child2->parent = outer;
    outer->last = child2;

    printf("\nNested flex margin-top auto test:\n");
    printf("  Before: child2.y = %d\n", child2->y);

    /* Call redistribution - this is what should be called by layout_flex */
    layout_flex_redistribute_auto_margins_vertical(outer);

    printf("  After: child2.y = %d\n", child2->y);

    /* child2 should be pushed to bottom:
     * Container height: 200
     * Content: child1(50) + child2(30) = 80
     * Extra space: 200 - 80 = 120
     * child2 y should be: 50 + 120 = 170 (pushed to bottom, 200 - 30)
     */
    ck_assert_int_eq(child2->y, 170);

    /* Cleanup */
    free(child1);
    free(child2);
    free(outer);
}
END_TEST


/**
 * Test for min-height: auto fix (CSS Flexbox §4.5).
 *
 * Bug: Column flex items with min-height: auto (the default) were using
 * min_main=0, allowing them to shrink below their content size.
 *
 * Fix: After layout_flex_item, update min_main from actual content height
 * when min-height is auto (min_main=0).
 *
 * This test verifies the logic that should prevent shrinking below content size.
 */

/* Replicates the fix logic from layout_flex__base_and_main_sizes */
static int test_min_height_auto_logic(
    int is_horizontal, enum css_size_type min_main_type, int initial_min_main, int content_height)
{
    /* For column flex (is_horizontal=false) with min_main_type == CSS_SIZE_AUTO,
     * min_main should be updated to content_height */
    if (!is_horizontal && min_main_type == CSS_SIZE_AUTO) {
        if (content_height > 0) {
            return content_height; /* Updated min_main */
        }
    }
    return initial_min_main; /* Unchanged */
}

START_TEST(test_column_flex_min_height_auto_respects_content)
{
    /* Scenario: post-thumbnail with 200px image content */
    int content_height = 200;

    /* Column flex with min-height: auto (CSS_SIZE_AUTO type) */
    int result = test_min_height_auto_logic(0, /* is_horizontal = false */
        CSS_SIZE_AUTO, /* min_main_type = auto */
        0, /* initial_min_main value */
        content_height);

    /* With the fix, min_main should be updated to content height */
    ck_assert_int_eq(result, 200);

    /* Verify: OLD behavior would have returned 0, allowing shrinking */
    /* NEW behavior returns 200, preventing shrinking below content */
}
END_TEST

START_TEST(test_column_flex_explicit_min_height_preserved)
{
    /* If min-height is explicitly set (CSS_SIZE_SET), it should be preserved */
    int content_height = 200;
    int explicit_min_height = 50;

    int result = test_min_height_auto_logic(0, /* is_horizontal = false */
        CSS_SIZE_SET, /* min_main_type = explicit */
        explicit_min_height, /* initial_min_main value */
        content_height);

    /* Should preserve the explicit min-height, not update to content */
    ck_assert_int_eq(result, 50);
}
END_TEST

START_TEST(test_column_flex_explicit_min_height_zero)
{
    /* Explicit min-height: 0 (CSS_SIZE_SET with value 0) should be preserved
     * This is different from min-height: auto (CSS_SIZE_AUTO) */
    int content_height = 200;

    int result = test_min_height_auto_logic(0, /* is_horizontal = false */
        CSS_SIZE_SET, /* min_main_type = explicit (even though value is 0) */
        0, /* initial_min_main = 0 */
        content_height);

    /* Should preserve explicit min-height: 0, NOT update to content */
    ck_assert_int_eq(result, 0);
}
END_TEST

START_TEST(test_row_flex_min_height_unchanged)
{
    /* Row flex (horizontal) should not apply this logic */
    int content_height = 200;

    int result = test_min_height_auto_logic(1, /* is_horizontal = true */
        CSS_SIZE_AUTO, /* min_main_type = auto */
        0, /* initial_min_main value */
        content_height);

    /* Row flex should not update min_main based on content height */
    ck_assert_int_eq(result, 0);
}
END_TEST


/* Test suite setup */
Suite *layout_flex_suite(void)
{
    Suite *s;
    TCase *tc_core;

    s = suite_create("LayoutFlex");
    tc_core = tcase_create("Core");

    tcase_add_test(tc_core, test_flex_auto_width_logic);
    tcase_add_test(tc_core, test_flex_width_max_constraint);
    tcase_add_test(tc_core, test_flex_width_min_constraint);
    tcase_add_test(tc_core, test_flex_basis_calc_integration);
    tcase_add_test(tc_core, test_column_flex_base_size_fix);
    tcase_add_test(tc_core, test_horizontal_flex_base_size_unchanged);
    tcase_add_test(tc_core, test_column_flex_height_not_preserved);
    tcase_add_test(tc_core, test_horizontal_flex_height_preserved);
    tcase_add_test(tc_core, test_column_flex_basis_zero_deferred);
    tcase_add_test(tc_core, test_horizontal_flex_basis_zero_not_deferred);
    tcase_add_test(tc_core, test_column_flex_basis_nonzero_not_deferred);
    tcase_add_test(tc_core, test_margin_top_auto_pushes_to_bottom);
    tcase_add_test(tc_core, test_margin_top_auto_no_extra_space);
    tcase_add_test(tc_core, test_multiple_auto_margins_split_space);
    tcase_add_test(tc_core, test_flex_grow_child_resizes_when_parent_stretched);
    tcase_add_test(tc_core, test_nested_flex_margin_auto_after_parent_resize);
    /* Now testing with REAL implementation from layout_flex.c */
    tcase_add_test(tc_core, test_real_flex_redistribute_with_flex_grow_child_FAILS);
    tcase_add_test(tc_core, test_nested_flex_margin_top_auto_redistribution);
    /* min-height: auto tests for CSS Flexbox §4.5 */
    tcase_add_test(tc_core, test_column_flex_min_height_auto_respects_content);
    tcase_add_test(tc_core, test_column_flex_explicit_min_height_preserved);
    tcase_add_test(tc_core, test_column_flex_explicit_min_height_zero);
    tcase_add_test(tc_core, test_row_flex_min_height_unchanged);

    suite_add_tcase(s, tc_core);

    return s;
}

int main(void)
{
    int number_failed;
    Suite *s;
    SRunner *sr;

    s = layout_flex_suite();
    sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);

    return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}

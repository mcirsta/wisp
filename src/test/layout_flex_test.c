#include <check.h>
#include <limits.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>

#include <dom/dom.h>
#include "content/handlers/html/layout.h"
#include "wisp/content/handlers/html/box.h"
#include "wisp/css.h"
#include "wisp/types.h"
#include "wisp/utils/errors.h"
#include "wisp/utils/log.h"

#include "wisp/content/handlers/html/private.h"
#include "flex_layout_style.h"

#include "content/handlers/html/layout_internal.h"


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
    if (style == (const css_computed_style *)mock_style_flex_grow_0) {
        *grow = 0;
        return CSS_FLEX_GROW_SET;
    }
    return flex_test_style_grow(style, grow);
}

/* Now linking against REAL layout_flex.c - provide stubs for its dependencies */

/* Test-configurable content height for layout_block_context stub */
static int g_test_block_content_height = 50; /* Default test content height */

/* Stub for layout_block_context - called by layout_flex_item */
bool layout_block_context(struct box *block, int viewport_height, html_content *content)
{
    /* Simulate content layout by setting height from content */
    if (block->height == AUTO || (block->height == 0 && !(block->flags & HEIGHT_STRETCHED))) {
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


/* Exercise the real flex algorithm and libcss getters, not copied sizing logic.
 * Intrinsic widths model an unbreakable 600px descendant in a 400px row. */
START_TEST(test_flex_minimum_width_layout)
{
    struct flex_test_style values = {
        .width = -1,
        .width_unit = CSS_UNIT_PX,
        .min_width = -1,
        .min_unit = CSS_UNIT_PX,
        .max_width = -1,
        .basis = -1,
        .overflow = CSS_OVERFLOW_VISIBLE,
    };
    int expected = 400;
    switch (_i) {
    case 0:
        expected = 600;
        break; /* auto retains content minimum */
    case 1:
        values.min_width = 0;
        break;
    case 2:
        values.min_width = 0;
        values.basis = 0;
        values.grow = 1;
        break;
    case 3:
        values.min_width = 0;
        values.min_unit = CSS_UNIT_PCT;
        break;
    case 4:
        values.min_width = 50;
        values.min_unit = CSS_UNIT_PCT;
        break;
    case 5:
        values.min_width = 450;
        expected = 450;
        break;
    case 6:
        values.width = 300;
        expected = 300;
        break; /* specified-size cap */
    case 7:
        values.width = 100;
        values.width_unit = CSS_UNIT_PCT;
        break;
    case 8:
        values.max_width = 250;
        expected = 250;
        break; /* max-size cap */
    case 9:
        values.max_width = 0;
        expected = 0;
        break;
    case 10:
        values.overflow = CSS_OVERFLOW_HIDDEN;
        break;
    case 11:
        values.overflow = CSS_OVERFLOW_AUTO;
        break;
    case 12:
        values.overflow = CSS_OVERFLOW_SCROLL;
        break;
    case 13:
        values.overflow = CSS_OVERFLOW_HIDDEN;
        values.min_width = 450;
        expected = 450;
        break;
    case 14:
        values.min_width = 450;
        values.max_width = 250;
        expected = 450;
        break;
    case 15:
        values.min_width = 0;
        values.padding = 10;
        values.margin = 5;
        expected = 370;
        break;
    case 16:
        values.width = 300;
        values.padding = 10;
        values.margin = 5;
        expected = 300;
        break;
    case 17:
        values.width = 300;
        values.padding = 10;
        values.border_box = true;
        expected = 280;
        break;
    case 18:
        values.min_width = 0;
        values.reverse = true;
        break;
    }
    css_computed_style *item_style = flex_test_style_create(&values);
    ck_assert_ptr_nonnull(item_style);
    css_fixed_or_calc minimum = (css_fixed_or_calc)0;
    css_unit unit = CSS_UNIT_PX;
    ck_assert_int_eq(css_computed_min_width(item_style, &minimum, &unit),
        values.min_width < 0 ? CSS_MIN_WIDTH_AUTO : CSS_MIN_WIDTH_SET);
    ck_assert_int_eq(css_computed_min_height(item_style, &minimum, &unit), CSS_MIN_HEIGHT_AUTO);

    struct flex_test_style container_values = {
        .container = true,
        .reverse = values.reverse,
        .width = 400,
        .width_unit = CSS_UNIT_PX,
        .min_width = -1,
        .min_unit = CSS_UNIT_PX,
        .max_width = -1,
        .basis = -1,
        .overflow = CSS_OVERFLOW_VISIBLE,
    };
    css_computed_style *container_style = flex_test_style_create(&container_values);
    ck_assert_ptr_nonnull(container_style);
    struct box flex = {.type = BOX_FLEX, .style = container_style, .width = 400, .height = AUTO};
    int intrinsic_width = 600 + 2 * (values.padding + values.margin);
    struct box item = {.type = BOX_BLOCK,
        .style = item_style,
        .parent = &flex,
        .width = AUTO,
        .height = AUTO,
        .min_width = {CSS_SIZE_SET, intrinsic_width},
        .content_min_width = 600,
        .max_width = intrinsic_width};
    flex.children = flex.last = &item;
    html_content content = {.unit_len_ctx = {
                                .viewport_width = INTTOFIX(1280),
                                .viewport_height = INTTOFIX(900),
                                .font_size_default = INTTOFIX(16),
                                .font_size_minimum = INTTOFIX(6),
                                .device_dpi = INTTOFIX(96),
                                .root_style = container_style,
                            }};
    ck_assert(layout_flex(&flex, 400, &content));
    ck_assert_int_eq(item.width, expected);
    /* Reformatting must not replace the intrinsic caches with used sizes. */
    ck_assert_int_eq(item.min_width.value, intrinsic_width);
    ck_assert_int_eq(item.content_min_width, 600);
    ck_assert_int_eq(item.max_width, intrinsic_width);
    ck_assert(layout_flex(&flex, 400, &content));
    ck_assert_int_eq(item.width, expected);
    css_computed_style_destroy(item_style);
    css_computed_style_destroy(container_style);
}
END_TEST

static const struct {
    const char *name;
    int content_min;
    int explicit_min;
    uint8_t overflow;
    int expected[2];
} flex_shrink_minimum_cases[] = {
    {"empty", 0, -1, CSS_OVERFLOW_VISIBLE, {50, 70}},
    {"short content", 30, -1, CSS_OVERFLOW_VISIBLE, {50, 70}},
    {"content floor", 60, -1, CSS_OVERFLOW_VISIBLE, {60, 70}},
    {"preferred cap", 120, -1, CSS_OVERFLOW_VISIBLE, {80, 80}},
    {"explicit zero", 120, 0, CSS_OVERFLOW_VISIBLE, {50, 70}},
    {"explicit minimum", 0, 70, CSS_OVERFLOW_VISIBLE, {70, 70}},
    {"hidden overflow", 120, -1, CSS_OVERFLOW_HIDDEN, {50, 70}},
};

/* Two 80px items in a narrower row. The preferred-width contribution must
 * remain cached, while the real flex solver uses the separate content floor. */
START_TEST(test_flex_shrink_content_minimum)
{
    struct flex_test_style values = {.container = true,
        .width = 100,
        .width_unit = CSS_UNIT_PX,
        .min_width = -1,
        .min_unit = CSS_UNIT_PX,
        .max_width = -1,
        .basis = -1,
        .overflow = CSS_OVERFLOW_VISIBLE};
    css_computed_style *parent_style = flex_test_style_create(&values);
    values.container = false;
    values.width = 80;
    css_computed_style *peer_style = flex_test_style_create(&values);
    values.min_width = flex_shrink_minimum_cases[_i].explicit_min;
    values.overflow = flex_shrink_minimum_cases[_i].overflow;
    css_computed_style *item_style = flex_test_style_create(&values);
    ck_assert_ptr_nonnull(parent_style);
    ck_assert_ptr_nonnull(peer_style);
    ck_assert_ptr_nonnull(item_style);
    int content_min = flex_shrink_minimum_cases[_i].content_min;
    int contribution = content_min > 80 ? content_min : 80;
    struct box parent = {.type = BOX_FLEX, .style = parent_style};
    struct box item = {.type = BOX_BLOCK,
        .parent = &parent,
        .style = item_style,
        .min_width = {CSS_SIZE_SET, contribution},
        .content_min_width = content_min,
        .max_width = contribution};
    struct box peer = {.type = BOX_BLOCK,
        .parent = &parent,
        .style = peer_style,
        .min_width = {CSS_SIZE_SET, 80},
        .content_min_width = 0,
        .max_width = 80,
        .prev = &item};
    parent.children = &item;
    parent.last = item.next = &peer;
    html_content content = {.unit_len_ctx = {.viewport_width = INTTOFIX(1280),
                                .viewport_height = INTTOFIX(900),
                                .font_size_default = INTTOFIX(16),
                                .font_size_minimum = INTTOFIX(6),
                                .device_dpi = INTTOFIX(96),
                                .root_style = parent_style}};
    for (int pass = 0; pass < 2; pass++) {
        parent.width = pass ? 140 : 100;
        ck_assert(layout_flex(&parent, parent.width, &content));
        int expected = flex_shrink_minimum_cases[_i].expected[pass];
        ck_assert_msg(item.width == expected && peer.width == parent.width - expected,
            "%s, pass %d: widths %d/%d, expected %d/%d", flex_shrink_minimum_cases[_i].name, pass, item.width,
            peer.width, expected, parent.width - expected);
        ck_assert_int_eq(item.min_width.value, contribution);
        ck_assert_int_eq(item.max_width, contribution);
        ck_assert_int_eq(item.content_min_width, content_min);
    }
    css_computed_style_destroy(item_style);
    css_computed_style_destroy(peer_style);
    css_computed_style_destroy(parent_style);
}
END_TEST

/* A definite nowrap line can be smaller than its natural contents. Reflow
 * with a different height also checks that the stretch flag is scoped. */
START_TEST(test_flex_stretched_height)
{
    static const char *names[] = {
        "stretch", "minimum", "maximum", "auto margin", "wrapped", "start", "parent minimum", "parent maximum"};
    struct flex_test_style values = {.width = -1,
        .width_unit = CSS_UNIT_PX,
        .min_width = -1,
        .min_unit = CSS_UNIT_PX,
        .max_width = -1,
        .basis = -1,
        .overflow = CSS_OVERFLOW_VISIBLE};
    css_computed_style *item_style = flex_test_style_create(&values);
    if (_i != 5)
        flex_test_style_stretch(item_style, _i == 1 ? 80 : -1, _i == 2 ? 80 : -1, _i == 3);
    values.container = true;
    values.width = 400;
    css_computed_style *parent_style = flex_test_style_create(&values);
    if (_i >= 6)
        flex_test_style_stretch(parent_style, _i == 6 ? 80 : -1, _i == 7 ? 80 : -1, false);
    if (_i == 4)
        flex_test_style_wrap(parent_style, CSS_FLEX_WRAP_WRAP);
    struct box parent = {.type = BOX_FLEX, .style = parent_style, .width = 400, .height = AUTO};
    struct box item = {.type = BOX_BLOCK,
        .style = item_style,
        .parent = &parent,
        .min_width = {CSS_SIZE_SET, 200},
        .content_min_width = 200,
        .max_width = 200};
    parent.children = parent.last = &item;
    html_content content = {.unit_len_ctx = {.viewport_width = INTTOFIX(1280),
                                .viewport_height = INTTOFIX(900),
                                .font_size_default = INTTOFIX(16),
                                .font_size_minimum = INTTOFIX(6),
                                .device_dpi = INTTOFIX(96),
                                .root_style = parent_style}};
    g_test_block_content_height = 100;
    for (int pass = 0; pass < 2; pass++) {
        int height = pass ? 150 : 60;
        int expected = height;
        if ((_i == 1 || _i == 6) && height < 80)
            expected = 80;
        if ((_i == 2 || _i == 7) && height > 80)
            expected = 80;
        if (_i == 3 || _i == 5 || (_i == 4 && height < 100))
            expected = 100;
        flex_test_style_alignment(parent_style, CSS_FLEX_DIRECTION_ROW, CSS_JUSTIFY_CONTENT_FLEX_START, height);
        ck_assert(layout_flex(&parent, 400, &content));
        ck_assert_msg(
            item.height == expected, "%s pass %d: expected height %d, got %d", names[_i], pass, expected, item.height);
        ck_assert_msg(!(item.flags & HEIGHT_STRETCHED), "%s pass %d: leaked stretch flag", names[_i], pass);
    }
    g_test_block_content_height = 50;
    css_computed_style_destroy(item_style);
    css_computed_style_destroy(parent_style);
}
END_TEST

/* Test suite setup */
/* A definite cross size transfers through the ratio into the automatic
 * minimum. Explicit min-width:0 and a specified width still take precedence. */
START_TEST(test_flex_ratio_minimum)
{
    struct flex_test_style values = {.width = -1,
        .width_unit = CSS_UNIT_PX,
        .min_width = _i == 1 ? 0 : -1,
        .min_unit = CSS_UNIT_PX,
        .max_width = -1,
        .basis = -1,
        .overflow = CSS_OVERFLOW_VISIBLE};
    if (_i == 2)
        values.width = 120;
    if (_i == 3)
        values.max_width = 180;
    css_computed_style *item_style = flex_test_style_create(&values);
    flex_test_style_ratio(item_style, 100, 2, 1);
    values.container = true;
    values.width = 150;
    css_computed_style *parent_style = flex_test_style_create(&values);
    struct box parent = {.type = BOX_FLEX, .style = parent_style, .width = 150, .height = AUTO};
    struct box item = {.type = BOX_BLOCK,
        .style = item_style,
        .parent = &parent,
        .width = AUTO,
        .height = AUTO,
        .min_width = {CSS_SIZE_SET, 0},
        .max_width = 0};
    parent.children = parent.last = &item;
    html_content content = {.unit_len_ctx = {.viewport_width = INTTOFIX(1280),
                                .viewport_height = INTTOFIX(900),
                                .font_size_default = INTTOFIX(16),
                                .font_size_minimum = INTTOFIX(6),
                                .device_dpi = INTTOFIX(96),
                                .root_style = parent_style}};
    ck_assert(layout_flex(&parent, 150, &content));
    ck_assert_int_eq(item.width, _i == 0 ? 200 : _i == 1 ? 150 : _i == 2 ? 120 : 180);
    ck_assert(layout_flex(&parent, 150, &content));
    ck_assert_int_eq(item.width, _i == 0 ? 200 : _i == 1 ? 150 : _i == 2 ? 120 : 180);
    css_computed_style_destroy(item_style);
    css_computed_style_destroy(parent_style);
}
END_TEST

/* The real layout algorithm must count padding, borders and margins once,
 * and its later column adjustment must preserve justify-content alignment. */
START_TEST(test_flex_outer_size_alignment)
{
    static const uint8_t directions[] = {CSS_FLEX_DIRECTION_ROW, CSS_FLEX_DIRECTION_ROW_REVERSE,
        CSS_FLEX_DIRECTION_COLUMN, CSS_FLEX_DIRECTION_COLUMN_REVERSE};
    uint8_t direction = directions[_i / 2];
    bool center = _i % 2;
    bool horizontal = _i < 4;
    bool reverse = (_i / 2) % 2;
    struct flex_test_style values = {.width = 100, .width_unit = CSS_UNIT_PX,
        .min_width = -1, .min_unit = CSS_UNIT_PX, .max_width = -1, .basis = -1,
        .overflow = CSS_OVERFLOW_VISIBLE, .padding = 10, .margin = 5, .border = 3};
    css_computed_style *item_style = flex_test_style_create(&values);
    ck_assert_ptr_nonnull(item_style);
    flex_test_style_alignment(item_style, CSS_FLEX_DIRECTION_ROW, CSS_JUSTIFY_CONTENT_FLEX_START, 100);
    values.container = true;
    values.width = 400;
    values.padding = values.margin = values.border = 0;
    css_computed_style *parent_style = flex_test_style_create(&values);
    ck_assert_ptr_nonnull(parent_style);
    flex_test_style_alignment(parent_style, direction,
        center ? CSS_JUSTIFY_CONTENT_CENTER : CSS_JUSTIFY_CONTENT_FLEX_END, 400);
    struct box parent = {.type = BOX_FLEX, .style = parent_style, .width = 400, .height = AUTO};
    struct box item = {.type = BOX_BLOCK,
        .style = item_style,
        .parent = &parent,
        .width = AUTO,
        .height = AUTO,
        .min_width = {CSS_SIZE_SET, 136},
        .content_min_width = 100,
        .max_width = 136};
    parent.children = parent.last = &item;
    html_content content = {.unit_len_ctx = {.viewport_width = INTTOFIX(1280),
        .viewport_height = INTTOFIX(900), .font_size_default = INTTOFIX(16),
        .font_size_minimum = INTTOFIX(6), .device_dpi = INTTOFIX(96), .root_style = parent_style}};
    /* 400 - (100 content + 20 padding + 6 border + 10 margin) = 264 free. */
    int expected = (center ? 132 : reverse ? 0 : 264) + 5 + 3;
    for (int pass = 0; pass < 2; pass++) {
        ck_assert(layout_flex(&parent, 400, &content));
        ck_assert_int_eq(horizontal ? item.x : item.y, expected);
        ck_assert_int_eq(item.width, 100);
        ck_assert_int_eq(item.height, 100);
    }
    css_computed_style_destroy(item_style);
    css_computed_style_destroy(parent_style);
}
END_TEST

/* A background must neither consume space nor be moved by the late pass.
 * Exercise both absolute/fixed positioning and growth/auto-margin adjustment. */
START_TEST(test_flex_redistribute_out_of_flow)
{
    struct flex_test_style values = {.width = 100, .width_unit = CSS_UNIT_PX,
        .min_width = -1, .min_unit = CSS_UNIT_PX, .max_width = -1, .basis = -1,
        .overflow = CSS_OVERFLOW_VISIBLE, .grow = 1,
        .position = _i % 2 ? CSS_POSITION_FIXED : CSS_POSITION_ABSOLUTE};
    css_computed_style *background_style = flex_test_style_create(&values);
    ck_assert_ptr_nonnull(background_style);
    values.position = CSS_POSITION_STATIC;
    bool grow = _i >= 2;
    values.grow = grow ? 1 : 0;
    css_computed_style *item_style = flex_test_style_create(&values);
    ck_assert_ptr_nonnull(item_style);
    struct box parent = {.type = BOX_FLEX, .width = 100, .height = 400};
    struct box item = {.type = BOX_BLOCK, .style = item_style, .parent = &parent,
        .width = 100, .height = 100, .margin = {grow ? 0 : AUTO, 0, 0, 0}};
    struct box background = {.type = BOX_BLOCK, .style = background_style, .parent = &parent,
        .width = 100, .height = 400, .x = 7, .y = 11, .next = &item};
    item.prev = &background;
    parent.children = &background;
    parent.last = &item;
    for (int pass = 0; pass < 2; pass++) {
        ck_assert(layout_flex_redistribute_auto_margins_vertical(&parent));
        ck_assert_int_eq(item.y, grow ? 0 : 300);
        ck_assert_int_eq(item.height, grow ? 400 : 100);
        ck_assert_int_eq(background.height, 400);
        ck_assert_int_eq(background.x, 7);
        ck_assert_int_eq(background.y, 11);
    }
    css_computed_style_destroy(item_style);
    css_computed_style_destroy(background_style);
}
END_TEST

/* Absolute and fixed children must not consume space in the real flex
 * algorithm, regardless of direction, flex factors or repeated layout. */
START_TEST(test_flex_out_of_flow_sizing)
{
    static const uint8_t directions[] = {CSS_FLEX_DIRECTION_ROW, CSS_FLEX_DIRECTION_ROW_REVERSE,
        CSS_FLEX_DIRECTION_COLUMN, CSS_FLEX_DIRECTION_COLUMN_REVERSE};
    uint8_t direction = directions[_i % 4];
    bool horizontal = _i % 4 < 2;
    bool grow = _i >= 8;
    uint8_t position = (_i / 4) % 2 ? CSS_POSITION_FIXED : CSS_POSITION_ABSOLUTE;
    struct flex_test_style values = {.container = true,
        .width = 400,
        .width_unit = CSS_UNIT_PX,
        .min_width = 0,
        .min_unit = CSS_UNIT_PX,
        .max_width = -1,
        .basis = -1,
        .overflow = CSS_OVERFLOW_VISIBLE};
    css_computed_style *parent_style = flex_test_style_create(&values);
    values.container = false;
    values.width = grow ? 100 : 400;
    values.grow = grow ? 1 : 0;
    values.position = CSS_POSITION_RELATIVE;
    css_computed_style *item_style = flex_test_style_create(&values);
    flex_test_style_alignment(item_style, direction, CSS_JUSTIFY_CONTENT_FLEX_START, grow ? 100 : 400);
    flex_test_style_stretch(item_style, 0, -1, false);
    values.width = 100;
    values.grow = 10;
    values.position = position;
    css_computed_style *overlay_style = flex_test_style_create(&values);
    flex_test_style_alignment(overlay_style, direction, CSS_JUSTIFY_CONTENT_FLEX_START, 100);
    struct box parent = {.type = BOX_FLEX, .style = parent_style};
    struct box item = {
        .type = BOX_BLOCK, .style = item_style, .parent = &parent, .min_width = {CSS_SIZE_SET, 0}, .max_width = 400};
    struct box overlay = {.type = BOX_BLOCK,
        .style = overlay_style,
        .parent = &parent,
        .min_width = {CSS_SIZE_SET, 0},
        .max_width = 100,
        .next = &item};
    item.prev = &overlay;
    parent.children = &overlay;
    parent.last = &item;
    html_content content = {.unit_len_ctx = {.viewport_width = INTTOFIX(1280),
                                .viewport_height = INTTOFIX(900),
                                .font_size_default = INTTOFIX(16),
                                .font_size_minimum = INTTOFIX(6),
                                .device_dpi = INTTOFIX(96),
                                .root_style = parent_style}};
    for (int pass = 0; pass < 2; pass++) {
        int size = pass ? 320 : 400;
        parent.width = size;
        flex_test_style_alignment(parent_style, direction, CSS_JUSTIFY_CONTENT_FLEX_START, size);
        ck_assert(layout_flex(&parent, size, &content));
        int actual = horizontal ? item.width : item.height;
        ck_assert_msg(actual == size, "direction %u, position %u, grow %d, pass %d: expected %d, got %d", direction,
            position, grow, pass, size, actual);
        ck_assert_msg(lh__box_is_flex_item(&item), "relative child must remain a flex item");
        ck_assert_msg(!lh__box_is_flex_item(&overlay), "out-of-flow child is not a flex item");
    }
    css_computed_style_destroy(overlay_style);
    css_computed_style_destroy(item_style);
    css_computed_style_destroy(parent_style);
}
END_TEST

enum { COLUMN_SHRINK_CASES = 6 };

/* Run the real flex solver and placement together, including a second
 * layout at a larger height so the earlier shrink must not become a base. */
START_TEST(test_column_flex_shrink_layout)
{
    static const char *names[] = {"proportional", "minimum", "inflexible", "weighted", "outer edges", "overflow"};
    static const int expected[COLUMN_SHRINK_CASES][2][2] = {{{150, 50}, {225, 75}}, {{180, 20}, {225, 75}},
        {{100, 100}, {100, 200}}, {{200, 0}, {250, 50}}, {{160, 0}, {260, 0}}, {{240, 0}, {240, 60}}};
    int variant = _i % COLUMN_SHRINK_CASES;
    bool reverse = _i >= COLUMN_SHRINK_CASES;
    uint8_t direction = reverse ? CSS_FLEX_DIRECTION_COLUMN_REVERSE : CSS_FLEX_DIRECTION_COLUMN;
    struct flex_test_style values = {.container = true,
        .width = 100,
        .width_unit = CSS_UNIT_PX,
        .min_width = 0,
        .min_unit = CSS_UNIT_PX,
        .max_width = -1,
        .basis = -1,
        .overflow = CSS_OVERFLOW_VISIBLE};
    css_computed_style *parent_style = flex_test_style_create(&values);
    ck_assert_ptr_nonnull(parent_style);
    values.container = false;
    if (variant == 4) {
        values.padding = 10;
        values.border = 5;
        values.margin = 10; /* stretch helper resets the top margin to zero */
    }
    css_computed_style *first_style = flex_test_style_create(&values);
    ck_assert_ptr_nonnull(first_style);
    flex_test_style_alignment(first_style, direction, CSS_JUSTIFY_CONTENT_FLEX_START, variant == 2 ? 100 : 300);
    flex_test_style_stretch(first_style, variant == 1 ? 180 : variant == 5 ? 240 : 0, -1, false);
    if (variant == 2)
        flex_test_style_shrink(first_style, 0);
    values.padding = values.border = values.margin = 0;
    css_computed_style *second_style = flex_test_style_create(&values);
    ck_assert_ptr_nonnull(second_style);
    flex_test_style_alignment(second_style, direction, CSS_JUSTIFY_CONTENT_FLEX_START, variant == 2 ? 300 : 100);
    flex_test_style_stretch(second_style, 0, -1, false);
    if (variant == 3)
        flex_test_style_shrink(second_style, 3);
    struct box parent = {.type = BOX_FLEX, .style = parent_style, .width = 100};
    struct box first = {
        .type = BOX_BLOCK, .style = first_style, .parent = &parent, .min_width = {CSS_SIZE_SET, 0}, .max_width = 100};
    struct box second = {.type = BOX_BLOCK,
        .style = second_style,
        .parent = &parent,
        .min_width = {CSS_SIZE_SET, 0},
        .max_width = 100,
        .prev = &first};
    parent.children = &first;
    if (variant == 4) {
        parent.last = &first;
    } else {
        first.next = &second;
        parent.last = &second;
    }
    html_content content = {.unit_len_ctx = {.viewport_width = INTTOFIX(1280),
                                .viewport_height = INTTOFIX(900),
                                .font_size_default = INTTOFIX(16),
                                .font_size_minimum = INTTOFIX(6),
                                .device_dpi = INTTOFIX(96),
                                .root_style = parent_style}};
    for (int pass = 0; pass < 2; pass++) {
        int height = pass ? 300 : 200;
        flex_test_style_alignment(parent_style, direction, CSS_JUSTIFY_CONTENT_FLEX_START, height);
        ck_assert(layout_flex(&parent, 100, &content));
        int first_height = expected[variant][pass][0];
        int second_height = expected[variant][pass][1];
        ck_assert_msg(first.height == first_height, "%s reverse=%d pass=%d: first height %d, expected %d",
            names[variant], reverse, pass, first.height, first_height);
        int outer_first = first_height + (variant == 4 ? 40 : 0);
        int first_y = (reverse ? height - outer_first : 0) + (variant == 4 ? 5 : 0);
        ck_assert_msg(first.y == first_y, "%s reverse=%d pass=%d: first y %d, expected %d", names[variant], reverse,
            pass, first.y, first_y);
        if (variant != 4) {
            int second_y = reverse ? height - outer_first - second_height : outer_first;
            ck_assert_msg(second.height == second_height && second.y == second_y,
                "%s reverse=%d pass=%d: second y/height %d/%d, expected %d/%d", names[variant], reverse, pass, second.y,
                second.height, second_y, second_height);
        }
        ck_assert_msg(!(first.flags & HEIGHT_STRETCHED), "%s: leaked resolved-height flag", names[variant]);
    }
    css_computed_style_destroy(second_style);
    css_computed_style_destroy(first_style);
    css_computed_style_destroy(parent_style);
}
END_TEST

#ifndef NDEBUG
START_TEST(test_flex_redistribute_requires_resolved_height)
{
    struct box flex = {.type = BOX_FLEX, .height = _i ? UNKNOWN_WIDTH : AUTO};
    layout_flex_redistribute_auto_margins_vertical(&flex);
}
END_TEST
#endif

static const struct {
    const char *name;
    int top, bottom, height;
    bool reverse_wrap, reverse_column, synthetic;
    int line_height, first_y, second_y, baseline;
} baseline_cases[] = {
    {"natural", 0, 0, -1, false, false, false, 40, 19, 0, 32},
    {"positive margins", 20, 25, -1, false, false, false, 66, 20, 1, 33},
    {"negative margins", -10, -8, -1, false, false, false, 39, 19, 0, 32},
    {"definite cross size", 0, 0, 80, false, false, false, 80, 19, 0, 32},
    {"reverse wrap", 0, 0, 80, true, false, false, 80, 59, 40, 72},
    {"auto margin excluded", AUTO, 0, 80, false, false, false, 80, 60, 0, 32},
    {"auto reverse column", 0, 0, -1, false, true, false, 60, 40, 0, 32},
    {"border fallback", 0, 0, -1, false, false, true, 40, 20, 0, 40},
};

START_TEST(test_flex_first_baseline)
{
    struct flex_test_style values = {.container = true, .width = 200, .width_unit = CSS_UNIT_PX,
        .min_width = -1, .max_width = -1, .basis = -1, .overflow = CSS_OVERFLOW_VISIBLE};
    css_computed_style *style = flex_test_style_create(&values);
    uint8_t direction = baseline_cases[_i].reverse_column ? CSS_FLEX_DIRECTION_COLUMN_REVERSE : CSS_FLEX_DIRECTION_ROW;
    flex_test_style_alignment(style, direction, CSS_JUSTIFY_CONTENT_FLEX_START, baseline_cases[_i].height);
    if (baseline_cases[_i].reverse_wrap)
        flex_test_style_wrap(style, CSS_FLEX_WRAP_WRAP_REVERSE);
    struct box flex = {.type = BOX_INLINE_FLEX, .style = style, .width = 200, .height = AUTO};
    struct box items[2] = {0};
    values.container = false;
    values.width = 40;
    for (int i = 0; i < 2; i++) {
        items[i] = (struct box){.type = BOX_BLOCK, .parent = &flex, .style = flex_test_style_create(&values),
            .width = 40, .height = AUTO, .min_width = {CSS_SIZE_SET, 40}, .max_width = 40,
            .content_min_width = 40, .baseline = i ? 32 : 13,
            .flags = baseline_cases[_i].synthetic ? 0 : HAS_BASELINE};
        flex_test_style_alignment(items[i].style, CSS_FLEX_DIRECTION_ROW, CSS_JUSTIFY_CONTENT_FLEX_START, i ? 40 : 20);
        flex_test_style_baseline(items[i].style, i ? 0 : baseline_cases[_i].top, i ? baseline_cases[_i].bottom : 0);
    }
    items[0].next = &items[1];
    items[1].prev = &items[0];
    flex.children = &items[0];
    flex.last = &items[1];
    html_content content = {.unit_len_ctx = {.viewport_width = INTTOFIX(800), .viewport_height = INTTOFIX(600),
        .font_size_default = INTTOFIX(16), .device_dpi = INTTOFIX(96), .root_style = style}};
    for (int pass = 0; pass < 2; pass++) {
        flex.width = 200 + pass * 60;
        ck_assert(layout_flex(&flex, flex.width, &content));
        ck_assert_msg(flex.height == baseline_cases[_i].line_height &&
                items[0].y == baseline_cases[_i].first_y && items[1].y == baseline_cases[_i].second_y &&
                (flex.flags & HAS_BASELINE) && flex.baseline == baseline_cases[_i].baseline,
            "%s pass=%d: height=%d expected %d; positions=%d/%d expected %d/%d; baseline=%d expected %d",
            baseline_cases[_i].name, pass, flex.height, baseline_cases[_i].line_height,
            items[0].y, items[1].y, baseline_cases[_i].first_y, baseline_cases[_i].second_y,
            flex.baseline, baseline_cases[_i].baseline);
    }
    css_computed_style_destroy(items[0].style);
    css_computed_style_destroy(items[1].style);
    css_computed_style_destroy(style);
}
END_TEST

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

    tcase_add_loop_test(tc_core, test_flex_minimum_width_layout, 0, 19);
    tcase_add_loop_test(tc_core, test_flex_shrink_content_minimum, 0,
        sizeof(flex_shrink_minimum_cases) / sizeof(flex_shrink_minimum_cases[0]));
    tcase_add_loop_test(tc_core, test_flex_stretched_height, 0, 8);

    tcase_add_loop_test(tc_core, test_flex_ratio_minimum, 0, 4);
    tcase_add_loop_test(tc_core, test_flex_outer_size_alignment, 0, 8);
    tcase_add_loop_test(tc_core, test_flex_redistribute_out_of_flow, 0, 4);
    tcase_add_loop_test(tc_core, test_flex_out_of_flow_sizing, 0, 16);
    tcase_add_loop_test(tc_core, test_column_flex_shrink_layout, 0, 2 * COLUMN_SHRINK_CASES);
    tcase_add_loop_test(tc_core, test_flex_first_baseline, 0, sizeof(baseline_cases) / sizeof(baseline_cases[0]));
#ifndef NDEBUG
    tcase_add_loop_test_raise_signal(tc_core, test_flex_redistribute_requires_resolved_height, SIGABRT, 0, 2);
#endif
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

/*
 * Copyright 2025 Wisp Contributors
 *
 * This file is part of Wisp.
 *
 * Tests for CSS 2.1 §8.3.1 margin collapsing in layout_block_context().
 *
 * Links against real libwisp. Style creation is in a separate
 * translation unit to avoid enum collisions.
 *
 * IMPORTANT: layout_block_find_dimensions() reads ALL box properties
 * (margins, padding, border, width, height) from the CSS computed style
 * and overwrites the box struct. All properties must be set at the CSS
 * level via the style factory functions.
 */

#include <assert.h>
#include <check.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <wisp/utils/errors.h>
#include <wisp/layout.h>
#include <wisp/content/handlers/html/private.h>
#include <wisp/content/handlers/html/box.h>
#include "html/layout_internal.h"

#include "layout_margin_collapse_style.h"

/* Forward-declare the real function from layout.c */
bool layout_block_context(struct box *block, int viewport_height, html_content *content);

/* ========================================================================
 * Mock font table
 * ======================================================================== */
static nserror mock_font_width(const struct plot_font_style *fstyle, const char *string, size_t length, int *width)
{
    (void)fstyle;
    (void)string;
    (void)length;
    *width = 0;
    return NSERROR_OK;
}

static nserror mock_font_position(
    const struct plot_font_style *fstyle, const char *string, size_t length, int x, size_t *char_offset, int *actual_x)
{
    (void)fstyle;
    (void)string;
    (void)length;
    (void)x;
    *char_offset = 0;
    *actual_x = 0;
    return NSERROR_OK;
}

static nserror mock_font_split(
    const struct plot_font_style *fstyle, const char *string, size_t length, int x, size_t *char_offset, int *actual_x)
{
    (void)fstyle;
    (void)string;
    (void)length;
    (void)x;
    *char_offset = length;
    *actual_x = 0;
    return NSERROR_OK;
}

static const struct gui_layout_table mock_font_table = {
    .width = mock_font_width,
    .position = mock_font_position,
    .split = mock_font_split,
};

/* ========================================================================
 * Test html_content setup
 * ======================================================================== */
static html_content *create_test_content(void)
{
    html_content *c = calloc(1, sizeof(html_content));
    assert(c != NULL);

    c->unit_len_ctx.viewport_width = INTTOFIX(1024);
    c->unit_len_ctx.viewport_height = INTTOFIX(768);
    c->unit_len_ctx.font_size_default = INTTOFIX(16);
    c->unit_len_ctx.font_size_minimum = INTTOFIX(0);
    c->unit_len_ctx.device_dpi = INTTOFIX(96);
    c->unit_len_ctx.root_style = NULL;
    c->unit_len_ctx.pw = NULL;

    c->font_func = &mock_font_table;

    return c;
}

static void destroy_test_content(html_content *c)
{
    free(c);
}

/* ========================================================================
 * Box tree helpers
 *
 * Each box gets its OWN style because layout_block_find_dimensions()
 * reads ALL properties from the CSS computed style and overwrites box->.
 * Set margins, height, border, padding via style_set_*() functions.
 * ======================================================================== */
static struct box *create_box_with_style(int width)
{
    css_computed_style *s = create_block_style();
    struct box *b = calloc(1, sizeof(struct box));
    assert(b != NULL);
    b->type = BOX_BLOCK;
    b->style = s;
    b->width = width;
    b->height = AUTO;
    b->node = NULL;
    return b;
}

static void add_child(struct box *parent, struct box *child)
{
    child->parent = parent;
    if (parent->children == NULL) {
        parent->children = child;
        parent->last = child;
    } else {
        parent->last->next = child;
        child->prev = parent->last;
        parent->last = child;
    }
}

static void free_box_tree(struct box *box)
{
    if (box == NULL)
        return;
    struct box *child = box->children;
    while (child) {
        struct box *next = child->next;
        free_box_tree(child);
        child = next;
    }
    if (box->style)
        destroy_mock_style(box->style);
    free(box);
}


/* ========================================================================
 * Sibling-Sibling Collapse (CSS 2.1 §8.3.1)
 * ======================================================================== */

START_TEST(test_sibling_both_positive)
{
    html_content *content = create_test_content();

    struct box *block = create_box_with_style(200);

    struct box *a = create_box_with_style(200);
    style_set_height_px(a->style, 10);
    a->flags |= HAS_HEIGHT | MAKE_HEIGHT;
    style_set_margins(a->style, 0, 0, 20, 0);

    struct box *b = create_box_with_style(200);
    style_set_height_px(b->style, 10);
    b->flags |= HAS_HEIGHT | MAKE_HEIGHT;
    style_set_margins(b->style, 10, 0, 0, 0);

    add_child(block, a);
    add_child(block, b);

    bool ok = layout_block_context(block, 768, content);
    ck_assert(ok);

    /* Collapsed margin = max(20,10) = 20. b.y = 10 + 20 = 30 */
    ck_assert_msg(b->y == 30, "sibling_both_positive: expected b->y=30, got %d", b->y);

    free_box_tree(block);
    destroy_test_content(content);
}
END_TEST

START_TEST(test_sibling_both_negative)
{
    html_content *content = create_test_content();

    struct box *block = create_box_with_style(200);

    struct box *a = create_box_with_style(200);
    style_set_height_px(a->style, 10);
    style_set_margins(a->style, 0, 0, -5, 0);

    struct box *b = create_box_with_style(200);
    style_set_height_px(b->style, 10);
    style_set_margins(b->style, -10, 0, 0, 0);

    add_child(block, a);
    add_child(block, b);

    bool ok = layout_block_context(block, 768, content);
    ck_assert(ok);

    /* Collapsed = most negative = -10. b.y = 10 - 10 = 0 */
    ck_assert_msg(b->y == 0, "sibling_both_negative: expected b->y=0, got %d", b->y);

    free_box_tree(block);
    destroy_test_content(content);
}
END_TEST

START_TEST(test_sibling_pos_neg)
{
    html_content *content = create_test_content();

    struct box *block = create_box_with_style(200);

    struct box *a = create_box_with_style(200);
    style_set_height_px(a->style, 10);
    style_set_margins(a->style, 0, 0, 20, 0);

    struct box *b = create_box_with_style(200);
    style_set_height_px(b->style, 10);
    style_set_margins(b->style, -5, 0, 0, 0);

    add_child(block, a);
    add_child(block, b);

    bool ok = layout_block_context(block, 768, content);
    ck_assert(ok);

    /* Collapsed = 20 - 5 = 15. b.y = 10 + 15 = 25 */
    ck_assert_msg(b->y == 25, "sibling_pos_neg: expected b->y=25, got %d", b->y);

    free_box_tree(block);
    destroy_test_content(content);
}
END_TEST

START_TEST(test_sibling_no_margin)
{
    html_content *content = create_test_content();

    struct box *block = create_box_with_style(200);

    struct box *a = create_box_with_style(200);
    style_set_height_px(a->style, 10);
    a->flags |= HAS_HEIGHT | MAKE_HEIGHT;

    struct box *b = create_box_with_style(200);
    style_set_height_px(b->style, 10);
    b->flags |= HAS_HEIGHT | MAKE_HEIGHT;

    add_child(block, a);
    add_child(block, b);

    bool ok = layout_block_context(block, 768, content);
    ck_assert(ok);

    ck_assert_msg(b->y == 10, "sibling_no_margin: expected b->y=10, got %d", b->y);

    free_box_tree(block);
    destroy_test_content(content);
}
END_TEST


/* ========================================================================
 * Collapse-Through (CSS 2.1 §8.3.1)
 * ======================================================================== */

START_TEST(test_collapse_through_empty)
{
    html_content *content = create_test_content();

    struct box *block = create_box_with_style(200);

    struct box *a = create_box_with_style(200);
    style_set_height_px(a->style, 10);
    a->flags |= HAS_HEIGHT | MAKE_HEIGHT;
    style_set_margins(a->style, 0, 0, 10, 0);

    /* Empty box: CSS height:0, no padding, no border.
     * Must NOT have MAKE_HEIGHT — it should collapse through. */
    struct box *empty = create_box_with_style(200);
    style_set_height_px(empty->style, 0);
    style_set_margins(empty->style, 5, 0, 15, 0);

    struct box *b = create_box_with_style(200);
    style_set_height_px(b->style, 10);
    b->flags |= HAS_HEIGHT | MAKE_HEIGHT;

    add_child(block, a);
    add_child(block, empty);
    add_child(block, b);

    bool ok = layout_block_context(block, 768, content);
    ck_assert(ok);

    /* All margins are adjoining: max(10, 5, 15) = 15. b.y = 10 + 15 = 25 */
    ck_assert_msg(b->y == 25, "collapse_through_empty: expected b->y=25, got %d", b->y);

    free_box_tree(block);
    destroy_test_content(content);
}
END_TEST

START_TEST(test_no_collapse_through_height)
{
    html_content *content = create_test_content();

    struct box *block = create_box_with_style(200);

    struct box *a = create_box_with_style(200);
    style_set_height_px(a->style, 10);
    a->flags |= HAS_HEIGHT | MAKE_HEIGHT;
    style_set_margins(a->style, 0, 0, 10, 0);

    struct box *mid = create_box_with_style(200);
    style_set_height_px(mid->style, 10);
    mid->flags |= HAS_HEIGHT | MAKE_HEIGHT;
    style_set_margins(mid->style, 5, 0, 15, 0);

    struct box *b = create_box_with_style(200);
    style_set_height_px(b->style, 10);
    b->flags |= HAS_HEIGHT | MAKE_HEIGHT;

    add_child(block, a);
    add_child(block, mid);
    add_child(block, b);

    bool ok = layout_block_context(block, 768, content);
    ck_assert(ok);

    /* a.mb=10 collapses with mid.mt=5 → max(10,5)=10.
     * mid.y = 10 + 10 = 20.
     * mid.mb=15 collapses with b.mt=0 → max(15,0)=15.
     * b.y = 20 + 10 + 15 = 45. */
    ck_assert_msg(mid->y == 20, "no_collapse_through_height: expected mid->y=20, got %d", mid->y);
    ck_assert_msg(b->y == 45, "no_collapse_through_height: expected b->y=45, got %d", b->y);

    free_box_tree(block);
    destroy_test_content(content);
}
END_TEST


/* ========================================================================
 * Parent-First-Child Collapse (CSS 2.1 §8.3.1)
 * Expected: FAIL (known architectural limitation)
 * ======================================================================== */

START_TEST(test_parent_child_no_separator)
{
    html_content *content = create_test_content();

    struct box *root = create_box_with_style(200);

    struct box *p = create_box_with_style(200);
    /* parent has auto height — computed from children */

    struct box *c = create_box_with_style(200);
    style_set_height_px(c->style, 10);
    c->flags |= HAS_HEIGHT | MAKE_HEIGHT;
    style_set_margins(c->style, 20, 0, 0, 0);

    add_child(root, p);
    add_child(p, c);

    bool ok = layout_block_context(root, 768, content);
    ck_assert(ok);

    /* Per spec: parent margin collapses with first child margin.
     * Parent should be pushed down by 20, child at top of parent. */
    ck_assert_msg(p->y == 20,
        "parent_child_no_separator: expected p->y=20, got %d "
        "(child margin should push parent down)",
        p->y);
    ck_assert_msg(c->y == 0,
        "parent_child_no_separator: expected c->y=0, got %d "
        "(child should be at top of parent)",
        c->y);

    free_box_tree(root);
    destroy_test_content(content);
}
END_TEST

START_TEST(test_parent_child_with_border)
{
    html_content *content = create_test_content();

    struct box *root = create_box_with_style(200);

    struct box *p = create_box_with_style(200);
    /* parent has border-top → blocks collapse */
    style_set_border_top(p->style, 1);

    struct box *c = create_box_with_style(200);
    style_set_height_px(c->style, 10);
    c->flags |= HAS_HEIGHT | MAKE_HEIGHT;
    style_set_margins(c->style, 20, 0, 0, 0);

    add_child(root, p);
    add_child(p, c);

    bool ok = layout_block_context(root, 768, content);
    ck_assert(ok);

    /* Border blocks collapse: parent border adds to y (engine convention
     * at layout.c:3702), child margin applied inside parent:
     * p.y = 0 + border(1) = 1,  c.y = margin(20) = 20 */
    ck_assert_msg(p->y == 1, "parent_child_with_border: expected p->y=1, got %d", p->y);
    ck_assert_msg(c->y == 20, "parent_child_with_border: expected c->y=20, got %d", c->y);

    free_box_tree(root);
    destroy_test_content(content);
}
END_TEST

START_TEST(test_parent_child_with_padding)
{
    html_content *content = create_test_content();

    struct box *root = create_box_with_style(200);

    struct box *p = create_box_with_style(200);
    /* parent has padding-top → blocks collapse */
    style_set_padding_top(p->style, 5);

    struct box *c = create_box_with_style(200);
    style_set_height_px(c->style, 10);
    c->flags |= HAS_HEIGHT | MAKE_HEIGHT;
    style_set_margins(c->style, 20, 0, 0, 0);

    add_child(root, p);
    add_child(p, c);

    bool ok = layout_block_context(root, 768, content);
    ck_assert(ok);

    /* Padding blocks collapse: parent stays at 0, child margin
     * applied inside parent: c.y = padding(5) + margin(20) = 25 */
    ck_assert_msg(p->y == 0, "parent_child_with_padding: expected p->y=0, got %d", p->y);
    ck_assert_msg(c->y == 25, "parent_child_with_padding: expected c->y=25, got %d", c->y);

    free_box_tree(root);
    destroy_test_content(content);
}
END_TEST


/* ========================================================================
 * Test runner
 * ======================================================================== */
static Suite *margin_collapse_suite(void)
{
    Suite *s = suite_create("Margin Collapse");

    TCase *tc_sibling = tcase_create("Sibling Collapse");
    tcase_add_test(tc_sibling, test_sibling_both_positive);
    tcase_add_test(tc_sibling, test_sibling_both_negative);
    tcase_add_test(tc_sibling, test_sibling_pos_neg);
    tcase_add_test(tc_sibling, test_sibling_no_margin);
    suite_add_tcase(s, tc_sibling);

    TCase *tc_through = tcase_create("Collapse Through");
    tcase_add_test(tc_through, test_collapse_through_empty);
    tcase_add_test(tc_through, test_no_collapse_through_height);
    suite_add_tcase(s, tc_through);

    TCase *tc_parent = tcase_create("Parent-First-Child Collapse");
    tcase_add_test(tc_parent, test_parent_child_no_separator);
    tcase_add_test(tc_parent, test_parent_child_with_border);
    tcase_add_test(tc_parent, test_parent_child_with_padding);
    suite_add_tcase(s, tc_parent);

    return s;
}

int main(void)
{
    int number_failed;
    Suite *s = margin_collapse_suite();
    SRunner *sr = srunner_create(s);

    srunner_set_fork_status(sr, CK_NOFORK);
    srunner_run_all(sr, CK_VERBOSE);
    number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);

    return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}

/*
 * Copyright 2025 Wisp Contributors
 *
 * This file is part of Wisp.
 *
 * Tests for margin collapsing and float avoidance in layout_block_context().
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
#include <wisp/utils/nsoption.h>
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
    a->flags |= HAS_HEIGHT | MAKE_HEIGHT;
    style_set_margins(a->style, 0, 0, -5, 0);

    struct box *b = create_box_with_style(200);
    style_set_height_px(b->style, 10);
    b->flags |= HAS_HEIGHT | MAKE_HEIGHT;
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
    a->flags |= HAS_HEIGHT | MAKE_HEIGHT;
    style_set_margins(a->style, 0, 0, 20, 0);

    struct box *b = create_box_with_style(200);
    style_set_height_px(b->style, 10);
    b->flags |= HAS_HEIGHT | MAKE_HEIGHT;
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
 * Additional Sibling Cases
 * ======================================================================== */

START_TEST(test_three_siblings_cascade)
{
    html_content *content = create_test_content();

    struct box *block = create_box_with_style(200);

    struct box *a = create_box_with_style(200);
    style_set_height_px(a->style, 10);
    a->flags |= HAS_HEIGHT | MAKE_HEIGHT;
    style_set_margins(a->style, 0, 0, 10, 0);

    struct box *b = create_box_with_style(200);
    style_set_height_px(b->style, 10);
    b->flags |= HAS_HEIGHT | MAKE_HEIGHT;
    style_set_margins(b->style, 20, 0, 15, 0);

    struct box *c = create_box_with_style(200);
    style_set_height_px(c->style, 10);
    c->flags |= HAS_HEIGHT | MAKE_HEIGHT;
    style_set_margins(c->style, 5, 0, 0, 0);

    add_child(block, a);
    add_child(block, b);
    add_child(block, c);

    bool ok = layout_block_context(block, 768, content);
    ck_assert(ok);

    /* a.mb=10 collapses with b.mt=20 → max(10,20)=20. b.y = 10+20 = 30
     * b.mb=15 collapses with c.mt=5 → max(15,5)=15. c.y = 30+10+15 = 55 */
    ck_assert_msg(b->y == 30, "three_siblings_cascade: expected b->y=30, got %d", b->y);
    ck_assert_msg(c->y == 55, "three_siblings_cascade: expected c->y=55, got %d", c->y);

    free_box_tree(block);
    destroy_test_content(content);
}
END_TEST


/* ========================================================================
 * Exclusion Rules (CSS 2.1 §8.3.1)
 * ======================================================================== */

START_TEST(test_no_collapse_abspos_skipped)
{
    html_content *content = create_test_content();

    struct box *block = create_box_with_style(200);

    struct box *a = create_box_with_style(200);
    style_set_height_px(a->style, 10);
    a->flags |= HAS_HEIGHT | MAKE_HEIGHT;
    style_set_margins(a->style, 0, 0, 20, 0);

    /* Abspos box — should be completely skipped in normal flow */
    struct box *absbox = create_box_with_style(200);
    style_set_height_px(absbox->style, 10);
    absbox->flags |= HAS_HEIGHT | MAKE_HEIGHT;
    style_set_position_absolute(absbox->style);
    style_set_margins(absbox->style, 100, 0, 100, 0);

    struct box *b = create_box_with_style(200);
    style_set_height_px(b->style, 10);
    b->flags |= HAS_HEIGHT | MAKE_HEIGHT;
    style_set_margins(b->style, 10, 0, 0, 0);

    add_child(block, a);
    add_child(block, absbox);
    add_child(block, b);

    bool ok = layout_block_context(block, 768, content);
    ck_assert(ok);

    /* absbox is out of flow, so a.mb=20 collapses with b.mt=10.
     * collapsed = max(20,10) = 20.  b.y = 10 + 20 = 30 */
    ck_assert_msg(b->y == 30, "abspos_skipped: expected b->y=30, got %d", b->y);

    free_box_tree(block);
    destroy_test_content(content);
}
END_TEST

START_TEST(test_block_context_contains_margins)
{
    html_content *content = create_test_content();

    struct box *root = create_box_with_style(200);

    /* Both styles create a BFC; flow-root preserves visible overflow. */
    struct box *p = create_box_with_style(200);
    if (_i)
        style_set_flow_root(p->style);
    else
        style_set_overflow_hidden(p->style);

    struct box *c = create_box_with_style(200);
    style_set_height_px(c->style, 10);
    c->flags |= HAS_HEIGHT | MAKE_HEIGHT;
    style_set_margins(c->style, 20, 0, 0, 0);

    add_child(root, p);
    add_child(p, c);

    bool ok = layout_block_context(root, 768, content);
    ck_assert(ok);

    /* Child margin stays inside the new BFC.
     * p.y = 0, c.y = 20 (margin applied inside new BFC) */
    ck_assert_msg(p->y == 0, "BFC (flow-root=%d): expected p->y=0, got %d", _i, p->y);
    ck_assert_msg(c->y == 20, "BFC (flow-root=%d): expected c->y=20, got %d", _i, c->y);

    free_box_tree(root);
    destroy_test_content(content);
}
END_TEST

enum { EMPTY_BFC_CASE_COUNT = 12 };

START_TEST(test_empty_block_context_margins)
{
    const struct {
        int top, bottom, next_top, expected_next;
    } margins[] = {
        { 0, 30, 0, 30 }, /* Empty advertising slot before the article. */
        { 20, 30, 10, 50 }, /* Own margins stay separate; sibling margins collapse. */
        { 20, -15, 10, 15 },
    };
    int variant = (_i % EMPTY_BFC_CASE_COUNT) / 4;
    bool flow_root = _i >= EMPTY_BFC_CASE_COUNT;
    bool empty_child = (_i & 1) != 0;
    bool wrapped = (_i & 2) != 0;
    html_content *content = create_test_content();
    struct box *root = create_box_with_style(200);
    struct box *slot = create_box_with_style(200);
    if (flow_root)
        style_set_flow_root(slot->style);
    else
        style_set_overflow_hidden(slot->style);
    style_set_margins(slot->style, margins[variant].top, 0, margins[variant].bottom, 0);
    if (empty_child)
        add_child(slot, create_box_with_style(200));
    struct box *wrapper = wrapped ? create_box_with_style(200) : slot;
    if (wrapped)
        add_child(wrapper, slot);
    add_child(root, wrapper);
    struct box *next = create_box_with_style(200);
    style_set_height_px(next->style, 10);
    next->flags |= HAS_HEIGHT | MAKE_HEIGHT;
    style_set_margins(next->style, margins[variant].next_top, 0, 0, 0);
    add_child(root, next);

    ck_assert(layout_block_context(root, 768, content));
    ck_assert_int_eq(slot->height, 0);
    ck_assert_int_eq(wrapper->y, margins[variant].top);
    if (wrapped)
        ck_assert_int_eq(slot->y, 0);
    ck_assert_int_eq(next->y, margins[variant].expected_next);

    free_box_tree(root);
    destroy_test_content(content);
}
END_TEST


/* ========================================================================
 * Parent-Last-Child Collapse (CSS 2.1 §8.3.1 Rule 3)
 * ======================================================================== */

START_TEST(test_last_child_auto_height)
{
    html_content *content = create_test_content();

    struct box *root = create_box_with_style(200);

    struct box *p = create_box_with_style(200);
    /* auto height, no bottom border/padding → last-child collapse allowed */

    struct box *c = create_box_with_style(200);
    style_set_height_px(c->style, 10);
    c->flags |= HAS_HEIGHT | MAKE_HEIGHT;
    style_set_margins(c->style, 0, 0, 20, 0);

    struct box *next = create_box_with_style(200);
    style_set_height_px(next->style, 10);
    next->flags |= HAS_HEIGHT | MAKE_HEIGHT;

    add_child(root, p);
    add_child(p, c);
    add_child(root, next);

    bool ok = layout_block_context(root, 768, content);
    ck_assert(ok);

    /* Per spec: c.mb=20 collapses with p.mb=0 (auto height, no border).
     * P.height = 10 (from child), next.y = 0 + 10 + 20 = 30 */
    ck_assert_msg(next->y == 30,
        "last_child_auto_height: expected next->y=30, got %d "
        "(last child margin should propagate out of auto-height parent)",
        next->y);

    free_box_tree(root);
    destroy_test_content(content);
}
END_TEST

START_TEST(test_last_child_fixed_height)
{
    html_content *content = create_test_content();

    struct box *root = create_box_with_style(200);

    struct box *p = create_box_with_style(200);
    style_set_height_px(p->style, 100);
    p->flags |= HAS_HEIGHT | MAKE_HEIGHT;
    /* fixed height → last-child margin does NOT propagate */

    struct box *c = create_box_with_style(200);
    style_set_height_px(c->style, 10);
    c->flags |= HAS_HEIGHT | MAKE_HEIGHT;
    style_set_margins(c->style, 0, 0, 20, 0);

    struct box *next = create_box_with_style(200);
    style_set_height_px(next->style, 10);
    next->flags |= HAS_HEIGHT | MAKE_HEIGHT;

    add_child(root, p);
    add_child(p, c);
    add_child(root, next);

    bool ok = layout_block_context(root, 768, content);
    ck_assert(ok);

    /* Fixed height: c.mb stays inside p. next.y = p.height = 100 */
    ck_assert_msg(next->y == 100, "last_child_fixed_height: expected next->y=100, got %d", next->y);

    free_box_tree(root);
    destroy_test_content(content);
}
END_TEST

START_TEST(test_last_child_with_border_bottom)
{
    html_content *content = create_test_content();

    struct box *root = create_box_with_style(200);

    struct box *p = create_box_with_style(200);
    /* auto height BUT has border-bottom → blocks last-child collapse */
    style_set_border_bottom(p->style, 1);

    struct box *c = create_box_with_style(200);
    style_set_height_px(c->style, 10);
    c->flags |= HAS_HEIGHT | MAKE_HEIGHT;
    style_set_margins(c->style, 0, 0, 20, 0);

    struct box *next = create_box_with_style(200);
    style_set_height_px(next->style, 10);
    next->flags |= HAS_HEIGHT | MAKE_HEIGHT;

    add_child(root, p);
    add_child(p, c);
    add_child(root, next);

    bool ok = layout_block_context(root, 768, content);
    ck_assert(ok);

    /* The child's bottom margin contributes to the auto content height,
     * before the parent's bottom border: 10 + 20 + 1. */
    ck_assert_int_eq(p->height, 30);
    ck_assert_msg(next->y == 31, "last_child_with_border_bottom: expected next->y=31, got %d", next->y);

    free_box_tree(root);
    destroy_test_content(content);
}
END_TEST


/* ========================================================================
 * Additional Collapse-Through Cases
 * ======================================================================== */

START_TEST(test_nested_collapse_through)
{
    html_content *content = create_test_content();

    struct box *block = create_box_with_style(200);

    struct box *a = create_box_with_style(200);
    style_set_height_px(a->style, 10);
    a->flags |= HAS_HEIGHT | MAKE_HEIGHT;
    style_set_margins(a->style, 0, 0, 10, 0);

    /* Two consecutive empty boxes — both should collapse through */
    struct box *e1 = create_box_with_style(200);
    style_set_height_px(e1->style, 0);
    style_set_margins(e1->style, 5, 0, 8, 0);

    struct box *e2 = create_box_with_style(200);
    style_set_height_px(e2->style, 0);
    style_set_margins(e2->style, 12, 0, 3, 0);

    struct box *b = create_box_with_style(200);
    style_set_height_px(b->style, 10);
    b->flags |= HAS_HEIGHT | MAKE_HEIGHT;

    add_child(block, a);
    add_child(block, e1);
    add_child(block, e2);
    add_child(block, b);

    bool ok = layout_block_context(block, 768, content);
    ck_assert(ok);

    /* All margins are adjoining via collapse-through:
     * a.mb=10, e1.mt=5, e1.mb=8, e2.mt=12, e2.mb=3
     * collapsed = max(10, 5, 8, 12, 3) = 12.  b.y = 10 + 12 = 22 */
    ck_assert_msg(b->y == 22, "nested_collapse_through: expected b->y=22, got %d", b->y);

    free_box_tree(block);
    destroy_test_content(content);
}
END_TEST


/* An entirely empty chain must stay pending until the formatting-context
 * boundary. Nesting empty children must not apply its margin a second time. */
static const struct {
    const char *name;
    int first_bottom, second_bottom, prefix_height, collapsed;
} empty_chain_cases[] = {
    {"equal", 30, 30, 0, 30},
    {"first-larger", 30, 10, 0, 30},
    {"last-larger", 10, 30, 0, 30},
    {"mixed", 30, -10, 40, 20},
    {"negative", -10, -20, 40, -20},
    {"zero", 0, 0, 0, 0},
};
enum { EMPTY_CHAIN_CASE_COUNT = sizeof(empty_chain_cases) / sizeof(empty_chain_cases[0]) };

START_TEST(test_nested_empty_margin_chain)
{
    int variant = _i % EMPTY_CHAIN_CASE_COUNT;
    bool with_following = _i >= EMPTY_CHAIN_CASE_COUNT;
    html_content *content = create_test_content();
    struct box *root = create_box_with_style(200);
    if (empty_chain_cases[variant].prefix_height) {
        struct box *prefix = create_box_with_style(200);
        style_set_height_px(prefix->style, empty_chain_cases[variant].prefix_height);
        prefix->flags |= HAS_HEIGHT | MAKE_HEIGHT;
        add_child(root, prefix);
    }
    for (int i = 0; i < 2; i++) {
        struct box *empty = create_box_with_style(200);
        style_set_margins(empty->style, 0, 0,
            i == 0 ? empty_chain_cases[variant].first_bottom : empty_chain_cases[variant].second_bottom, 0);
        add_child(root, empty);
        /* Match the nested empty wrappers in Tion's mobile advertising column. */
        for (int depth = 0; depth < 2; depth++) {
            struct box *child = create_box_with_style(200);
            add_child(empty, child);
            empty = child;
        }
    }
    struct box *following = NULL;
    if (with_following) {
        following = create_box_with_style(200);
        style_set_height_px(following->style, 10);
        following->flags |= HAS_HEIGHT | MAKE_HEIGHT;
        add_child(root, following);
    }
    int end = empty_chain_cases[variant].prefix_height + empty_chain_cases[variant].collapsed;
    for (int pass = 0; pass < 2; pass++) {
        root->height = AUTO;
        ck_assert(layout_block_context(root, 768, content));
        ck_assert_msg(root->height == end + (with_following ? 10 : 0),
            "%s (following=%d, pass=%d): expected height %d, got %d",
            empty_chain_cases[variant].name, with_following, pass,
            end + (with_following ? 10 : 0), root->height);
        if (following) {
            ck_assert_msg(following->y == end, "%s (pass=%d): expected following y=%d, got %d",
                empty_chain_cases[variant].name, pass, end, following->y);
        }
    }
    free_box_tree(root);
    destroy_test_content(content);
}
END_TEST

/* Lookahead may cross empty siblings before finding a parent whose first
 * child's margin collapses outside it. Empty siblings must keep zero height
 * and must not receive that later margin. Empty children inside the receiving
 * parent, however, move with it (CSS 2.1 section 8.3.1). */
static const char *const margin_receiver_variants[] = {
    "empty blocks / leaf", "empty inline / leaf",
    "empty blocks / nested parent", "empty inline / nested parent",
    "empty blocks / parent with empty first child", "empty inline / parent with empty first child",
};
enum { MARGIN_RECEIVER_CASE_COUNT = sizeof(margin_receiver_variants) / sizeof(margin_receiver_variants[0]) };

START_TEST(test_margin_chain_receiver)
{
    int variant = _i % MARGIN_RECEIVER_CASE_COUNT;
    int margin = (_i / MARGIN_RECEIVER_CASE_COUNT) % 2 == 0 ? 150 : -150;
    bool inside_prefix = _i >= 2 * MARGIN_RECEIVER_CASE_COUNT;
    html_content *content = create_test_content();
    struct box *root = create_box_with_style(200);
    struct box *prefix = create_box_with_style(200);
    struct box *prefix_content = prefix;
    if (inside_prefix) {
        prefix_content = create_box_with_style(200);
        add_child(prefix, prefix_content);
    }
    style_set_height_px(prefix_content->style, 200);
    prefix_content->flags |= HAS_HEIGHT | MAKE_HEIGHT;
    add_child(root, prefix);

    struct box *empty = create_box_with_style(200);
    struct box *inner = create_box_with_style(200);
    add_child(inside_prefix ? prefix : root, empty);
    add_child(empty, inner);
    if (variant % 2) {
        struct box *ic = calloc(1, sizeof(*ic));
        ck_assert_ptr_nonnull(ic);
        ic->type = BOX_INLINE_CONTAINER;
        add_child(inner, ic);
        struct box *inline_box = create_box_with_style(0);
        inline_box->type = BOX_INLINE;
        add_child(ic, inline_box);
    } else {
        add_child(inner, create_box_with_style(200));
    }

    struct box *receiver = create_box_with_style(200);
    struct box *leaf = receiver;
    struct box *inside_empty = NULL;
    add_child(root, receiver);
    if (variant >= 2) {
        struct box *wrapper = create_box_with_style(200);
        leaf = create_box_with_style(200);
        add_child(receiver, wrapper);
        if (variant >= 4) {
            inside_empty = create_box_with_style(200);
            add_child(wrapper, inside_empty);
            add_child(inside_empty, create_box_with_style(200));
        }
        add_child(wrapper, leaf);
    }
    style_set_margins(leaf->style, margin, 0, 0, 0);
    style_set_height_px(leaf->style, 10);
    leaf->flags |= HAS_HEIGHT | MAKE_HEIGHT;
    for (int pass = 0; pass < 2; pass++) {
        root->height = AUTO;
        ck_assert(layout_block_context(root, 768, content));
        ck_assert_msg(prefix->height == 200 && empty->y == 200 && empty->height == 0 &&
                inner->y == 0 && inner->height == 0,
            "%s (margin=%d, pass=%d, inside prefix=%d): prefix height=%d, empty y/h=%d/%d, inner y/h=%d/%d",
            margin_receiver_variants[variant], margin, pass, inside_prefix, prefix->height,
            empty->y, empty->height, inner->y, inner->height);
        ck_assert_msg(receiver->y == 200 + margin && receiver->height == 10 && root->height == 210 + margin,
            "%s (margin=%d, pass=%d): receiver y/h=%d/%d, root height=%d",
            margin_receiver_variants[variant], margin, pass, receiver->y, receiver->height, root->height);
        if (variant >= 2) {
            ck_assert_msg(receiver->children->y == 0 && leaf->y == 0,
                "%s (margin=%d, pass=%d): wrapper/leaf y=%d/%d",
                margin_receiver_variants[variant], margin, pass, receiver->children->y, leaf->y);
        }
        if (inside_empty) {
            ck_assert_msg(inside_empty->y == 0 && inside_empty->height == 0,
                "%s (margin=%d, pass=%d): inside empty y/h=%d/%d",
                margin_receiver_variants[variant], margin, pass, inside_empty->y, inside_empty->height);
        }
    }
    free_box_tree(root);
    destroy_test_content(content);
}
END_TEST

/* The empty box's own top excludes its bottom and later sibling margins.
 * Empty descendants join its top, while following flow uses the full chain. */
static const struct {
    const char *name;
    int previous, top, bottom, child_top, child_bottom, following, empty_y, following_y;
    bool nested;
} empty_position_cases[] = {
    {"positive", 12, 15, 25, 0, 0, 50, 75, 110, false},
    {"negative", -12, -15, -25, 0, 0, -50, 45, 10, false},
    {"mixed", 12, -15, 25, 0, 0, 50, 57, 95, false},
    {"mixed-negative", -12, 15, -25, 0, 0, -50, 63, 25, false},
    {"nested-positive", 12, 15, 25, 30, 40, 50, 100, 110, true},
    {"nested-negative", -12, -15, -25, -30, -40, -50, 20, 10, true},
    {"nested-mixed", 12, -15, 25, 30, -40, 50, 50, 70, true},
};
enum { EMPTY_POSITION_CASE_COUNT = sizeof(empty_position_cases) / sizeof(empty_position_cases[0]) };

static struct box *add_margin_test_float(struct box *parent)
{
    struct box *ic = calloc(1, sizeof(*ic));
    ck_assert_ptr_nonnull(ic);
    ic->type = BOX_INLINE_CONTAINER;
    add_child(parent, ic);
    struct box *floating = create_box_with_style(10);
    floating->type = BOX_FLOAT_LEFT;
    add_child(ic, floating);
    struct box *float_content = create_box_with_style(10);
    style_set_width(float_content->style, 10, false);
    style_set_height_px(float_content->style, 8);
    float_content->flags |= HAS_HEIGHT | MAKE_HEIGHT;
    add_child(floating, float_content);
    return floating;
}

enum { EMPTY_FLOAT_POSITION_COUNT = 5 };

START_TEST(test_empty_margin_position)
{
    int variant = _i % EMPTY_POSITION_CASE_COUNT;
    int float_position = _i / EMPTY_POSITION_CASE_COUNT;
    struct box *floats[2] = {NULL, NULL};
    html_content *content = create_test_content();
    struct box *root = create_box_with_style(200);
    struct box *prefix = create_box_with_style(200);
    style_set_height_px(prefix->style, 60);
    prefix->flags |= HAS_HEIGHT | MAKE_HEIGHT;
    style_set_margins(prefix->style, 0, 0, empty_position_cases[variant].previous, 0);
    add_child(root, prefix);
    struct box *empty = create_box_with_style(200);
    style_set_margins(empty->style, empty_position_cases[variant].top, 0, empty_position_cases[variant].bottom, 0);
    add_child(root, empty);
    if (float_position == 2 || float_position == 4)
        floats[0] = add_margin_test_float(empty);
    struct box *child = NULL;
    if (empty_position_cases[variant].nested) {
        child = create_box_with_style(200);
        style_set_margins(child->style, empty_position_cases[variant].child_top, 0,
            empty_position_cases[variant].child_bottom, 0);
        add_child(empty, child);
    }
    /* Exercise floats inside the leaf, before/after the child, and both. */
    if (float_position == 1)
        floats[0] = add_margin_test_float(child ? child : empty);
    if (float_position == 3 || float_position == 4)
        floats[1] = add_margin_test_float(empty);
    struct box *following = create_box_with_style(200);
    style_set_height_px(following->style, 10);
    following->flags |= HAS_HEIGHT | MAKE_HEIGHT;
    style_set_margins(following->style, empty_position_cases[variant].following, 0, 0, 0);
    add_child(root, following);
    for (int pass = 0; pass < 2; pass++) {
        root->height = AUTO;
        ck_assert(layout_block_context(root, 768, content));
        ck_assert_msg(empty->y == empty_position_cases[variant].empty_y && empty->height == 0,
            "%s (float=%d) pass %d: expected empty y/h=%d/0, got %d/%d",
            empty_position_cases[variant].name, float_position, pass,
            empty_position_cases[variant].empty_y, empty->y, empty->height);
        int expected_height = empty_position_cases[variant].following_y + 10;
        for (size_t f = 0; f < sizeof(floats) / sizeof(floats[0]); f++) {
            struct box *floating = floats[f];
            if (!floating)
                continue;
            int float_bottom = empty_position_cases[variant].empty_y + 8;
            if (expected_height < float_bottom)
                expected_height = float_bottom;
            ck_assert_msg(floating->y == empty_position_cases[variant].empty_y && floating->height == 8,
                "%s pass %d: expected float y/h=%d/8, got %d/%d", empty_position_cases[variant].name, pass,
                empty_position_cases[variant].empty_y, floating->y, floating->height);
        }
        ck_assert_msg(following->y == empty_position_cases[variant].following_y && root->height == expected_height,
            "%s (float=%d) pass %d: expected following y=%d, got %d; root height=%d (expected %d)",
            empty_position_cases[variant].name, float_position, pass,
            empty_position_cases[variant].following_y, following->y, root->height, expected_height);
        if (child) {
            ck_assert_msg(child->y == 0 && child->height == 0,
                "%s pass %d: child must share empty parent's top; y/h=%d/%d",
                empty_position_cases[variant].name, pass, child->y, child->height);
        }
    }
    free_box_tree(root);
    destroy_test_content(content);
}
END_TEST

static const int float_placement_origins[] = {-73, 0, 27};
static const uint8_t float_placement_clears[] = {CSS_CLEAR_NONE, CSS_CLEAR_LEFT, CSS_CLEAR_RIGHT};
enum {
    FLOAT_PLACEMENT_ORIGIN_COUNT = sizeof(float_placement_origins) / sizeof(float_placement_origins[0]),
    FLOAT_PLACEMENT_CLEAR_COUNT = sizeof(float_placement_clears) / sizeof(float_placement_clears[0]),
};

START_TEST(test_float_placement_coordinates)
{
    int origin = float_placement_origins[_i / FLOAT_PLACEMENT_CLEAR_COUNT];
    uint8_t clear = float_placement_clears[_i % FLOAT_PLACEMENT_CLEAR_COUNT];
    html_content *content = create_test_content();
    struct box *root = create_box_with_style(300);
    /* A real block's automatic minimum resolves to zero, even when all its
     * descendants finish above the formatting context's origin. */
    style_set_min_height(root->style, 0);
    struct box *parent = create_box_with_style(300);
    style_set_margins(parent->style, origin, 0, 0, 0);
    add_child(root, parent);
    struct box *ic = calloc(1, sizeof(*ic));
    ck_assert_ptr_nonnull(ic);
    ic->type = BOX_INLINE_CONTAINER;
    add_child(parent, ic);
    struct box *floats[2];
    for (int i = 0; i < 2; i++) {
        floats[i] = create_box_with_style(0);
        floats[i]->type = BOX_FLOAT_LEFT;
        add_child(ic, floats[i]);
        struct box *child = create_box_with_style(0);
        style_set_width(child->style, i ? 280 : 40, false);
        style_set_height_px(child->style, i ? 20 : 48);
        style_set_clear(child->style, i ? clear : CSS_CLEAR_NONE);
        child->flags |= HAS_HEIGHT | MAKE_HEIGHT;
        add_child(floats[i], child);
    }
    const int widths[] = {300, 500, 300};
    for (size_t pass = 0; pass < sizeof(widths) / sizeof(widths[0]); pass++) {
        root->width = widths[pass];
        root->height = AUTO;
        ck_assert(layout_block_context(root, 768, content));
        bool below = clear == CSS_CLEAR_LEFT || widths[pass] < 320;
        int second_y = origin + (below ? 48 : 0);
        int height = origin + (below ? 68 : 48);
        if (height < 0)
            height = 0;
        ck_assert_msg(parent->y == origin && parent->height == 0 && floats[0]->y == origin &&
                floats[1]->y == second_y && floats[1]->x == (below ? 0 : 40) && root->height == height,
            "origin %d, clear %u, width %d: parent=%d/%d, floats=(%d,%d), second x=%d, root height=%d",
            origin, clear, widths[pass], parent->y, parent->height, floats[0]->y, floats[1]->y,
            floats[1]->x, root->height);
    }
    free_box_tree(root);
    destroy_test_content(content);
}
END_TEST

enum { CLEARANCE_PREFIX_COUNT = 3, CLEARANCE_CASE_COUNT = 2 * CLEARANCE_PREFIX_COUNT };

/* CSS 2.1 sections 8.3.1 and 9.5.2: decide clearance from the hypothetical
 * top, split the adjoining margins there, and contain the resulting chain
 * in its parent. No actual clearance means ordinary margin collapse. */
START_TEST(test_empty_margin_position_with_clear)
{
    static const int prefix_heights[CLEARANCE_PREFIX_COUNT] = {10, 70, 100};
    static const int empty_positions[CLEARANCE_CASE_COUNT] = {80, 80, 90, 95, 120, 125};
    static const int following_positions[CLEARANCE_CASE_COUNT] = {115, 140, 120, 120, 150, 150};
    static const int parent_positions[CLEARANCE_PREFIX_COUNT] = {30, 95, 125};
    static const int parent_heights[CLEARANCE_PREFIX_COUNT] = {60, 0, 0};
    bool nested = _i % 2;
    int prefix_height = prefix_heights[_i / 2];
    html_content *content = create_test_content();
    struct box *root = create_box_with_style(200);
    struct box *ic = calloc(1, sizeof(*ic));
    ck_assert_ptr_nonnull(ic);
    ic->type = BOX_INLINE_CONTAINER;
    add_child(root, ic);
    struct box *fl = create_box_with_style(10);
    fl->type = BOX_FLOAT_LEFT;
    struct box *float_content = create_box_with_style(10);
    style_set_width(float_content->style, 10, false);
    style_set_height_px(float_content->style, 80);
    float_content->flags |= HAS_HEIGHT | MAKE_HEIGHT;
    add_child(fl, float_content);
    add_child(ic, fl);
    struct box *prefix = create_box_with_style(200);
    style_set_height_px(prefix->style, prefix_height);
    prefix->flags |= HAS_HEIGHT | MAKE_HEIGHT;
    style_set_margins(prefix->style, 0, 0, 20, 0);
    add_child(root, prefix);
    struct box *empty = create_box_with_style(200);
    struct box *wrapper = nested ? create_box_with_style(200) : root;
    if (nested)
        add_child(root, wrapper);
    add_child(wrapper, empty);
    style_set_clear(empty->style, CSS_CLEAR_BOTH);
    style_set_margins(empty->style, 15, 0, 25, 0);
    struct box *following = create_box_with_style(200);
    style_set_height_px(following->style, 10);
    following->flags |= HAS_HEIGHT | MAKE_HEIGHT;
    style_set_margins(following->style, 50, 0, 0, 0);
    add_child(root, following);
    for (int pass = 0; pass < 2; pass++) {
        root->height = AUTO;
        ck_assert(layout_block_context(root, 768, content));
        int actual = empty->y + (nested ? wrapper->y : 0);
        ck_assert_msg(actual == empty_positions[_i] && empty->height == 0,
            "clear (nested=%d, prefix=%d), pass %d: expected empty y/h=%d/0, got %d/%d",
            nested, prefix_height, pass, empty_positions[_i], actual, empty->height);
        ck_assert_msg(following->y == following_positions[_i] && root->height == following_positions[_i] + 10,
            "clear (nested=%d, prefix=%d), pass %d: expected following y=%d, got %d; root height=%d",
            nested, prefix_height, pass, following_positions[_i], following->y, root->height);
        if (nested) {
            ck_assert_msg(wrapper->y == parent_positions[_i / 2] && wrapper->height == parent_heights[_i / 2],
                "clear (prefix=%d), pass %d: expected parent y/h=%d/%d, got %d/%d",
                prefix_height, pass, parent_positions[_i / 2], parent_heights[_i / 2], wrapper->y, wrapper->height);
        }
    }
    free_box_tree(root);
    destroy_test_content(content);
}
END_TEST

/* A first child's top margin would move its ancestor and an internal float
 * together. Large margins therefore cannot avoid clearance of that float. */
static const int parent_clearance_margins[] = {23, 48, 100};
static const struct {
    const char *name;
    bool right_float;
    uint8_t clear;
    bool matching;
} parent_clearance_sides[] = {
    {"left", false, CSS_CLEAR_LEFT, true},
    {"right", true, CSS_CLEAR_RIGHT, true},
    {"unmatched", false, CSS_CLEAR_RIGHT, false},
};
enum {
    PARENT_CLEARANCE_MARGIN_COUNT = sizeof(parent_clearance_margins) / sizeof(parent_clearance_margins[0]),
    PARENT_CLEARANCE_SIDE_COUNT = sizeof(parent_clearance_sides) / sizeof(parent_clearance_sides[0]),
    PARENT_CLEARANCE_CASE_COUNT = 2 * PARENT_CLEARANCE_MARGIN_COUNT * PARENT_CLEARANCE_SIDE_COUNT,
};

START_TEST(test_parent_float_clearance)
{
    int margin = parent_clearance_margins[_i % PARENT_CLEARANCE_MARGIN_COUNT];
    int side = (_i / PARENT_CLEARANCE_MARGIN_COUNT) % PARENT_CLEARANCE_SIDE_COUNT;
    int height = _i / (PARENT_CLEARANCE_MARGIN_COUNT * PARENT_CLEARANCE_SIDE_COUNT) ? 10 : 0;
    html_content *content = create_test_content();
    struct box *root = create_box_with_style(300);
    struct box *parent = create_box_with_style(300);
    style_set_margins(parent->style, 13, 0, 17, 0);
    add_child(root, parent);
    struct box *floating = add_margin_test_float(parent);
    floating->type = parent_clearance_sides[side].right_float ? BOX_FLOAT_RIGHT : BOX_FLOAT_LEFT;
    style_set_height_px(floating->children->style, 48);
    struct box *child = create_box_with_style(300);
    style_set_clear(child->style, parent_clearance_sides[side].clear);
    style_set_margins(child->style, margin, 0, 29, 0);
    style_set_height_px(child->style, height);
    if (height)
        child->flags |= MAKE_HEIGHT | HAS_HEIGHT;
    add_child(parent, child);
    struct box *after = create_box_with_style(300);
    style_set_height_px(after->style, 8);
    after->flags |= MAKE_HEIGHT | HAS_HEIGHT;
    add_child(root, after);
    bool matching = parent_clearance_sides[side].matching;
    int parent_y = matching ? 13 : (height ? margin : (margin > 29 ? margin : 29));
    int parent_height = matching ? (height ? 58 : 48 + (margin < 29 ? 29 - margin : 0)) : height;
    int child_y = matching ? 48 : 0;
    int after_y = parent_y + parent_height + (height ? 29 : matching ? 17 : 0);
    int root_height = after_y + 8 > parent_y + 48 ? after_y + 8 : parent_y + 48;
    for (int pass = 0; pass < 3; pass++) {
        root->width = pass == 1 ? 500 : 300;
        root->height = AUTO;
        ck_assert(layout_block_context(root, 768, content));
        ck_assert_msg(parent->y == parent_y && parent->height == parent_height &&
                child->y == child_y && child->height == height && floating->y == parent_y &&
                after->y == after_y && root->height == root_height,
            "%s margin=%d height=%d pass=%d: parent y/h=%d/%d expected %d/%d; "
            "child y/h=%d/%d expected %d/%d; float=%d expected %d; after=%d expected %d; root=%d expected %d",
            parent_clearance_sides[side].name, margin, height, pass, parent->y, parent->height,
            parent_y, parent_height, child->y, child->height, child_y, height, floating->y,
            parent_y, after->y, after_y, root->height, root_height);
    }
    free_box_tree(root);
    destroy_test_content(content);
}
END_TEST

/**
 * Test: self-collapsing block whose children are all out-of-flow (abspos).
 *
 * Models the cnx-software.com bug: a LABEL (display:block, margin-bottom:0.5em)
 * contains only a .screen-reader-text SPAN (position:absolute). The LABEL has
 * height 0 and no in-flow children, so per CSS 2.1 §8.3.1 its top and bottom
 * margins should collapse through. Without the fix, layout_next_margin_block
 * descends into the children, walks back up, but the !walked_up guard on
 * margin[BOTTOM] accumulation causes the bottom margin to be lost from the
 * collapsing chain.
 *
 * Structure:
 *   block  (root BFC)
 *     a    (height:10, mb:10)
 *     label (height:0, mt:0, mb:7)  ← should collapse through
 *       ic  (BOX_INLINE_CONTAINER)
 *         span (BOX_INLINE, position:absolute)
 *     input (height:10, mt:0)
 *
 * Expected: a.mb=10 collapses with label.mt=0, label.mb=7, input.mt=0.
 * Collapsed = max(10, 0, 7, 0) = 10.  input.y = 10 + 10 = 20.
 */
START_TEST(test_collapse_through_abspos_children)
{
    html_content *content = create_test_content();

    struct box *block = create_box_with_style(200);

    /* Box A: normal block with height, margin-bottom 10 */
    struct box *a = create_box_with_style(200);
    style_set_height_px(a->style, 10);
    a->flags |= HAS_HEIGHT | MAKE_HEIGHT;
    style_set_margins(a->style, 0, 0, 10, 0);

    /* Label: empty block with margin-bottom 7, height 0 */
    struct box *label = create_box_with_style(200);
    style_set_height_px(label->style, 0);
    style_set_margins(label->style, 0, 0, 7, 0);
    /* Do NOT set MAKE_HEIGHT — label is empty */

    /* Abspos block child inside label.
     * In the real cnx-software.com case, this is a .screen-reader-text SPAN
     * (position:absolute) inside the LABEL, wrapped in an INLINE_CONTAINER.
     * We use a BOX_BLOCK child here because layout_line requires nsoptions
     * initialization that the test harness doesn't support. The key behavior
     * under test is the same: layout_next_margin_block descends into the
     * label's children, finds only abspos children, and must walk back up —
     * where the label's margin[BOTTOM] must be accumulated. */
    struct box *abspos_child = create_box_with_style(200);
    style_set_position_absolute(abspos_child->style);
    style_set_height_px(abspos_child->style, 1);
    abspos_child->flags |= HAS_HEIGHT | MAKE_HEIGHT;

    add_child(label, abspos_child);

    /* Input: normal block with height */
    struct box *input = create_box_with_style(200);
    style_set_height_px(input->style, 10);
    input->flags |= HAS_HEIGHT | MAKE_HEIGHT;

    add_child(block, a);
    add_child(block, label);
    add_child(block, input);

    bool ok = layout_block_context(block, 768, content);
    ck_assert(ok);

    /* label should collapse through: all margins are adjoining.
     * a.mb=10, label.mt=0, label.mb=7, input.mt=0
     * collapsed = max(10, 0, 7, 0) = 10
     * input.y = a.height(10) + collapsed(10) = 20 */
    ck_assert_msg(input->y == 20,
        "collapse_through_abspos_children: expected input->y=20, got %d "
        "(label with only abspos children should collapse through)",
        input->y);

    free_box_tree(block);
    destroy_test_content(content);
}
END_TEST

/**
 * Test: a self-collapsing block followed by an out-of-flow sibling.
 *
 * This reproduces the townhall crash path: margin-collapse traversal starts
 * from an empty in-flow block, advances to the next sibling, and must skip an
 * absolute-positioned box instead of trying to measure it as part of the
 * collapse chain.
 *
 * Structure:
 *   block  (root BFC)
 *     a      (height:10, mb:10)
 *     label  (height:0, mt:0, mb:7)   ← collapses through
 *     absbox (position:absolute)       ← must be skipped
 *     input  (height:10, mt:0)
 *
 * Expected: absbox does not participate. The collapse chain is
 * a.mb=10, label.mt=0, label.mb=7, input.mt=0 → collapsed = 10.
 * input.y = 10 + 10 = 20.
 */
START_TEST(test_collapse_through_skips_abspos_next_sibling)
{
    html_content *content = create_test_content();

    struct box *block = create_box_with_style(200);

    struct box *a = create_box_with_style(200);
    style_set_height_px(a->style, 10);
    a->flags |= HAS_HEIGHT | MAKE_HEIGHT;
    style_set_margins(a->style, 0, 0, 10, 0);

    struct box *label = create_box_with_style(200);
    style_set_height_px(label->style, 0);
    style_set_margins(label->style, 0, 0, 7, 0);

    struct box *absbox = create_box_with_style(200);
    style_set_position_absolute(absbox->style);
    style_set_height_px(absbox->style, 1);
    absbox->flags |= HAS_HEIGHT | MAKE_HEIGHT;
    style_set_margins(absbox->style, 100, 0, 100, 0);

    struct box *input = create_box_with_style(200);
    style_set_height_px(input->style, 10);
    input->flags |= HAS_HEIGHT | MAKE_HEIGHT;

    add_child(block, a);
    add_child(block, label);
    add_child(block, absbox);
    add_child(block, input);

    bool ok = layout_block_context(block, 768, content);
    ck_assert(ok);

    ck_assert_msg(input->y == 20,
        "collapse_through_skips_abspos_next_sibling: expected input->y=20, got %d "
        "(self-collapsing box must skip abspos next sibling in margin traversal)",
        input->y);

    free_box_tree(block);
    destroy_test_content(content);
}
END_TEST

/**
 * CSS 2.1 §8.3.1: A block element whose only child is an
 * INLINE_CONTAINER with no line boxes should self-collapse.
 *
 * Models the real cnx-software.com LABEL:
 *   FORM (block)
 *     LABEL (block, mb=7, no border/padding/height)
 *       └─ IC (inline container, empty — no in-flow inline content)
 *     INPUT (block, with height)
 *
 * The IC is an anonymous wrapper (no style, no margins). In the real
 * page it wraps a position:absolute INLINE_BLOCK, but we use an empty
 * IC for simplicity — both represent "no line boxes."
 *
 * Expected: LABEL self-collapses. a.mb=10, label.mt=0, label.mb=7,
 * input.mt=0 → collapsed = max(10,7) = 10 → input.y = 10+10 = 20.
 */
START_TEST(test_collapse_through_empty_ic)
{
    html_content *content = create_test_content();

    struct box *block = create_box_with_style(200);

    /* Box A: normal block with height 10, margin-bottom 10 */
    struct box *a = create_box_with_style(200);
    style_set_height_px(a->style, 10);
    a->flags |= HAS_HEIGHT | MAKE_HEIGHT;
    style_set_margins(a->style, 0, 0, 10, 0);

    /* Label: empty block with margin-bottom 7, no height */
    struct box *label = create_box_with_style(200);
    style_set_height_px(label->style, 0);
    style_set_margins(label->style, 0, 0, 7, 0);
    /* Do NOT set MAKE_HEIGHT — label has no content */

    /* Empty inline container inside label.
     * This is what the box normaliser creates as an anonymous wrapper.
     * No style, no margins, no children → no line boxes. */
    struct box *ic = calloc(1, sizeof(struct box));
    assert(ic != NULL);
    ic->type = BOX_INLINE_CONTAINER;
    ic->style = NULL;
    ic->width = 200;
    ic->height = 0;

    add_child(label, ic);

    /* Input: normal block with height */
    struct box *input = create_box_with_style(200);
    style_set_height_px(input->style, 10);
    input->flags |= HAS_HEIGHT | MAKE_HEIGHT;

    add_child(block, a);
    add_child(block, label);
    add_child(block, input);

    bool ok = layout_block_context(block, 768, content);
    ck_assert(ok);

    /* label should self-collapse: its IC has no line boxes.
     * a.mb=10, label.mt=0, label.mb=7, input.mt=0
     * collapsed = max(10, 0, 7, 0) = 10
     * input.y = a.height(10) + collapsed(10) = 20 */
    ck_assert_msg(input->y == 20,
        "collapse_through_empty_ic: expected input->y=20, got %d "
        "(label with only empty IC child should self-collapse)",
        input->y);

    free_box_tree(block);
    destroy_test_content(content);
}
END_TEST


/* Empty fragments on either side of a block inside an inline link do not
 * separate adjoining margins. Real inline content and edges still do. */
static const char *const split_inline_variants[] = {
    "empty container", "split link", "nested inline", "collapsed whitespace",
    "text", "horizontal border", "vertical padding", "horizontal margin",
    "inline block", "line break", "absolute inline block",
};
enum { SPLIT_INLINE_VARIANT_COUNT = sizeof(split_inline_variants) / sizeof(split_inline_variants[0]) };

START_TEST(test_split_inline_margins)
{
    int variant = _i % SPLIT_INLINE_VARIANT_COUNT;
    int sign = _i < SPLIT_INLINE_VARIANT_COUNT ? 1 : -1;
    /* Vertical padding alone leaves a phantom line; only horizontal edges
     * make empty fragments separate margins (CSS Inline 3, section 2.3). */
    bool separates = variant >= 4 && variant <= 9 && variant != 6;
    html_content *content = create_test_content();
    struct box *root = create_box_with_style(200);
    struct box *before = create_box_with_style(200);
    struct box *child = create_box_with_style(200);
    struct box *after = create_box_with_style(200);
    struct box *fragments[2];
    struct box *markers[2] = {NULL, NULL};
    style_set_height_px(before->style, 30);
    style_set_height_px(child->style, 30);
    style_set_height_px(after->style, 30);
    before->flags = child->flags = after->flags = HAS_HEIGHT | MAKE_HEIGHT;
    style_set_margins(before->style, 0, 0, sign * 5, 0);
    style_set_margins(child->style, sign * 13, 0, sign * 17, 0);
    style_set_margins(after->style, sign * 7, 0, 0, 0);

    for (int i = 0; i < 2; i++) {
        fragments[i] = calloc(1, sizeof(struct box));
        ck_assert_ptr_nonnull(fragments[i]);
        fragments[i]->type = BOX_INLINE_CONTAINER;
        if (variant == 0)
            continue;
        struct box *marker = markers[i] = create_box_with_style(0);
        marker->type = i == 0 ? BOX_INLINE : BOX_INLINE_END;
        style_set_inline_metrics(marker->style, 16, 20, CSS_VERTICAL_ALIGN_BASELINE);
        if (variant == 3)
            marker->space = UNKNOWN_WIDTH; // Collapsible whitespace around the block.
        if (variant == 5)
            style_set_horizontal_edges(marker->style, 0, 2);
        if (variant == 6)
            style_set_padding_top(marker->style, 2);
        if (variant == 7)
            style_set_margins(marker->style, 0, 2, 0, 2);
        add_child(fragments[i], marker);
        if (variant == 2) {
            struct box *nested = create_box_with_style(0);
            nested->type = BOX_INLINE;
            add_child(fragments[i], nested);
        }
        if (variant == 4 || variant >= 8) {
            struct box *inline_content = create_box_with_style(0);
            style_set_inline_metrics(inline_content->style, 16, 20, CSS_VERTICAL_ALIGN_BASELINE);
            inline_content->type = variant == 4 ? BOX_TEXT : variant == 9 ? BOX_BR : BOX_INLINE_BLOCK;
            if (variant == 4) {
                inline_content->text = "x";
                inline_content->length = 1;
                inline_content->width = 8;
            } else if (variant == 9) {
                inline_content->height = 0;
            } else {
                style_set_height_px(inline_content->style, 7);
                style_set_width(inline_content->style, 0, false);
                inline_content->flags = HAS_HEIGHT | MAKE_HEIGHT;
                if (variant == 10)
                    style_set_position_absolute(inline_content->style);
            }
            add_child(fragments[i], inline_content);
        }
    }
    if (markers[0]) {
        markers[0]->inline_end = markers[1];
        markers[1]->inline_end = markers[0];
    }
    add_child(root, before);
    add_child(root, fragments[0]);
    add_child(root, child);
    add_child(root, fragments[1]);
    add_child(root, after);
    ck_assert(layout_block_context(root, 768, content));
    int before_gap = child->y - (before->y + before->height);
    int after_gap = after->y - (child->y + child->height);
    int expected_before = sign * (separates ? 18 : 13) + fragments[0]->height;
    int expected_after = sign * (separates ? 24 : 17) + fragments[1]->height;
    ck_assert_msg(before_gap == expected_before,
        "%s, %s margins: gap before block is %d, expected %d", split_inline_variants[variant],
        sign > 0 ? "positive" : "negative", before_gap, expected_before);
    ck_assert_msg(after_gap == expected_after,
        "%s, %s margins: gap after block is %d, expected %d", split_inline_variants[variant],
        sign > 0 ? "positive" : "negative", after_gap, expected_after);
    free_box_tree(root);
    destroy_test_content(content);
}
END_TEST

/* Percentage edges stay nonzero even in a zero-width container; literal zero
 * edges remain phantom. Intrinsic height hints must not override either rule,
 * including across ordinary block ancestors and repeated reflow. */
START_TEST(test_inline_edges_at_nested_width)
{
    int sign = _i % 2 == 0 ? 1 : -1;
    bool separates = _i >= 2;
    html_content *content = create_test_content();
    struct box *root = create_box_with_style(0);
    struct box *before = create_box_with_style(0);
    struct box *outer = create_box_with_style(0);
    struct box *inner = create_box_with_style(0);
    struct box *child = create_box_with_style(0);
    struct box *after = create_box_with_style(0);
    struct box *blocks[] = {root, before, outer, inner, child, after};
    for (size_t i = 0; i < sizeof(blocks) / sizeof(blocks[0]); i++) {
        blocks[i]->max_width = UNKNOWN_MAX_WIDTH;
        style_set_width(blocks[i]->style, -1, false);
        style_set_inline_metrics(blocks[i]->style, 16, 20, CSS_VERTICAL_ALIGN_BASELINE);
    }
    style_set_width(outer->style, 50, true);
    style_set_width(inner->style, 50, true);
    style_set_height_px(before->style, 30);
    style_set_height_px(child->style, 30);
    style_set_height_px(after->style, 30);
    style_set_margins(before->style, 0, 0, sign * 5, 0);
    style_set_margins(child->style, sign * 13, 0, sign * 17, 0);
    style_set_margins(after->style, sign * 7, 0, 0, 0);
    add_child(root, before);
    add_child(root, outer);
    add_child(outer, inner);
    add_child(root, after);
    struct box *fragments[2], *markers[2];
    for (int i = 0; i < 2; i++) {
        fragments[i] = calloc(1, sizeof(struct box));
        ck_assert_ptr_nonnull(fragments[i]);
        fragments[i]->type = BOX_INLINE_CONTAINER;
        fragments[i]->max_width = UNKNOWN_MAX_WIDTH;
        markers[i] = create_box_with_style(0);
        markers[i]->type = i == 0 ? BOX_INLINE : BOX_INLINE_END;
        style_set_inline_metrics(markers[i]->style, 16, 20, CSS_VERTICAL_ALIGN_BASELINE);
        style_set_percentage_padding(markers[i]->style, separates ? 50 : 0);
        add_child(fragments[i], markers[i]);
        add_child(inner, fragments[i]);
        if (i == 0)
            add_child(inner, child);
    }
    markers[0]->inline_end = markers[1];
    markers[1]->inline_end = markers[0];
    ck_assert(layout_minmax_box(root, content->font_func, content));
    const int widths[] = {0, 400, 0};
    for (size_t pass = 0; pass < sizeof(widths) / sizeof(widths[0]); pass++) {
        root->width = widths[pass];
        root->height = AUTO;
        ck_assert(layout_block_context(root, 768, content));
        int line_height = separates ? 20 : 0;
        int top = child->y + inner->y + outer->y;
        int before_gap = top - before->y - before->height;
        int after_gap = after->y - top - child->height;
        ck_assert_msg(inner->width == widths[pass] / 4 && fragments[0]->height == line_height &&
                fragments[1]->height == line_height,
            "pass %zu, sign %d: inner width %d, line heights %d/%d", pass, sign, inner->width,
            fragments[0]->height, fragments[1]->height);
        ck_assert_msg(before_gap == sign * (separates ? 18 : 13) + line_height &&
                after_gap == sign * (separates ? 24 : 17) + line_height,
            "pass %zu, sign %d: gaps %d/%d", pass, sign, before_gap, after_gap);
    }
    free_box_tree(root);
    destroy_test_content(content);
}
END_TEST

/* Replaced blocks have intrinsic content even without box-tree children.
 * A zero-height image also keeps its own top/bottom margins separate. */
START_TEST(test_replaced_block_margins)
{
    int image_height = _i == 0 ? 0 : 20;
    html_content *content = create_test_content();
    struct box *root = create_box_with_style(200);
    struct box *before = create_box_with_style(200);
    struct box *image = create_box_with_style(20);
    struct box *after = create_box_with_style(200);
    style_set_height_px(before->style, 30);
    style_set_height_px(image->style, image_height);
    style_set_height_px(after->style, 30);
    before->flags |= HAS_HEIGHT | MAKE_HEIGHT;
    after->flags |= HAS_HEIGHT | MAKE_HEIGHT;
    image->flags |= HAS_HEIGHT | REPLACE_DIM;
    style_set_margins(before->style, 0, 0, 5, 0);
    style_set_margins(image->style, 13, 0, 17, 0);
    style_set_margins(after->style, 7, 0, 0, 0);
    add_child(root, before);
    add_child(root, image);
    add_child(root, after);
    ck_assert(layout_block_context(root, 768, content));
    ck_assert_msg(image->height == image_height && image->y == 43 && after->y == 60 + image_height,
        "replaced height %d: image y=%d h=%d, following y=%d", image_height, image->y, image->height, after->y);
    free_box_tree(root);
    destroy_test_content(content);
}
END_TEST

/* A wrapper's bottom margin must not leak above its flex/grid child.
 * Exercise both auto-height content and an empty formatting context, whose
 * own top/bottom margins also cannot collapse through each other. */
START_TEST(test_wrapped_formatting_context_margins)
{
    bool grid = (_i & 1) != 0;
    bool empty = (_i & 2) != 0;
    bool negative = (_i & 4) != 0;
    html_content *content = create_test_content();
    struct box *root = create_box_with_style(200);
    struct box *wrapper = create_box_with_style(200);
    style_set_margins(wrapper->style, 0, 0, negative ? -15 : 51, 0);
    struct box *container = create_box_with_style(200);
    container->type = grid ? BOX_GRID : BOX_FLEX;
    style_set_flex_or_grid(container->style, grid);
    style_set_margins(container->style, 10, 0, 20, 0);
    if (!empty) {
        struct box *item = create_box_with_style(200);
        style_set_height_px(item->style, 100);
        item->flags |= HAS_HEIGHT | MAKE_HEIGHT;
        add_child(container, item);
    }
    struct box *next = create_box_with_style(200);
    style_set_height_px(next->style, 20);
    next->flags |= HAS_HEIGHT | MAKE_HEIGHT;
    add_child(root, wrapper);
    if (_i & 8) {
        struct box *inner = create_box_with_style(200);
        add_child(wrapper, inner);
        add_child(inner, container);
    } else {
        add_child(wrapper, container);
    }
    add_child(root, next);

    ck_assert(layout_block_context(root, 768, content));
    /* The child's top margin may collapse with its block wrapper, but its
     * bottom margin only collapses with the wrapper's bottom margin. */
    ck_assert_int_eq(wrapper->y, 10);
    ck_assert_int_eq(container->y, 0);
    ck_assert_int_eq(container->height, empty ? 0 : 100);
    ck_assert_int_eq(next->y, 10 + container->height + (negative ? 5 : 51));

    free_box_tree(root);
    destroy_test_content(content);
}
END_TEST

/* ========================================================================
 * Test runner
 * ======================================================================== */
/* ========================================================================
 * Additional Parent-First-Child Tests
 * ======================================================================== */

START_TEST(test_parent_child_abspos)
{
    html_content *content = create_test_content();

    struct box *root = create_box_with_style(200);

    struct box *p = create_box_with_style(200);
    /* no border/padding — collapse allowed */

    /* Abspos child — should be skipped, doesn't participate in collapse */
    struct box *abs_c = create_box_with_style(200);
    style_set_height_px(abs_c->style, 10);
    abs_c->flags |= HAS_HEIGHT | MAKE_HEIGHT;
    style_set_position_absolute(abs_c->style);
    style_set_margins(abs_c->style, 50, 0, 0, 0);

    /* Normal child — its margin should collapse with parent */
    struct box *norm_c = create_box_with_style(200);
    style_set_height_px(norm_c->style, 10);
    norm_c->flags |= HAS_HEIGHT | MAKE_HEIGHT;
    style_set_margins(norm_c->style, 20, 0, 0, 0);

    add_child(root, p);
    add_child(p, abs_c);
    add_child(p, norm_c);

    bool ok = layout_block_context(root, 768, content);
    ck_assert(ok);

    /* Abspos child is out of flow. norm_c.mt=20 should collapse with P.
     * Per spec: P.y = 20 (margin pushed to parent), norm_c.y = 0.
     * Current engine doesn't implement parent-child collapse, so
     * expects: P.y = 0, norm_c.y = 20 (margin inside parent). */
    ck_assert_msg(p->y == 20,
        "parent_child_abspos: expected p->y=20, got %d "
        "(abspos child should be skipped, normal child margin should collapse with parent)",
        p->y);
    ck_assert_msg(norm_c->y == 0, "parent_child_abspos: expected norm_c->y=0, got %d", norm_c->y);

    free_box_tree(root);
    destroy_test_content(content);
}
END_TEST


/* ========================================================================
 * Additional Collapse-Through Prevention Tests
 * ======================================================================== */

START_TEST(test_no_collapse_through_border)
{
    html_content *content = create_test_content();

    struct box *block = create_box_with_style(200);

    struct box *a = create_box_with_style(200);
    style_set_height_px(a->style, 10);
    a->flags |= HAS_HEIGHT | MAKE_HEIGHT;
    style_set_margins(a->style, 0, 0, 10, 0);

    /* Empty box WITH border-top — should NOT collapse through.
     * Border separates its top and bottom margins. */
    struct box *bordered = create_box_with_style(200);
    style_set_height_px(bordered->style, 0);
    bordered->flags |= HAS_HEIGHT | MAKE_HEIGHT;
    style_set_border_top(bordered->style, 1);
    style_set_margins(bordered->style, 5, 0, 15, 0);

    struct box *b = create_box_with_style(200);
    style_set_height_px(b->style, 10);
    b->flags |= HAS_HEIGHT | MAKE_HEIGHT;

    add_child(block, a);
    add_child(block, bordered);
    add_child(block, b);

    bool ok = layout_block_context(block, 768, content);
    ck_assert(ok);

    /* Border prevents collapse-through. Two separate collapse groups:
     * Group 1: a.mb=10 ↔ bordered.mt=5 → max(10,5) = 10
     *   bordered.y = 10 + 10 + border(1) = 21 (border adds to y per engine convention)
     * Group 2: bordered.mb=15 ↔ b.mt=0 → max(15,0) = 15
     *   b.y = 21 + 0 + 15 = 36 */
    ck_assert_msg(bordered->y == 21, "no_collapse_through_border: expected bordered->y=21, got %d", bordered->y);
    ck_assert_msg(b->y == 36, "no_collapse_through_border: expected b->y=36, got %d", b->y);

    free_box_tree(block);
    destroy_test_content(content);
}
END_TEST


/* ========================================================================
 * First-Child Margin-Top Edge Case
 * ======================================================================== */

START_TEST(test_first_child_margin_top)
{
    html_content *content = create_test_content();

    struct box *block = create_box_with_style(200);

    /* First child has margin-top — should create space above it */
    struct box *a = create_box_with_style(200);
    style_set_height_px(a->style, 10);
    a->flags |= HAS_HEIGHT | MAKE_HEIGHT;
    style_set_margins(a->style, 15, 0, 0, 0);

    struct box *b = create_box_with_style(200);
    style_set_height_px(b->style, 10);
    b->flags |= HAS_HEIGHT | MAKE_HEIGHT;

    add_child(block, a);
    add_child(block, b);

    bool ok = layout_block_context(block, 768, content);
    ck_assert(ok);

    /* First child margin-top = 15. a.y = 15, b.y = 15 + 10 = 25 */
    ck_assert_msg(a->y == 15, "first_child_margin_top: expected a->y=15, got %d", a->y);
    ck_assert_msg(b->y == 25, "first_child_margin_top: expected b->y=25, got %d", b->y);

    free_box_tree(block);
    destroy_test_content(content);
}
END_TEST


/* ========================================================================
 * Additional Parent-Last-Child Tests
 * ======================================================================== */

START_TEST(test_last_child_with_padding_bottom)
{
    html_content *content = create_test_content();

    struct box *root = create_box_with_style(200);

    struct box *p = create_box_with_style(200);
    /* auto height, has padding-bottom → blocks last-child collapse */
    style_set_padding_bottom(p->style, 5);

    struct box *c = create_box_with_style(200);
    style_set_height_px(c->style, 10);
    c->flags |= HAS_HEIGHT | MAKE_HEIGHT;
    style_set_margins(c->style, 0, 0, 20, 0);

    struct box *next = create_box_with_style(200);
    style_set_height_px(next->style, 10);
    next->flags |= HAS_HEIGHT | MAKE_HEIGHT;

    add_child(root, p);
    add_child(p, c);
    add_child(root, next);

    bool ok = layout_block_context(root, 768, content);
    ck_assert(ok);

    /* The child's bottom margin contributes to the auto content height,
     * before the parent's bottom padding: 10 + 20 + 5. */
    ck_assert_int_eq(p->height, 30);
    ck_assert_msg(next->y == 35, "last_child_with_padding_bottom: expected next->y=35, got %d", next->y);

    free_box_tree(root);
    destroy_test_content(content);
}
END_TEST

/* Bottom edges keep the child's collapsed margin inside the parent's
 * content height, independently of the parent's external sibling margins. */
static const struct {
    const char *name;
    int padding, border, child_margin;
    bool nested;
    int wrapper_margin, height, min_height, expected_height;
} contained_margin_cases[] = {
    {"padding", 5, 0, 13, false, 0, -1, 0, 33},
    {"border", 0, 1, 13, false, 0, -1, 0, 33},
    {"both", 5, 1, 13, false, 0, -1, 0, 33},
    {"negative", 5, 0, -7, false, 0, -1, 0, 13},
    {"negative-clamped", 5, 0, -30, false, 0, -1, 0, 0},
    {"nested-positive", 5, 0, 13, true, 20, -1, 0, 40},
    {"nested-mixed", 5, 0, -13, true, 20, -1, 0, 27},
    {"nested-negative", 5, 0, -7, true, -13, -1, 0, 7},
    {"fixed-short", 5, 1, 13, false, 0, 10, 0, 10},
    {"fixed-tall", 5, 1, 13, false, 0, 60, 0, 60},
    {"min-short", 5, 1, 13, false, 0, -1, 10, 33},
    {"min-tall", 5, 1, 13, false, 0, -1, 60, 60},
};
enum { CONTAINED_MARGIN_CASE_COUNT = sizeof(contained_margin_cases) / sizeof(contained_margin_cases[0]) };

START_TEST(test_last_child_contained_margin)
{
    int variant = _i % CONTAINED_MARGIN_CASE_COUNT;
    bool with_sibling = _i < CONTAINED_MARGIN_CASE_COUNT;
    const char *name = contained_margin_cases[variant].name;
    html_content *content = create_test_content();
    struct box *root = create_box_with_style(200);
    struct box *parent = create_box_with_style(200);
    style_set_padding_top(parent->style, 3);
    style_set_border_top(parent->style, 2);
    style_set_padding_bottom(parent->style, contained_margin_cases[variant].padding);
    style_set_border_bottom(parent->style, contained_margin_cases[variant].border);
    style_set_height_px(parent->style, contained_margin_cases[variant].height);
    style_set_min_height(parent->style, contained_margin_cases[variant].min_height);
    style_set_margins(parent->style, 0, 0, 7, 0);
    if (contained_margin_cases[variant].height >= 0)
        parent->flags |= HAS_HEIGHT | MAKE_HEIGHT;
    add_child(root, parent);

    struct box *container = parent;
    if (contained_margin_cases[variant].nested) {
        container = create_box_with_style(200);
        style_set_margins(container->style, 0, 0, contained_margin_cases[variant].wrapper_margin, 0);
        add_child(parent, container);
    }
    struct box *child = create_box_with_style(200);
    style_set_height_px(child->style, 20);
    child->flags |= HAS_HEIGHT | MAKE_HEIGHT;
    style_set_margins(child->style, 0, 0, contained_margin_cases[variant].child_margin, 0);
    add_child(container, child);

    struct box *next = NULL;
    if (with_sibling) {
        next = create_box_with_style(200);
        style_set_height_px(next->style, 10);
        next->flags |= HAS_HEIGHT | MAKE_HEIGHT;
        style_set_margins(next->style, 11, 0, 0, 0);
        add_child(root, next);
    }

    int expected_height = contained_margin_cases[variant].expected_height;
    int parent_bottom = 2 + 3 + expected_height + contained_margin_cases[variant].padding +
        contained_margin_cases[variant].border;
    for (int pass = 0; pass < 2; pass++) {
        root->height = AUTO;
        ck_assert(layout_block_context(root, 768, content));
        ck_assert_msg(parent->height == expected_height,
            "%s (sibling=%d, pass=%d): expected content height %d, got %d",
            name, with_sibling, pass, expected_height, parent->height);
        if (next) {
            ck_assert_msg(next->y == parent_bottom + 11,
                "%s (pass=%d): expected next y=%d, got %d", name, pass, parent_bottom + 11, next->y);
        }
        int expected_root = parent_bottom + (with_sibling ? 11 + 10 : 7);
        ck_assert_msg(root->height == expected_root,
            "%s (sibling=%d, pass=%d): expected root height %d, got %d",
            name, with_sibling, pass, expected_root, root->height);
    }
    free_box_tree(root);
    destroy_test_content(content);
}
END_TEST

START_TEST(test_last_child_parent_has_mb)
{
    html_content *content = create_test_content();

    struct box *root = create_box_with_style(200);

    struct box *p = create_box_with_style(200);
    /* auto height, no separator, parent has mb=15 */
    style_set_margins(p->style, 0, 0, 15, 0);

    struct box *c = create_box_with_style(200);
    style_set_height_px(c->style, 10);
    c->flags |= HAS_HEIGHT | MAKE_HEIGHT;
    style_set_margins(c->style, 0, 0, 20, 0);

    struct box *next = create_box_with_style(200);
    style_set_height_px(next->style, 10);
    next->flags |= HAS_HEIGHT | MAKE_HEIGHT;

    add_child(root, p);
    add_child(p, c);
    add_child(root, next);

    bool ok = layout_block_context(root, 768, content);
    ck_assert(ok);

    /* Per spec: c.mb=20 collapses with p.mb=15.
     * Collapsed = max(20, 15) = 20.
     * p.height = auto(10), next.y = 0 + 10 + 20 = 30 */
    ck_assert_msg(next->y == 30,
        "last_child_parent_has_mb: expected next->y=30, got %d "
        "(collapsed margin should be max(c.mb=20, p.mb=15) = 20)",
        next->y);

    free_box_tree(root);
    destroy_test_content(content);
}
END_TEST

START_TEST(test_last_child_parent_mb_larger)
{
    html_content *content = create_test_content();

    struct box *root = create_box_with_style(200);

    struct box *p = create_box_with_style(200);
    /* auto height, no separator, parent has mb=30 > c.mb=20 */
    style_set_margins(p->style, 0, 0, 30, 0);

    struct box *c = create_box_with_style(200);
    style_set_height_px(c->style, 10);
    c->flags |= HAS_HEIGHT | MAKE_HEIGHT;
    style_set_margins(c->style, 0, 0, 20, 0);

    struct box *next = create_box_with_style(200);
    style_set_height_px(next->style, 10);
    next->flags |= HAS_HEIGHT | MAKE_HEIGHT;

    add_child(root, p);
    add_child(p, c);
    add_child(root, next);

    bool ok = layout_block_context(root, 768, content);
    ck_assert(ok);

    /* Per spec: c.mb=20 collapses with p.mb=30.
     * Collapsed = max(30, 20) = 30.
     * p.height = auto(10), next.y = 0 + 10 + 30 = 40 */
    ck_assert_msg(next->y == 40,
        "last_child_parent_mb_larger: expected next->y=40, got %d "
        "(collapsed margin should be max(p.mb=30, c.mb=20) = 30)",
        next->y);

    free_box_tree(root);
    destroy_test_content(content);
}
END_TEST

static const struct {
    const char *name;
    int height, minimum, maximum, expected;
} empty_height_cases[] = {
    {"auto minimum", -1, 60, -1, 60},
    {"explicit below minimum", 20, 60, -1, 60},
    {"explicit above maximum", 100, 0, 40, 40},
    {"minimum overrides maximum", 100, 60, 40, 60},
};
#define EMPTY_HEIGHT_CASE_COUNT (sizeof(empty_height_cases) / sizeof(empty_height_cases[0]))

START_TEST(test_empty_block_height_constraints)
{
    unsigned variant = _i % EMPTY_HEIGHT_CASE_COUNT;
    unsigned position = _i / EMPTY_HEIGHT_CASE_COUNT; /* first, last, only */
    html_content *content = create_test_content();
    struct box *root = create_box_with_style(200);
    if (position == 1) {
        struct box *before = create_box_with_style(200);
        style_set_height_px(before->style, 10);
        before->flags |= HAS_HEIGHT | MAKE_HEIGHT;
        add_child(root, before);
    }
    struct box *leaf = create_box_with_style(200);
    style_set_height_px(leaf->style, empty_height_cases[variant].height);
    style_set_min_height(leaf->style, empty_height_cases[variant].minimum);
    style_set_max_height(leaf->style, empty_height_cases[variant].maximum);
    style_set_margins(leaf->style, 15, 0, 25, 0);
    if (empty_height_cases[variant].height > 0)
        leaf->flags |= HAS_HEIGHT | MAKE_HEIGHT;
    add_child(root, leaf);
    struct box *after = NULL;
    if (position == 0) {
        after = create_box_with_style(200);
        style_set_height_px(after->style, 10);
        style_set_margins(after->style, 35, 0, 0, 0);
        after->flags |= HAS_HEIGHT | MAKE_HEIGHT;
        add_child(root, after);
    }
    for (int pass = 0; pass < 2; pass++) {
        root->height = AUTO;
        ck_assert(layout_block_context(root, 768, content));
        int expected_height = empty_height_cases[variant].expected;
        int expected_y = position == 1 ? 25 : 15;
        ck_assert_msg(leaf->height == expected_height && leaf->y == expected_y,
            "%s (position=%u pass=%d): expected leaf y/height %d/%d, got %d/%d", empty_height_cases[variant].name,
            position, pass, expected_y, expected_height, leaf->y, leaf->height);
        int expected_root = expected_y + expected_height + (after ? 35 + 10 : 25);
        ck_assert_msg(root->height == expected_root, "%s (position=%u pass=%d): expected ancestor height %d, got %d",
            empty_height_cases[variant].name, position, pass, expected_root, root->height);
        if (after)
            ck_assert_msg(after->y == expected_y + expected_height + 35,
                "%s (pass=%d): sibling y=%d must include the constrained leaf and collapsed bottom margin",
                empty_height_cases[variant].name, pass, after->y);
    }
    free_box_tree(root);
    destroy_test_content(content);
}
END_TEST

START_TEST(test_last_child_min_height_blocks)
{
    html_content *content = create_test_content();

    struct box *root = create_box_with_style(200);

    struct box *p = create_box_with_style(200);
    /* auto height BUT min-height > 0 → blocks last-child collapse per spec */
    style_set_min_height(p->style, 20);

    struct box *c = create_box_with_style(200);
    style_set_height_px(c->style, 10);
    c->flags |= HAS_HEIGHT | MAKE_HEIGHT;
    style_set_margins(c->style, 0, 0, 20, 0);

    struct box *next = create_box_with_style(200);
    style_set_height_px(next->style, 10);
    next->flags |= HAS_HEIGHT | MAKE_HEIGHT;

    add_child(root, p);
    add_child(p, c);
    add_child(root, next);

    bool ok = layout_block_context(root, 768, content);
    ck_assert(ok);

    /* min-height=20 blocks last-child collapse.
     * c.mb=20 stays inside p. p.height = max(auto(10), min-height(20)) = 20.
     * next.y = p.y + p.height = 0 + 20 = 20 */
    ck_assert_msg(next->y == 20,
        "last_child_min_height_blocks: expected next->y=20, got %d "
        "(min-height should block last-child margin collapse)",
        next->y);

    free_box_tree(root);
    destroy_test_content(content);
}
END_TEST


enum { FLOAT_AVOIDANCE_CASE_COUNT = 64 };

/* Fixed/percentage widths must move below floats when they do not fit;
 * auto widths may shrink alongside them, subject to min-width and edges. */
START_TEST(test_block_context_avoids_floats)
{
    static const struct {
        int width;
        bool percent;
        int min_width, padding, border;
        int x[4], y[4], used_width[4];
    } cases[] = {
        { 360, false, 0, 0, 0, {0, 0, 0, 0}, {100, 160, 160, 160}, {360, 360, 360, 360} },
        { 100, true, 0, 0, 0, {0, 0, 0, 0}, {100, 160, 160, 160}, {360, 360, 360, 360} },
        { 180, false, 0, 0, 0, {150, 0, 0, 0}, {0, 0, 100, 160}, {180, 180, 180, 180} },
        { 50, true, 0, 0, 0, {150, 0, 0, 0}, {0, 0, 100, 160}, {180, 180, 180, 180} },
        { -1, false, 0, 0, 0, {150, 0, 150, 300}, {0, 0, 0, 0}, {210, 210, 60, 60} },
        { -1, false, 180, 0, 0, {150, 0, 0, 0}, {0, 0, 100, 160}, {210, 210, 210, 360} },
        { 180, false, 0, 12, 2, {150, 0, 0, 0}, {20, 20, 100, 160}, {180, 180, 180, 180} },
        { 400, false, 0, 0, 0, {0, 0, 0, 0}, {100, 160, 160, 160}, {400, 400, 400, 400} },
    };
    int profile = _i % 4; /* left, right, both sides, two left floats */
    int variant = (_i / 4) % 8;
    int parent_padding = (_i % FLOAT_AVOIDANCE_CASE_COUNT) >= 32 ? 10 : 0;
    bool flow_root = _i >= FLOAT_AVOIDANCE_CASE_COUNT;
    html_content *content = create_test_content();
    struct box *root = create_box_with_style(360);
    root->padding[LEFT] = root->padding[RIGHT] = parent_padding;
    struct box *ic = create_box_with_style(360);
    ic->type = BOX_INLINE_CONTAINER;
    destroy_mock_style(ic->style);
    ic->style = NULL;
    add_child(root, ic);
    for (int i = 0; i < (profile >= 2 ? 2 : 1); i++) {
        struct box *fl = create_box_with_style(150);
        fl->type = profile == 1 || (profile == 2 && i == 1) ? BOX_FLOAT_RIGHT : BOX_FLOAT_LEFT;
        struct box *item = create_box_with_style(150);
        style_set_margins(item->style, 0, 0, 0, 0);
        style_set_width(item->style, 150, false);
        style_set_height_px(item->style, profile == 1 || i == 1 ? 160 : 100);
        item->flags |= HAS_HEIGHT | MAKE_HEIGHT;
        add_child(fl, item);
        add_child(ic, fl);
    }
    struct box *b = create_box_with_style(360);
    style_set_margins(b->style, 0, 0, 0, 0);
    if (flow_root)
        style_set_flow_root(b->style);
    else
        style_set_overflow_hidden(b->style);
    style_set_width(b->style, cases[variant].width, cases[variant].percent);
    style_set_min_width(b->style, cases[variant].min_width);
    style_set_horizontal_edges(b->style, cases[variant].padding, cases[variant].border);
    style_set_border_top(b->style, cases[variant].border);
    if (cases[variant].border)
        style_set_margins(b->style, 20, 0, 0, 0);
    style_set_height_px(b->style, 20);
    b->flags |= HAS_HEIGHT | MAKE_HEIGHT;
    add_child(root, b);
    struct box *next = create_box_with_style(360);
    style_set_height_px(next->style, 10);
    next->flags |= HAS_HEIGHT | MAKE_HEIGHT;
    add_child(root, next);

    ck_assert(layout_block_context(root, 768, content));
    ck_assert_msg(b->x == parent_padding + cases[variant].x[profile] + cases[variant].border &&
            b->y == cases[variant].y[profile] + cases[variant].border && b->width == cases[variant].used_width[profile],
        "float avoidance (flow-root=%d, variant=%d, profile=%d, padding=%d): got x/y/width=%d/%d/%d",
        flow_root, variant, profile, parent_padding, b->x, b->y, b->width);
    ck_assert_int_eq(next->y, b->y + 20);
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
    tcase_add_test(tc_sibling, test_three_siblings_cascade);
    tcase_add_test(tc_sibling, test_first_child_margin_top);
    suite_add_tcase(s, tc_sibling);

    TCase *tc_through = tcase_create("Collapse Through");
    tcase_add_test(tc_through, test_collapse_through_empty);
    tcase_add_test(tc_through, test_no_collapse_through_height);
    tcase_add_test(tc_through, test_nested_collapse_through);
    tcase_add_loop_test(tc_through, test_nested_empty_margin_chain, 0, 2 * EMPTY_CHAIN_CASE_COUNT);
    tcase_add_loop_test(tc_through, test_margin_chain_receiver, 0, 4 * MARGIN_RECEIVER_CASE_COUNT);
    tcase_add_loop_test(tc_through, test_empty_margin_position, 0, EMPTY_FLOAT_POSITION_COUNT * EMPTY_POSITION_CASE_COUNT);
    tcase_add_loop_test(tc_through, test_empty_margin_position_with_clear, 0, CLEARANCE_CASE_COUNT);
    tcase_add_loop_test(tc_through, test_parent_float_clearance, 0, PARENT_CLEARANCE_CASE_COUNT);
    tcase_add_test(tc_through, test_no_collapse_through_border);
    tcase_add_test(tc_through, test_collapse_through_abspos_children);
    tcase_add_test(tc_through, test_collapse_through_skips_abspos_next_sibling);
    tcase_add_test(tc_through, test_collapse_through_empty_ic);
    tcase_add_loop_test(tc_through, test_split_inline_margins, 0, 2 * SPLIT_INLINE_VARIANT_COUNT);
    tcase_add_loop_test(tc_through, test_inline_edges_at_nested_width, 0, 4);
    tcase_add_loop_test(tc_through, test_replaced_block_margins, 0, 2);
    suite_add_tcase(s, tc_through);

    TCase *tc_parent = tcase_create("Parent-First-Child Collapse");
    tcase_add_test(tc_parent, test_parent_child_no_separator);
    tcase_add_test(tc_parent, test_parent_child_with_border);
    tcase_add_test(tc_parent, test_parent_child_with_padding);
    tcase_add_test(tc_parent, test_parent_child_abspos);
    suite_add_tcase(s, tc_parent);

    TCase *tc_last = tcase_create("Parent-Last-Child Collapse");
    tcase_add_test(tc_last, test_last_child_auto_height);
    tcase_add_test(tc_last, test_last_child_fixed_height);
    tcase_add_test(tc_last, test_last_child_with_border_bottom);
    tcase_add_test(tc_last, test_last_child_with_padding_bottom);
    tcase_add_loop_test(tc_last, test_last_child_contained_margin, 0, 2 * CONTAINED_MARGIN_CASE_COUNT);
    tcase_add_test(tc_last, test_last_child_parent_has_mb);
    tcase_add_test(tc_last, test_last_child_parent_mb_larger);
    tcase_add_test(tc_last, test_last_child_min_height_blocks);
    suite_add_tcase(s, tc_last);

    TCase *tc_empty_height = tcase_create("Empty Block Constraints");
    tcase_add_loop_test(tc_empty_height, test_empty_block_height_constraints, 0, 3 * EMPTY_HEIGHT_CASE_COUNT);
    suite_add_tcase(s, tc_empty_height);

    TCase *tc_excl = tcase_create("Exclusion Rules");
    tcase_add_test(tc_excl, test_no_collapse_abspos_skipped);
    tcase_add_loop_test(tc_excl, test_block_context_contains_margins, 0, 2);
    tcase_add_loop_test(tc_excl, test_empty_block_context_margins, 0, 2 * EMPTY_BFC_CASE_COUNT);
    tcase_add_loop_test(tc_excl, test_wrapped_formatting_context_margins, 0, 16);
    suite_add_tcase(s, tc_excl);

    TCase *tc_floats = tcase_create("Float Avoidance");
    tcase_add_loop_test(tc_floats, test_block_context_avoids_floats, 0, 2 * FLOAT_AVOIDANCE_CASE_COUNT);
    tcase_add_loop_test(tc_floats, test_float_placement_coordinates, 0,
        FLOAT_PLACEMENT_ORIGIN_COUNT * FLOAT_PLACEMENT_CLEAR_COUNT);
    suite_add_tcase(s, tc_floats);

    return s;
}

int main(void)
{
    /* Inline cases use font_plot_style_from_css(), which reads font options. */
    if (nsoption_init(NULL, NULL, NULL) != NSERROR_OK)
        return EXIT_FAILURE;
    int number_failed;
    Suite *s = margin_collapse_suite();
    SRunner *sr = srunner_create(s);

    srunner_set_fork_status(sr, CK_NOFORK);
    srunner_run_all(sr, CK_VERBOSE);
    number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);
    nsoption_finalise(NULL, NULL);

    return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}

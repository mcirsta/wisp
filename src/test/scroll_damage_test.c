/* Native pixel comparisons live in frontends/qt/test_sticky_scroll_reuse.py. */
#include <check.h>
#include <limits.h>
#include <stdlib.h>
#include <wisp/content/handlers/html/private.h>
#include "content/handlers/html/box_sticky.h"
#include "content/handlers/html/scroll_damage.h"

css_computed_style *sticky_test_style(uint8_t position, uint8_t overflow_x, uint8_t overflow_y);
void sticky_test_set_clip(css_computed_style *style, int top, int right, int bottom, int left);

struct damage_log {
    struct rect rects[16];
    unsigned count, capacity;
};

static bool collect(const struct rect *r, void *pw)
{
    struct damage_log *log = pw;
    if (log->count == log->capacity)
        return false;
    log->rects[log->count++] = *r;
    return true;
}

static struct html_content html;
static struct box root, header;
static struct box_sticky sticky;

static void setup(void)
{
    html.sticky_layout_valid = html.scroll_reuse_safe = html.redraw_region_safe = true;
    html.restyling = false;
    html.unit_len_ctx.device_dpi = INTTOFIX(96);
    root = (struct box){.type = BOX_BLOCK, .children = &header, .flags = HAS_STICKY_DESCENDANT};
    html.layout = &root;
    html.viewport.box.flags = IS_VIEWPORT;
    sticky = (struct box_sticky){.owner = &html,
        .valid = true,
        .containing_block = &root,
        .scrolling_ancestor = &html.viewport.box,
        .x = {.start_auto = true, .end_auto = true},
        .y = {.border_start = 100,
            .border_size = 40,
            .containing_size = 1000,
            .scrollport_size = 120,
            .inset_start = 10,
            .end_auto = true}};
    header = (struct box){.type = BOX_BLOCK,
        .parent = &root,
        .x = 20,
        .y = 100,
        .sticky = &sticky,
        .width = 100,
        .height = 40,
        .descendant_x0 = -5,
        .descendant_x1 = 105,
        .descendant_y1 = 40};
}

static void assert_rect(struct damage_log *log, unsigned i, int x0, int y0, int x1, int y1)
{
    ck_assert_msg(i < log->count, "missing damage rectangle %u (count %u)", i, log->count);
    struct rect r = log->rects[i];
    ck_assert_msg(r.x0 == x0 && r.y0 == y0 && r.x1 == x1 && r.y1 == y1,
        "damage %u: (%d,%d)-(%d,%d), expected (%d,%d)-(%d,%d)", i, r.x0, r.y0, r.x1, r.y1, x0, y0, x1, y1);
}

START_TEST(test_copied_and_current_coverage)
{
    struct damage_log log = {.capacity = 16};
    struct rect old = {0, 90, 200, 210}, now = {0, 130, 200, 250};
    ck_assert(html_compute_scroll_damage(&html, &old, &now, collect, &log));
    ck_assert_uint_eq(log.count, 2);
    assert_rect(&log, 0, 15, 0, 126, 11);
    assert_rect(&log, 1, 15, 10, 126, 51);
    ck_assert_int_eq(header.y, 100);
    ck_assert_int_eq(sticky.y.border_start, 100);

    /* Reverse across the threshold. Only the actually copied old pixels can
     * leave a remnant; the gap between it and the newly drawn header is clean. */
    old = (struct rect){0, 150, 200, 270};
    now = (struct rect){0, 50, 200, 170};
    log.count = 0;
    ck_assert(html_compute_scroll_damage(&html, &old, &now, collect, &log));
    assert_rect(&log, 0, 15, 110, 126, 120);
    assert_rect(&log, 1, 15, 50, 126, 91);
}
END_TEST

START_TEST(test_unchanged_and_pruned_branches)
{
    struct damage_log log = {.capacity = 16};
    struct rect old = {0, 0, 200, 120}, now = {0, 20, 200, 140};
    ck_assert(html_compute_scroll_damage(&html, &old, &now, collect, &log));
    ck_assert_uint_eq(log.count, 0); /* Before sticking, everything translates. */

    root.flags = 0;
    old.y0 = 90;
    old.y1 = 210;
    now.y0 = 130;
    now.y1 = 250;
    ck_assert(html_compute_scroll_damage(&html, &old, &now, collect, &log));
    ck_assert_uint_eq(log.count, 0); /* No tree traversal on an ordinary page. */
}
END_TEST

START_TEST(test_nested_movement_and_overflow_clip)
{
    /* The child sticks independently while its parent remains in normal flow.
     * Its position escapes the parent's settled descendant rectangle. */
    sticky.y.start_auto = true;
    header.flags |= HAS_STICKY_DESCENDANT;
    struct box_sticky inner = {.owner = &html,
        .valid = true,
        .containing_block = &root,
        .scrolling_ancestor = &html.viewport.box,
        .x = {.start_auto = true, .end_auto = true},
        .y = {.border_start = 100,
            .border_size = 10,
            .containing_size = 1000,
            .scrollport_size = 120,
            .inset_start = 25,
            .end_auto = true}};
    struct box child = {.type = BOX_BLOCK,
        .parent = &header,
        .sticky = &inner,
        .flags = HAS_STICKY_ANCESTOR,
        .width = 20,
        .height = 10,
        .descendant_x1 = 20,
        .descendant_y1 = 10};
    header.children = &child;
    struct rect old = {0, 150, 200, 270}, now = {0, 160, 200, 280};
    struct damage_log log = {.capacity = 16};
    ck_assert(html_compute_scroll_damage(&html, &old, &now, collect, &log));
    ck_assert_uint_eq(log.count, 2);
    assert_rect(&log, 0, 20, 15, 41, 26);
    assert_rect(&log, 1, 20, 25, 41, 36);

    header.style = sticky_test_style(CSS_POSITION_STATIC, CSS_OVERFLOW_HIDDEN, CSS_OVERFLOW_HIDDEN);
    log.count = 0;
    ck_assert(html_compute_scroll_damage(&html, &old, &now, collect, &log));
    ck_assert_uint_eq(log.count, 0); /* Both positions are outside the real ancestor clip. */
    css_computed_style_destroy(header.style);
}
END_TEST

START_TEST(test_refusal_and_lifecycle)
{
    struct rect old = {0, 90, 200, 210}, now = {0, 130, 200, 250};
    struct damage_log log = {.capacity = 0};
    ck_assert(!html_compute_scroll_damage(&html, &old, &now, collect, &log));
    log.capacity = 16;
    ck_assert(html_compute_scroll_damage(&html, &old, &now, collect, &log));
    ck_assert_uint_eq(log.count, 2); /* Refusal leaves no query state behind. */
    log.count = 0;
    bool *guards[] = {&html.sticky_layout_valid, &html.scroll_reuse_safe, &html.redraw_region_safe};
    for (unsigned i = 0; i < sizeof(guards) / sizeof(guards[0]); i++) {
        *guards[i] = false;
        ck_assert_msg(!html_compute_scroll_damage(&html, &old, &now, collect, &log), "guard %u ignored", i);
        *guards[i] = true;
    }
    html.restyling = true;
    ck_assert(!html_compute_scroll_damage(&html, &old, &now, collect, &log));
    html.restyling = false;
    ck_assert_uint_eq(log.count, 0);
    now.x1++;
    ck_assert(!html_compute_scroll_damage(&html, &old, &now, collect, &log));
    now = (struct rect){INT_MIN, 0, INT_MAX, 120};
    ck_assert(!html_compute_scroll_damage(&html, &old, &now, collect, &log));
}
END_TEST

START_TEST(test_moving_ink_with_identical_clipped_bounds)
{
    struct box clip = {.type = BOX_BLOCK,
        .parent = &root,
        .width = 200,
        .height = 500,
        .children = &header,
        .flags = HAS_STICKY_DESCENDANT,
        .style = sticky_test_style(CSS_POSITION_ABSOLUTE, CSS_OVERFLOW_VISIBLE, CSS_OVERFLOW_VISIBLE)};
    sticky_test_set_clip(clip.style, 180, 200, 200, 0);
    root.children = &clip;
    header.parent = &clip;
    sticky.containing_block = &clip;
    sticky.y.border_size = 100;
    sticky.y.containing_size = 500;
    header.height = header.descendant_y1 = 100;
    struct rect old = {0, 130, 200, 250}, now = {0, 150, 200, 270};
    struct damage_log log = {.capacity = 16};
    ck_assert(html_compute_scroll_damage(&html, &old, &now, collect, &log));
    ck_assert_uint_eq(log.count, 2);
    assert_rect(&log, 0, 15, 30, 126, 50);
    assert_rect(&log, 1, 15, 30, 126, 50);
    css_computed_style_destroy(clip.style);
}
END_TEST

START_TEST(test_embedded_viewport_coverage)
{
    /* A frame may paint viewport-dependent child content even though its outer
     * box has unchanged document coordinates. Follow frame-only branches too. */
    header.sticky = NULL;
    header.flags = IFRAME;
    root.flags = HAS_IFRAME_DESCENDANT;
    struct rect old = {0, 90, 200, 210}, now = {0, 130, 200, 250};
    struct damage_log log = {.capacity = 16};
    ck_assert(html_compute_scroll_damage(&html, &old, &now, collect, &log));
    ck_assert_uint_eq(log.count, 2);
    assert_rect(&log, 0, 15, 0, 126, 11);
    assert_rect(&log, 1, 15, 0, 126, 11);

    /* Fully offscreen frames cost no paint. */
    old = (struct rect){0, 200, 200, 320};
    now = (struct rect){0, 220, 200, 340};
    log.count = 0;
    ck_assert(html_compute_scroll_damage(&html, &old, &now, collect, &log));
    ck_assert_uint_eq(log.count, 0);
}
END_TEST

int main(void)
{
    Suite *suite = suite_create("Scroll damage");
    TCase *tc = tcase_create("Settled sticky coverage");
    tcase_add_checked_fixture(tc, setup, NULL);
    tcase_add_test(tc, test_copied_and_current_coverage);
    tcase_add_test(tc, test_unchanged_and_pruned_branches);
    tcase_add_test(tc, test_nested_movement_and_overflow_clip);
    tcase_add_test(tc, test_refusal_and_lifecycle);
    tcase_add_test(tc, test_moving_ink_with_identical_clipped_bounds);
    tcase_add_test(tc, test_embedded_viewport_coverage);
    suite_add_tcase(suite, tc);
    SRunner *runner = srunner_create(suite);
    srunner_run_all(runner, CK_NORMAL);
    int failed = srunner_ntests_failed(runner);
    srunner_free(runner);
    return failed ? EXIT_FAILURE : EXIT_SUCCESS;
}

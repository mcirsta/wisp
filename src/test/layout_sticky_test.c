/* Numeric sticky constraints. HTML fixture: test/sticky-position.html. */
#include <check.h>
#include <inttypes.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <dom/core/implementation.h>
#include <wisp/content/handlers/html/box.h>
#include <wisp/content/handlers/html/box_inspect.h>
#include <wisp/content/handlers/html/private.h>
#include <wisp/utils/corestrings.h>
#include "content/handlers/html/box_manipulate.h"
#include "utils/talloc.h"

#include "content/handlers/html/box_sticky.h"

css_computed_style *sticky_test_style(uint8_t position, uint8_t overflow_x, uint8_t overflow_y);

static const int scrolls[] = {0, 100, 300, 380, 450, 0};

/* Positions relative to the scrollport, following CSS Position 3 section 3.4:
 * https://www.w3.org/TR/css-position-3/#stickypos-insets
 * Chromium 153.0.8010.52 and Firefox 157.0 agree except for the single-inset
 * oversized cases documented below; their measurements are not the rule.
 * Returning to zero is intentional: queries must never accumulate movement.
 * Geometry is in the scroller's unscrolled content coordinates. */
static const struct {
    const char *name;
    struct box_sticky_axis axis;
    int positions[sizeof(scrolls) / sizeof(scrolls[0])];
} axis_cases[] = {
    {"top",
        {.border_start = 80,
            .border_size = 40,
            .containing_size = 400,
            .scrollport_size = 120,
            .inset_start = 10,
            .end_auto = true},
        {80, 10, 10, -20, -90, 80}},
    {"bottom",
        {.border_start = 80,
            .border_size = 40,
            .containing_size = 400,
            .scrollport_size = 120,
            .inset_end = 10,
            .start_auto = true},
        {70, -20, -220, -300, -370, 70}},
    {"both",
        {.border_start = 80,
            .border_size = 40,
            .containing_size = 400,
            .scrollport_size = 120,
            .inset_start = 10,
            .inset_end = 20},
        {60, 10, 10, -20, -90, 60}},
    {"tall-top",
        {.border_start = 80,
            .border_size = 180,
            .containing_size = 400,
            .scrollport_size = 120,
            .inset_start = 10,
            .end_auto = true},
        {80, 10, -80, -160, -230, 80}},
    /* Auto top contributes zero to a 110px view rectangle. Expanding it to
     * 180px reduces bottom:10px to -60px. At scroll 100 the normal top is -20px,
     * already inside that constraint, so the box must not shift to -70px. */
    {"tall-bottom",
        {.border_start = 80,
            .border_size = 180,
            .containing_size = 400,
            .scrollport_size = 120,
            .inset_end = 10,
            .start_auto = true},
        {0, -20, -220, -300, -370, 0}},
    {"tall-both",
        {.border_start = 80,
            .border_size = 180,
            .containing_size = 400,
            .scrollport_size = 120,
            .inset_start = 10,
            .inset_end = 20},
        {10, 10, -80, -160, -230, 10}},
    {"margin",
        {.border_start = 80,
            .border_size = 40,
            .containing_size = 400,
            .margin_end = 30,
            .scrollport_size = 120,
            .inset_start = 10,
            .end_auto = true},
        {80, 10, 10, -50, -120, 80}},
    {"negative-margin",
        {.border_start = 80,
            .border_size = 40,
            .containing_size = 400,
            .margin_end = -20,
            .scrollport_size = 120,
            .inset_start = 10,
            .end_auto = true},
        {80, 10, 10, 0, -70, 80}},
    {"left",
        {.border_start = 80,
            .border_size = 60,
            .containing_size = 500,
            .margin_start = 80,
            .scrollport_size = 200,
            .inset_start = 10,
            .end_auto = true},
        {80, 10, 10, 10, -10, 80}},
    {"right-margin-limited",
        {.border_start = 250,
            .border_size = 60,
            .containing_size = 500,
            .margin_start = 250,
            .scrollport_size = 200,
            .inset_end = 10,
            .start_auto = true},
        {250, 150, -50, -130, -200, 250}},
    {"wide",
        {.border_start = 80,
            .border_size = 240,
            .containing_size = 500,
            .margin_start = 80,
            .scrollport_size = 200,
            .inset_start = 10,
            .inset_end = 20},
        {80, 10, -40, -120, -190, 80}},
    {"wide-rtl-margin-limited",
        {.border_start = 80,
            .border_size = 240,
            .containing_size = 500,
            .margin_start = 80,
            .margin_end = 180,
            .scrollport_size = 200,
            .inset_start = 10,
            .inset_end = 20,
            .reverse = true},
        {80, -20, -220, -300, -370, 80}},
    /* Sibling spacing, rather than margins, leaves room for movement.
     * A 240px box in a 200px scrollport reduces the logical-end inset to
     * -40px: right in LTR, left in RTL. Auto on the other side stays nonbinding. */
    {"wide-right",
        {.border_start = 80,
            .border_size = 240,
            .containing_size = 500,
            .scrollport_size = 200,
            .inset_end = 10,
            .start_auto = true},
        {0, -20, -220, -300, -370, 0}},
    {"wide-rtl-left",
        {.border_start = 80,
            .border_size = 240,
            .containing_size = 500,
            .scrollport_size = 200,
            .inset_start = 10,
            .end_auto = true,
            .reverse = true},
        {80, -20, -40, -120, -190, 80}},
    {"wide-rtl-both",
        {.border_start = 80,
            .border_size = 240,
            .containing_size = 500,
            .scrollport_size = 200,
            .inset_start = 10,
            .inset_end = 20,
            .reverse = true},
        {0, -60, -60, -120, -190, 0}},
};

START_TEST(test_axes)
{
    for (size_t i = 0; i < sizeof(scrolls) / sizeof(scrolls[0]); i++) {
        struct box_sticky_axis axis = axis_cases[_i].axis;
        axis.scrollport_start = scrolls[i];
        int64_t position = axis.border_start + box_sticky_axis_offset(&axis) - scrolls[i];
        ck_assert_msg(position == axis_cases[_i].positions[i], "%s at scroll %d: got %" PRId64 ", expected %d",
            axis_cases[_i].name, scrolls[i], position, axis_cases[_i].positions[i]);
    }
}
END_TEST

START_TEST(test_auto_and_zero)
{
    struct box_sticky_axis axis = {.border_start = 20,
        .border_size = 40,
        .containing_size = 400,
        .scrollport_start = 100,
        .scrollport_size = 120,
        .start_auto = true,
        .end_auto = true,
        .inset_start = INT_MAX,
        .inset_end = INT_MIN};
    ck_assert_int_eq(box_sticky_axis_offset(&axis), 0);
    axis.start_auto = false;
    axis.inset_start = 0;
    ck_assert_int_eq(box_sticky_axis_offset(&axis), 80);
    axis.inset_start = -10;
    ck_assert_int_eq(box_sticky_axis_offset(&axis), 70);
}
END_TEST

START_TEST(test_containment_without_travel)
{
    struct box_sticky_axis axis = {.border_start = 0,
        .border_size = 180,
        .containing_size = 100,
        .scrollport_start = 80,
        .scrollport_size = 120,
        .inset_start = 10,
        .end_auto = true};
    ck_assert_msg(box_sticky_axis_offset(&axis) == 0, "A box overflowing its parent has no forward travel");
    axis.border_size = 40;
    axis.border_start = 30;
    axis.margin_end = 100;
    ck_assert_msg(box_sticky_axis_offset(&axis) == 0, "An overflowing end margin cannot cause a backwards jump");
}
END_TEST

START_TEST(test_wide_arithmetic)
{
    struct box_sticky_axis axis = {.border_start = INT_MAX - 100,
        .border_size = 40,
        .containing_start = INT_MAX - 200,
        .containing_size = 400,
        .scrollport_start = INT_MAX - 20,
        .scrollport_size = 120,
        .inset_start = 10,
        .end_auto = true};
    ck_assert_msg(box_sticky_axis_offset(&axis) == 90, "Interval ends must not overflow signed int");
    axis.border_start = INT_MIN + 200;
    axis.containing_start = INT_MIN + 100;
    axis.scrollport_start = INT_MAX - 20;
    ck_assert_msg(box_sticky_axis_offset(&axis) == 260, "Far-apart coordinates still respect containing bounds");
}
END_TEST

static html_content html;
static dom_node *element;
static struct box *port, *container, *wrapper, *header;

static struct box *new_box(struct box *parent, uint8_t position, uint8_t x, uint8_t y)
{
    css_computed_style *style = sticky_test_style(position, x, y);
    ck_assert_ptr_nonnull(style);
    struct box *box = box_create(NULL, style, true, NULL, NULL, NULL, NULL, parent ? (void *)parent : html.bctx);
    ck_assert_ptr_nonnull(box);
    box->type = BOX_BLOCK;
    box->node = dom_node_ref(element);
    box->parent = parent;
    box->width = 200;
    box->height = 100;
    return box;
}

static void prepare_setup(void)
{
    memset(&html, 0, sizeof(html));
    ck_assert_int_eq(corestrings_init(), NSERROR_OK);
    ck_assert_int_eq(
        dom_implementation_create_document(DOM_IMPLEMENTATION_HTML, NULL, NULL, NULL, NULL, NULL, &html.document),
        DOM_NO_ERR);
    dom_string *name;
    ck_assert_int_eq(dom_string_create((const uint8_t *)"div", 3, &name), DOM_NO_ERR);
    ck_assert_int_eq(dom_document_create_element(html.document, name, (dom_element **)&element), DOM_NO_ERR);
    dom_string_unref(name);
    html.bctx = talloc_new(NULL);
    html.unit_len_ctx.device_dpi = INTTOFIX(96);
    html.viewport.box = (struct box){.flags = IS_VIEWPORT, .width = 800, .height = 600};
    html.layout = new_box(NULL, CSS_POSITION_STATIC, CSS_OVERFLOW_VISIBLE, CSS_OVERFLOW_VISIBLE);
    port = new_box(html.layout, CSS_POSITION_STATIC, CSS_OVERFLOW_HIDDEN, CSS_OVERFLOW_VISIBLE);
    port->x = 30;
    port->y = 40;
    port->padding[LEFT] = port->padding[RIGHT] = 5;
    port->padding[TOP] = port->padding[BOTTOM] = 10;
    container = new_box(port, CSS_POSITION_STATIC, CSS_OVERFLOW_VISIBLE, CSS_OVERFLOW_VISIBLE);
    container->x = 7;
    container->y = 11;
    container->padding[TOP] = 3;
    container->height = 400;
    wrapper = new_box(container, CSS_POSITION_STATIC, CSS_OVERFLOW_VISIBLE, CSS_OVERFLOW_VISIBLE);
    wrapper->type = BOX_INLINE_CONTAINER;
    wrapper->x = 2;
    wrapper->y = 4;
    header = new_box(wrapper, CSS_POSITION_STICKY, CSS_OVERFLOW_VISIBLE, CSS_OVERFLOW_VISIBLE);
    header->type = BOX_INLINE_BLOCK;
    header->x = 13;
    header->y = 17;
    header->width = 50;
    header->height = 20;
    header->padding[TOP] = 2;
    header->padding[BOTTOM] = 3;
    header->border[TOP].width = 1;
    header->border[BOTTOM].width = 2;
    header->margin[TOP] = -5;
    header->margin[BOTTOM] = 9;
}

static void prepare_teardown(void)
{
    talloc_free(html.bctx);
    dom_node_unref(element);
    dom_node_unref(html.document);
    corestrings_fini();
}

START_TEST(test_prepare_ancestors_and_geometry)
{
    ck_assert(box_sticky_prepare(&html, header));
    ck_assert_ptr_null(box_sticky_get(header)); // Unfinished document must not expose constraints.
    html.sticky_layout_valid = true;
    const struct box_sticky *s = box_sticky_get(header);
    ck_assert_ptr_nonnull(s);
    ck_assert_ptr_eq(s->containing_block, container);
    ck_assert_ptr_eq(s->scrolling_ancestor, port);
    ck_assert_ptr_null(port->scroll_x);
    ck_assert_ptr_null(port->scroll_y); // Hidden without overflow still establishes the scrollport.
    ck_assert_int_eq(s->y.border_start, 71);
    ck_assert_int_eq(s->y.border_size, 28);
    ck_assert_int_eq(s->y.containing_start, 54);
    ck_assert_int_eq(s->y.containing_size, 400);
    ck_assert_int_eq(s->y.scrollport_start, 40);
    ck_assert_int_eq(s->y.scrollport_size, 120);
    ck_assert_int_eq(s->y.inset_start, 12); // Percentage of scrollport, not containing block.
    ck_assert_int_eq(s->y.margin_start, -5);
    ck_assert_int_eq(s->y.margin_end, 9);
    ck_assert(s->y.end_auto && s->x.start_auto && s->x.end_auto);
    ck_assert_int_eq(s->x.border_start, 52);
    ck_assert_int_eq(s->x.scrollport_size, 210);
    ck_assert_int_eq(header->x, 13);
    ck_assert_int_eq(header->y, 17);
    for (struct box *b = header->parent; b; b = b->parent)
        ck_assert(b->flags & HAS_STICKY_DESCENDANT);
}
END_TEST

START_TEST(test_prepare_lifetime)
{
    size_t before = talloc_total_blocks(html.bctx);
    ck_assert(box_sticky_prepare(&html, header));
    struct box_sticky *record = header->sticky;
    ck_assert_ptr_eq(talloc_parent(record), header);
    ck_assert_uint_eq(talloc_total_blocks(html.bctx), before + 1);
    html.sticky_layout_valid = true;
    ck_assert_ptr_eq(box_sticky_get(header), record);
    html.sticky_layout_valid = false;
    ck_assert_ptr_null(box_sticky_get(header));
    header->y = 30;
    ck_assert(box_sticky_prepare(&html, header));
    ck_assert_ptr_eq(header->sticky, record);
    ck_assert_int_eq(record->y.border_start, 84);
    ck_assert_uint_eq(talloc_total_blocks(html.bctx), before + 1);
    html.sticky_layout_valid = true;
    header->height = INT_MAX;
    ck_assert(box_sticky_prepare(&html, header));
    ck_assert_ptr_null(box_sticky_get(header)); // Reject unresolved/overflowed geometry.
    ck_assert(!record->valid);
    header->height = 20;
    ck_assert(box_sticky_prepare(&html, header));
    ck_assert_ptr_eq(box_sticky_get(header), record);
    header->type = BOX_INLINE;
    ck_assert(box_sticky_prepare(&html, header));
    ck_assert_ptr_null(header->sticky);
    ck_assert_uint_eq(talloc_total_blocks(html.bctx), before);
    header->type = BOX_INLINE_BLOCK;
    ck_assert(box_sticky_prepare(&html, header));
    talloc_free(header); // Frees its owned record without a separate registry.
    ck_assert_uint_eq(talloc_total_blocks(html.bctx), before - 1);
}
END_TEST

START_TEST(test_prepare_auto_scrollport)
{
    css_computed_style_destroy(port->style);
    port->style = sticky_test_style(CSS_POSITION_STATIC, CSS_OVERFLOW_AUTO, CSS_OVERFLOW_SCROLL);
    // Layout reserves scrollbar thickness in padding, before scrollbar creation.
    port->padding[RIGHT] += 16;
    port->padding[BOTTOM] += 16;
    port->descendant_x1 = 1000;
    ck_assert(box_sticky_prepare(&html, header));
    ck_assert_int_eq(header->sticky->x.scrollport_size, 210);
    ck_assert_int_eq(header->sticky->y.scrollport_size, 120);
    ck_assert_ptr_null(port->scroll_x);
    ck_assert_ptr_null(port->scroll_y);
    css_computed_style_destroy(port->style);
    port->style = sticky_test_style(CSS_POSITION_STATIC, CSS_OVERFLOW_AUTO, CSS_OVERFLOW_VISIBLE);
    port->descendant_x1 = 0;
    ck_assert(box_sticky_prepare(&html, header));
    ck_assert_ptr_eq(header->sticky->scrolling_ancestor, port); // Auto without overflow is still a scrollport.
    css_computed_style_destroy(port->style);
    port->style = sticky_test_style(CSS_POSITION_STATIC, CSS_OVERFLOW_VISIBLE, CSS_OVERFLOW_VISIBLE);
    ck_assert(box_sticky_prepare(&html, header));
    ck_assert_ptr_eq(header->sticky->scrolling_ancestor, &html.viewport.box);
    ck_assert_int_eq(header->sticky->y.inset_start, 60);
    ck_assert(box_sticky_prepare(&html, container));
    ck_assert_ptr_null(container->sticky); // Ordinary boxes allocate no record.
}
END_TEST

START_TEST(test_visual_ancestry)
{
    html_content content = {.sticky_layout_valid = true};
    content.viewport.box.flags = IS_VIEWPORT;
    struct box document = {.type = BOX_BLOCK};
    struct box cb = {.type = BOX_BLOCK, .parent = &document, .y = 20};
    struct box_sticky outer = {.owner = &content,
        .valid = true,
        .containing_block = &cb,
        .scrolling_ancestor = &content.viewport.box,
        .x = {.start_auto = true, .end_auto = true},
        .y = {.border_start = 100,
            .border_size = 40,
            .containing_start = 20,
            .containing_size = 400,
            .scrollport_size = 120,
            .inset_start = 10,
            .end_auto = true}};
    struct box header = {.type = BOX_BLOCK, .parent = &cb, .y = 80, .sticky = &outer};
    struct box child = {.type = BOX_BLOCK, .parent = &header, .y = 7, .flags = HAS_STICKY_ANCESTOR};
    struct box absolute = {
        .type = BOX_BLOCK, .parent = &child, .abs_containing_block = &header, .y = 10, .flags = HAS_STICKY_ANCESTOR};
    struct box fixed = {.type = BOX_BLOCK, .parent = &child, .abs_containing_block = &content.viewport.box, .y = 12};
    struct box floating = {.type = BOX_FLOAT_LEFT,
        .parent = &header,
        .float_container = &document,
        .y = 110,
        .flags = HAS_STICKY_ANCESTOR};
    struct box_sticky inner = {.owner = &content,
        .valid = true,
        .containing_block = &header,
        .scrolling_ancestor = &content.viewport.box,
        .x = {.start_auto = true, .end_auto = true},
        .y = {.border_start = 105,
            .border_size = 10,
            .containing_start = 100,
            .containing_size = 40,
            .scrollport_size = 120,
            .inset_start = 25,
            .end_auto = true}};
    struct box nested = {.type = BOX_BLOCK, .parent = &header, .y = 5, .sticky = &inner, .flags = HAS_STICKY_ANCESTOR};
    static const int cases[][3] = {{0, 100, 105}, {150, 160, 175}, {450, 380, 410}, {0, 100, 105}};
    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        int x, y, scroll = cases[i][0], expected = cases[i][1];
        box_coords_at_scroll(&header, &x, &y, 0, scroll);
        ck_assert_msg(y == expected, "header at scroll %d: %d != %d", scroll, y, expected);
        box_coords_at_scroll(&child, &x, &y, 0, scroll);
        ck_assert_msg(y == expected + 7, "ordinary child at scroll %d: %d", scroll, y);
        box_coords_at_scroll(&absolute, &x, &y, 0, scroll);
        ck_assert_msg(y == expected + 10, "absolute child at scroll %d: %d", scroll, y);
        box_coords_at_scroll(&fixed, &x, &y, 0, scroll);
        ck_assert_msg(y == scroll + 12, "independent fixed child at scroll %d: %d", scroll, y);
        box_coords_at_scroll(&floating, &x, &y, 0, scroll);
        ck_assert_msg(y == expected + 10, "float bypassing sticky coordinate parent at scroll %d: %d", scroll, y);
        box_coords_at_scroll(&nested, &x, &y, 0, scroll);
        ck_assert_msg(y == cases[i][2], "nested sticky at scroll %d: %d != %d", scroll, y, cases[i][2]);
        box_coords_unscrolled(&nested, &x, &y);
        ck_assert_int_eq(y, 105);
    }
    content.sticky_layout_valid = false;
    int x, y;
    box_coords_at_scroll(&nested, &x, &y, 0, 150);
    ck_assert_int_eq(y, 105);
}
END_TEST

START_TEST(test_clipped_text_selection)
{
    html_content content = {.sticky_layout_valid = true};
    struct box document = {.type = BOX_BLOCK};
    content.layout = &document;
    struct box clip = {.type = BOX_BLOCK,
        .parent = &document,
        .y = 40,
        .width = 100,
        .height = 20,
        .style = sticky_test_style(CSS_POSITION_STATIC, CSS_OVERFLOW_HIDDEN, CSS_OVERFLOW_HIDDEN)};
    struct box_sticky sticky = {.owner = &content,
        .valid = true,
        .containing_block = &clip,
        .scrolling_ancestor = &clip,
        .x = {.start_auto = true, .end_auto = true},
        .y = {.start_auto = true, .end_auto = true}};
    struct box header = {.type = BOX_BLOCK, .parent = &clip, .sticky = &sticky, .width = 100, .height = 40};
    struct box line = {
        .type = BOX_INLINE_CONTAINER, .parent = &header, .width = 100, .height = 40, .flags = HAS_STICKY_ANCESTOR};
    struct box visible = {
        .type = BOX_TEXT, .parent = &line, .width = 100, .height = 10, .text = "visible", .flags = HAS_STICKY_ANCESTOR};
    struct box hidden = {.type = BOX_TEXT,
        .parent = &line,
        .y = 30,
        .width = 100,
        .height = 10,
        .text = "clipped",
        .flags = HAS_STICKY_ANCESTOR};
    document.children = &clip;
    clip.children = &header;
    header.children = &line;
    line.children = &visible;
    visible.next = &hidden;
    int dx, dy;
    ck_assert_ptr_eq(box_pick_text_box(&content, 5, 45, -1, &dx, &dy), &visible);
    ck_assert_int_eq(dy, 5);
    // Dragging beyond the clip must not select the invisible, closer line.
    ck_assert_ptr_eq(box_pick_text_box(&content, 5, 75, -1, &dx, &dy), &visible);
    ck_assert_int_eq(dy, 10);
    visible.y = 30;
    ck_assert_ptr_null(box_pick_text_box(&content, 5, 75, -1, &dx, &dy));
    ck_assert_int_eq(dx, 0);
    ck_assert_int_eq(dy, 0);
    css_computed_style_destroy(clip.style);
}
END_TEST

int main(void)
{
    Suite *suite = suite_create("Sticky geometry");
    TCase *tc = tcase_create("Resolved axis constraints");
    tcase_add_loop_test(tc, test_axes, 0, sizeof(axis_cases) / sizeof(axis_cases[0]));
    tcase_add_test(tc, test_auto_and_zero);
    tcase_add_test(tc, test_containment_without_travel);
    tcase_add_test(tc, test_wide_arithmetic);
    tcase_add_test(tc, test_visual_ancestry);
    tcase_add_test(tc, test_clipped_text_selection);
    suite_add_tcase(suite, tc);
    tc = tcase_create("Settled layout preparation");
    tcase_add_checked_fixture(tc, prepare_setup, prepare_teardown);
    tcase_add_test(tc, test_prepare_ancestors_and_geometry);
    tcase_add_test(tc, test_prepare_lifetime);
    tcase_add_test(tc, test_prepare_auto_scrollport);
    suite_add_tcase(suite, tc);
    SRunner *runner = srunner_create(suite);
    srunner_run_all(runner, CK_NORMAL);
    int failed = srunner_ntests_failed(runner);
    srunner_free(runner);
    return failed ? EXIT_FAILURE : EXIT_SUCCESS;
}

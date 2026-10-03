/* SPDX-License-Identifier: GPL-2.0-only */
/* C-only layout headers are kept separate from the Qt test driver. */
#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <wisp/layout.h>
#include <wisp/plot_style.h>
#include <wisp/content/handlers/html/private.h>
#include <wisp/content/handlers/html/box.h>
#include "utils/talloc.h"
#include "content/handlers/html/layout_internal.h"
#include "content/handlers/html/font.h"
#include "test/layout_margin_collapse_style.h"

struct paragraph {
    html_content content;
    struct box root, text;
    struct box *lines;
};

static void init(struct paragraph *p, const char *value, const struct gui_layout_table *fonts)
{
    memset(p, 0, sizeof(*p));
    p->content.unit_len_ctx.viewport_height = INTTOFIX(1000);
    p->content.unit_len_ctx.font_size_default = INTTOFIX(16);
    p->content.unit_len_ctx.device_dpi = INTTOFIX(96);
    p->content.font_func = fonts;
    p->content.bctx = talloc_new(NULL);
    assert(p->content.bctx);
    p->lines = talloc_zero(p->content.bctx, struct box);
    assert(p->lines);
    p->root.type = BOX_BLOCK;
    p->root.style = create_block_style();
    style_set_margins(p->root.style, 0, 0, 0, 0);
    style_set_inline_metrics(p->root.style, 16, 23, CSS_VERTICAL_ALIGN_BASELINE);
    p->root.children = p->root.last = p->lines;
    p->lines->type = BOX_INLINE_CONTAINER;
    p->lines->parent = &p->root;
    p->lines->children = p->lines->last = &p->text;
    p->text.type = BOX_TEXT;
    p->text.parent = p->lines;
    p->text.style = p->root.style;
    p->text.text = (char *)value;
    p->text.length = strlen(value);
    p->text.width = UNKNOWN_WIDTH;
}

static void finish(struct paragraph *p)
{
    /* All split fragments must retire with their inline container, without
     * leaving allocations under the longer-lived document. */
    talloc_free(p->lines);
    assert(talloc_total_blocks(p->content.bctx) == 1);
    talloc_free(p->content.bctx);
    destroy_mock_style(p->root.style);
}

static void layout(struct paragraph *p, int width)
{
    p->content.unit_len_ctx.viewport_width = INTTOFIX(width);
    p->root.width = width;
    p->root.height = AUTO;
    assert(layout_block_context(&p->root, 1000, &p->content));
}

static void check_spaces(struct paragraph *p)
{
    const struct gui_layout_table *fonts = p->content.font_func;
    plot_font_style_t font;
    int width, space;
    font_plot_style_from_css(&p->content.unit_len_ctx, p->root.style, &font);
    assert(fonts->width(&font, " ", 1, &space) == NSERROR_OK);
    for (struct box *b = p->lines->children; b; b = b->next) {
        if (b->type != BOX_TEXT)
            continue;
        if (b->flags & CLONE)
            assert(talloc_parent(b) == p->lines);
        assert(b->length > 0 && b->text[b->length - 1] != ' ');
        assert(fonts->width(&font, b->text, b->length, &width) == NSERROR_OK);
        if (b->width != width)
            fprintf(stderr, "text '%.*s': box width %d, measured %d, flags %x\n",
                (int)b->length, b->text, b->width, width, b->flags);
        assert(b->width == width);
        if (b->next) {
            assert(b->space == space);
            assert(b->next->text == b->text + b->length + 1);
            if (b->y == b->next->y)
                assert(b->next->x == b->x + b->width + b->space);
        }
    }
}

void test_text_reflow(const struct gui_layout_table *fonts)
{
    const char *values[] = {
        "With the proprietary Chaos Group V-RAY renderer, there was an upset in better performance on Windows "
            "with the RTX GPU back-end.",
        "café știri Français renderer, naïve words with UTF-8 text",
        "unbreakablelongword next short words",
    };
    /* Widening rejoins old fragments, which must retain their separating space. */
    const int widths[] = {369, 369, 600, 240, 240, 1000, 369};
    for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); i++) {
        struct paragraph retained;
        init(&retained, values[i], fonts);
        for (size_t j = 0; j < sizeof(widths) / sizeof(widths[0]); j++) {
            layout(&retained, widths[j]);
            check_spaces(&retained);
            struct paragraph fresh;
            init(&fresh, values[i], fonts);
            layout(&fresh, widths[j]);
            assert(retained.root.height == fresh.root.height);
            finish(&fresh);
        }
        finish(&retained);
    }
}

static void add_float(struct paragraph *p, css_computed_style *style, bool right)
{
    struct box *floating = talloc_zero(p->lines, struct box);
    assert(floating);
    struct box *child = talloc_zero(floating, struct box);
    assert(child);
    floating->type = right ? BOX_FLOAT_RIGHT : BOX_FLOAT_LEFT;
    floating->parent = p->lines;
    floating->children = floating->last = child;
    floating->next = &p->text;
    p->text.prev = floating;
    p->lines->children = floating;
    child->type = BOX_BLOCK;
    child->parent = floating;
    child->style = style;
    child->height = AUTO;
    child->max_width = UNKNOWN_MAX_WIDTH;
    child->flags |= HAS_HEIGHT | MAKE_HEIGHT;
}

void test_long_text_reflow(const struct gui_layout_table *fonts, const char *text)
{
    /* Compare line assignments as well as height: retained layouts can have
     * different fragment boundaries while still placing exactly the same text. */
    const int widths[] = {640, 240, 240, 900, 369, 640};
    for (int variant = 0; variant < 3; variant++) {
        css_computed_style *float_style = create_block_style();
        style_set_clear(float_style, CSS_CLEAR_NONE);
        style_set_margins(float_style, 0, 0, 0, 0);
        style_set_width(float_style, 80, false);
        style_set_height_px(float_style, 69); // Narrow the first three 23px lines.
        struct paragraph retained;
        init(&retained, text, fonts);
        if (variant)
            add_float(&retained, float_style, variant == 2);
        for (size_t i = 0; i < sizeof(widths) / sizeof(widths[0]); i++) {
            struct paragraph fresh;
            init(&fresh, text, fonts);
            if (variant)
                add_float(&fresh, float_style, variant == 2);
            layout(&retained, widths[i]);
            layout(&fresh, widths[i]);
            check_spaces(&retained);
            check_spaces(&fresh);
            assert(retained.root.height == fresh.root.height);
            struct box *expected = &fresh.text;
            bool beside_float = false, below_float = false;
            for (struct box *b = &retained.text; b; b = b->next) {
                while (expected->next && expected->next->text <= b->text)
                    expected = expected->next;
                if (b->y != expected->y)
                    fprintf(stderr, "long reflow variant %d, step %zu, width %d, byte %zu: y %d, expected %d\n",
                        variant, i, widths[i], (size_t)(b->text - text), b->y, expected->y);
                assert(b->y == expected->y);
                int left = variant == 1 && b->y < 69 ? 80 : 0;
                int right = widths[i] - (variant == 2 && b->y < 69 ? 80 : 0);
                if (b->x < left || b->x + b->width > right)
                    fprintf(stderr, "long reflow bounds variant %d, step %zu, byte %zu: x=%d y=%d width=%d, [%d,%d]\n",
                        variant, i, (size_t)(b->text - text), b->x, b->y, b->width, left, right);
                assert(b->x >= left && b->x + b->width <= right);
                beside_float |= b->y < 69;
                below_float |= b->y >= 69;
            }
            assert(beside_float && below_float);
            finish(&fresh);
        }
        finish(&retained);
        destroy_mock_style(float_style);
    }
}

void test_fractional_text_reflow(const struct gui_layout_table *fonts)
{
    /* The Qt driver selects the generated face: "a b" is just over 23px.
     * Check the core's whole-box fast path as well as retained split boxes. */
    const int widths[] = {23, 23, 24, 23, 24};
    struct paragraph retained;
    init(&retained, "a b", fonts);
    for (size_t i = 0; i < sizeof(widths) / sizeof(widths[0]); i++) {
        struct paragraph fresh;
        init(&fresh, "a b", fonts);
        layout(&retained, widths[i]);
        layout(&fresh, widths[i]);
        assert(fresh.root.height == (widths[i] == 23 ? 46 : 23));
        assert(retained.root.height == fresh.root.height);
        check_spaces(&retained);
        finish(&fresh);
    }
    finish(&retained);

    /* Splitting estimates the remainder by subtracting rounded advances.
     * An apparent exact fit must still be measured before accepting it. */
    struct paragraph estimated;
    init(&estimated, "a b", fonts);
    estimated.text.width = 23;
    layout(&estimated, 23);
    assert(estimated.root.height == 46);
    check_spaces(&estimated);
    finish(&estimated);

    /* An overestimate can invoke split() even when the whole remainder fits.
     * The box must retain that measured width without creating another clone. */
    init(&estimated, "a", fonts);
    estimated.text.width = 11;
    layout(&estimated, 10);
    assert(estimated.text.width == 10 && estimated.text.next == NULL);
    check_spaces(&estimated);
    finish(&estimated);
}

void test_fragment_rounding_reflow(const struct gui_layout_table *fonts)
{
    /* Whole-run rounding differs from summing rounded words and spaces in
     * both directions. Layout history must not change the final line breaks. */
    static const struct {
        int font_px;
        const char *text;
        int widths[8];
    } cases[] = {
        {13, "aaaaa bbbbb", {81, 81, 82, 40, 81, 100, 82, 81}},
        {14, "a b", {20, 21, 21, 9, 21, 100, 20, 21}},
        {13, "aaaaa bbbbb ccccc ddddd eeeee fffff", {81, 81, 82, 160, 40, 300, 81, 82}},
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        struct paragraph retained;
        init(&retained, cases[i].text, fonts);
        style_set_inline_metrics(retained.root.style, cases[i].font_px, 23, CSS_VERTICAL_ALIGN_BASELINE);
        for (size_t j = 0; j < sizeof(cases[i].widths) / sizeof(cases[i].widths[0]); j++) {
            struct paragraph fresh;
            init(&fresh, cases[i].text, fonts);
            style_set_inline_metrics(fresh.root.style, cases[i].font_px, 23, CSS_VERTICAL_ALIGN_BASELINE);
            layout(&retained, cases[i].widths[j]);
            layout(&fresh, cases[i].widths[j]);
            if (retained.root.height != fresh.root.height)
                fprintf(stderr, "fragment reflow case %zu, step %zu, width %d: retained %d, fresh %d\n",
                    i, j, cases[i].widths[j], retained.root.height, fresh.root.height);
            assert(retained.root.height == fresh.root.height);
            struct box *expected = fresh.lines->children;
            for (struct box *b = retained.lines->children; b; b = b->next) {
                while (expected->next && expected->next->text <= b->text)
                    expected = expected->next;
                assert(b->y == expected->y);
            }
            check_spaces(&retained);
            finish(&fresh);
        }
        finish(&retained);
    }
}

void test_inline_spacing_reflow(const struct gui_layout_table *fonts)
{
    /* AA<span> <span> BB </span></span>: opening markers own both
     * spaces before BB. Only one survives, including after reflow. */
    struct paragraph p;
    init(&p, "AA", fonts);
    struct box inlines[5] = {0};
    p.text.next = &inlines[0];
    p.lines->last = &inlines[4];
    for (size_t i = 0; i < 5; i++) {
        inlines[i].type = i < 2 ? BOX_INLINE : i == 2 ? BOX_TEXT : BOX_INLINE_END;
        inlines[i].style = p.root.style;
        inlines[i].parent = p.lines;
        inlines[i].prev = i == 0 ? &p.text : &inlines[i - 1];
        inlines[i].next = i == 4 ? NULL : &inlines[i + 1];
    }
    inlines[0].inline_end = &inlines[4];
    inlines[4].inline_end = &inlines[0];
    inlines[1].inline_end = &inlines[3];
    inlines[3].inline_end = &inlines[1];
    inlines[0].space = inlines[1].space = inlines[2].space = UNKNOWN_WIDTH;
    inlines[2].text = "BB";
    inlines[2].length = 2;
    inlines[2].width = UNKNOWN_WIDTH;
    p.root.max_width = p.lines->max_width = UNKNOWN_MAX_WIDTH;
    layout_minmax_box(&p.root, fonts, &p.content);

    plot_font_style_t font;
    int word, space;
    font_plot_style_from_css(&p.content.unit_len_ctx, p.root.style, &font);
    assert(fonts->width(&font, "AA", 2, &word) == NSERROR_OK);
    assert(fonts->width(&font, " ", 1, &space) == NSERROR_OK);
    int fit = word * 2 + space;
    const int widths[] = {fit, fit - 1, 200, word, fit};
    for (size_t i = 0; i < sizeof(widths) / sizeof(widths[0]); i++) {
        bool wraps = widths[i] < fit;
        layout(&p, widths[i]);
        assert(inlines[2].x == (wraps ? 0 : word + space));
        assert(inlines[2].y == (wraps ? 23 : 0));
        assert(p.root.height == (wraps ? 46 : 23));
    }
    finish(&p);
}

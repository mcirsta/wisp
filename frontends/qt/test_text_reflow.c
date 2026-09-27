/* SPDX-License-Identifier: GPL-2.0-only */
/* C-only layout headers are kept separate from the Qt test driver. */
#undef NDEBUG
#include <assert.h>
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
    struct box root, lines, text;
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
    p->root.type = BOX_BLOCK;
    p->root.style = create_block_style();
    style_set_margins(p->root.style, 0, 0, 0, 0);
    style_set_inline_metrics(p->root.style, 16, 23, CSS_VERTICAL_ALIGN_BASELINE);
    p->root.children = p->root.last = &p->lines;
    p->lines.type = BOX_INLINE_CONTAINER;
    p->lines.parent = &p->root;
    p->lines.children = p->lines.last = &p->text;
    p->text.type = BOX_TEXT;
    p->text.parent = &p->lines;
    p->text.style = p->root.style;
    p->text.text = (char *)value;
    p->text.length = strlen(value);
    p->text.width = UNKNOWN_WIDTH;
}

static void finish(struct paragraph *p)
{
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
    for (struct box *b = p->lines.children; b; b = b->next) {
        assert(b->length > 0 && b->text[b->length - 1] != ' ');
        assert(fonts->width(&font, b->text, b->length, &width) == NSERROR_OK);
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
}

void test_inline_spacing_reflow(const struct gui_layout_table *fonts)
{
    /* AA<span> <span> BB </span></span>: opening markers own both
     * spaces before BB. Only one survives, including after reflow. */
    struct paragraph p;
    init(&p, "AA", fonts);
    struct box inlines[5] = {0};
    p.text.next = &inlines[0];
    p.lines.last = &inlines[4];
    for (size_t i = 0; i < 5; i++) {
        inlines[i].type = i < 2 ? BOX_INLINE : i == 2 ? BOX_TEXT : BOX_INLINE_END;
        inlines[i].style = p.root.style;
        inlines[i].parent = &p.lines;
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
    p.root.max_width = p.lines.max_width = UNKNOWN_MAX_WIDTH;
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

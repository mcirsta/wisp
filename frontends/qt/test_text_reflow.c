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

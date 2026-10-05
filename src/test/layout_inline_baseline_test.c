/* Inline text and object baselines, using the real block/inline layout code. */
#include <check.h>
#include <stdlib.h>
#include <string.h>

#include <wisp/layout.h>
#include <wisp/plot_style.h>
#include <wisp/utils/nsoption.h>
#include <wisp/content/handlers/html/private.h>
#include <wisp/content/handlers/html/box.h>
#include "html/layout_internal.h"
#include "html/layout_grid.h"
#include "html/font.h"
#include "layout_margin_collapse_style.h"

static nserror metrics(const struct plot_font_style *style, int *ascent, int *descent)
{
    (void)style;
    *ascent = 14;
    *descent = 2;
    return NSERROR_OK;
}

static nserror failed_metrics(const struct plot_font_style *style, int *ascent, int *descent)
{
    (void)style;
    (void)ascent;
    (void)descent;
    return NSERROR_NOT_IMPLEMENTED;
}

static nserror width(const struct plot_font_style *style, const char *text, size_t length, int *result)
{
    (void)style;
    (void)text;
    *result = length * 8;
    return NSERROR_OK;
}

static const struct gui_layout_table fonts = {.width = width, .font_metrics = metrics};

/* The boundary tests use one unbreakable word, so the split callback returns
 * that whole word even when it is wider than the available line. */
static nserror split_word(const struct plot_font_style *style, const char *text, size_t length,
    int available, size_t *offset, int *actual)
{
    (void)available;
    *offset = length;
    return width(style, text, length, actual);
}

static nserror scaled_metrics(const struct plot_font_style *style, int *ascent, int *descent)
{
    int pixels = (style->size * 4) / (PLOT_STYLE_SCALE * 3);
    *ascent = pixels * 7 / 8;
    *descent = pixels / 8;
    return NSERROR_OK;
}

static const struct gui_layout_table scaled_fonts = {.width = width, .font_metrics = scaled_metrics};

static struct box *make_box(box_type type, int line_height, uint8_t align)
{
    struct box *b = calloc(1, sizeof(*b));
    ck_assert_ptr_nonnull(b);
    b->type = type;
    b->width = 200;
    b->height = AUTO;
    if (type != BOX_INLINE_CONTAINER) {
        b->style = create_block_style();
        style_set_margins(b->style, 0, 0, 0, 0);
        style_set_inline_metrics(b->style, 16, line_height, align);
    }
    return b;
}

static void append(struct box *parent, struct box *child)
{
    child->parent = parent;
    child->prev = parent->last;
    if (parent->last)
        parent->last->next = child;
    else
        parent->children = child;
    parent->last = child;
}

static void free_tree(struct box *b)
{
    for (struct box *c = b->children, *next; c; c = next) {
        next = c->next;
        free_tree(c);
    }
    if (box_has_outside_marker(b))
        free_tree(b->list_marker);
    if (b->style)
        destroy_mock_style(b->style);
    free(b);
}

START_TEST(test_positioned_parent_text_line)
{
    html_content content = {.unit_len_ctx = {.viewport_width = INTTOFIX(200), .viewport_height = INTTOFIX(500),
        .font_size_default = INTTOFIX(16), .device_dpi = INTTOFIX(96)}, .font_func = &fonts};
    struct box *root = make_box(BOX_BLOCK, 24, CSS_VERTICAL_ALIGN_BASELINE);
    style_set_position_absolute(root->style);
    struct box *ic = make_box(BOX_INLINE_CONTAINER, 0, 0);
    struct box *text = make_box(BOX_TEXT, 24, CSS_VERTICAL_ALIGN_BASELINE);
    destroy_mock_style(text->style);
    text->style = root->style; /* Direct text shares the positioned parent's style. */
    text->text = "H";
    text->length = 1;
    text->width = UNKNOWN_WIDTH;
    append(root, ic);
    append(ic, text);
    ck_assert(layout_block_context(root, 500, &content));
    ck_assert_msg(root->height == 24 && ic->height == 24,
        "Direct text must occupy a line inside a positioned block: block=%d line=%d", root->height, ic->height);
    text->style = NULL;
    free_tree(root);
}
END_TEST

START_TEST(test_inline_object_baseline)
{
    static const struct {
        uint8_t align;
        int line, image, image_y, line_height;
    } cases[] = {
        {CSS_VERTICAL_ALIGN_BASELINE, 20, 80, 0, 84},
        {CSS_VERTICAL_ALIGN_TOP, 20, 80, 0, 80},
        {CSS_VERTICAL_ALIGN_BOTTOM, 20, 80, 0, 80},
        {CSS_VERTICAL_ALIGN_BASELINE, 20, 8, 8, 20},
        {CSS_VERTICAL_ALIGN_BASELINE, 40, 80, 0, 94},
        {CSS_VERTICAL_ALIGN_BASELINE, 0, 80, 0, 80},
    };
    int variant = _i % 6;
    bool with_text = _i >= 6;
    html_content content = {
        .unit_len_ctx = {.viewport_width = INTTOFIX(200), .viewport_height = INTTOFIX(500),
            .font_size_default = INTTOFIX(16), .device_dpi = INTTOFIX(96)},
        .font_func = &fonts,
    };
    struct box *root = make_box(BOX_BLOCK, cases[variant].line, CSS_VERTICAL_ALIGN_BASELINE);
    struct box *ic = make_box(BOX_INLINE_CONTAINER, 0, 0);
    append(root, ic);
    struct box *image = make_box(BOX_INLINE, cases[variant].line, cases[variant].align);
    image->flags |= REPLACE_DIM;
    style_set_width(image->style, 20, false);
    style_set_height_px(image->style, cases[variant].image);
    append(ic, image);
    struct box *text = NULL;
    if (with_text) {
        text = make_box(BOX_TEXT, cases[variant].line, CSS_VERTICAL_ALIGN_BASELINE);
        text->text = "Ag";
        text->length = 2;
        text->width = UNKNOWN_WIDTH;
        append(ic, text);
    }

    ck_assert(layout_block_context(root, 500, &content));
    ck_assert_int_eq(image->y, cases[variant].image_y);
    ck_assert_int_eq(ic->height, cases[variant].line_height);
    ck_assert_int_eq(root->height, cases[variant].line_height);
    if (with_text && cases[variant].align == CSS_VERTICAL_ALIGN_BASELINE) {
        plot_font_style_t style = {.size = 12 * PLOT_STYLE_SCALE};
        ck_assert_int_eq(text->y + font_baseline(&fonts, &style, text->height), image->y + image->height);
    }
    free_tree(root);
}
END_TEST

static const struct {
    int font, line, height, y;
    uint8_t align;
} empty_inline_cases[] = {
    {16, 20, 20, 0, CSS_VERTICAL_ALIGN_BASELINE},
    {32, 40, 40, 0, CSS_VERTICAL_ALIGN_BASELINE},
    {32, 20, 26, 0, CSS_VERTICAL_ALIGN_BASELINE},
    {8, 10, 20, 8, CSS_VERTICAL_ALIGN_BASELINE},
    {16, 40, 40, 0, CSS_VERTICAL_ALIGN_BASELINE},
    {16, 0, 20, 10, CSS_VERTICAL_ALIGN_BASELINE},
    {0, 0, 20, 16, CSS_VERTICAL_ALIGN_BASELINE},
    {32, 40, 40, 0, CSS_VERTICAL_ALIGN_TOP},
    {32, 40, 40, 0, CSS_VERTICAL_ALIGN_BOTTOM},
};
enum { EMPTY_INLINE_CASE_COUNT = sizeof(empty_inline_cases) / sizeof(empty_inline_cases[0]) };

START_TEST(test_empty_inline_metrics)
{
    /* Keep the mock font sizes literal, independent of the user's minimum. */
    int minimum_font = nsoption_int(font_min_size);
    nsoption_set_int(font_min_size, 0);
    int variant = _i % EMPTY_INLINE_CASE_COUNT;
    int mode = _i / EMPTY_INLINE_CASE_COUNT; /* phantom, left edge, right edge, adjacent text */
    html_content content = {
        .unit_len_ctx = {.viewport_width = INTTOFIX(320), .viewport_height = INTTOFIX(500),
            .font_size_default = INTTOFIX(16), .device_dpi = INTTOFIX(96)},
        .font_func = &scaled_fonts,
    };
    struct box *root = make_box(BOX_BLOCK, 20, CSS_VERTICAL_ALIGN_BASELINE);
    struct box *ic = make_box(BOX_INLINE_CONTAINER, 0, 0);
    struct box *start = make_box(BOX_INLINE, 20, CSS_VERTICAL_ALIGN_BASELINE);
    struct box *end = make_box(BOX_INLINE_END, 20, CSS_VERTICAL_ALIGN_BASELINE);
    start->inline_end = end;
    end->inline_end = start;
    style_set_inline_metrics(start->style, empty_inline_cases[variant].font,
        empty_inline_cases[variant].line, empty_inline_cases[variant].align);
    style_set_inline_metrics(end->style, empty_inline_cases[variant].font,
        empty_inline_cases[variant].line, empty_inline_cases[variant].align);
    style_set_margins(start->style, 0, mode == 2 ? 5 : 0, 0, mode == 1 ? 5 : 0);
    style_set_margins(end->style, 0, mode == 2 ? 5 : 0, 0, mode == 1 ? 5 : 0);
    append(root, ic);
    append(ic, start);
    append(ic, end);
    struct box *text = NULL;
    if (mode == 3) {
        text = make_box(BOX_TEXT, 20, CSS_VERTICAL_ALIGN_BASELINE);
        /* Only the empty span differs from the parent; the text itself must
         * not be what triggers mixed-font alignment. */
        destroy_mock_style(text->style);
        text->style = root->style;
        text->text = "H";
        text->length = 1;
        text->width = UNKNOWN_WIDTH;
        append(ic, text);
    }
    const int widths[] = {200, 320, 200};
    for (size_t pass = 0; pass < sizeof(widths) / sizeof(widths[0]); pass++) {
        root->width = widths[pass];
        root->height = AUTO;
        ck_assert(layout_block_context(root, 500, &content));
        int expected = mode ? empty_inline_cases[variant].height : 0;
        ck_assert_msg(ic->height == expected && root->height == expected,
            "mode %d, font %d, line %d, width %d: expected %d; inline %d, block %d", mode,
            empty_inline_cases[variant].font, empty_inline_cases[variant].line, widths[pass], expected,
            ic->height, root->height);
        if (mode) {
            ck_assert_msg(start->y == empty_inline_cases[variant].y,
                "mode %d, variant %d, width %d: span y %d, expected %d", mode, variant, widths[pass],
                start->y, empty_inline_cases[variant].y);
        }
    }
    if (text)
        text->style = NULL; /* Shared with root. */
    free_tree(root);
    nsoption_set_int(font_min_size, minimum_font);
}
END_TEST

START_TEST(test_empty_marker_line)
{
    html_content content = {
        .unit_len_ctx = {.viewport_width = INTTOFIX(200), .viewport_height = INTTOFIX(500),
            .font_size_default = INTTOFIX(16), .device_dpi = INTTOFIX(96)},
        .font_func = &scaled_fonts,
    };
    struct box *root = make_box(BOX_BLOCK, 24, CSS_VERTICAL_ALIGN_BASELINE);
    struct box *item = root, *tail = NULL, *floating = NULL;
    if (_i != 0) {
        item = make_box(BOX_BLOCK, 24, CSS_VERTICAL_ALIGN_BASELINE);
        style_set_margins(item->style, 13, 0, 17, 0);
        append(root, item);
        tail = make_box(BOX_BLOCK, 24, CSS_VERTICAL_ALIGN_BASELINE);
        style_set_height_px(tail->style, 8);
        /* Intrinsic layout normally records this before block layout. */
        tail->flags |= HAS_HEIGHT | MAKE_HEIGHT;
        append(root, tail);
    }
    if (_i == 2) {
        struct box *ic = make_box(BOX_INLINE_CONTAINER, 0, 0);
        floating = make_box(BOX_FLOAT_LEFT, 0, 0);
        struct box *child = make_box(BOX_BLOCK, 24, CSS_VERTICAL_ALIGN_BASELINE);
        style_set_width(child->style, 40, false);
        style_set_height_px(child->style, 48);
        child->max_width = 40;
        append(item, ic);
        append(ic, floating);
        append(floating, child);
    }
    struct box *marker = make_box(BOX_BLOCK, 24, CSS_VERTICAL_ALIGN_BASELINE);
    item->list_marker = marker;
    marker->parent = item;
    marker->text = "1.";
    marker->length = 2;
    marker->height = 24;
    const int sizes[] = {32, 12, 16, 32};
    for (size_t pass = 0; pass < sizeof(sizes) / sizeof(sizes[0]); pass++) {
        style_set_inline_metrics(marker->style, sizes[pass], 24, CSS_VERTICAL_ALIGN_BASELINE);
        root->height = AUTO;
        ck_assert(layout_block_context(root, 500, &content));
        int push = sizes[pass] == 32 ? 6 : 0;
        ck_assert_msg(item->height == 24 + push && (item->flags & HAS_BASELINE) &&
                item->baseline == 18 + push,
            "variant %d font %d: height %d baseline %d", _i, sizes[pass], item->height, item->baseline);
        if (tail) {
            ck_assert_msg(item->y == 13 && tail->y == 13 + item->height + 17,
                "variant %d font %d: item y %d, tail y %d", _i, sizes[pass], item->y, tail->y);
        }
        if (floating) {
            ck_assert_msg(floating->y == item->y + push,
                "font %d: float y %d, item y %d push %d", sizes[pass], floating->y, item->y, push);
        }
    }
    free_tree(root);
}
END_TEST

/* Block-only fallback supplies height without inventing a line baseline or
 * moving children. Explicit constraints still use the normal sizing path. */
static const struct {
    const char *name;
    int child_height, top, bottom, padding;
    enum { MARKER_AUTO, MARKER_FIXED, MARKER_MIN, MARKER_MAX } constraint;
    int item_top, item_height, child_y, after_y;
} block_marker_cases[] = {
    {"empty", 0, 0, 0, 0, MARKER_AUTO, 13, 24, 0, 54},
    {"short", 20, 0, 0, 0, MARKER_AUTO, 13, 24, 0, 54},
    {"tall", 60, 0, 0, 0, MARKER_AUTO, 13, 60, 0, 90},
    {"margins", 0, 11, 17, 0, MARKER_AUTO, 17, 24, 0, 58},
    {"large margins", 0, 70, 90, 0, MARKER_AUTO, 90, 24, 0, 204},
    {"negative margins", 0, -23, -29, 0, MARKER_AUTO, -16, 24, 0, -4},
    {"fixed", 0, 0, 0, 0, MARKER_FIXED, 13, 10, 0, 40},
    {"minimum", 0, 0, 0, 0, MARKER_MIN, 13, 60, 0, 90},
    {"maximum", 0, 0, 0, 0, MARKER_MAX, 13, 10, 0, 40},
    {"padding", 0, 11, 17, 5, MARKER_AUTO, 13, 19, 16, 54},
    /* -1 requests real inline text, whose baseline aligns the marker instead. */
    {"text", -1, 0, 0, 0, MARKER_AUTO, 13, 30, 6, 60},
};
enum { BLOCK_MARKER_CASE_COUNT = sizeof(block_marker_cases) / sizeof(block_marker_cases[0]) };

START_TEST(test_block_marker_fallback)
{
    int variant = _i % BLOCK_MARKER_CASE_COUNT;
    bool wrapped = _i >= BLOCK_MARKER_CASE_COUNT;
    html_content content = {
        .unit_len_ctx = {.viewport_width = INTTOFIX(200), .viewport_height = INTTOFIX(500),
            .font_size_default = INTTOFIX(16), .device_dpi = INTTOFIX(96)},
        .font_func = &scaled_fonts,
    };
    struct box *root = make_box(BOX_BLOCK, 24, CSS_VERTICAL_ALIGN_BASELINE);
    struct box *wrapper = wrapped ? make_box(BOX_BLOCK, 24, CSS_VERTICAL_ALIGN_BASELINE) : root;
    if (wrapped)
        append(root, wrapper);
    struct box *item = make_box(BOX_BLOCK, 24, CSS_VERTICAL_ALIGN_BASELINE);
    style_set_margins(item->style, 13, 0, 17, 0);
    style_set_padding_top(item->style, block_marker_cases[variant].padding);
    if (block_marker_cases[variant].constraint == MARKER_FIXED) {
        style_set_height_px(item->style, 10);
        item->flags |= MAKE_HEIGHT | HAS_HEIGHT;
    } else if (block_marker_cases[variant].constraint == MARKER_MIN) {
        style_set_min_height(item->style, 60);
    } else if (block_marker_cases[variant].constraint == MARKER_MAX) {
        style_set_max_height(item->style, 10);
    }
    append(wrapper, item);
    struct box *child = make_box(BOX_BLOCK, 24, CSS_VERTICAL_ALIGN_BASELINE);
    style_set_height_px(child->style, block_marker_cases[variant].child_height);
    style_set_margins(child->style, block_marker_cases[variant].top, 0, block_marker_cases[variant].bottom, 0);
    append(item, child);
    bool text = block_marker_cases[variant].child_height < 0;
    if (text) {
        struct box *ic = make_box(BOX_INLINE_CONTAINER, 0, 0);
        struct box *label = make_box(BOX_TEXT, 24, CSS_VERTICAL_ALIGN_BASELINE);
        label->text = "H";
        label->length = 1;
        label->width = UNKNOWN_WIDTH;
        append(child, ic);
        append(ic, label);
    } else if (block_marker_cases[variant].child_height > 0) {
        child->flags |= MAKE_HEIGHT;
    }
    if (text || block_marker_cases[variant].child_height > 0) {
        /* Match the intrinsic pass: content sets HAS_HEIGHT, not MAKE_HEIGHT. */
        root->flags |= HAS_HEIGHT;
        wrapper->flags |= HAS_HEIGHT;
        item->flags |= HAS_HEIGHT;
        child->flags |= HAS_HEIGHT;
    }
    struct box *marker = make_box(BOX_BLOCK, 24, CSS_VERTICAL_ALIGN_BASELINE);
    style_set_inline_metrics(marker->style, 32, 24, CSS_VERTICAL_ALIGN_BASELINE);
    marker->text = "1.";
    marker->length = 2;
    marker->height = 24;
    marker->parent = item;
    item->list_marker = marker;
    struct box *after = make_box(BOX_BLOCK, 24, CSS_VERTICAL_ALIGN_BASELINE);
    style_set_height_px(after->style, 8);
    after->flags |= MAKE_HEIGHT | HAS_HEIGHT;
    append(root, after);
    const int widths[] = {200, 320, 200};
    for (size_t pass = 0; pass < sizeof(widths) / sizeof(widths[0]); pass++) {
        root->width = widths[pass];
        root->height = AUTO;
        ck_assert(layout_block_context(root, 500, &content));
        int top = item->y + (wrapped ? wrapper->y : 0);
        ck_assert_msg(top == block_marker_cases[variant].item_top &&
                item->height == block_marker_cases[variant].item_height &&
                child->y == block_marker_cases[variant].child_y && after->y == block_marker_cases[variant].after_y &&
                root->height == block_marker_cases[variant].after_y + 8 &&
                !!(item->flags & HAS_BASELINE) == text && marker->y == 0,
            "%s wrapped=%d pass=%zu: item=%d/%d expected %d/%d; child=%d expected %d; "
            "after=%d expected %d; root=%d; baseline=%d marker=%d",
            block_marker_cases[variant].name, wrapped, pass, top, item->height,
            block_marker_cases[variant].item_top, block_marker_cases[variant].item_height,
            child->y, block_marker_cases[variant].child_y, after->y, block_marker_cases[variant].after_y,
            root->height, !!(item->flags & HAS_BASELINE), marker->y);
    }
    free_tree(root);
}
END_TEST

START_TEST(test_outside_marker_baseline)
{
    html_content content = {
        .unit_len_ctx = {.viewport_width = INTTOFIX(200), .viewport_height = INTTOFIX(500),
            .font_size_default = INTTOFIX(16), .device_dpi = INTTOFIX(96)},
        .font_func = &scaled_fonts,
    };
    struct box *root = make_box(BOX_BLOCK, 24, CSS_VERTICAL_ALIGN_BASELINE);
    struct box *marker = make_box(BOX_BLOCK, 24, CSS_VERTICAL_ALIGN_BASELINE);
    root->list_marker = marker;
    marker->parent = root;
    marker->text = "1.";
    marker->length = 2;
    marker->height = 24;
    struct box *parent = root;
    if (_i != 0) {
        parent = make_box(BOX_BLOCK, 24, CSS_VERTICAL_ALIGN_BASELINE);
        append(root, parent);
        if (_i == 2) {
            style_set_padding_top(parent->style, 6);
            style_set_border_top(parent->style, 2);
        }
    }
    struct box *ic = make_box(BOX_INLINE_CONTAINER, 0, 0);
    struct box *text = make_box(BOX_TEXT, 24, CSS_VERTICAL_ALIGN_BASELINE);
    text->text = "Ag";
    text->length = 2;
    text->width = UNKNOWN_WIDTH;
    append(parent, ic);
    append(ic, text);

    /* Reflow both directions: cached first baselines must not retain the push
     * from an earlier, larger marker. The mock font has 7/8 ascent, 1/8 descent. */
    const int sizes[] = {32, 12, 16, 32};
    for (size_t pass = 0; pass < sizeof(sizes) / sizeof(sizes[0]); pass++) {
        style_set_inline_metrics(marker->style, sizes[pass], 24, CSS_VERTICAL_ALIGN_BASELINE);
        root->height = AUTO;
        ck_assert(layout_block_context(root, 500, &content));
        int marker_ascent = (24 + sizes[pass] * 3 / 4) / 2;
        int text_baseline = ic->y + text->y + 18 + (parent == root ? 0 : parent->y);
        ck_assert_msg(marker->y + marker_ascent == text_baseline,
            "nesting %d, font %d: marker baseline %d, text baseline %d",
            _i, sizes[pass], marker->y + marker_ascent, text_baseline);
        int expected_height = _i == 2 ? 32 : (sizes[pass] == 32 ? 30 : 24);
        ck_assert_msg(root->height == expected_height,
            "nesting %d, font %d: height %d, expected %d", _i, sizes[pass], root->height, expected_height);
    }
    free_tree(root);
}
END_TEST

START_TEST(test_inline_block_alignment)
{
    static const struct {
        const char *name;
        uint8_t align;
        int short_y, height;
    } cases[] = {
        {"baseline", CSS_VERTICAL_ALIGN_BASELINE, 40, 84},
        {"top", CSS_VERTICAL_ALIGN_TOP, 0, 80},
        {"bottom", CSS_VERTICAL_ALIGN_BOTTOM, 40, 80},
        {"middle", CSS_VERTICAL_ALIGN_MIDDLE, 20, 80},
    };
    html_content content = {
        .unit_len_ctx = {.viewport_width = INTTOFIX(200), .viewport_height = INTTOFIX(500),
            .font_size_default = INTTOFIX(16), .device_dpi = INTTOFIX(96)},
        .font_func = &fonts,
    };
    struct box *root = make_box(BOX_BLOCK, 20, CSS_VERTICAL_ALIGN_BASELINE);
    struct box *ic = make_box(BOX_INLINE_CONTAINER, 0, 0);
    append(root, ic);
    struct box *blocks[2];
    for (int i = 0; i < 2; i++) {
        blocks[i] = make_box(BOX_INLINE_BLOCK, 20, cases[_i].align);
        style_set_width(blocks[i]->style, 20, false);
        style_set_height_px(blocks[i]->style, i == 0 ? 80 : 40);
        blocks[i]->max_width = 20;
        append(ic, blocks[i]);
    }
    ck_assert(layout_block_context(root, 500, &content));
    ck_assert_msg(blocks[0]->y == 0 && blocks[1]->y == cases[_i].short_y &&
            ic->height == cases[_i].height,
        "%s: tall y=%d, short y=%d, line height=%d", cases[_i].name,
        blocks[0]->y, blocks[1]->y, ic->height);
    free_tree(root);
}
END_TEST

START_TEST(test_inline_block_last_baseline)
{
    html_content content = {
        .unit_len_ctx = {.viewport_width = INTTOFIX(200), .viewport_height = INTTOFIX(500),
            .font_size_default = INTTOFIX(16), .device_dpi = INTTOFIX(96)},
        .font_func = &fonts,
    };
    struct box *root = make_box(BOX_BLOCK, 20, CSS_VERTICAL_ALIGN_BASELINE);
    struct box *ic = make_box(BOX_INLINE_CONTAINER, 0, 0);
    append(root, ic);
    struct box *image = make_box(BOX_INLINE, 20, CSS_VERTICAL_ALIGN_BASELINE);
    image->flags |= REPLACE_DIM;
    style_set_width(image->style, 20, false);
    style_set_height_px(image->style, 100);
    append(ic, image);
    struct box *block = make_box(BOX_INLINE_BLOCK, 20, CSS_VERTICAL_ALIGN_BASELINE);
    style_set_width(block->style, 80, false);
    style_set_height_px(block->style, 80);
    style_set_padding_top(block->style, 5);
    style_set_border_top(block->style, 2);
    style_set_margins(block->style, 3, 0, 7, 0);
    block->max_width = 80;
    append(ic, block);
    struct box *last = NULL;
    for (int i = 0; i < 2; i++) {
        struct box *p = make_box(BOX_BLOCK, 20, CSS_VERTICAL_ALIGN_BASELINE);
        struct box *line = make_box(BOX_INLINE_CONTAINER, 0, 0);
        last = make_box(BOX_TEXT, 20, CSS_VERTICAL_ALIGN_BASELINE);
        last->text = "Ag";
        last->length = 2;
        last->width = UNKNOWN_WIDTH;
        append(block, p);
        append(p, line);
        append(line, last);
    }
    if (_i)
        style_set_overflow_hidden(block->style);
    /* Reflow must refresh the cached baseline when the last line changes. */
    const int heights[] = {20, 40, 20};
    for (size_t i = 0; i < sizeof(heights) / sizeof(heights[0]); i++) {
        style_set_inline_metrics(last->style, 16, heights[i], CSS_VERTICAL_ALIGN_BASELINE);
        root->height = AUTO;
        ck_assert(layout_block_context(root, 500, &content));
        int expected_baseline = image->y + image->height;
        int actual_baseline = _i ? block->y + block->padding[TOP] + block->height + block->margin[BOTTOM] :
            block->y + last->parent->parent->y + last->parent->y + last->y + (heights[i] + 12) / 2;
        ck_assert_msg(actual_baseline == expected_baseline,
            "overflow %s, line %d: baseline %d, expected %d", _i ? "hidden" : "visible", heights[i],
            actual_baseline, expected_baseline);
    }
    free_tree(root);
}
END_TEST

static const struct {
    const char *name;
    int parent_font, parent_line, child_font, child_line, height, child_y;
} text_cases[] = {
    {"parent descent, child ascent", 16, 28, 32, 32, 36, 0},
    {"zero parent line-height", 16, 0, 32, 32, 32, 0},
    {"same font and line-height", 32, 32, 32, 32, 32, 0},
    {"parent ascent, child descent", 32, 32, 16, 28, 36, 8},
    {"zero parent font", 0, 0, 32, 32, 32, 0},
    {"negative child leading", 32, 32, 16, 0, 32, 22},
};

START_TEST(test_mixed_text_baseline)
{
    html_content content = {
        .unit_len_ctx = {.viewport_width = INTTOFIX(200), .viewport_height = INTTOFIX(500),
            .font_size_default = INTTOFIX(16), .device_dpi = INTTOFIX(96)},
        .font_func = &scaled_fonts,
    };
    struct box *root = make_box(BOX_BLOCK, text_cases[_i].parent_line, CSS_VERTICAL_ALIGN_BASELINE);
    style_set_inline_metrics(root->style, text_cases[_i].parent_font, text_cases[_i].parent_line,
        CSS_VERTICAL_ALIGN_BASELINE);
    struct box *ic = make_box(BOX_INLINE_CONTAINER, 0, 0);
    append(root, ic);
    struct box *child = make_box(BOX_TEXT, text_cases[_i].child_line, CSS_VERTICAL_ALIGN_BASELINE);
    style_set_inline_metrics(child->style, text_cases[_i].child_font, text_cases[_i].child_line,
        CSS_VERTICAL_ALIGN_BASELINE);
    child->text = "Ag";
    child->length = 2;
    child->width = UNKNOWN_WIDTH;
    append(ic, child);

    /* The invisible parent strut must suffice; adding actual parent-sized text
     * must not change the line height or the child's baseline. */
    for (int with_parent_text = 0; with_parent_text < 2; with_parent_text++) {
        struct box *parent_text = NULL;
        if (with_parent_text) {
            parent_text = make_box(BOX_TEXT, text_cases[_i].parent_line, CSS_VERTICAL_ALIGN_BASELINE);
            style_set_inline_metrics(parent_text->style, text_cases[_i].parent_font, text_cases[_i].parent_line,
                CSS_VERTICAL_ALIGN_BASELINE);
            parent_text->text = "Ag";
            parent_text->length = 2;
            parent_text->width = UNKNOWN_WIDTH;
            append(ic, parent_text);
        }
        root->height = AUTO;
        ck_assert(layout_block_context(root, 500, &content));
        ck_assert_msg(root->height == text_cases[_i].height && ic->height == text_cases[_i].height &&
                child->y == text_cases[_i].child_y,
            "%s, parent text %d: root height %d, line height %d, child y %d; expected %d, %d",
            text_cases[_i].name, with_parent_text, root->height, ic->height, child->y,
            text_cases[_i].height, text_cases[_i].child_y);
        if (parent_text) {
            int parent_baseline = parent_text->y +
                (text_cases[_i].parent_line + text_cases[_i].parent_font * 3 / 4) / 2;
            int child_baseline = child->y +
                (text_cases[_i].child_line + text_cases[_i].child_font * 3 / 4) / 2;
            ck_assert_msg(parent_baseline == child_baseline, "%s: parent baseline %d, child baseline %d",
                text_cases[_i].name, parent_baseline, child_baseline);
        }
    }
    free_tree(root);
}
END_TEST

static int baseline_queries, child_ascent;

static nserror changing_metrics(const struct plot_font_style *style, int *ascent, int *descent)
{
    baseline_queries++;
    if (style->size >= 24 * PLOT_STYLE_SCALE) {
        *ascent = child_ascent;
        *descent = style->size * 4 / (PLOT_STYLE_SCALE * 3) - child_ascent;
    } else {
        *ascent = 14;
        *descent = 2;
    }
    return NSERROR_OK;
}

START_TEST(test_repeated_text_baselines)
{
    const struct gui_layout_table changing_fonts = {.width = width, .font_metrics = changing_metrics};
    html_content content = {
        .unit_len_ctx = {.viewport_width = INTTOFIX(200), .viewport_height = INTTOFIX(500),
            .font_size_default = INTTOFIX(16), .device_dpi = INTTOFIX(96)},
        .font_func = &changing_fonts,
    };
    struct box *root = make_box(BOX_BLOCK, 21, CSS_VERTICAL_ALIGN_BASELINE);
    struct box *ic = make_box(BOX_INLINE_CONTAINER, 0, 0);
    append(root, ic);
    struct box *image = make_box(BOX_INLINE, 21, CSS_VERTICAL_ALIGN_BASELINE);
    image->flags |= REPLACE_DIM;
    style_set_width(image->style, 20, false);
    style_set_height_px(image->style, 80);
    append(ic, image);

    enum { FRAGMENT_COUNT = 6 };
    struct box *fragments[FRAGMENT_COUNT];
    for (size_t i = 0; i < FRAGMENT_COUNT; i++) {
        fragments[i] = make_box(BOX_TEXT, 33, CSS_VERTICAL_ALIGN_BASELINE);
        if (i == 0) {
            style_set_inline_metrics(fragments[i]->style, 32, 33, CSS_VERTICAL_ALIGN_BASELINE);
        } else if (!_i) {
            /* Retained fragments of one text run share its computed style. */
            destroy_mock_style(fragments[i]->style);
            fragments[i]->style = fragments[0]->style;
        } else {
            /* More distinct styles than fit in the cache must remain correct. */
            style_set_inline_metrics(fragments[i]->style, 32 + 2 * i, 33, CSS_VERTICAL_ALIGN_BASELINE);
        }
        fragments[i]->text = "Ag";
        fragments[i]->length = 2;
        fragments[i]->width = UNKNOWN_WIDTH;
        append(ic, fragments[i]);
    }

    const int ascents[] = {28, 18, 28};
    for (size_t pass = 0; pass < sizeof(ascents) / sizeof(ascents[0]); pass++) {
        /* A newly available font may change metrics without changing styles. */
        child_ascent = ascents[pass];
        baseline_queries = 0;
        root->height = AUTO;
        ck_assert(layout_block_context(root, 500, &content));
        int child_baseline = (33 + child_ascent - (32 - child_ascent)) / 2;
        int last_baseline = child_baseline - (_i ? FRAGMENT_COUNT - 1 : 0);
        ck_assert_msg(image->y == 0 && ic->height == 80 + 33 - last_baseline,
            "distinct styles %d, pass %zu: image y=%d, line height=%d", _i, pass, image->y, ic->height);
        for (size_t i = 0; i < FRAGMENT_COUNT; i++) {
            int baseline = child_baseline - (_i ? i : 0);
            ck_assert_msg(fragments[i]->y + baseline == 80,
                "distinct styles %d, pass %zu, fragment %zu: baseline=%d", _i, pass, i,
                fragments[i]->y + baseline);
        }
        if (!_i) {
            ck_assert_msg(baseline_queries == 2,
                "pass %zu: expected one metric query per style, got %d", pass, baseline_queries);
        }
    }
    if (!_i) {
        for (size_t i = 1; i < FRAGMENT_COUNT; i++)
            fragments[i]->style = NULL; /* The test helper owns the shared style only once. */
    }
    free_tree(root);
}
END_TEST

enum { NESTED_FLOAT_DEPTH = 4 };

START_TEST(test_nested_float_layout_once)
{
    int depth = _i % (NESTED_FLOAT_DEPTH + 1);
    bool right = _i > NESTED_FLOAT_DEPTH;
    const struct gui_layout_table counting_fonts = {.width = width, .font_metrics = changing_metrics};
    html_content content = {
        .unit_len_ctx = {.viewport_width = INTTOFIX(400), .viewport_height = INTTOFIX(500),
            .font_size_default = INTTOFIX(16), .device_dpi = INTTOFIX(96)},
        .font_func = &counting_fonts,
    };
    struct box *root = make_box(BOX_BLOCK, 20, CSS_VERTICAL_ALIGN_BASELINE);
    struct box *parent = root;
    struct box *floats[NESTED_FLOAT_DEPTH];
    for (int i = 0; i < depth; i++) {
        struct box *ic = make_box(BOX_INLINE_CONTAINER, 0, 0);
        append(parent, ic);
        floats[i] = make_box(right ? BOX_FLOAT_RIGHT : BOX_FLOAT_LEFT, 0, 0);
        destroy_mock_style(floats[i]->style);
        floats[i]->style = NULL;
        append(ic, floats[i]);
        parent = make_box(BOX_BLOCK, 20, CSS_VERTICAL_ALIGN_BASELINE);
        style_set_width(parent->style, 50, true);
        append(floats[i], parent);
    }
    struct box *ic = make_box(BOX_INLINE_CONTAINER, 0, 0);
    append(parent, ic);
    struct box *text = make_box(BOX_TEXT, 20, CSS_VERTICAL_ALIGN_BASELINE);
    text->text = "x";
    text->length = 1;
    text->width = UNKNOWN_WIDTH;
    append(ic, text);

    const int widths[] = {400, 160, 320, 400};
    for (size_t pass = 0; pass < sizeof(widths) / sizeof(widths[0]); pass++) {
        root->width = widths[pass];
        root->height = AUTO;
        baseline_queries = 0;
        ck_assert(layout_block_context(root, 500, &content));
        /* The leaf line needs its parent and text metrics once each, however
         * deeply nested the floats are. Resizing must still lay it out anew. */
        ck_assert_msg(baseline_queries == 2,
            "depth %d, right %d, width %d: expected 2 metric queries, got %d",
            depth, right, root->width, baseline_queries);
        ck_assert_int_eq(root->height, 20);
        ck_assert_int_eq(text->y, 0);
        int available = root->width;
        for (int i = 0; i < depth; i++) {
            int expected_width = available / 2;
            ck_assert_msg(floats[i]->width == expected_width && floats[i]->height == 20 &&
                    floats[i]->x == (right ? available - expected_width : 0) && floats[i]->y == 0,
                "depth %d, level %d, right %d, width %d: float=(%d,%d %dx%d)", depth, i, right,
                root->width, floats[i]->x, floats[i]->y, floats[i]->width, floats[i]->height);
            available = expected_width;
        }
    }
    free_tree(root);
}
END_TEST

START_TEST(test_font_baseline_fallback)
{
    plot_font_style_t style = {.size = 12 * PLOT_STYLE_SCALE};
    struct gui_layout_table missing = {0}, failed = {.font_metrics = failed_metrics};
    ck_assert_int_eq(font_baseline(&fonts, &style, 20), 16);
    ck_assert_int_eq(font_baseline(&fonts, &style, 0), 6);
    ck_assert_int_eq(font_baseline(&missing, &style, 20), 14);
    ck_assert_int_eq(font_baseline(&failed, &style, 20), 14);
    style.size = 0;
    ck_assert_int_eq(font_baseline(&fonts, &style, 0), 0);
    ck_assert_int_eq(font_baseline(&fonts, &style, 20), 10);
}
END_TEST

static const struct {
    const char *name;
    int height, top, bottom, exclusion;
} float_outer_height_cases[] = {
    {"empty", 0, 0, 0, 0},
    {"cancelled height", 20, 0, -20, 0},
    {"negative outer height", 20, 0, -30, 0},
    {"offset empty", 0, 10, -10, 0},
    {"positive height", 1, 0, 0, 40},
    {"positive margin", 0, 10, 0, 40},
};
enum { FLOAT_OUTER_HEIGHT_COUNT = sizeof(float_outer_height_cases) / sizeof(float_outer_height_cases[0]) };

START_TEST(test_float_outer_height_exclusion)
{
    int variant = _i % FLOAT_OUTER_HEIGHT_COUNT;
    bool right = (_i / FLOAT_OUTER_HEIGHT_COUNT) % 2;
    bool prior = (_i / (2 * FLOAT_OUTER_HEIGHT_COUNT)) % 2;
    bool atomic = _i >= 4 * FLOAT_OUTER_HEIGHT_COUNT;
    const struct gui_layout_table boundary_fonts = {.width = width, .split = split_word, .font_metrics = metrics};
    html_content content = {
        .unit_len_ctx = {.viewport_width = INTTOFIX(240),
            .viewport_height = INTTOFIX(500),
            .font_size_default = INTTOFIX(16),
            .device_dpi = INTTOFIX(96)},
        .font_func = &boundary_fonts,
    };
    int line_height = prior ? 80 : 20;
    struct box *root = make_box(BOX_BLOCK, line_height, CSS_VERTICAL_ALIGN_BASELINE);
    uint8_t align = right ? CSS_TEXT_ALIGN_RIGHT : CSS_TEXT_ALIGN_LEFT;
    style_set_text_alignment(root->style, align, CSS_DIRECTION_LTR);
    struct box *ic = make_box(BOX_INLINE_CONTAINER, 0, 0);
    append(root, ic);
    struct box *floating = NULL;
    for (int i = 0; i <= prior; i++) {
        floating = make_box(right ? BOX_FLOAT_RIGHT : BOX_FLOAT_LEFT, 0, 0);
        struct box *child = make_box(BOX_BLOCK, 20, CSS_VERTICAL_ALIGN_BASELINE);
        bool tested = i == prior;
        style_set_width(child->style, tested && prior ? 160 : 40, false);
        style_set_height_px(child->style, tested ? float_outer_height_cases[variant].height : 40);
        style_set_clear(child->style, tested && prior ? CSS_CLEAR_BOTH : CSS_CLEAR_NONE);
        if (tested)
            style_set_margins(
                child->style, float_outer_height_cases[variant].top, 0, float_outer_height_cases[variant].bottom, 0);
        child->flags |= HAS_HEIGHT | MAKE_HEIGHT;
        append(floating, child);
        append(ic, floating);
    }
    if (prior) {
        /* A preceding cleared float lies inside this tall line's vertical
         * range. Zero/negative outer heights still have no exclusion area. */
        struct box *block = make_box(BOX_BLOCK, line_height, CSS_VERTICAL_ALIGN_BASELINE);
        style_set_text_alignment(block->style, align, CSS_DIRECTION_LTR);
        append(root, block);
        ic = make_box(BOX_INLINE_CONTAINER, 0, 0);
        append(block, ic);
    }
    struct box *target = make_box(atomic ? BOX_INLINE_BLOCK : BOX_TEXT, line_height, CSS_VERTICAL_ALIGN_TOP);
    if (atomic) {
        style_set_width(target->style, 40, false);
        style_set_height_px(target->style, line_height);
        target->flags |= HAS_HEIGHT | MAKE_HEIGHT;
    } else {
        target->text = "XXXX";
        target->length = 4;
        target->width = UNKNOWN_WIDTH;
    }
    append(ic, target);
    const int widths[] = {200, 240, 200};
    for (size_t pass = 0; pass < sizeof(widths) / sizeof(widths[0]); pass++) {
        root->width = widths[pass];
        root->height = AUTO;
        ck_assert(layout_block_context(root, 500, &content));
        int exclusion = float_outer_height_cases[variant].exclusion;
        if (prior)
            exclusion = exclusion ? 160 : 40;
        int x = right ? root->width - exclusion - (atomic ? 40 : 32) : exclusion;
        ck_assert_msg(target->x == x && target->y == 0 && ic->height == line_height,
            "%s right=%d prior=%d atomic=%d width=%d: expected x=%d y=0 line=%d, got %d/%d/%d",
            float_outer_height_cases[variant].name, right, prior, atomic, root->width, x, line_height, target->x,
            target->y, ic->height);
        ck_assert_msg(floating->height ==
                    float_outer_height_cases[variant].height + float_outer_height_cases[variant].top +
                        float_outer_height_cases[variant].bottom &&
                floating->y == (prior ? 40 : 0),
            "%s: exclusion must not change the float's outer geometry", float_outer_height_cases[variant].name);
    }
    free_tree(root);
}
END_TEST

static const struct {
    const char *name;
    int left_width, left_height, right_width, right_height, word_width, x, y;
} float_boundary_cases[] = {
    {"left", 180, 40, 0, 0, 48, 0, 40},
    {"right", 0, 0, 180, 40, 48, 0, 40},
    {"shorter left", 90, 40, 90, 80, 48, 0, 40},
    {"shorter right", 90, 80, 90, 40, 48, 90, 40},
    {"both needed", 90, 40, 90, 80, 144, 0, 80},
    {"equal bottoms", 90, 40, 90, 40, 48, 0, 40},
    {"exact fit", 152, 40, 0, 0, 48, 152, 0},
    {"oversized word", 180, 40, 0, 0, 240, 0, 40},
    {"oversized without floats", 0, 0, 0, 0, 240, 0, 0},
};
enum {
    FLOAT_BOUNDARY_CASE_COUNT = sizeof(float_boundary_cases) / sizeof(float_boundary_cases[0]),
    FLOAT_BOUNDARY_MAX_WRAPPERS = 2,
};

START_TEST(test_line_at_float_boundary)
{
    int variant = _i % FLOAT_BOUNDARY_CASE_COUNT;
    bool preceding_container = (_i / FLOAT_BOUNDARY_CASE_COUNT) % 2;
    int wrappers = (_i / (2 * FLOAT_BOUNDARY_CASE_COUNT)) % (FLOAT_BOUNDARY_MAX_WRAPPERS + 1);
    bool decorated = _i >= 2 * FLOAT_BOUNDARY_CASE_COUNT * (FLOAT_BOUNDARY_MAX_WRAPPERS + 1);
    const struct gui_layout_table boundary_fonts = {.width = width, .split = split_word, .font_metrics = metrics};
    html_content content = {
        .unit_len_ctx = {.viewport_width = INTTOFIX(600), .viewport_height = INTTOFIX(500),
            .font_size_default = INTTOFIX(16), .device_dpi = INTTOFIX(96)},
        .font_func = &boundary_fonts,
    };
    struct box *root = make_box(BOX_BLOCK, 20, CSS_VERTICAL_ALIGN_BASELINE);
    struct box *ic = make_box(BOX_INLINE_CONTAINER, 0, 0);
    append(root, ic);
    for (int side = 0; side < 2; side++) {
        int w = side ? float_boundary_cases[variant].right_width : float_boundary_cases[variant].left_width;
        int h = side ? float_boundary_cases[variant].right_height : float_boundary_cases[variant].left_height;
        if (w == 0)
            continue;
        struct box *floating = make_box(side ? BOX_FLOAT_RIGHT : BOX_FLOAT_LEFT, 20, CSS_VERTICAL_ALIGN_BASELINE);
        struct box *child = make_box(BOX_BLOCK, 20, CSS_VERTICAL_ALIGN_BASELINE);
        style_set_width(child->style, w, false);
        style_set_height_px(child->style, h);
        style_set_clear(child->style, CSS_CLEAR_NONE);
        child->flags |= HAS_HEIGHT | MAKE_HEIGHT;
        append(floating, child);
        append(ic, floating);
    }
    if (preceding_container) {
        struct box *block = make_box(BOX_BLOCK, 20, CSS_VERTICAL_ALIGN_BASELINE);
        append(root, block);
        ic = make_box(BOX_INLINE_CONTAINER, 0, 0);
        append(block, ic);
    }
    struct box *text = make_box(BOX_TEXT, 20, CSS_VERTICAL_ALIGN_BASELINE);
    text->text = "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXX";
    text->length = float_boundary_cases[variant].word_width / 8;
    text->width = UNKNOWN_WIDTH;
    struct box *starts[FLOAT_BOUNDARY_MAX_WRAPPERS];
    for (int i = 0; i < wrappers; i++) {
        starts[i] = make_box(BOX_INLINE, 20, CSS_VERTICAL_ALIGN_BASELINE);
        if (decorated)
            style_set_horizontal_edges(starts[i]->style, 5, 0);
        append(ic, starts[i]);
    }
    append(ic, text);
    for (int i = wrappers - 1; i >= 0; i--) {
        struct box *end = make_box(BOX_INLINE_END, 20, CSS_VERTICAL_ALIGN_BASELINE);
        starts[i]->inline_end = end;
        end->inline_end = starts[i];
        append(ic, end);
    }
    const int widths[] = {200, 600, 200};
    for (size_t pass = 0; pass < sizeof(widths) / sizeof(widths[0]); pass++) {
        root->width = widths[pass];
        root->height = AUTO;
        ck_assert(layout_block_context(root, 500, &content));
        int x = pass == 1 ? float_boundary_cases[variant].left_width : float_boundary_cases[variant].x;
        int y = pass == 1 ? 0 : float_boundary_cases[variant].y;
        if (decorated && wrappers) {
            /* The bare word fits exactly beside this float; its decorations
             * make the complete run too wide, so all of it must move below. */
            if (pass != 1 && float_boundary_cases[variant].left_width == 152) {
                x = 0;
                y = 40;
            }
            x += wrappers * 5;
        }
        ck_assert_msg(text->x == x && text->y == y && ic->height == y + 20,
            "%s (preceding container=%d, wrappers=%d, decorated=%d), width %d: "
            "expected text %d/%d, line bottom %d; got %d/%d, %d",
            float_boundary_cases[variant].name, preceding_container, wrappers, decorated, widths[pass], x, y, y + 20,
            text->x, text->y, ic->height);
    }
    free_tree(root);
}
END_TEST

static const struct {
    const char *name, *prefix, *inside, *suffix;
    bool space_before, space_inside;
    int minimum, maximum;
} intrinsic_run_cases[] = {
    {"one word", NULL, "AAAAAAAAAA", NULL, false, false, 90, 90},
    {"joined prefix", "AAAAA", "AAAAA", NULL, false, false, 90, 90},
    {"joined last word", "AA BBB", "CCC", NULL, false, false, 58, 82},
    {"internal space", NULL, "AA BBBB", NULL, false, false, 37, 66},
    {"joined suffix", NULL, "AAAA", "BBBB", false, false, 74, 74},
    {"internal space and suffix", NULL, "AA BBBB", "CCC", false, false, 61, 90},
    {"space before opening", "AA", "BBBB", NULL, true, false, 42, 66},
    {"space before closing", NULL, "AAAA", "CCC", false, true, 42, 74},
    {"trailing space", NULL, "AAAA", NULL, false, true, 42, 42},
    {"empty span", NULL, NULL, NULL, false, false, 10, 10},
};

static void append_intrinsic_text(struct box *ic, const char *value, bool space)
{
    struct box *text = make_box(BOX_TEXT, 20, CSS_VERTICAL_ALIGN_BASELINE);
    text->text = (char *)value;
    text->length = strlen(value);
    text->width = UNKNOWN_WIDTH;
    text->space = space ? UNKNOWN_WIDTH : 0;
    append(ic, text);
}

/* Exercise the shared outer-size contract through block, flex and grid entry
 * points. Cached child contributions isolate container constraints from tracks. */
static const struct {
    const char *name;
    int preferred, minimum, maximum, content_min, content_max;
    bool border_box;
    int padding, border, margin;
    int normal_min, normal_max, flex_min, flex_max;
} intrinsic_container_cases[] = {
    {"empty fixed", 40, 0, -1, 0, 0, false, 0, 0, 0, 40, 40, 40, 40},
    {"preferred", 40, 0, -1, 30, 60, false, 0, 0, 0, 40, 40, 40, 60},
    {"auto", -1, 0, -1, 30, 60, false, 0, 0, 0, 30, 60, 30, 60},
    {"empty minimum", -1, 60, -1, 0, 0, false, 0, 0, 0, 60, 60, 60, 60},
    {"maximum", -1, 0, 40, 50, 80, false, 0, 0, 0, 40, 40, 40, 40},
    {"minimum wins", -1, 80, 40, 30, 60, false, 0, 0, 0, 80, 80, 80, 80},
    {"fixed minimum wins", 20, 80, 40, 30, 60, false, 0, 0, 0, 80, 80, 80, 80},
    {"content edges", 40, 0, -1, 0, 0, false, 3, 2, 7, 64, 64, 64, 64},
    {"border edges", 40, 0, -1, 0, 0, true, 3, 2, 7, 54, 54, 54, 54},
    {"border floor", 2, 0, -1, 0, 0, true, 3, 2, 0, 10, 10, 10, 10},
    {"border minimum", -1, 60, -1, 55, 55, true, 3, 2, 0, 65, 65, 65, 65},
    {"border maximum", -1, 0, 60, 55, 55, true, 3, 2, 0, 60, 60, 60, 60},
    {"border preferred floor", 60, 0, -1, 55, 55, true, 3, 2, 0, 60, 60, 65, 65},
    {"border conflict", -1, 80, 40, 30, 60, true, 3, 2, 0, 80, 80, 80, 80},
};
#define INTRINSIC_CONTAINER_CASE_COUNT (sizeof(intrinsic_container_cases) / sizeof(intrinsic_container_cases[0]))

static const struct {
    box_type type;
    bool flex_item;
} intrinsic_container_modes[] = {
    {BOX_INLINE_BLOCK, false},
    {BOX_INLINE_FLEX, false},
    {BOX_INLINE_GRID, false},
    {BOX_GRID, false},
    {BOX_BLOCK, true},
    {BOX_FLEX, true},
    {BOX_GRID, true},
};
#define INTRINSIC_CONTAINER_MODE_COUNT (sizeof(intrinsic_container_modes) / sizeof(intrinsic_container_modes[0]))

START_TEST(test_intrinsic_container_constraints)
{
    const size_t index = _i % INTRINSIC_CONTAINER_CASE_COUNT;
    const unsigned mode = _i / INTRINSIC_CONTAINER_CASE_COUNT;
    const bool flex_item = intrinsic_container_modes[mode].flex_item;
    const box_type type = intrinsic_container_modes[mode].type;
    html_content content = {.unit_len_ctx = {.device_dpi = INTTOFIX(96), .font_size_default = INTTOFIX(16)}};
    struct box parent = {.type = BOX_FLEX};
    struct box *box = make_box(type, 20, CSS_VERTICAL_ALIGN_BASELINE);
    if (type == BOX_FLEX || type == BOX_INLINE_FLEX)
        style_set_flex_or_grid(box->style, false);
    if (type == BOX_GRID || type == BOX_INLINE_GRID)
        style_set_flex_or_grid(box->style, true);
    box->max_width = UNKNOWN_MAX_WIDTH;
    if (flex_item)
        box->parent = &parent;
    style_set_width(box->style, intrinsic_container_cases[index].preferred, false);
    style_set_min_width(box->style, intrinsic_container_cases[index].minimum);
    style_set_max_width(box->style, intrinsic_container_cases[index].maximum);
    style_set_box_sizing(box->style,
        intrinsic_container_cases[index].border_box ? CSS_BOX_SIZING_BORDER_BOX : CSS_BOX_SIZING_CONTENT_BOX);
    style_set_horizontal_edges(
        box->style, intrinsic_container_cases[index].padding, intrinsic_container_cases[index].border);
    style_set_margins(
        box->style, 0, intrinsic_container_cases[index].margin, 0, intrinsic_container_cases[index].margin);
    struct box *child = make_box(BOX_BLOCK, 20, CSS_VERTICAL_ALIGN_BASELINE);
    child->min_width.value = intrinsic_container_cases[index].content_min;
    child->max_width = intrinsic_container_cases[index].content_max;
    append(box, child);
    for (int run = 0; run < 2; run++) {
        ck_assert(layout_minmax_box(box, &fonts, &content));
        int expected_min = flex_item ? intrinsic_container_cases[index].flex_min
                                     : intrinsic_container_cases[index].normal_min;
        int expected_max = flex_item ? intrinsic_container_cases[index].flex_max
                                     : intrinsic_container_cases[index].normal_max;
        ck_assert_msg(box->min_width.value == expected_min && box->max_width == expected_max,
            "%s type=%d flex-item=%d run=%d: expected %d/%d, got %d/%d", intrinsic_container_cases[index].name, type,
            flex_item, run, expected_min, expected_max, box->min_width.value, box->max_width);
        ck_assert_int_eq(box->content_min_width, intrinsic_container_cases[index].content_min);
    }
    free_tree(box);
}
END_TEST

static const struct {
    const char *name;
    int preferred, minimum, maximum, content, padding, border;
    bool border_box, stretched;
    int expected;
} grid_height_cases[] = {
    {"empty fixed", 30, 0, -1, 0, 0, 0, false, false, 30},
    {"overflow fixed", 30, 0, -1, 60, 0, 0, false, false, 30},
    {"zero fixed", 0, 0, -1, 60, 0, 0, false, false, 0},
    {"auto content", -1, 0, -1, 30, 0, 0, false, false, 30},
    {"empty minimum", -1, 40, -1, 0, 0, 0, false, false, 40},
    {"auto maximum", -1, 0, 30, 60, 0, 0, false, false, 30},
    {"minimum wins", 20, 40, 10, 0, 0, 0, false, false, 40},
    {"border box", 30, 0, -1, 60, 3, 2, true, false, 20},
    {"border floor", 2, 0, -1, 0, 3, 2, true, false, 0},
    {"border minimum", -1, 30, -1, 0, 3, 2, true, false, 20},
    {"border maximum", -1, 0, 30, 60, 3, 2, true, false, 20},
    {"stretched auto", -1, 0, -1, 10, 0, 0, false, true, 70},
    {"flex overrides preferred", 20, 0, -1, 10, 0, 0, false, true, 70},
};
#define GRID_HEIGHT_CASE_COUNT (sizeof(grid_height_cases) / sizeof(grid_height_cases[0]))

START_TEST(test_grid_container_height)
{
    size_t index = _i % GRID_HEIGHT_CASE_COUNT;
    box_type type = _i < GRID_HEIGHT_CASE_COUNT ? BOX_GRID : BOX_INLINE_GRID;
    html_content content = {
        .unit_len_ctx = {.device_dpi = INTTOFIX(96), .font_size_default = INTTOFIX(16)}, .font_func = &fonts};
    struct box *grid = make_box(type, 20, CSS_VERTICAL_ALIGN_BASELINE);
    style_set_flex_or_grid(grid->style, true);
    style_set_height_px(grid->style, grid_height_cases[index].preferred);
    style_set_min_height(grid->style, grid_height_cases[index].minimum);
    style_set_max_height(grid->style, grid_height_cases[index].maximum);
    style_set_box_sizing(
        grid->style, grid_height_cases[index].border_box ? CSS_BOX_SIZING_BORDER_BOX : CSS_BOX_SIZING_CONTENT_BOX);
    style_set_padding_top(grid->style, grid_height_cases[index].padding);
    style_set_padding_bottom(grid->style, grid_height_cases[index].padding);
    style_set_border_top(grid->style, grid_height_cases[index].border);
    style_set_border_bottom(grid->style, grid_height_cases[index].border);
    struct box *child = make_box(BOX_BLOCK, 20, CSS_VERTICAL_ALIGN_BASELINE);
    style_set_width(child->style, 20, false);
    style_set_height_px(child->style, grid_height_cases[index].content);
    append(grid, child);
    /* Deliberately leave a previous used height in place: auto heights must
     * be remeasured, while an explicit parent-assigned stretch must survive. */
    grid->height = 70;
    for (int run = 0; run < 2; run++) {
        if (grid_height_cases[index].stretched)
            grid->flags |= HEIGHT_STRETCHED;
        ck_assert(layout_grid(grid, 200, &content));
        ck_assert_msg(grid->height == grid_height_cases[index].expected,
            "%s type=%d run=%d: expected height %d, got %d", grid_height_cases[index].name, type, run,
            grid_height_cases[index].expected, grid->height);
        ck_assert_int_eq(child->height, grid_height_cases[index].content);
    }
    if (!grid_height_cases[index].stretched && grid_height_cases[index].preferred < 0 &&
        grid_height_cases[index].minimum == 0) {
        grid->children = grid->last = NULL;
        ck_assert(layout_grid(grid, 200, &content));
        ck_assert_msg(grid->height == 0, "%s: removed contents left a stale auto height %d",
            grid_height_cases[index].name, grid->height);
        free_tree(child);
    }
    free_tree(grid);
}
END_TEST

START_TEST(test_inline_intrinsic_run)
{
    html_content content = {
        .unit_len_ctx = {.viewport_width = INTTOFIX(200), .viewport_height = INTTOFIX(500),
            .font_size_default = INTTOFIX(16), .device_dpi = INTTOFIX(96)},
        .font_func = &fonts,
    };
    struct box *root = make_box(BOX_INLINE_BLOCK, 20, CSS_VERTICAL_ALIGN_BASELINE);
    style_set_width(root->style, -1, false);
    root->max_width = UNKNOWN_MAX_WIDTH;
    struct box *ic = make_box(BOX_INLINE_CONTAINER, 0, 0);
    ic->max_width = UNKNOWN_MAX_WIDTH;
    append(root, ic);
    if (intrinsic_run_cases[_i].prefix)
        append_intrinsic_text(ic, intrinsic_run_cases[_i].prefix, intrinsic_run_cases[_i].space_before);
    struct box *opening = make_box(BOX_INLINE, 20, CSS_VERTICAL_ALIGN_BASELINE);
    style_set_horizontal_edges(opening->style, 5, 0);
    append(ic, opening);
    if (intrinsic_run_cases[_i].inside)
        append_intrinsic_text(ic, intrinsic_run_cases[_i].inside, intrinsic_run_cases[_i].space_inside);
    struct box *closing = make_box(BOX_INLINE_END, 20, CSS_VERTICAL_ALIGN_BASELINE);
    opening->inline_end = closing;
    closing->inline_end = opening;
    append(ic, closing);
    if (intrinsic_run_cases[_i].suffix)
        append_intrinsic_text(ic, intrinsic_run_cases[_i].suffix, false);

    ck_assert(layout_minmax_box(root, &fonts, &content));
    ck_assert_msg(ic->min_width.value == intrinsic_run_cases[_i].minimum &&
            ic->max_width == intrinsic_run_cases[_i].maximum,
        "%s: expected min/max %d/%d, got %d/%d", intrinsic_run_cases[_i].name,
        intrinsic_run_cases[_i].minimum, intrinsic_run_cases[_i].maximum, ic->min_width.value, ic->max_width);
    free_tree(root);
}
END_TEST

static const uint8_t text_alignments[] = {CSS_TEXT_ALIGN_LEFT, CSS_TEXT_ALIGN_RIGHT, CSS_TEXT_ALIGN_CENTER,
    CSS_TEXT_ALIGN_DEFAULT, CSS_TEXT_ALIGN_LIBCSS_LEFT, CSS_TEXT_ALIGN_LIBCSS_RIGHT, CSS_TEXT_ALIGN_LIBCSS_CENTER};
enum { TEXT_ALIGNMENT_COUNT = sizeof(text_alignments) / sizeof(text_alignments[0]) };

START_TEST(test_line_overflow_alignment)
{
    const struct gui_layout_table word_fonts = {.width = width, .split = split_word, .font_metrics = metrics};
    html_content content = {
        .unit_len_ctx = {.viewport_width = INTTOFIX(200), .viewport_height = INTTOFIX(500),
            .font_size_default = INTTOFIX(16), .device_dpi = INTTOFIX(96)},
        .font_func = &word_fonts,
    };
    uint8_t align = text_alignments[_i % TEXT_ALIGNMENT_COUNT];
    bool rtl = _i >= TEXT_ALIGNMENT_COUNT;
    struct box *root = make_box(BOX_BLOCK, 20, CSS_VERTICAL_ALIGN_BASELINE);
    style_set_text_alignment(root->style, align, rtl ? CSS_DIRECTION_RTL : CSS_DIRECTION_LTR);
    struct box *ic = make_box(BOX_INLINE_CONTAINER, 0, 0);
    append(root, ic);
    struct box *opening = make_box(BOX_INLINE, 20, CSS_VERTICAL_ALIGN_BASELINE);
    style_set_horizontal_edges(opening->style, 5, 0);
    append(ic, opening);
    append_intrinsic_text(ic, "AAAAAAAAAA", true);
    struct box *text = ic->last;
    struct box *closing = make_box(BOX_INLINE_END, 20, CSS_VERTICAL_ALIGN_BASELINE);
    opening->inline_end = closing;
    closing->inline_end = opening;
    append(ic, closing);

    /* 80px word + 10px edges; trailing collapsed space is excluded. Test
     * overflow, exact fit, spare room and repeat layout on the same tree. */
    const int widths[] = {60, 90, 140, 60};
    for (size_t pass = 0; pass < sizeof(widths) / sizeof(widths[0]); pass++) {
        root->width = widths[pass];
        root->height = AUTO;
        ck_assert(layout_block_context(root, 500, &content));
        int remaining = widths[pass] - 90;
        int expected = 0;
        if (remaining < 0 || align == CSS_TEXT_ALIGN_DEFAULT)
            expected = rtl ? remaining : 0;
        else if (align == CSS_TEXT_ALIGN_RIGHT || align == CSS_TEXT_ALIGN_LIBCSS_RIGHT)
            expected = remaining;
        else if (align == CSS_TEXT_ALIGN_CENTER || align == CSS_TEXT_ALIGN_LIBCSS_CENTER)
            expected = remaining / 2;
        ck_assert_msg(text->x == expected + 5 && text->y == 0 && ic->height == 20,
            "align %u, rtl %d, width %d: text=(%d,%d), line height=%d; expected x=%d, y=0, height=20",
            align, rtl, widths[pass], text->x, text->y, ic->height, expected + 5);
    }
    free_tree(root);
}
END_TEST

/* Exercise row sizing separately from box construction and frontend metrics. */
static struct box *make_table_box(box_type type, uint8_t display)
{
    struct box *box = make_box(type, 24, CSS_VERTICAL_ALIGN_BASELINE);
    style_set_table_display(box->style, display);
    return box;
}

static struct box *append_cell_text(struct box *cell, int font, int line)
{
    style_set_inline_metrics(cell->style, font, line, CSS_VERTICAL_ALIGN_BASELINE);
    struct box *ic = make_box(BOX_INLINE_CONTAINER, 0, 0);
    struct box *text = make_box(BOX_TEXT, line, CSS_VERTICAL_ALIGN_BASELINE);
    style_set_inline_metrics(text->style, font, line, CSS_VERTICAL_ALIGN_BASELINE);
    text->text = "H";
    text->length = 1;
    text->width = UNKNOWN_WIDTH;
    append(cell, ic);
    append(ic, text);
    return ic;
}

enum table_baseline_case {
    TABLE_BASELINE,
    TABLE_MIDDLE,
    TABLE_BOTTOM,
    TABLE_TOP,
    TABLE_NO_BASELINE,
    TABLE_PADDED,
    TABLE_CELL_MIN,
    TABLE_ROW_MIN,
    TABLE_EMPTY,
    TABLE_SPAN,
    TABLE_BASELINE_CASE_COUNT
};

START_TEST(test_table_first_baseline)
{
    static const char *names[] = {"baseline", "middle", "bottom", "top", "no baseline group", "padded", "cell minimum",
        "row minimum", "empty", "row span"};
    html_content content = {.unit_len_ctx = {.viewport_width = INTTOFIX(200),
                                .viewport_height = INTTOFIX(500),
                                .font_size_default = INTTOFIX(16),
                                .device_dpi = INTTOFIX(96)},
        .font_func = &scaled_fonts};
    struct box *table = make_table_box(BOX_TABLE, CSS_DISPLAY_TABLE);
    struct column columns[2] = {{.type = COLUMN_WIDTH_FIXED, .width = 100, .min = 100, .max = 100},
        {.type = COLUMN_WIDTH_FIXED, .width = 100, .min = 100, .max = 100}};
    table->columns = 2;
    table->col = columns;
    style_set_width(table->style, 200, false);
    struct box *group = make_table_box(BOX_TABLE_ROW_GROUP, CSS_DISPLAY_TABLE_ROW_GROUP);
    struct box *row = make_table_box(BOX_TABLE_ROW, CSS_DISPLAY_TABLE_ROW);
    append(table, group);
    append(group, row);
    struct box *cells[2], *lines[2] = {NULL, NULL};
    for (int i = 0; i < 2; i++) {
        cells[i] = make_table_box(BOX_TABLE_CELL, CSS_DISPLAY_TABLE_CELL);
        cells[i]->columns = cells[i]->rows = 1;
        cells[i]->start_column = i;
        append(row, cells[i]);
        if (_i != TABLE_EMPTY || i != 0)
            lines[i] = append_cell_text(cells[i], 16, 24);
    }
    uint8_t align = _i == TABLE_MIDDLE                 ? CSS_VERTICAL_ALIGN_MIDDLE
        : _i == TABLE_BOTTOM                           ? CSS_VERTICAL_ALIGN_BOTTOM
        : (_i == TABLE_TOP || _i == TABLE_NO_BASELINE) ? CSS_VERTICAL_ALIGN_TOP
                                                       : CSS_VERTICAL_ALIGN_BASELINE;
    style_set_inline_metrics(cells[0]->style, 16, 24, align);
    if (_i == TABLE_PADDED)
        style_set_padding_top(cells[0]->style, 20);
    if (_i == TABLE_CELL_MIN)
        style_set_height_px(cells[0]->style, 80);
    if (_i == TABLE_ROW_MIN)
        style_set_height_px(row->style, 80);
    if (_i == TABLE_SPAN) {
        cells[0]->rows = 2;
        style_set_inline_metrics(cells[0]->style, 64, 72, CSS_VERTICAL_ALIGN_BASELINE);
        style_set_inline_metrics(lines[0]->children->style, 64, 72, CSS_VERTICAL_ALIGN_BASELINE);
        struct box *last = make_table_box(BOX_TABLE_ROW, CSS_DISPLAY_TABLE_ROW);
        struct box *cell = make_table_box(BOX_TABLE_CELL, CSS_DISPLAY_TABLE_CELL);
        cell->columns = cell->rows = 1;
        cell->start_column = 1;
        append(group, last);
        append(last, cell);
        append_cell_text(cell, 16, 24);
    }
    /* Reuse the same boxes, changing font metrics and then restoring them. */
    for (int pass = 0; pass < 3; pass++) {
        int font = pass == 1 ? 48 : 32, line = font + 8;
        style_set_inline_metrics(cells[1]->style, font, line,
            _i == TABLE_NO_BASELINE ? CSS_VERTICAL_ALIGN_TOP : CSS_VERTICAL_ALIGN_BASELINE);
        style_set_inline_metrics(lines[1]->children->style, font, line, CSS_VERTICAL_ALIGN_BASELINE);
        ck_assert_msg(layout_table(table, 200, &content), "%s, pass %d: layout failed", names[_i], pass);
        int first = _i == TABLE_EMPTY ? 0 : _i == TABLE_SPAN ? 60 : _i == TABLE_PADDED ? 38 : 18;
        int second = 4 + font * 7 / 8;
        int baseline = _i == TABLE_NO_BASELINE ? line : max(first, second);
        int shift = align == CSS_VERTICAL_ALIGN_MIDDLE ? (line - 24) / 2
            : align == CSS_VERTICAL_ALIGN_BOTTOM       ? line - 24
            : align == CSS_VERTICAL_ALIGN_TOP          ? 0
                                                       : baseline - first;
        int row_height = line + max(0, first - second);
        if (_i == TABLE_CELL_MIN || _i == TABLE_ROW_MIN)
            row_height = 80;
        int height = row_height + (_i == TABLE_SPAN ? 24 : 0);
        int y = shift + (_i == TABLE_PADDED ? 20 : 0);
        ck_assert_msg((table->flags & HAS_BASELINE) && table->baseline == baseline && row->height == row_height &&
                table->height == height,
            "%s, pass %d: baseline=%d row=%d table=%d; expected %d/%d/%d", names[_i], pass, table->baseline,
            row->height, table->height, baseline, row_height, height);
        ck_assert_msg(cells[0]->padding[TOP] == y && (!lines[0] || lines[0]->y == y),
            "%s, pass %d: first content top=%d, line=%d; expected %d", names[_i], pass, cells[0]->padding[TOP],
            lines[0] ? lines[0]->y : -1, y);
        ck_assert_msg(
            lines[1]->y == max(0, first - second), "%s, pass %d: second line top=%d", names[_i], pass, lines[1]->y);
    }
    free_tree(table);
}
END_TEST

static const struct {
    const char *name;
    bool border_box, edges;
    uint8_t align;
} inline_table_cases[] = {
    {"content box", false, false, CSS_VERTICAL_ALIGN_BASELINE},
    {"content box with edges", false, true, CSS_VERTICAL_ALIGN_BASELINE},
    {"border box with edges", true, true, CSS_VERTICAL_ALIGN_BASELINE},
    {"top alignment", false, true, CSS_VERTICAL_ALIGN_TOP},
    {"bottom alignment", false, true, CSS_VERTICAL_ALIGN_BOTTOM},
};
enum { INLINE_TABLE_CASE_COUNT = sizeof(inline_table_cases) / sizeof(inline_table_cases[0]) };

START_TEST(test_inline_table_placement)
{
    const struct gui_layout_table table_fonts = {.width = width, .split = split_word, .font_metrics = scaled_metrics};
    html_content content = {.unit_len_ctx = {.viewport_width = INTTOFIX(200),
                                .viewport_height = INTTOFIX(500),
                                .font_size_default = INTTOFIX(16),
                                .device_dpi = INTTOFIX(96)},
        .font_func = &table_fonts};
    struct box *root = make_box(BOX_INLINE_BLOCK, 40, CSS_VERTICAL_ALIGN_BASELINE);
    style_set_inline_metrics(root->style, 32, 40, CSS_VERTICAL_ALIGN_BASELINE);
    style_set_width(root->style, -1, false);
    struct box *ic = make_box(BOX_INLINE_CONTAINER, 0, 0);
    ic->max_width = UNKNOWN_MAX_WIDTH;
    append(root, ic);
    struct box *before = make_box(BOX_TEXT, 40, CSS_VERTICAL_ALIGN_BASELINE);
    style_set_inline_metrics(before->style, 32, 40, CSS_VERTICAL_ALIGN_BASELINE);
    before->text = "H";
    before->length = 1;
    before->width = UNKNOWN_WIDTH;
    append(ic, before);
    struct box *table = make_table_box(BOX_TABLE, CSS_DISPLAY_INLINE_TABLE);
    struct column column = {.type = COLUMN_WIDTH_AUTO};
    table->columns = 1;
    table->col = &column;
    table->max_width = UNKNOWN_MAX_WIDTH;
    style_set_width(table->style, 80, false);
    style_set_box_sizing(
        table->style, inline_table_cases[_i].border_box ? CSS_BOX_SIZING_BORDER_BOX : CSS_BOX_SIZING_CONTENT_BOX);
    style_set_inline_metrics(table->style, 16, 24, inline_table_cases[_i].align);
    if (inline_table_cases[_i].edges)
        style_set_horizontal_edges(table->style, 3, 2);
    append(ic, table);
    struct box *group = make_table_box(BOX_TABLE_ROW_GROUP, CSS_DISPLAY_TABLE_ROW_GROUP);
    append(table, group);
    for (int i = 0; i < 2; i++) {
        struct box *row = make_table_box(BOX_TABLE_ROW, CSS_DISPLAY_TABLE_ROW);
        struct box *cell = make_table_box(BOX_TABLE_CELL, CSS_DISPLAY_TABLE_CELL);
        cell->columns = cell->rows = 1;
        cell->max_width = UNKNOWN_MAX_WIDTH;
        append(group, row);
        append(row, cell);
        append_cell_text(cell, 16, 24)->max_width = UNKNOWN_MAX_WIDTH;
    }
    struct box *after = make_box(BOX_TEXT, 40, CSS_VERTICAL_ALIGN_BASELINE);
    style_set_inline_metrics(after->style, 32, 40, CSS_VERTICAL_ALIGN_BASELINE);
    after->text = "H";
    after->length = 1;
    after->width = UNKNOWN_WIDTH;
    append(ic, after);
    root->max_width = UNKNOWN_MAX_WIDTH;
    ck_assert(layout_minmax_box(root, &table_fonts, &content));
    int edges = inline_table_cases[_i].edges ? 10 : 0;
    int border_width = 80 + (inline_table_cases[_i].border_box ? 0 : edges);
    ck_assert_msg(table->min_width.value == border_width && table->max_width == border_width &&
            root->min_width.value == border_width && root->max_width == border_width + 16,
        "%s: table min/max=%d/%d, parent=%d/%d; table outer width=%d", inline_table_cases[_i].name,
        table->min_width.value, table->max_width, root->min_width.value, root->max_width, border_width);

    /* At exact fit the text and table stay together. One pixel less moves the
     * trailing text, and widening restores the same tree's first-row baseline. */
    const int widths[] = {border_width + 16, border_width + 15, border_width + 16};
    for (size_t pass = 0; pass < sizeof(widths) / sizeof(widths[0]); pass++) {
        root->width = widths[pass];
        root->height = AUTO;
        ck_assert_msg(layout_block_context(root, 500, &content), "%s: layout failed", inline_table_cases[_i].name);
        int y = inline_table_cases[_i].align == CSS_VERTICAL_ALIGN_BASELINE ? 14 : 0;
        int line_height = 48 + y;
        ck_assert_msg(table->x == 8 + (edges ? 2 : 0) && table->y == y && table->width == border_width - edges &&
                table->height == 48 && table->baseline == 18 && group->x == (edges ? 3 : 0),
            "%s pass %zu: table=(%d,%d %dx%d), baseline=%d, group x=%d", inline_table_cases[_i].name, pass, table->x,
            table->y, table->width, table->height, table->baseline, group->x);
        int after_y = pass == 1 ? line_height : 0;
        ck_assert_msg(after->x == (pass == 1 ? 0 : 8 + border_width) && after->y == after_y &&
                root->height == line_height + (pass == 1 ? 40 : 0),
            "%s pass %zu: trailing text=(%d,%d), parent height=%d", inline_table_cases[_i].name, pass, after->x,
            after->y, root->height);
    }
    free_tree(root);
}
END_TEST

int main(void)
{
    if (nsoption_init(NULL, NULL, NULL) != NSERROR_OK)
        return EXIT_FAILURE;
    Suite *suite = suite_create("Inline text and object alignment");
    TCase *tc = tcase_create("Line metrics");
    tcase_add_loop_test(tc, test_inline_object_baseline, 0, 12);
    tcase_add_loop_test(tc, test_inline_block_alignment, 0, 4);
    tcase_add_loop_test(tc, test_inline_block_last_baseline, 0, 2);
    tcase_add_loop_test(tc, test_mixed_text_baseline, 0, sizeof(text_cases) / sizeof(text_cases[0]));
    tcase_add_loop_test(tc, test_outside_marker_baseline, 0, 3);
    tcase_add_loop_test(tc, test_empty_marker_line, 0, 3);
    tcase_add_loop_test(tc, test_block_marker_fallback, 0, 2 * BLOCK_MARKER_CASE_COUNT);
    tcase_add_test(tc, test_positioned_parent_text_line);
    tcase_add_loop_test(tc, test_empty_inline_metrics, 0, 4 * EMPTY_INLINE_CASE_COUNT);
    tcase_add_loop_test(tc, test_repeated_text_baselines, 0, 2);
    tcase_add_loop_test(tc, test_nested_float_layout_once, 0, 2 * (NESTED_FLOAT_DEPTH + 1));
    tcase_add_loop_test(tc, test_float_outer_height_exclusion, 0, 8 * FLOAT_OUTER_HEIGHT_COUNT);
    tcase_add_loop_test(tc, test_line_at_float_boundary, 0,
        4 * (FLOAT_BOUNDARY_MAX_WRAPPERS + 1) * FLOAT_BOUNDARY_CASE_COUNT);
    tcase_add_test(tc, test_font_baseline_fallback);
    suite_add_tcase(suite, tc);
    TCase *intrinsics = tcase_create("Intrinsic inline runs");
    tcase_add_loop_test(intrinsics, test_inline_intrinsic_run, 0,
        sizeof(intrinsic_run_cases) / sizeof(intrinsic_run_cases[0]));
    tcase_add_loop_test(intrinsics, test_intrinsic_container_constraints, 0,
        INTRINSIC_CONTAINER_MODE_COUNT * INTRINSIC_CONTAINER_CASE_COUNT);
    tcase_add_loop_test(intrinsics, test_grid_container_height, 0, 2 * GRID_HEIGHT_CASE_COUNT);
    suite_add_tcase(suite, intrinsics);
    TCase *alignment = tcase_create("Line alignment");
    tcase_add_loop_test(alignment, test_line_overflow_alignment, 0, 2 * TEXT_ALIGNMENT_COUNT);
    suite_add_tcase(suite, alignment);
    TCase *tables = tcase_create("Table baselines");
    tcase_add_loop_test(tables, test_table_first_baseline, 0, TABLE_BASELINE_CASE_COUNT);
    tcase_add_loop_test(tables, test_inline_table_placement, 0, INLINE_TABLE_CASE_COUNT);
    suite_add_tcase(suite, tables);
    SRunner *runner = srunner_create(suite);
    srunner_set_fork_status(runner, CK_NOFORK);
    srunner_run_all(runner, CK_VERBOSE);
    int failed = srunner_ntests_failed(runner);
    srunner_free(runner);
    nsoption_finalise(NULL, NULL);
    return failed ? EXIT_FAILURE : EXIT_SUCCESS;
}

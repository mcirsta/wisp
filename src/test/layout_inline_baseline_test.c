/* Inline text and object baselines, using the real block/inline layout code. */
#include <check.h>
#include <stdlib.h>

#include <wisp/layout.h>
#include <wisp/plot_style.h>
#include <wisp/utils/nsoption.h>
#include <wisp/content/handlers/html/private.h>
#include <wisp/content/handlers/html/box.h>
#include "html/layout_internal.h"
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
    int wrappers = _i / (2 * FLOAT_BOUNDARY_CASE_COUNT);
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
        ck_assert_msg(text->x == x && text->y == y && ic->height == y + 20,
            "%s (preceding container=%d, wrappers=%d), width %d: expected text %d/%d, line bottom %d; got %d/%d, %d",
            float_boundary_cases[variant].name, preceding_container, wrappers, widths[pass], x, y, y + 20,
            text->x, text->y, ic->height);
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
    tcase_add_loop_test(tc, test_empty_inline_metrics, 0, 4 * EMPTY_INLINE_CASE_COUNT);
    tcase_add_loop_test(tc, test_repeated_text_baselines, 0, 2);
    tcase_add_loop_test(tc, test_nested_float_layout_once, 0, 2 * (NESTED_FLOAT_DEPTH + 1));
    tcase_add_loop_test(tc, test_line_at_float_boundary, 0,
        2 * (FLOAT_BOUNDARY_MAX_WRAPPERS + 1) * FLOAT_BOUNDARY_CASE_COUNT);
    tcase_add_test(tc, test_font_baseline_fallback);
    suite_add_tcase(suite, tc);
    SRunner *runner = srunner_create(suite);
    srunner_set_fork_status(runner, CK_NOFORK);
    srunner_run_all(runner, CK_VERBOSE);
    int failed = srunner_ntests_failed(runner);
    srunner_free(runner);
    nsoption_finalise(NULL, NULL);
    return failed ? EXIT_FAILURE : EXIT_SUCCESS;
}

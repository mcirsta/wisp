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

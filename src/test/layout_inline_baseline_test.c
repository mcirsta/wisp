/* Inline replaced-element baselines, using the real block/inline layout code. */
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
    Suite *suite = suite_create("Inline image baseline");
    TCase *tc = tcase_create("Line metrics");
    tcase_add_loop_test(tc, test_inline_object_baseline, 0, 12);
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

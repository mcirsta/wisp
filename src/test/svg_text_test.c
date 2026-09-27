/* SVG text styles must be independent, including when measuring anchors. */
#include <check.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <svgtiny.h>

#include "content/handlers/image/svg.h"
#include "desktop/knockout.h"
#include "wisp/desktop/gui_internal.h"
#include "wisp/layout.h"
#include "wisp/plotters.h"

static const int weights[] = {400, 700, 400, 700, 400, 700, 400};
static unsigned int measured, painted;

static nserror measure_text(const struct plot_font_style *style, const char *text, size_t length, int *width)
{
    (void)text;
    (void)length;
    ck_assert_uint_lt(measured, sizeof(weights) / sizeof(weights[0]));
    ck_assert_int_eq(style->weight, weights[measured]);
    measured++;
    /* Distinct widths ensure end anchoring uses the corresponding style. */
    *width = style->weight == 700 ? 30 : 20;
    return NSERROR_OK;
}

static nserror paint_text(const struct redraw_context *ctx, const struct plot_font_style *style,
    int x, int y, const char *text, size_t length)
{
    (void)y;
    const int *display_width = ctx->priv;
    ck_assert_uint_lt(painted, sizeof(weights) / sizeof(weights[0]));
    int weight = weights[painted++];
    ck_assert_int_eq(style->weight, weight);
    ck_assert_int_eq(x, *display_width / 2 - (weight == 700 ? 30 : 20));
    ck_assert_uint_eq(length, 5);
    ck_assert_int_eq(memcmp(text, "Label", length), 0);
    ck_assert_int_ne(style->flags & FONTF_SIZE_PIXELS, 0);
    /* An 8px font at 60% scale must retain the fractional size for the frontend. */
    int expected_size = *display_width == 360 ? (int)(4.8 * PLOT_STYLE_SCALE) : 8 * PLOT_STYLE_SCALE;
    ck_assert_int_eq(style->size, expected_size);
    return NSERROR_OK;
}

START_TEST(test_svg_text_styles)
{
    const char *source =
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"600\" height=\"200\" "
        "font-size=\"8\" text-anchor=\"end\">"
        "<text x=\"300\" y=\"20\">Label</text>"
        "<text x=\"300\" y=\"40\" font-weight=\"bold\">Label</text>"
        "<text x=\"300\" y=\"60\">Label</text>"
        "<g font-weight=\"bold\">"
        "<text x=\"300\" y=\"80\">Label</text>"
        "<text x=\"300\" y=\"100\" font-weight=\"normal\">Label</text>"
        "<text x=\"300\" y=\"120\">Label</text>"
        "</g><text x=\"300\" y=\"140\">Label</text></svg>";
    struct svgtiny_diagram *diagram = svgtiny_create();
    ck_assert_ptr_nonnull(diagram);
    ck_assert_int_eq(svgtiny_parse(diagram, source, strlen(source), "data:", 600, 200), svgtiny_OK);
    ck_assert_uint_eq(diagram->shape_count, sizeof(weights) / sizeof(weights[0]));

    const struct plotter_table plotters = {.text = paint_text};
    struct gui_layout_table layout = {.width = measure_text};
    struct gui_layout_table *old_layout = guit->layout;
    guit->layout = &layout;
    const int widths[] = {600, 360};
    for (unsigned int i = 0; i < sizeof(widths) / sizeof(widths[0]); i++) {
        struct rect clip = {0, 0, widths[i], widths[i] / 3};
        struct redraw_context ctx = {.plot = &plotters, .priv = (void *)&widths[i]};
        measured = painted = 0;
        ck_assert(svg_redraw_diagram(diagram, 0, 0, widths[i], widths[i] / 3, &clip, &ctx, 0xffffff, 0));
        ck_assert_uint_eq(measured, sizeof(weights) / sizeof(weights[0]));
        ck_assert_uint_eq(painted, measured);
    }
    guit->layout = old_layout;
    svgtiny_free(diagram);
}
END_TEST

/* Origins are in the root viewBox's user units, before its 2x scale. */
static const struct {
    float a, b, c, d, x, y, font_scale;
    int anchor;
} transformed_labels[] = {
    {0, 1, -1, 0, 100, 50, 1, -10},
    {0, -1, 1, 0, 68, 4, 2, -20},
    {-1, 0, 0, -1, 150, 100, 1, 0},
    {0, 1, -1, 0, 96, 153, 1, -20},
    {0.8944272f, 0.4472136f, -0.8944272f, 0.4472136f, 100, 100, 1.5811388f, -10},
    {1, 0, 0, 1, 180, 180, 1, 0},
};
static const unsigned int label_count = sizeof(transformed_labels) / sizeof(transformed_labels[0]);

struct text_capture {
    float scale;
    unsigned int index, pushes, pops;
    bool active, fail_text, unsupported;
};

static nserror measure_label(const struct plot_font_style *style, const char *text, size_t length, int *width)
{
    (void)style;
    (void)text;
    (void)length;
    *width = 20;
    return NSERROR_OK;
}

static nserror push_text_transform(const struct redraw_context *ctx, const float m[6])
{
    struct text_capture *cap = ctx->priv;
    ck_assert(!cap->active);
    if (cap->unsupported)
        return NSERROR_NOT_IMPLEMENTED;
    unsigned int n = cap->index;
    ck_assert_uint_lt(n, label_count - 1);
    const float expected[] = {transformed_labels[n].a, transformed_labels[n].b,
        transformed_labels[n].c, transformed_labels[n].d,
        11 + cap->scale * transformed_labels[n].x, 17 + cap->scale * transformed_labels[n].y};
    for (unsigned int i = 0; i < 6; i++)
        ck_assert_msg(fabsf(m[i] - expected[i]) < 0.001f,
            "label %u matrix[%u]: expected %g, got %g", n, i, expected[i], m[i]);
    cap->active = true;
    cap->pushes++;
    return NSERROR_OK;
}

static nserror pop_text_transform(const struct redraw_context *ctx)
{
    struct text_capture *cap = ctx->priv;
    ck_assert(cap->active);
    cap->active = false;
    cap->pops++;
    return NSERROR_OK;
}

static nserror paint_label(const struct redraw_context *ctx, const struct plot_font_style *style,
    int x, int y, const char *text, size_t length)
{
    struct text_capture *cap = ctx->priv;
    unsigned int n = cap->index++;
    ck_assert_uint_lt(n, label_count);
    ck_assert_int_eq(cap->active, n < label_count - 1 && !cap->unsupported);
    int origin_x = cap->active ? 0 : (int)(11 + cap->scale * transformed_labels[n].x);
    int origin_y = cap->active ? 0 : (int)(17 + cap->scale * transformed_labels[n].y);
    int tolerance = cap->active ? 0 : 1; /* Integer fallback rounds transformed float positions. */
    ck_assert_msg(abs(x - origin_x - transformed_labels[n].anchor) <= tolerance,
        "label %u: x=%d, expected %d", n, x, origin_x + transformed_labels[n].anchor);
    ck_assert_msg(abs(y - origin_y) <= tolerance, "label %u: y=%d, expected %d", n, y, origin_y);
    int size = (int)(8 * cap->scale * transformed_labels[n].font_scale * PLOT_STYLE_SCALE);
    ck_assert_msg(abs(style->size - size) <= 1, "label %u: font size %d, expected %d", n, style->size, size);
    ck_assert_uint_eq(length, 5);
    ck_assert_int_eq(memcmp(text, "Label", length), 0);
    return cap->fail_text ? NSERROR_INVALID : NSERROR_OK;
}

START_TEST(test_svg_text_transforms)
{
    const char *source =
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"400\" height=\"400\" viewBox=\"0 0 200 200\" font-size=\"8\">"
        "<text x=\"100\" y=\"50\" text-anchor=\"middle\" transform=\"rotate(90 100 50)\">Label</text>"
        "<g transform=\"translate(20 30) rotate(-90) scale(2)\">"
        "<text x=\"10\" y=\"20\" dx=\"3\" dy=\"4\" text-anchor=\"end\">Label</text></g>"
        "<text x=\"150\" y=\"100\" transform=\"rotate(180 150 100)\">Label</text>"
        "<text x=\"100\" y=\"150\" transform=\"rotate(90 100 150)\">"
        "<tspan dx=\"3\" dy=\"4\" text-anchor=\"end\">Label</tspan></text>"
        "<g transform=\"translate(100 100) scale(2 1)\">"
        "<text transform=\"rotate(45)\" text-anchor=\"middle\">Label</text></g>"
        "<text x=\"180\" y=\"180\">Label</text>"
        "<text transform=\"scale(0)\">Invisible</text></svg>";
    struct svgtiny_diagram *diagram = svgtiny_create();
    ck_assert_ptr_nonnull(diagram);
    ck_assert_int_eq(svgtiny_parse(diagram, source, strlen(source), "data:", 400, 400), svgtiny_OK);
    const struct plotter_table plotters = {.text = paint_label,
        .push_transform = push_text_transform, .pop_transform = pop_text_transform};
    struct gui_layout_table layout = {.width = measure_label};
    struct gui_layout_table *old_layout = guit->layout;
    guit->layout = &layout;
    for (unsigned int mode = 0; mode < 5; mode++) {
        int width = mode == 1 ? 240 : 400;
        struct text_capture cap = {.scale = (float)width / 200,
            .fail_text = mode == 2, .unsupported = mode >= 3};
        struct rect clip = {0, 0, 500, 500};
        struct redraw_context ctx = {.plot = &plotters, .priv = &cap};
        const struct plotter_table without_transforms = {.text = paint_label};
        if (mode == 4) {
            /* The buffering wrapper must not advertise a transform which the
             * underlying frontend cannot apply. Check the actual fallback. */
            struct redraw_context direct = {.plot = &without_transforms, .priv = &cap};
            ck_assert(knockout_plot_start(&direct, &ctx));
        }
        bool ok = svg_redraw_diagram(diagram, 11, 17, width, width, &clip, &ctx, 0xffffff, 0);
        if (mode == 4)
            knockout_plot_end(&ctx);
        ck_assert_int_eq(ok, !cap.fail_text);
        ck_assert_uint_eq(cap.index, label_count);
        ck_assert_uint_eq(cap.pushes, cap.unsupported ? 0 : label_count - 1);
        ck_assert_uint_eq(cap.pops, cap.pushes);
        ck_assert(!cap.active);
    }
    guit->layout = old_layout;
    svgtiny_free(diagram);
}
END_TEST

int main(void)
{
    Suite *suite = suite_create("svg_text");
    TCase *tc = tcase_create("styles");
    tcase_add_test(tc, test_svg_text_styles);
    tcase_add_test(tc, test_svg_text_transforms);
    suite_add_tcase(suite, tc);
    SRunner *runner = srunner_create(suite);
    srunner_run_all(runner, CK_ENV);
    int failed = srunner_ntests_failed(runner);
    srunner_free(runner);
    return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

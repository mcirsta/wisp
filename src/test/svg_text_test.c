/* SVG text styles must be independent, including when measuring anchors. */
#include <check.h>
#include <stdlib.h>
#include <string.h>
#include <svgtiny.h>

#include "content/handlers/image/svg.h"
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

int main(void)
{
    Suite *suite = suite_create("svg_text");
    TCase *tc = tcase_create("styles");
    tcase_add_test(tc, test_svg_text_styles);
    suite_add_tcase(suite, tc);
    SRunner *runner = srunner_create(suite);
    srunner_run_all(runner, CK_ENV);
    int failed = srunner_ntests_failed(runner);
    srunner_free(runner);
    return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

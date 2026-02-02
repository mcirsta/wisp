/**
 * Test for SVG dashed line to rectangles conversion.
 *
 * Verifies that wide dashed lines (stroke-width > 50) are rendered as
 * filled rectangles instead of relying on platform-specific dash APIs.
 */

#include <check.h>
#include <stdlib.h>
#include <string.h>

#include "wisp/plotters.h"
#include "wisp/types.h"
#include "wisp/utils/errors.h"

#include <stdint.h>
#include <svgtiny.h>
typedef uint32_t colour;

extern bool svg_redraw_diagram(struct svgtiny_diagram *diagram, int x, int y, int width, int height,
    const struct rect *clip, const struct redraw_context *ctx, colour background_colour, colour current_color);

typedef struct {
    int rect_count;
    int path_count;
    struct rect rects[64];
    colour rect_colours[64];
} dash_capture_t;

static nserror dash_cap_clip(const struct redraw_context *ctx, const struct rect *clip)
{
    (void)ctx;
    (void)clip;
    return NSERROR_OK;
}

static nserror dash_cap_rectangle(const struct redraw_context *ctx, const plot_style_t *style, const struct rect *r)
{
    dash_capture_t *cap = (dash_capture_t *)ctx->priv;
    if (cap->rect_count < 64) {
        cap->rects[cap->rect_count] = *r;
        cap->rect_colours[cap->rect_count] = style->fill_colour;
        cap->rect_count++;
    }
    return NSERROR_OK;
}

static nserror dash_cap_path(const struct redraw_context *ctx, const plot_style_t *style, const float *p,
    unsigned int n, const float transform[6])
{
    (void)style;
    (void)p;
    (void)n;
    (void)transform;
    dash_capture_t *cap = (dash_capture_t *)ctx->priv;
    cap->path_count++;
    return NSERROR_OK;
}

static nserror dash_cap_text(const struct redraw_context *ctx, const struct plot_font_style *fstyle, int x, int y,
    const char *text, size_t length)
{
    (void)ctx;
    (void)fstyle;
    (void)x;
    (void)y;
    (void)text;
    (void)length;
    return NSERROR_OK;
}

static const struct plotter_table dash_cap_plotters = {
    .clip = dash_cap_clip,
    .rectangle = dash_cap_rectangle,
    .path = dash_cap_path,
    .text = dash_cap_text,
    .option_knockout = false,
};

/**
 * Test that a horizontal dashed line with wide stroke creates rectangles.
 */
START_TEST(test_horizontal_dashed_line_to_rects)
{
    /* SVG with horizontal dashed line: stroke-width=100, dasharray=50,50 */
    const char *src = "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"400\" height=\"200\">"
                      "<line x1=\"0\" y1=\"100\" x2=\"400\" y2=\"100\" "
                      "stroke=\"#FF0000\" stroke-width=\"100\" stroke-dasharray=\"50,50\"/>"
                      "</svg>";

    struct svgtiny_diagram *diagram = svgtiny_create();
    ck_assert_ptr_nonnull(diagram);

    svgtiny_code code = svgtiny_parse(diagram, src, strlen(src), "data:", 400, 200);
    ck_assert_int_eq(code, svgtiny_OK);

    /* Debug: check if dasharray was parsed */
    ck_assert_int_ge(diagram->shape_count, 1);

    struct rect clip = {.x0 = 0, .y0 = 0, .x1 = 400, .y1 = 200};
    dash_capture_t cap;
    memset(&cap, 0, sizeof(cap));
    struct redraw_context ctx = {
        .interactive = true,
        .background_images = true,
        .plot = &dash_cap_plotters,
        .priv = &cap,
    };

    bool ok = svg_redraw_diagram(diagram, 0, 0, 400, 200, &clip, &ctx, 0xFFFFFF, 0);
    ck_assert_msg(ok, "svg_redraw_diagram returned false");

    /* Should have created rectangles, not paths */
    ck_assert_msg(cap.rect_count > 0, "Expected rectangles for dashed line, got %d rects, %d paths", cap.rect_count,
        cap.path_count);

    /* With 400px line and 50,50 dash pattern, expect 4 dashes (50px each) */
    ck_assert_int_ge(cap.rect_count, 3);
    ck_assert_int_le(cap.rect_count, 5);

    /* Verify rectangles have correct colour (red = 0xFF0000 in BGR = 0x0000FF) */
    for (int i = 0; i < cap.rect_count; i++) {
        ck_assert_msg((cap.rect_colours[i] & 0xFFFFFF) == 0x0000FF, "Rectangle %d has wrong colour: 0x%06X", i,
            cap.rect_colours[i]);
    }

    /* Verify rectangles are positioned correctly (around y=100, width=100) */
    for (int i = 0; i < cap.rect_count; i++) {
        int rect_height = cap.rects[i].y1 - cap.rects[i].y0;
        int rect_center_y = (cap.rects[i].y0 + cap.rects[i].y1) / 2;

        ck_assert_int_ge(rect_height, 90); /* Approximately stroke_width */
        ck_assert_int_le(rect_height, 110);

        ck_assert_int_ge(rect_center_y, 90); /* Around y=100 */
        ck_assert_int_le(rect_center_y, 110);
    }

    svgtiny_free(diagram);
}
END_TEST

/**
 * Test that a vertical dashed line with wide stroke creates rectangles.
 */
START_TEST(test_vertical_dashed_line_to_rects)
{
    /* SVG with vertical dashed line */
    const char *src = "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"200\" height=\"400\">"
                      "<line x1=\"100\" y1=\"0\" x2=\"100\" y2=\"400\" "
                      "stroke=\"#00FF00\" stroke-width=\"80\" stroke-dasharray=\"100,100\"/>"
                      "</svg>";

    struct svgtiny_diagram *diagram = svgtiny_create();
    ck_assert_ptr_nonnull(diagram);

    svgtiny_code code = svgtiny_parse(diagram, src, strlen(src), "data:", 200, 400);
    ck_assert_int_eq(code, svgtiny_OK);

    struct rect clip = {.x0 = 0, .y0 = 0, .x1 = 200, .y1 = 400};
    dash_capture_t cap;
    memset(&cap, 0, sizeof(cap));
    struct redraw_context ctx = {
        .interactive = true,
        .background_images = true,
        .plot = &dash_cap_plotters,
        .priv = &cap,
    };

    bool ok = svg_redraw_diagram(diagram, 0, 0, 200, 400, &clip, &ctx, 0xFFFFFF, 0);
    ck_assert_msg(ok, "svg_redraw_diagram returned false");

    /* Should have created rectangles */
    ck_assert_msg(cap.rect_count > 0, "Expected rectangles for dashed line, got %d rects", cap.rect_count);

    /* With 400px line and 100,100 dash pattern, expect 2 dashes */
    ck_assert_int_eq(cap.rect_count, 2);

    /* Verify rectangles are vertical (narrow width, tall height) */
    for (int i = 0; i < cap.rect_count; i++) {
        int rect_width = cap.rects[i].x1 - cap.rects[i].x0;
        int rect_height = cap.rects[i].y1 - cap.rects[i].y0;

        ck_assert_int_ge(rect_width, 70); /* Approximately stroke_width */
        ck_assert_int_le(rect_width, 90);

        ck_assert_int_ge(rect_height, 90); /* Approximately dash length */
        ck_assert_int_le(rect_height, 110);
    }

    svgtiny_free(diagram);
}
END_TEST

/**
 * Test that ALL dashed lines use rectangle rendering (even thin strokes).
 * This ensures universal cross-platform dash rendering.
 */
START_TEST(test_all_dashed_lines_use_rects)
{
    /* SVG with thin dashed line (stroke-width=5) */
    const char *src = "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"400\" height=\"200\">"
                      "<line x1=\"0\" y1=\"100\" x2=\"400\" y2=\"100\" "
                      "stroke=\"#0000FF\" stroke-width=\"5\" stroke-dasharray=\"10,10\"/>"
                      "</svg>";

    struct svgtiny_diagram *diagram = svgtiny_create();
    ck_assert_ptr_nonnull(diagram);

    svgtiny_code code = svgtiny_parse(diagram, src, strlen(src), "data:", 400, 200);
    ck_assert_int_eq(code, svgtiny_OK);

    struct rect clip = {.x0 = 0, .y0 = 0, .x1 = 400, .y1 = 200};
    dash_capture_t cap;
    memset(&cap, 0, sizeof(cap));
    struct redraw_context ctx = {
        .interactive = true,
        .background_images = true,
        .plot = &dash_cap_plotters,
        .priv = &cap,
    };

    bool ok = svg_redraw_diagram(diagram, 0, 0, 400, 200, &clip, &ctx, 0xFFFFFF, 0);
    ck_assert_msg(ok, "svg_redraw_diagram returned false");

    /* ALL dashed lines now use rectangle rendering for cross-platform compatibility */
    ck_assert_msg(cap.rect_count > 0, "Expected rectangles for thin dashed line, got %d rects, %d paths",
        cap.rect_count, cap.path_count);

    /* With 400px line and 10,10 dash pattern, expect 20 dashes */
    ck_assert_int_ge(cap.rect_count, 18);
    ck_assert_int_le(cap.rect_count, 22);

    svgtiny_free(diagram);
}
END_TEST

Suite *svg_dash_suite(void)
{
    Suite *s = suite_create("svg_dash_rects");
    TCase *tc = tcase_create("dash_to_rects");
    tcase_add_test(tc, test_horizontal_dashed_line_to_rects);
    tcase_add_test(tc, test_vertical_dashed_line_to_rects);
    tcase_add_test(tc, test_all_dashed_lines_use_rects);
    suite_add_tcase(s, tc);
    return s;
}

int main(void)
{
    Suite *s = svg_dash_suite();
    SRunner *sr = srunner_create(s);
    srunner_run_all(sr, CK_ENV);
    int failed = srunner_ntests_failed(sr);
    srunner_free(sr);
    return (failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}

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
    int path_count;
    const float *paths[32];
    unsigned int path_lengths[32];
    float minx;
    float miny;
    float maxx;
    float maxy;
} capture_t;

static nserror cap_clip(const struct redraw_context *ctx, const struct rect *clip)
{
    (void)ctx;
    (void)clip;
    return NSERROR_OK;
}

static nserror cap_rectangle(const struct redraw_context *ctx, const plot_style_t *style, const struct rect *r)
{
    (void)ctx;
    (void)style;
    (void)r;
    return NSERROR_OK;
}

static nserror cap_text(const struct redraw_context *ctx, const struct plot_font_style *fstyle, int x, int y,
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

static nserror cap_path(const struct redraw_context *ctx, const plot_style_t *style, const float *p, unsigned int n,
    const float transform[6])
{
    (void)style;
    capture_t *cap = (capture_t *)ctx->priv;
    if (cap->path_count < 32) {
        cap->paths[cap->path_count] = p;
        cap->path_lengths[cap->path_count] = n;

        unsigned int j = 0;
        int initbb = 0;
        while (j < n) {
            int cmd = (int)p[j++];
            switch (cmd) {
            case PLOTTER_PATH_MOVE:
            case PLOTTER_PATH_LINE: {
                float xx = p[j++];
                float yy = p[j++];
                xx += transform[4];
                yy += transform[5];
                if (!initbb) {
                    cap->minx = cap->maxx = xx;
                    cap->miny = cap->maxy = yy;
                    initbb = 1;
                }
                if (xx < cap->minx)
                    cap->minx = xx;
                if (xx > cap->maxx)
                    cap->maxx = xx;
                if (yy < cap->miny)
                    cap->miny = yy;
                if (yy > cap->maxy)
                    cap->maxy = yy;
                break;
            }
            case PLOTTER_PATH_BEZIER: {
                float x1 = p[j++];
                float y1 = p[j++];
                float x2 = p[j++];
                float y2 = p[j++];
                float x3 = p[j++];
                float y3 = p[j++];
                x1 += transform[4];
                y1 += transform[5];
                x2 += transform[4];
                y2 += transform[5];
                x3 += transform[4];
                y3 += transform[5];
                if (!initbb) {
                    cap->minx = cap->maxx = x1;
                    cap->miny = cap->maxy = y1;
                    initbb = 1;
                }
                if (x1 < cap->minx)
                    cap->minx = x1;
                if (x1 > cap->maxx)
                    cap->maxx = x1;
                if (y1 < cap->miny)
                    cap->miny = y1;
                if (y1 > cap->maxy)
                    cap->maxy = y1;
                if (x2 < cap->minx)
                    cap->minx = x2;
                if (x2 > cap->maxx)
                    cap->maxx = x2;
                if (y2 < cap->miny)
                    cap->miny = y2;
                if (y2 > cap->maxy)
                    cap->maxy = y2;
                if (x3 < cap->minx)
                    cap->minx = x3;
                if (x3 > cap->maxx)
                    cap->maxx = x3;
                if (y3 < cap->miny)
                    cap->miny = y3;
                if (y3 > cap->maxy)
                    cap->maxy = y3;
                break;
            }
            case PLOTTER_PATH_CLOSE:
            default:
                break;
            }
        }

        cap->path_count++;
    }
    return NSERROR_OK;
}

static const struct plotter_table cap_plotters = {
    .clip = cap_clip,
    .rectangle = cap_rectangle,
    .path = cap_path,
    .text = cap_text,
    .option_knockout = false,
};

START_TEST(test_svg_rect_path_geometry)
{
    const char *src =
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"10\" height=\"10\"><rect x=\"1\" y=\"2\" width=\"5\" height=\"6\" fill=\"#0000ff\"/></svg>";

    struct svgtiny_diagram *diagram = svgtiny_create();
    ck_assert_ptr_nonnull(diagram);

    svgtiny_parse(diagram, src, strlen(src), "data:", 10, 10);

    struct rect clip = {.x0 = 0, .y0 = 0, .x1 = 100, .y1 = 100};
    capture_t cap;
    memset(&cap, 0, sizeof(cap));
    struct redraw_context ctx = {
        .interactive = true,
        .background_images = true,
        .plot = &cap_plotters,
        .priv = &cap,
    };

    bool ok = svg_redraw_diagram(diagram, 0, 0, 10, 10, &clip, &ctx, 0xFFFFFF, 0);
    ck_assert_msg(ok, "svg_redraw_diagram returned false");
    ck_assert_int_gt(cap.path_count, 0);

    int minx = (int)floorf(cap.minx);
    int miny = (int)floorf(cap.miny);
    int maxx = (int)ceilf(cap.maxx);
    int maxy = (int)ceilf(cap.maxy);

    ck_assert_int_eq(minx, 1);
    ck_assert_int_eq(miny, 2);
    ck_assert_int_eq(maxx, 6);
    ck_assert_int_eq(maxy, 8);

    svgtiny_free(diagram);
}
END_TEST

Suite *svg_suite(void)
{
    Suite *s = suite_create("renderer_svg");
    TCase *tc = tcase_create("svg_rect");
    tcase_add_test(tc, test_svg_rect_path_geometry);
    suite_add_tcase(s, tc);
    return s;
}

int main(void)
{
    Suite *s = svg_suite();
    SRunner *sr = srunner_create(s);
    srunner_run_all(sr, CK_ENV);
    int failed = srunner_ntests_failed(sr);
    srunner_free(sr);
    return (failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}

#include <check.h>
#include <stdlib.h>
#include <string.h>

#include "neosurf/content/handlers/html/box.h"
#include "neosurf/css.h"
#include "neosurf/plotters.h"
#include "neosurf/types.h"
#include "neosurf/utils/errors.h"

/* Under test */
bool html_redraw_borders(struct box *box, int x, int y, int p_width, int p_height, const struct rect *clip, float scale,
    const struct redraw_context *ctx);

typedef struct {
    int polygon_count;
    int polygon_points[32][8];
    int rectangle_count;
    rect rectangles[32];
} capture_t;

static nserror cap_clip(const struct redraw_context *ctx, const struct rect *clip)
{
    (void)ctx;
    (void)clip;
    return NSERROR_OK;
}

static nserror cap_rectangle(const struct redraw_context *ctx, const plot_style_t *style, const struct rect *r)
{
    (void)style;
    capture_t *cap = (capture_t *)ctx->priv;
    if (cap->rectangle_count < 32) {
        cap->rectangles[cap->rectangle_count] = *r;
        cap->rectangle_count++;
    }
    return NSERROR_OK;
}

static nserror cap_line(const struct redraw_context *ctx, const plot_style_t *style, const struct rect *line)
{
    (void)ctx;
    (void)style;
    (void)line;
    return NSERROR_OK;
}

static nserror cap_polygon(const struct redraw_context *ctx, const plot_style_t *style, const int *p, unsigned int n)
{
    (void)style;
    capture_t *cap = (capture_t *)ctx->priv;
    if (cap->polygon_count < 32 && n == 4) {
        memcpy(cap->polygon_points[cap->polygon_count], p, sizeof(int) * 8);
        cap->polygon_count++;
    }
    return NSERROR_OK;
}

static const struct plotter_table cap_plotters = {
    .clip = cap_clip,
    .rectangle = cap_rectangle,
    .line = cap_line,
    .polygon = cap_polygon,
    .option_knockout = false,
};

START_TEST(test_borders_left_only_geometry)
{
    struct box b;
    memset(&b, 0, sizeof(b));

    b.x = 10;
    b.y = 20;
    b.style = (css_computed_style *)0x1; /* non-NULL */

    b.border[LEFT].style = CSS_BORDER_STYLE_SOLID;
    b.border[LEFT].c = 0xFF112233; /* opaque */
    b.border[LEFT].width = 4;

    /* Other sides zero to isolate left side */
    b.border[TOP].style = CSS_BORDER_STYLE_SOLID;
    b.border[TOP].c = 0xFF112233;
    b.border[TOP].width = 0;
    b.border[RIGHT].style = CSS_BORDER_STYLE_SOLID;
    b.border[RIGHT].c = 0xFF112233;
    b.border[RIGHT].width = 0;
    b.border[BOTTOM].style = CSS_BORDER_STYLE_SOLID;
    b.border[BOTTOM].c = 0xFF112233;
    b.border[BOTTOM].width = 0;

    struct rect clip = {.x0 = 0, .y0 = 0, .x1 = 200, .y1 = 200};

    capture_t cap = {0};
    struct redraw_context ctx = {
        .interactive = true,
        .background_images = true,
        .plot = &cap_plotters,
        .priv = &cap,
    };

    /* Pass the actual box position (x, y) since html_redraw_borders no longer
     * adds box->x/y internally. Previously we passed x_parent=0, y_parent=0
     * and the function added box->x/y. Now we pass the final position. */
    bool ok = html_redraw_borders(&b, b.x, b.y,
        /* p_width */ 100,
        /* p_height */ 50, &clip, 1.0f, &ctx);
    ck_assert_msg(ok, "html_redraw_borders returned false");
    ck_assert_int_gt(cap.rectangle_count, 0);

    /* Compute bounding box of captured rectangles */
    int minx = 100000, miny = 100000, maxx = -100000, maxy = -100000;
    for (int i = 0; i < cap.rectangle_count; i++) {
        rect rr = cap.rectangles[i];
        if (rr.x0 < minx)
            minx = rr.x0;
        if (rr.x1 < minx)
            minx = rr.x1;
        if (rr.x0 > maxx)
            maxx = rr.x0;
        if (rr.x1 > maxx)
            maxx = rr.x1;
        if (rr.y0 < miny)
            miny = rr.y0;
        if (rr.y1 < miny)
            miny = rr.y1;
        if (rr.y0 > maxy)
            maxy = rr.y0;
        if (rr.y1 > maxy)
            maxy = rr.y1;
    }

    /* Expected bounds for left border only: x in [b.x - left, b.x], y in
     * [b.y, b.y + p_height] */
    ck_assert_int_eq(minx, b.x - b.border[LEFT].width);
    ck_assert_int_le(maxx, b.x);
    ck_assert_int_ge(miny, b.y);
    ck_assert_int_le(maxy, b.y + 50);
}
END_TEST

Suite *renderer_suite(void)
{
    Suite *s = suite_create("renderer_borders");
    TCase *tc = tcase_create("borders_left");
    tcase_add_test(tc, test_borders_left_only_geometry);
    suite_add_tcase(s, tc);
    return s;
}

int main(void)
{
    Suite *s = renderer_suite();
    SRunner *sr = srunner_create(s);
    srunner_run_all(sr, CK_ENV);
    int failed = srunner_ntests_failed(sr);
    srunner_free(sr);
    return (failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}

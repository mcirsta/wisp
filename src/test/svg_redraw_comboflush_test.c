#include <check.h>
#include <stdio.h>
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
    int path_calls;
    unsigned int last_len;
    unsigned int max_len;
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
    (void)transform;
    capture_t *cap = (capture_t *)ctx->priv;
    cap->path_calls++;
    cap->last_len = n;
    if (n > cap->max_len)
        cap->max_len = n;
    if (n >= 900) {
        return NSERROR_BAD_PARAMETER;
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

START_TEST(test_svg_wordmark_final_flush_failure)
{
    const char *svg_path = "../contrib/libsvgtiny/test/data/wikipedia-wordmark-en.svg";
    FILE *fd = fopen(svg_path, "rb");
    ck_assert_msg(fd != NULL, "Failed to open %s", svg_path);

    fseek(fd, 0, SEEK_END);
    long size = ftell(fd);
    ck_assert_msg(size > 0, "Empty SVG file: %s", svg_path);
    fseek(fd, 0, SEEK_SET);

    char *buffer = malloc((size_t)size);
    ck_assert_ptr_nonnull(buffer);
    size_t nread = fread(buffer, 1, (size_t)size, fd);
    fclose(fd);
    ck_assert_int_eq(nread, (size_t)size);

    struct svgtiny_diagram *diagram = svgtiny_create();
    ck_assert_ptr_nonnull(diagram);

    svgtiny_parse(diagram, buffer, (size_t)size, svg_path, 1000, 1000);
    free(buffer);

    struct rect clip = {.x0 = -100000, .y0 = -100000, .x1 = 100000, .y1 = 100000};
    capture_t cap;
    memset(&cap, 0, sizeof(cap));
    struct redraw_context ctx = {
        .interactive = true,
        .background_images = true,
        .plot = &cap_plotters,
        .priv = &cap,
    };

    bool ok = svg_redraw_diagram(diagram, 0, 0, 1000, 1000, &clip, &ctx, 0xFFFFFF, 0);
    ck_assert_msg(
        ok == true, "Expected svg_redraw_diagram to succeed; last_len=%u max_len=%u", cap.last_len, cap.max_len);

    svgtiny_free(diagram);
}
END_TEST

Suite *svg_combo_suite(void)
{
    Suite *s = suite_create("renderer_svg_combo");
    TCase *tc = tcase_create("svg_wordmark_final_flush");
    tcase_add_test(tc, test_svg_wordmark_final_flush_failure);
    suite_add_tcase(s, tc);
    return s;
}

int main(void)
{
    Suite *s = svg_combo_suite();
    SRunner *sr = srunner_create(s);
    srunner_run_all(sr, CK_ENV);
    int failed = srunner_ntests_failed(sr);
    srunner_free(sr);
    return (failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}

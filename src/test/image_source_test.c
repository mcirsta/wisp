/* HTML source-set parsing, CSS sizes evaluation and density normalization. */
#include <check.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <libcss/sizes.h>
#include <wisp/utils/nsurl.h>
#include <wisp/utils/corestrings.h>
#include "content/handlers/html/image.h"

static const css_unit_ctx units = {.viewport_width = (1200 * F_1),
    .viewport_height = (800 * F_1),
    .font_size_default = (16 * F_1),
    .device_dpi = F_96,
    .container_width = CSS_UNIT_CTX_UNSET,
    .container_height = CSS_UNIT_CTX_UNSET};
static const css_media media = {.type = CSS_MEDIA_SCREEN, .width = (1200 * F_1), .height = (800 * F_1)};

static const struct {
    const char *text;
    double expected;
} sizes_cases[] = {
    {"", 1200},
    {"320px", 320},
    {"50vw", 600},
    {".5vw", 6},
    {"50vh", 400},
    {"50vmin", 400},
    {"20em", 320},
    {"20rem", 320},
    {"1in", 96},
    {"1cm", 96 / 2.54},
    {"0", 0},
    {"0px", 0},
    {"-1px, 120px", 120},
    {"50%, 200px", 200},
    {"calc(100% - 10px), 200px", 200},
    {"calc(50vw - 20px)", 580},
    {"calc((100vw - 40px) / 2)", 580},
    {"calc(2 * 10em + 10px)", 330},
    {"calc(-20px), 100px", 0},
    {"calc(0), 200px", 200},
    {"calc(20px + 10), 200px", 200},
    {"(max-width: 600px) 100vw, (min-width: 1000px) 50vw, 200px", 600},
    {"(width >= 1000px) calc(100vw / 3), 200px", 400},
    {"not (max-width: 600px) 50vw, 200px", 600},
    {"(width > 500px) and (height < 900px) 75vw, 200px", 900},
    {"(width < 500px) or (height < 900px) 75vw, 200px", 900},
    {"screen and (min-width: 1px) 10px, 200px", 200},
    {"(max-width: 600px) garbage 10px, 200px", 200},
    {"(unknown:yes) 10px, 200px", 200},
    {"((width:1200px) broken) 10px, 200px", 200},
    {"not (unknown:yes) 10px, 200px", 10},
    {"((width:1200px)) 10px, 200px", 10},
    {"(not) 10px, 200px", 200},
    {"not 10px, 200px", 200},
    {"((width:1200px) 10px), 200px", 200},
    {"(min-width: 50em) 20em, 200px", 320},
    {"auto, 320px", 320},
    {"auto", 1200},
    {"AUTO, 320PX", 320},
    {"100px, 200px", 100},
    {" , , 100px , ", 100},
    {"/*prefix*/calc(100vw/**/ - 40px)/*suffix*/", 1160},
    {"c\\61lc(10px + 20px)", 30},
    {"attr(width), 200px", 200},
    {"var(--size), 200px", 200},
    {"garbage(a,b), 200px", 200},
    {"calc(100vw - 40px", 1160},
    {"(width > 1200px) 10px, 20px", 20},
    {"(width >= 1200px) 10px, 20px", 10},
    {"(width < 1200px) 10px, 20px", 20},
    {"(width <= 1200px) 10px, 20px", 10},
    {"(1200px < width) 10px, 20px", 20},
    {"(1200px <= width) 10px, 20px", 10},
    {"(1200px > width) 10px, 20px", 20},
    {"(1200px >= width) 10px, 20px", 10},
    {"(500px < width <= 1200px) 10px, 20px", 10},
    {"(500px < width < 1200px) 10px, 20px", 20},
    {"(1400px > width >= 1200px) 10px, 20px", 10},
    {"(1400px > width > 1200px) 10px, 20px", 20},
    {"(500px < width > 1000px) 10px, 20px", 20},
    {"(min-width: 1px) 50%", 1200},
    {"(width:1200px) 30px", 30},
    {"1fr, 50px", 50},
};

START_TEST(test_sizes)
{
    double pixels = -1;
    const char *text = sizes_cases[_i].text;
    ck_assert_int_eq(css_source_size((const uint8_t *)text, strlen(text), &units, &media, -1, &pixels), CSS_OK);
    ck_assert_msg(fabs(pixels - sizes_cases[_i].expected) < .001, "sizes '%s': expected %.6f, got %.6f", text,
        sizes_cases[_i].expected, pixels);
}
END_TEST

START_TEST(test_sizes_auto_and_initial_units)
{
    double pixels;
    const char text[] = "auto, 100vw";
    ck_assert_int_eq(css_source_size((const uint8_t *)text, strlen(text), &units, &media, 321.5, &pixels), CSS_OK);
    ck_assert_double_eq(pixels, 321.5);
    css_unit_ctx narrow = {.viewport_width = INTTOFIX(360),
        .viewport_height = INTTOFIX(640),
        .font_size_default = INTTOFIX(16),
        .font_size_minimum = INTTOFIX(50),
        .device_dpi = F_96};
    css_media narrow_media = {.type = CSS_MEDIA_SCREEN, .width = INTTOFIX(360), .height = INTTOFIX(640)};
    const char query[] = "(max-width: 30em) calc(100vw - 2em), 50vw";
    ck_assert_int_eq(
        css_source_size((const uint8_t *)query, strlen(query), &narrow, &narrow_media, -1, &pixels), CSS_OK);
    ck_assert_double_eq(pixels, 328);
}
END_TEST

static const struct {
    const char *src, *set, *sizes, *selected;
    double target, density;
} source_cases[] = {
    {"fallback.png", NULL, NULL, "fallback.png", 1, 1},
    {"", "", NULL, NULL, 1, 1},
    {NULL, NULL, NULL, NULL, 1, 1},
    {"fallback.png", "big.png 2x", NULL, "fallback.png", 1, 1},
    {"fallback.png", "big.png 2x", NULL, "big.png", 2, 2},
    {"fallback.png", "small.png 1x, big.png 2x", NULL, "small.png", 1, 1},
    {"fallback.png", "small.png, big.png 2x", NULL, "small.png", 1, 1},
    {"fallback.png", "small.png 400w, big.png 800w", NULL, "big.png", 1, 800.0 / 1200},
    {"fallback.png", "small.png 400w, big.png 800w", "320px", "small.png", 1, 1.25},
    {"fallback.png", "small.png 400w, big.png 800w", "320px", "big.png", 2, 2.5},
    {"fallback.png", "small.png 400w, big.png 800w", "(width < 600px) 320px, 600px", "big.png", 1, 800.0 / 600},
    {"fallback.png", "small.png 400w", "0", "small.png", 1, INFINITY},
    {"fallback.png", "small.png 400w", "calc(50vw - 200px)", "small.png", 1, 1},
    {NULL, "half.png .5x, big.png 2e0x", NULL, "big.png", 1, 2},
    {NULL, "half.png .5x, one.png 1x, big.png 2E+0x", NULL, "big.png", 1.5, 2},
    {NULL, "zero.png 0x", NULL, "zero.png", 1, 0},
    {NULL, "zero.png -0x", NULL, "zero.png", 1, 0},
    {"fallback.png", "bad.png +1x, bad.png 1.x, bad.png -1x", NULL, "fallback.png", 1, 1},
    {"fallback.png", "bad.png 0w, bad.png 4.5w, bad.png 1e2w", NULL, "fallback.png", 1, 1},
    {"fallback.png", "bad.png 1x 2x, bad.png 400w 1x, bad.png 400w 600w", NULL, "fallback.png", 1, 1},
    {"fallback.png", "bad.png 100h, bad.png 1x 200h, bad.png 200h 1x", NULL, "fallback.png", 1, 1},
    {"fallback.png", "good.png 400w 300h", "200px", "good.png", 1, 2},
    {"fallback.png", "good.png 300h 400w", "200px", "good.png", 1, 2},
    {"fallback.png", "bad.png 400w 300h 300h, good.png 1x", NULL, "good.png", 1, 1},
    {"fallback.png", "a,b.png 1x, c.png 2x", NULL, "a,b.png", 1, 1},
    {"fallback.png", "small.png,big.png 2x", NULL, "small.png,big.png", 2, 2},
    {"fallback.png", "small.png,,, big.png 2x", NULL, "small.png", 1, 1},
    {"fallback.png", "bad.png future(a, b) 1x, good.png 2x", NULL, "good.png", 2, 2},
    {"fallback.png", "bad.png 1X, bad.png 400W", NULL, "fallback.png", 1, 1},
    {"fallback.png", "good.png\f400w", "200px", "good.png", 1, 2},
    {"fallback.png", "bad.png 400w\v, good.png 2x", NULL, "good.png", 2, 2},
    {"fallback.png", "bad.png 1e999x, good.png 2x", NULL, "good.png", 2, 2},
    {"fallback.png", "first.png 1x, second.png 1x", NULL, "first.png", 1, 1},
};

static dom_string *string(const char *text)
{
    if (text == NULL)
        return NULL;
    dom_string *out;
    ck_assert_int_eq(dom_string_create((const uint8_t *)text, strlen(text), &out), DOM_NO_ERR);
    return out;
}

START_TEST(test_source_set)
{
    nsurl *base, *url;
    ck_assert_int_eq(nsurl_create("https://example.test/", &base), NSERROR_OK);
    dom_string *src = string(source_cases[_i].src), *set = string(source_cases[_i].set),
               *sizes = string(source_cases[_i].sizes);
    double density;
    ck_assert(html_image_source_parse(src, set, sizes, &units, &media, source_cases[_i].target, base, &url, &density));
    const char *want = source_cases[_i].selected;
    ck_assert_msg((url == NULL) == (want == NULL), "source case %d: unexpected empty/nonempty source", _i);
    if (want != NULL) {
        ck_assert_msg(strcmp(nsurl_access(url) + strlen("https://example.test/"), want) == 0,
            "source case %d: expected %s, got %s", _i, want, nsurl_access(url));
        ck_assert_msg(density == source_cases[_i].density || fabs(density - source_cases[_i].density) < 1e-9,
            "source case %d: expected density %.9g, got %.9g", _i, source_cases[_i].density, density);
        nsurl_unref(url);
    }
    nsurl_unref(base);
    dom_string_unref(src);
    dom_string_unref(set);
    dom_string_unref(sizes);
}
END_TEST

int main(void)
{
    if (corestrings_init() != NSERROR_OK)
        return EXIT_FAILURE;
    Suite *suite = suite_create("Image source selection");
    TCase *tc = tcase_create("HTML srcset and CSS sizes");
    tcase_add_loop_test(tc, test_sizes, 0, sizeof(sizes_cases) / sizeof(sizes_cases[0]));
    tcase_add_test(tc, test_sizes_auto_and_initial_units);
    tcase_add_loop_test(tc, test_source_set, 0, sizeof(source_cases) / sizeof(source_cases[0]));
    suite_add_tcase(suite, tc);
    SRunner *runner = srunner_create(suite);
    srunner_run_all(runner, CK_NORMAL);
    int failed = srunner_ntests_failed(runner);
    srunner_free(runner);
    corestrings_fini();
    return failed ? EXIT_FAILURE : EXIT_SUCCESS;
}

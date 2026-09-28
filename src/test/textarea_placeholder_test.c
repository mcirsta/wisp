/* Shared text controls: hints are paint-only, never editable values. */
#include <check.h>
#include <stdlib.h>
#include <string.h>

#include <wisp/clipboard.h>
#include <wisp/desktop/gui_internal.h>
#include <wisp/desktop/textarea.h>
#include <wisp/keypress.h>
#include <wisp/layout.h>
#include <wisp/plotters.h>
#include <wisp/utils/utf8.h>
#include <wisp/content/handlers/css/utils.h>

enum { WIDTH = 100, HEIGHT = 96, PADDING = 4 };
static const unsigned int modes[] = {TEXTAREA_DEFAULT, TEXTAREA_PASSWORD, TEXTAREA_MULTILINE};
static unsigned int modifications, full_redraws;
static size_t copied;
static char last_value[128];

static nserror width(const plot_font_style_t *style, const char *text, size_t length, int *result)
{
    (void)style;
    *result = utf8_bounded_length(text, length) * 8;
    return NSERROR_OK;
}

static nserror split(const plot_font_style_t *style, const char *text, size_t length, int available,
    size_t *offset, int *actual)
{
    *offset = length;
    width(style, text, length, actual);
    if (*actual <= available)
        return NSERROR_OK;
    for (size_t i = 1; i < length; i++) {
        if (text[i] == ' ') {
            int w;
            width(style, text, i, &w);
            if (*offset != length && w > available)
                break;
            *offset = i;
            *actual = w;
        }
    }
    return NSERROR_OK;
}

static void copy(const char *text, size_t length, nsclipboard_styles styles[], int count)
{
    (void)text;
    (void)styles;
    (void)count;
    copied = length;
}

static void callback(void *data, struct textarea_msg *msg)
{
    (void)data;
    if (msg->type == TEXTAREA_MSG_TEXT_MODIFIED) {
        modifications++;
        ck_assert_uint_le(msg->data.modified.len, sizeof(last_value));
        memcpy(last_value, msg->data.modified.text, msg->data.modified.len);
    } else if (msg->type == TEXTAREA_MSG_REDRAW_REQUEST) {
        struct rect r = msg->data.redraw;
        if (r.x0 <= 0 && r.y0 <= 0 && r.x1 >= WIDTH && r.y1 >= HEIGHT)
            full_redraws++;
    }
}

struct paint {
    unsigned int count;
    struct rect clip;
    struct {
        char text[128];
        int x, y;
        plot_font_style_t style;
        struct rect clip;
    } runs[16];
};

static nserror plot_clip(const struct redraw_context *ctx, const struct rect *clip)
{
    ((struct paint *)ctx->priv)->clip = *clip;
    return NSERROR_OK;
}

static nserror plot_rect(const struct redraw_context *ctx, const plot_style_t *style, const struct rect *rect)
{
    (void)ctx;
    (void)style;
    (void)rect;
    return NSERROR_OK;
}

static nserror plot_text(const struct redraw_context *ctx, const plot_font_style_t *style,
    int x, int y, const char *text, size_t length)
{
    struct paint *p = ctx->priv;
    unsigned int i = p->count++;
    ck_assert_uint_lt(i, 16);
    ck_assert_uint_lt(length, sizeof(p->runs[i].text));
    memcpy(p->runs[i].text, text, length);
    p->runs[i].text[length] = '\0';
    p->runs[i].style = *style;
    p->runs[i].x = x;
    p->runs[i].y = y;
    p->runs[i].clip = p->clip;
    return NSERROR_OK;
}

static const struct plotter_table plotters = {
    .clip = plot_clip, .rectangle = plot_rect, .line = plot_rect, .text = plot_text,
};

static struct paint draw(struct textarea *ta, float scale)
{
    struct paint p = {0};
    struct redraw_context ctx = {.plot = &plotters, .priv = &p};
    struct rect clip = {0, 0, WIDTH * scale, HEIGHT * scale};
    textarea_redraw(ta, 0, 0, 0xffffff, scale, &clip, &ctx);
    return p;
}

static struct textarea *create(unsigned int flags)
{
    textarea_setup setup = {
        .width = WIDTH, .height = HEIGHT,
        .pad_top = PADDING, .pad_right = PADDING, .pad_bottom = PADDING, .pad_left = PADDING,
        .border_col = NS_TRANSPARENT,
        .text = {.size = 12 * PLOT_STYLE_SCALE, .weight = 400, .background = NS_TRANSPARENT},
    };
    modifications = full_redraws = copied = 0;
    last_value[0] = '\0';
    struct textarea *ta = textarea_create(flags, &setup, callback, NULL);
    ck_assert_ptr_nonnull(ta);
    return ta;
}

static void expect_text(struct textarea *ta, const char *expected)
{
    struct paint p = draw(ta, 1);
    ck_assert_uint_eq(p.count, 1);
    ck_assert_str_eq(p.runs[0].text, expected);
}

START_TEST(test_placeholder_editing)
{
    struct textarea *ta = create(modes[_i]);
    const char *hint = "Search …";
    ck_assert(textarea_set_placeholder(ta, hint));
    ck_assert_uint_eq(modifications, 0);
    ck_assert_int_eq(textarea_get_text(ta, NULL, 0), 1);
    ck_assert_str_eq(textarea_data(ta, NULL), "");
    expect_text(ta, hint);

    /* Focus and selection/copy must not consume or select the hint. */
    ck_assert(textarea_set_caret(ta, 0));
    expect_text(ta, hint);
    ck_assert(textarea_keypress(ta, NS_KEY_SELECT_ALL));
    ck_assert(textarea_keypress(ta, NS_KEY_COPY_SELECTION));
    ck_assert_uint_eq(copied, 0);
    unsigned int redraws = full_redraws;
    ck_assert(textarea_keypress(ta, 'x'));
    ck_assert_uint_gt(full_redraws, redraws);
    ck_assert_str_eq(last_value, "x");
    ck_assert_str_eq(textarea_data(ta, NULL), "x");
    expect_text(ta, modes[_i] == TEXTAREA_PASSWORD ? "•" : "x");

    redraws = full_redraws;
    ck_assert(textarea_keypress(ta, NS_KEY_DELETE_LEFT));
    ck_assert_uint_gt(full_redraws, redraws);
    ck_assert_str_eq(last_value, "");
    expect_text(ta, hint);
    ck_assert(textarea_keypress(ta, NS_KEY_UNDO));
    /* Password controls deliberately do not retain undo history. */
    ck_assert_str_eq(textarea_data(ta, NULL), modes[_i] == TEXTAREA_PASSWORD ? "" : "x");
    ck_assert(textarea_keypress(ta, NS_KEY_REDO));
    expect_text(ta, hint);

    /* Value assignment/reset and dropped text use other editing paths. */
    ck_assert(textarea_drop_text(ta, "new", 3));
    ck_assert_str_eq(textarea_data(ta, NULL), "new");
    redraws = full_redraws;
    ck_assert(textarea_set_text(ta, ""));
    ck_assert_uint_gt(full_redraws, redraws);
    expect_text(ta, hint);
    ck_assert(textarea_set_text(ta, " "));
    expect_text(ta, modes[_i] == TEXTAREA_PASSWORD ? "•" : " ");

    ck_assert(textarea_set_text(ta, ""));
    unsigned int modified = modifications;
    redraws = full_redraws;
    ck_assert(textarea_set_placeholder(ta, "Another"));
    ck_assert_uint_gt(full_redraws, redraws);
    ck_assert_uint_eq(modifications, modified);
    expect_text(ta, "Another");
    ck_assert(textarea_set_placeholder(ta, NULL));
    ck_assert_uint_eq(draw(ta, 1).count, 0);
    textarea_destroy(ta);
}
END_TEST

START_TEST(test_placeholder_lines_and_style)
{
    struct textarea *ta = create(TEXTAREA_MULTILINE);
    ck_assert(textarea_set_placeholder(ta, "café\r\n\rștiri\nfinal"));
    struct paint p = draw(ta, 1);
    ck_assert_uint_eq(p.count, 3);
    ck_assert_str_eq(p.runs[0].text, "café");
    ck_assert_str_eq(p.runs[1].text, "știri");
    ck_assert_str_eq(p.runs[2].text, "final");
    ck_assert_int_eq(p.runs[1].y - p.runs[0].y, 2 * (p.runs[2].y - p.runs[1].y));
    ck_assert_uint_eq(p.runs[0].style.foreground, 0x757575);

    ck_assert(textarea_set_placeholder(ta, "alpha beta gamma"));
    p = draw(ta, 1);
    ck_assert_uint_eq(p.count, 2);
    ck_assert_str_eq(p.runs[0].text, "alpha beta");
    ck_assert_str_eq(p.runs[1].text, "gamma");
    plot_font_style_t style = p.runs[0].style;
    style.foreground = 0x0000ff;
    style.weight = 700;
    style.flags = FONTF_ITALIC;
    textarea_set_placeholder_style(ta, &style);
    struct paint zoomed = draw(ta, 2);
    ck_assert_uint_eq(zoomed.count, 2);
    ck_assert_int_eq(zoomed.runs[0].x, PADDING * 2);
    ck_assert_int_eq(zoomed.runs[1].y, p.runs[1].y * 2);
    ck_assert_int_eq(zoomed.runs[0].clip.x1, (WIDTH - PADDING) * 2);
    ck_assert_uint_eq(zoomed.runs[0].style.foreground, style.foreground);
    ck_assert_int_eq(zoomed.runs[0].style.weight, 700);
    ck_assert_int_eq(zoomed.runs[0].style.size, style.size * 2);
    textarea_set_dimensions(ta, 60, HEIGHT);
    ck_assert_uint_eq(draw(ta, 1).count, 3);
    textarea_set_dimensions(ta, 0, HEIGHT);
    ck_assert_uint_eq(draw(ta, 1).count, 0);
    textarea_destroy(ta);

    ta = create(TEXTAREA_DEFAULT | TEXTAREA_READONLY);
    ck_assert(textarea_set_placeholder(ta, "a\r\nb\rc\nd"));
    expect_text(ta, "abcd");
    ck_assert_str_eq(textarea_data(ta, NULL), "");
    textarea_destroy(ta);
}
END_TEST

int main(void)
{
    static struct gui_layout_table fonts = {.width = width, .split = split};
    static struct gui_clipboard_table clipboard = {.set = copy};
    static struct wisp_table gui = {.layout = &fonts, .clipboard = &clipboard};
    guit = &gui;
    nscss_screen_dpi = INTTOFIX(96);
    Suite *suite = suite_create("Textarea placeholders");
    TCase *tc = tcase_create("Value separation and painting");
    tcase_add_loop_test(tc, test_placeholder_editing, 0, sizeof(modes) / sizeof(modes[0]));
    tcase_add_test(tc, test_placeholder_lines_and_style);
    suite_add_tcase(suite, tc);
    SRunner *runner = srunner_create(suite);
    srunner_run_all(runner, CK_NORMAL);
    int failed = srunner_ntests_failed(runner);
    srunner_free(runner);
    return failed ? EXIT_FAILURE : EXIT_SUCCESS;
}

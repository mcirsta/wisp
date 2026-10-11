#include <ctype.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libcss/libcss.h>

#include "utils/css_utils.h"

#include "dump.h"
#include "testutils.h"

/** \todo at some point, we need to extend this to handle nested blocks */
typedef struct line_ctx {
    size_t buflen;
    size_t bufused;
    uint8_t *buf;

    size_t explen;
    size_t expused;
    char *exp;

    bool indata;
    bool inerrors;
    bool inexp;

    bool inrule;
    bool failed;
} line_ctx;

static bool handle_line(const char *data, size_t datalen, void *pw);
static void css__parse_expected(line_ctx *ctx, const char *data, size_t len);
static bool run_test(const uint8_t *data, size_t len, const char *exp, size_t explen);

static css_error resolve_url(void *pw, const char *base, lwc_string *rel, lwc_string **abs)
{
    UNUSED(pw);
    UNUSED(base);

    /* About as useless as possible */
    *abs = lwc_string_ref(rel);

    return CSS_OK;
}

static bool fail_because_lwc_leaked = false;

static void printing_lwc_iterator(lwc_string *str, void *pw)
{
    UNUSED(pw);

    printf(" DICT: %*s\n", (int)(lwc_string_length(str)), lwc_string_data(str));
    fail_because_lwc_leaked = true;
}

int main(int argc, char **argv)
{
    line_ctx ctx;

    if (argc != 2) {
        printf("Usage: %s <filename>\n", argv[0]);
        return 1;
    }

    ctx.buflen = css__parse_filesize(argv[1]);
    if (ctx.buflen == 0)
        return 1;

    ctx.buf = malloc(ctx.buflen);
    if (ctx.buf == NULL) {
        printf("Failed allocating %u bytes\n", (unsigned int)ctx.buflen);
        return 1;
    }

    ctx.buf[0] = '\0';
    ctx.bufused = 0;
    ctx.explen = 0;
    ctx.expused = 0;
    ctx.exp = NULL;
    ctx.indata = false;
    ctx.inerrors = false;
    ctx.inexp = false;
    ctx.failed = false;

    assert(css__parse_testfile(argv[1], handle_line, &ctx) == true);

    /* and run final test */
    if (ctx.bufused > 0)
        ctx.failed |= !run_test(ctx.buf, ctx.bufused, ctx.exp, ctx.expused);

    free(ctx.buf);
    free(ctx.exp);

    lwc_iterate_strings(printing_lwc_iterator, NULL);

    assert(fail_because_lwc_leaked == false);

    printf(ctx.failed ? "FAIL\n" : "PASS\n");
    return ctx.failed ? EXIT_FAILURE : EXIT_SUCCESS;
}

bool handle_line(const char *data, size_t datalen, void *pw)
{
    line_ctx *ctx = (line_ctx *)pw;

    if (data[0] == '#') {
        if (ctx->inexp) {
            /* This marks end of testcase, so run it */

            ctx->failed |= !run_test(ctx->buf, ctx->bufused, ctx->exp, ctx->expused);

            ctx->buf[0] = '\0';
            ctx->bufused = 0;

            ctx->expused = 0;
        }

        if (ctx->indata && strncasecmp(data + 1, "errors", 6) == 0) {
            ctx->indata = false;
            ctx->inerrors = true;
            ctx->inexp = false;
        } else if (ctx->inerrors && strncasecmp(data + 1, "expected", 8) == 0) {
            ctx->indata = false;
            ctx->inerrors = false;
            ctx->inexp = true;
            ctx->inrule = false;
        } else if (ctx->inexp && strncasecmp(data + 1, "data", 4) == 0) {
            ctx->indata = true;
            ctx->inerrors = false;
            ctx->inexp = false;
        } else if (ctx->indata) {
            memcpy(ctx->buf + ctx->bufused, data, datalen);
            ctx->bufused += datalen;
        } else {
            ctx->indata = (strncasecmp(data + 1, "data", 4) == 0);
            ctx->inerrors = (strncasecmp(data + 1, "errors", 6) == 0);
            ctx->inexp = (strncasecmp(data + 1, "expected", 8) == 0);
        }
    } else {
        if (ctx->indata) {
            memcpy(ctx->buf + ctx->bufused, data, datalen);
            ctx->bufused += datalen;
        }
        if (ctx->inexp) {
            css__parse_expected(ctx, data, datalen);
        }
    }

    return true;
}

void css__parse_expected(line_ctx *ctx, const char *data, size_t len)
{
    while (ctx->expused + len >= ctx->explen) {
        size_t required = ctx->explen == 0 ? 64 : ctx->explen * 2;
        char *temp = realloc(ctx->exp, required);
        if (temp == NULL) {
            assert(0 && "No memory for expected output");
        }

        ctx->exp = temp;
        ctx->explen = required;
    }

    memcpy(ctx->exp + ctx->expused, data, len);

    ctx->expused += len;
}

bool run_test(const uint8_t *data, size_t len, const char *exp, size_t explen)
{
    /* Optional callbacks, including font-face notification, default to NULL. */
    css_stylesheet_params params = {0};
    css_stylesheet *sheet;
    css_error error;
    struct dump_output out = {0};
    static int testnum;

    params.params_version = CSS_STYLESHEET_PARAMS_VERSION_1;
    params.level = CSS_LEVEL_21;
    params.charset = "UTF-8";
    params.url = "foo";
    params.title = NULL;
    params.allow_quirks = false;
    params.inline_style = false;
    params.resolve = resolve_url;
    params.resolve_pw = NULL;
    params.import = NULL;
    params.import_pw = NULL;
    params.color = NULL;
    params.color_pw = NULL;
    params.font = NULL;
    params.font_pw = NULL;

    assert(css_stylesheet_create(&params, &sheet) == CSS_OK);

    error = css_stylesheet_append_data(sheet, data, len);
    if (error != CSS_OK && error != CSS_NEEDDATA) {
        printf("Failed appending data: %d\n", error);
        assert(0);
    }

    assert(css_stylesheet_data_done(sheet) == CSS_OK);

    testnum++;

    dump_sheet(sheet, &out);
    bool passed = !out.failed && out.length == explen && (explen == 0 || memcmp(out.data, exp, explen) == 0);
    if (out.failed) {
        fprintf(stderr, "Test %d: failed to allocate or format dump output\n", testnum);
    } else if (!passed) {
        printf("Test %d: result doesn't match expected\nExpected (%zu):\n", testnum, explen);
        if (explen != 0)
            fwrite(exp, 1, explen, stdout);
        printf("\nResult (%zu):\n", out.length);
        if (out.length != 0)
            fwrite(out.data, 1, out.length, stdout);
        putchar('\n');
    }

    css_stylesheet_destroy(sheet);

    dump_output_fini(&out);
    if (passed)
        printf("Test %d: PASS\n", testnum);
    return passed;
}

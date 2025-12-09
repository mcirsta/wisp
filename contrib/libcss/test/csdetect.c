#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <parserutils/charset/mibenum.h>

#include <libcss/libcss.h>

#include "charset/detect.h"
#include "utils/utils.h"

#include "testutils.h"

typedef struct line_ctx {
	size_t buflen;
	size_t bufused;
	uint8_t *buf;
	char enc[64];
	bool indata;
	bool inenc;
} line_ctx;

static bool handle_line(const char *data, size_t datalen, void *pw);
static void run_test(const uint8_t *data, size_t len, char *expected);

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
		printf("Failed allocating %u bytes\n",
				(unsigned int) ctx.buflen);
		return 1;
	}

	ctx.buf[0] = '\0';
	ctx.enc[0] = '\0';
	ctx.bufused = 0;
	ctx.indata = false;
	ctx.inenc = false;

	assert(css__parse_testfile(argv[1], handle_line, &ctx) == true);

	/* and run final test */
	if (ctx.bufused > 0 && ctx.buf[ctx.bufused - 1] == '\n')
		ctx.bufused -= 1;

	run_test(ctx.buf, ctx.bufused, ctx.enc);

	free(ctx.buf);

	printf("PASS\n");

	return 0;
}

bool handle_line(const char *data, size_t datalen, void *pw)
{
	line_ctx *ctx = (line_ctx *) pw;

	if (data[0] == '#') {
		if (ctx->inenc) {
			/* This marks end of testcase, so run it */

			if (ctx->buf[ctx->bufused - 1] == '\n')
				ctx->bufused -= 1;

			run_test(ctx->buf, ctx->bufused, ctx->enc);

			ctx->buf[0] = '\0';
			ctx->enc[0] = '\0';
			ctx->bufused = 0;
		}

		ctx->indata = (strncasecmp(data+1, "data", 4) == 0);
		ctx->inenc  = (strncasecmp(data+1, "encoding", 8) == 0);
	} else {
		if (ctx->indata) {
			memcpy(ctx->buf + ctx->bufused, data, datalen);
			ctx->bufused += datalen;
		}
        if (ctx->inenc) {
            /* Copy the raw encoding line into a local buffer and NUL-terminate */
            char line[64];
            size_t copy_len = datalen < sizeof(line) - 1 ? datalen : sizeof(line) - 1;
            memcpy(line, data, copy_len);
            line[copy_len] = '\0';

            /* Strip a single trailing newline (test lines are LF or CRLF) */
            size_t line_len = strlen(line);
            if (line_len > 0 && line[line_len - 1] == '\n') {
                line[--line_len] = '\0';
            }

            /* Remove UTF-8 BOM (EF BB BF) if present */
            if (line_len >= 3 && memcmp(line, "\xEF\xBB\xBF", 3) == 0) {
                size_t new_len = line_len - 3;
                memmove(line, line + 3, new_len);
                line[new_len] = '\0';
                line_len = new_len;
            }

            /* Trim leading/trailing ASCII whitespace (space, tab, CR) */
            size_t start = 0;
            size_t end = line_len;
            while (start < end && (line[start] == ' ' || line[start] == '\t' || line[start] == '\r')) {
                start++;
            }
            while (end > start && (line[end - 1] == ' ' || line[end - 1] == '\t' || line[end - 1] == '\r')) {
                end--;
            }
            if (start > 0 || end < line_len) {
                memmove(line, line + start, end - start);
                line[end - start] = '\0';
            }

            /* Normalize to a token containing only [A-Za-z0-9+-] */
            char token[64];
            size_t out = 0;
            for (size_t i = 0; line[i] != '\0'; i++) {
                unsigned char c = (unsigned char)line[i];
                if ((c >= 'A' && c <= 'Z') ||
                    (c >= 'a' && c <= 'z') ||
                    (c >= '0' && c <= '9') ||
                    c == '-' || c == '+') {
                    if (out < sizeof(token) - 1) {
                        token[out++] = (char)c;
                    }
                }
            }
            token[out] = '\0';

            /* Update expected encoding only if we parsed a non-empty token */
            if (token[0] != '\0') {
                strcpy(ctx->enc, token);
            }
        }
	}

	return true;
}

void run_test(const uint8_t *data, size_t len, char *expected)
{
	uint16_t mibenum = 0;
	css_charset_source source = CSS_CHARSET_DEFAULT;
	static int testnum;

	assert(css__charset_extract(data, len, &mibenum, &source) ==
			PARSERUTILS_OK);

	assert(mibenum != 0);

    printf("%d: Detected charset %s (%d) Source %d Expected %s (%d)\n",
            ++testnum, parserutils_charset_mibenum_to_name(mibenum),
            mibenum, source, expected,
            parserutils_charset_mibenum_from_name(
                expected, strlen(expected)));
    fflush(stdout);
    {
        size_t elen = strlen(expected);
        printf("Expected len=%zu bytes:", elen);
        for (size_t i = 0; i < elen; i++) {
            printf(" %02X", (unsigned char)expected[i]);
        }
        printf("\n");
        fflush(stdout);
    }

	assert(mibenum == parserutils_charset_mibenum_from_name(
			expected, strlen(expected)));
}

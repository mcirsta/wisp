/* HTML source sizes. Licensed under the MIT License. */
#ifndef libcss_sizes_h_
#define libcss_sizes_h_

#ifdef __cplusplus
extern "C" {
#endif
#include <libcss/errors.h>
#include <libcss/unit.h>

/* Evaluate HTML's comma-separated source-size list with the CSS tokenizer,
 * length/math grammar and media-condition matcher. Result is CSS pixels; invalid
 * entries are skipped and the default is 100vw. Font units use initial values.
 * auto_width is the HTML caller's eligible auto size, or negative if unavailable.
 * This never selects styles, lays out content or invokes script. */
css_error css_source_size(const uint8_t *text, size_t length, const css_unit_ctx *units, const css_media *media,
    double auto_width, double *pixels);
#ifdef __cplusplus
}
#endif
#endif

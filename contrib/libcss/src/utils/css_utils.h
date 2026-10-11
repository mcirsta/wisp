/*
 * This file is part of LibCSS.
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 * Copyright 2007-8 John-Mark Bell <jmb@netsurf-browser.org>
 */

#ifndef css_utils_h_
#define css_utils_h_

#include <stdio.h>

#include <libwapcaplet/libwapcaplet.h>

#include <libcss/errors.h>
#include <libcss/types.h>

#define CSS_UNIMPLEMENTED(msg)                                                                                         \
    fprintf(stderr, "[LibCSS Warning] Unimplemented feature encountered at %s:%d: %s\n", __FILE__, __LINE__, msg)

#ifndef max
#define max(a, b) ((a) > (b) ? (a) : (b))
#endif

#ifndef min
#define min(a, b) ((a) < (b) ? (a) : (b))
#endif

#ifndef SLEN
/* Calculate length of a string constant */
#define SLEN(s) (sizeof((s)) - 1) /* -1 for '\0' */
#endif

#ifndef UNUSED
#define UNUSED(x) ((void)(x))
#endif

#ifndef N_ELEMENTS
#define N_ELEMENTS(x) (sizeof((x)) / sizeof((x)[0]))
#endif

/* Allocation-free ASCII comparison with a lowercase keyword. */
bool css__keyword_equal(lwc_string *text, const char *keyword);

/* CSS Values <custom-ident>, after tokenization: excludes CSS-wide keywords
 * and default. Property-specific exclusions are checked by the caller. */
bool css__is_custom_ident(lwc_string *text);

/* The five generic families supported by the current font selection model. */
bool css__is_font_family_generic(lwc_string *text);

/* Sign of an already-tokenized decimal number, before fixed-point rounding.
 * Dimensions may follow the numeric prefix. This does not validate syntax. */
int css__number_sign(lwc_string *text);

css_fixed css__number_from_lwc_string(lwc_string *string, bool int_only, size_t *consumed);
css_fixed css__number_from_string(const uint8_t *data, size_t len, bool int_only, size_t *consumed);

static inline bool isDigit(uint8_t c)
{
    return '0' <= c && c <= '9';
}

static inline bool isHex(uint8_t c)
{
    return isDigit(c) || ('a' <= c && c <= 'f') || ('A' <= c && c <= 'F');
}

static inline uint32_t charToHex(uint8_t c)
{
    c -= '0';

    if (c > 9)
        c -= 'A' - '9' - 1;

    if (c > 15)
        c -= 'a' - 'A';

    return c;
}

static inline css_error css_error_from_lwc_error(lwc_error err)
{
    switch (err) {
    case lwc_error_ok:
        return CSS_OK;
    case lwc_error_oom:
        return CSS_NOMEM;
    case lwc_error_range:
        return CSS_BADPARM;
    default:
        break;
    }
    return CSS_INVALID;
}

#endif

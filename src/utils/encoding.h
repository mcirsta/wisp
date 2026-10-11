/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef WISP_UTILS_ENCODING_H
#define WISP_UTILS_ENCODING_H

#include <stddef.h>
#include <stdint.h>

/* Encoding Standard label lookup: trim ASCII whitespace and compare ASCII
 * case-insensitively. The input is length-bearing, not necessarily terminated.
 * Returns a static canonical name, or NULL for an unknown label. No allocation. */
const char *wisp_encoding_from_label(const char *label, size_t length);

/* Given a canonical name (or NULL for an unknown document encoding), return a
 * static name suitable for output. Replacement and UTF-16 become UTF-8. */
const char *wisp_encoding_for_output(const char *name);

/* A stateless scalar-to-byte encoder. Returns 0..255, or -1 for an unencodable
 * code point. Error policy (e.g. HTML decimal references) belongs to the caller.
 * The lookup returns NULL for encodings still handled by the platform backend. */
typedef int (*wisp_single_byte_encoder)(uint32_t codepoint);
wisp_single_byte_encoder wisp_encoding_get_single_byte_encoder(const char *canonical_name);

#endif

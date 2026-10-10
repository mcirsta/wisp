/* SPDX-License-Identifier: GPL-2.0-or-later
 * Encoding is a boundary operation: stored entries stay scalar UTF-8, with
 * original newlines and explicit byte lengths. No DOM or JS is accessed here.
 */
#include <errno.h>
#include <iconv.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <strings.h>
#include <utf8proc.h>
#include <wisp/content/fetch.h>
#include "content/handlers/html/form_data.h"

struct buffer {
    char *bytes;
    size_t length, capacity;
};

static nserror reserve(struct buffer *buffer, size_t extra)
{
    if (extra > SIZE_MAX - buffer->length - 1)
        return NSERROR_NOMEM;
    size_t needed = buffer->length + extra + 1;
    if (needed <= buffer->capacity)
        return NSERROR_OK;
    size_t capacity = buffer->capacity == 0 ? 64 : buffer->capacity;
    while (capacity < needed) {
        if (capacity > SIZE_MAX / 2) {
            capacity = needed;
            break;
        }
        capacity *= 2;
    }
    char *bytes = realloc(buffer->bytes, capacity);
    if (bytes == NULL)
        return NSERROR_NOMEM;
    buffer->bytes = bytes;
    buffer->capacity = capacity;
    return NSERROR_OK;
}

static nserror append(struct buffer *buffer, const char *bytes, size_t length)
{
    nserror result = reserve(buffer, length);
    if (result != NSERROR_OK)
        return result;
    if (length != 0)
        memcpy(buffer->bytes + buffer->length, bytes, length);
    buffer->length += length;
    buffer->bytes[buffer->length] = 0;
    return NSERROR_OK;
}

/* HTML's conversion to name/value pairs normalizes all newlines, including
 * filenames. Multipart does so for field names and string values, not files. */
static nserror normalize(dom_string *string, bool newlines, struct buffer *buffer)
{
    const char *bytes = dom_string_data(string);
    size_t length = dom_string_byte_length(string);
    if (!newlines)
        return append(buffer, bytes, length);
    size_t start = 0;
    for (size_t i = 0; i < length; i++) {
        if (bytes[i] != '\r' && bytes[i] != '\n')
            continue;
        nserror result = append(buffer, bytes + start, i - start);
        if (result == NSERROR_OK)
            result = append(buffer, "\r\n", 2);
        if (result != NSERROR_OK)
            return result;
        if (bytes[i] == '\r' && i + 1 < length && bytes[i + 1] == '\n')
            i++;
        start = i + 1;
    }
    return append(buffer, bytes + start, length - start);
}

static nserror convert_chunk(iconv_t converter, const char **input, size_t *remaining, struct buffer *output)
{
    for (;;) {
        nserror result = reserve(output, *remaining < 64 ? 64 : *remaining);
        if (result != NSERROR_OK)
            return result;
        char *out = output->bytes + output->length;
        size_t available = output->capacity - output->length - 1;
        size_t converted = iconv(converter, (void *)input, remaining, &out, &available);
        output->length = out - output->bytes;
        output->bytes[output->length] = 0;
        if (converted != (size_t)-1)
            return NSERROR_OK;
        if (errno != E2BIG)
            return NSERROR_BAD_ENCODING;
    }
}

static nserror encode(dom_string *string, bool newlines, const char *encoding, struct buffer *output)
{
    struct buffer normalized = {0};
    nserror result = normalize(string, newlines, &normalized);
    if (result != NSERROR_OK)
        goto out;
    if (strcasecmp(encoding, "UTF-8") == 0) {
        *output = normalized;
        return NSERROR_OK;
    }
    /* A descriptor per component avoids carrying shift state between names and
     * values. UTF-8, the common case, needs no converter. Encoding labels are
     * selected by the caller; unsupported labels fail rather than changing the
     * encoding silently or transliterating user input. */
    iconv_t converter = strchr(encoding, '/') == NULL ? iconv_open(encoding, "UTF-8") : (iconv_t)-1;
    if (converter == (iconv_t)-1) {
        result = NSERROR_BAD_ENCODING;
        goto out;
    }
    const char *input = normalized.bytes;
    size_t remaining = normalized.length;
    while (remaining != 0) {
        result = convert_chunk(converter, &input, &remaining, output);
        if (result != NSERROR_BAD_ENCODING)
            break;
        if (errno != EILSEQ)
            break;
        utf8proc_int32_t codepoint;
        utf8proc_ssize_t consumed = utf8proc_iterate((const uint8_t *)input, remaining, &codepoint);
        if (consumed <= 0)
            break;
        /* Encoding's HTML error mode uses decimal character references for
         * scalar values absent from a legacy output encoding. */
        char reference[16];
        const char *replacement = reference;
        size_t size = snprintf(reference, sizeof(reference), "&#%u;", (unsigned)codepoint);
        result = convert_chunk(converter, &replacement, &size, output);
        if (result != NSERROR_OK)
            break;
        input += consumed;
        remaining -= consumed;
    }
    if (result == NSERROR_OK) {
        /* Flush stateful encodings, including an empty component. */
        const char *end = NULL;
        size_t empty = 0;
        result = convert_chunk(converter, &end, &empty, output);
    }
    iconv_close(converter);
out:
    free(normalized.bytes);
    return result;
}

static nserror percent_encode(struct buffer *output, const struct buffer *input)
{
    static const char hex[] = "0123456789ABCDEF";
    if (input->length > SIZE_MAX / 3)
        return NSERROR_NOMEM;
    nserror result = reserve(output, input->length * 3);
    if (result != NSERROR_OK)
        return result;
    for (size_t i = 0; i < input->length; i++) {
        unsigned char c = input->bytes[i];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '*' || c == '-' ||
            c == '.' || c == '_') {
            output->bytes[output->length++] = c;
        } else if (c == ' ') {
            output->bytes[output->length++] = '+';
        } else {
            output->bytes[output->length++] = '%';
            output->bytes[output->length++] = hex[c >> 4];
            output->bytes[output->length++] = hex[c & 15];
        }
    }
    output->bytes[output->length] = 0;
    return NSERROR_OK;
}

nserror html_form_data_urlencode(const struct html_form_data *data, const char *encoding, char **output)
{
    *output = NULL;
    struct buffer encoded = {0};
    nserror result = append(&encoded, "", 0);
    for (size_t i = 0; result == NSERROR_OK && i < data->length; i++) {
        dom_string *strings[] = {data->entries[i].name, data->entries[i].value};
        if (i != 0)
            result = append(&encoded, "&", 1);
        for (unsigned j = 0; result == NSERROR_OK && j < 2; j++) {
            struct buffer component = {0};
            if (j != 0)
                result = append(&encoded, "=", 1);
            if (result == NSERROR_OK)
                result = encode(strings[j], true, encoding, &component);
            if (result == NSERROR_OK)
                result = percent_encode(&encoded, &component);
            free(component.bytes);
        }
    }
    if (result == NSERROR_OK)
        *output = encoded.bytes;
    else
        free(encoded.bytes);
    return result;
}

nserror
html_form_data_multipart(const struct html_form_data *data, const char *encoding, struct fetch_multipart_data **output)
{
    *output = NULL;
    struct fetch_multipart_data **next = output;
    nserror result = NSERROR_OK;
    for (size_t i = 0; i < data->length; i++) {
        const struct html_form_entry *entry = &data->entries[i];
        struct buffer name = {0}, value = {0};
        result = encode(entry->name, true, encoding, &name);
        if (result == NSERROR_OK)
            result = encode(entry->value, entry->file_path == NULL, encoding, &value);
        /* libcurl's MIME name/filename API takes C strings. Never truncate a
         * header silently; a byte-oriented multipart header writer is pending. */
        if (result == NSERROR_OK &&
            (memchr(name.bytes, 0, name.length) != NULL ||
                (entry->file_path != NULL && memchr(value.bytes, 0, value.length) != NULL)))
            result = NSERROR_NOT_IMPLEMENTED;
        struct fetch_multipart_data *part = result == NSERROR_OK ? calloc(1, sizeof(*part)) : NULL;
        if (result == NSERROR_OK && part == NULL)
            result = NSERROR_NOMEM;
        if (result != NSERROR_OK) {
            free(name.bytes);
            free(value.bytes);
            break;
        }
        part->name = name.bytes;
        part->value = value.bytes;
        part->value_length = value.length;
        part->file = entry->file_path != NULL;
        *next = part;
        next = &part->next;
        if (part->file) {
            part->rawfile = strdup(entry->file_path);
            if (part->rawfile == NULL) {
                result = NSERROR_NOMEM;
                break;
            }
        }
    }
    if (result != NSERROR_OK) {
        fetch_multipart_data_destroy(*output);
        *output = NULL;
    }
    return result;
}

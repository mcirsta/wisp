/* Growable output for test diagnostics. Licensed under the MIT License. */
#ifndef test_dump_output_h_
#define test_dump_output_h_

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Zero-initialize before use. Failure is sticky and preserves the completed
 * prefix; callers must check failed before comparing or printing the result. */
struct dump_output {
    char *data;
    size_t length, capacity;
    bool failed;
};

static bool dump_reserve(struct dump_output *out, size_t extra)
{
    if (out->failed)
        return false;
    if (out->length == SIZE_MAX || extra > SIZE_MAX - out->length - 1) {
        out->failed = true;
        return false;
    }
    size_t needed = out->length + extra + 1;
    if (needed <= out->capacity)
        return true;
    size_t capacity = out->capacity != 0 ? out->capacity : 256;
    while (capacity < needed) {
        if (capacity > SIZE_MAX / 2) {
            capacity = needed;
            break;
        }
        capacity *= 2;
    }
    char *data = realloc(out->data, capacity);
    if (data == NULL) {
        out->failed = true;
        return false;
    }
    out->data = data;
    out->capacity = capacity;
    out->data[out->length] = '\0';
    return true;
}

static void dump_write(struct dump_output *out, const char *data, size_t length)
{
    if (!dump_reserve(out, length))
        return;
    if (length != 0)
        memcpy(out->data + out->length, data, length);
    out->length += length;
    out->data[out->length] = '\0';
}

static void dump_char(struct dump_output *out, char ch)
{
    dump_write(out, &ch, 1);
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((format(printf, 2, 3)))
#endif
static void dump_printf(struct dump_output *out, const char *format, ...)
{
    if (out->failed)
        return;
    size_t available = out->capacity - out->length;
    va_list args, retry;
    va_start(args, format);
    va_copy(retry, args);
    int written = vsnprintf(out->data != NULL ? out->data + out->length : NULL, available, format, args);
    va_end(args);
    if (written < 0) {
        out->failed = true;
    } else if ((size_t)written >= available) {
        if (dump_reserve(out, (size_t)written)) {
            int actual = vsnprintf(out->data + out->length, out->capacity - out->length, format, retry);
            if (actual != written)
                out->failed = true;
        }
    }
    va_end(retry);
    if (!out->failed)
        out->length += (size_t)written;
    if (out->data != NULL)
        out->data[out->length] = '\0';
}

static void dump_output_fini(struct dump_output *out)
{
    free(out->data);
    *out = (struct dump_output){0};
}

#endif

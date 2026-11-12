/* SPDX-License-Identifier: GPL-2.0-or-later
 * HTTP representation helpers. No JS callbacks or network side effects here.
 */
#include "xhr_internal.h"
#include "dom.h"
#include "utils/http/content-type.h"
#include <wisp/utils/corestrings.h>
#include <errno.h>
#include <iconv.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

char *qjs_xhr_bytestring(JSContext *ctx, JSValueConst value, size_t *byte_length)
{
    size_t length;
    const uint16_t *units = JS_ToCStringLenUTF16(ctx, &length, value);
    if (units == NULL)
        return NULL;
    char *bytes = js_malloc(ctx, length + 1);
    if (bytes != NULL) {
        for (size_t i = 0; i < length; i++) {
            if (units[i] > 255) {
                JS_ThrowTypeError(ctx, "ByteString contains a character above 255");
                js_free(ctx, bytes);
                bytes = NULL;
                break;
            }
            bytes[i] = units[i];
        }
        if (bytes != NULL)
            bytes[length] = 0;
    }
    *byte_length = length;
    JS_FreeCStringUTF16(ctx, units);
    return bytes;
}

JSValue qjs_xhr_from_bytes(JSContext *ctx, const char *bytes)
{
    size_t n = strlen(bytes);
    uint16_t *units = js_malloc(ctx, (n + 1) * sizeof(*units));
    if (units == NULL)
        return JS_EXCEPTION;
    for (size_t i = 0; i < n; i++)
        units[i] = (unsigned char)bytes[i];
    JSValue value = JS_NewStringUTF16(ctx, units, n);
    js_free(ctx, units);
    return value;
}

bool qjs_xhr_token(const char *text)
{
    if (*text == 0)
        return false;
    for (const unsigned char *p = (const unsigned char *)text; *p; p++) {
        if (!((*p >= '0' && *p <= '9') || (*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') ||
                strchr("!#$%&'*+-.^_`|~", *p)))
            return false;
    }
    return true;
}

bool qjs_xhr_forbidden_header(const char *name, const char *value)
{
    static const char *names[] = {"accept-charset", "accept-encoding", "access-control-request-headers",
        "access-control-request-method", "connection", "content-length", "cookie", "cookie2", "date", "dnt", "expect",
        "host", "keep-alive", "origin", "permissions-policy", "referer", "set-cookie", "te", "trailer",
        "transfer-encoding", "upgrade", "via"};
    if (!strncasecmp(name, "proxy-", 6) || !strncasecmp(name, "sec-", 4))
        return true;
    for (size_t i = 0; i < sizeof(names) / sizeof(*names); i++)
        if (!strcasecmp(name, names[i]))
            return true;
    if (!strcasecmp(name, "x-http-method") || !strcasecmp(name, "x-http-method-override") ||
        !strcasecmp(name, "x-method-override")) {
        for (const char *p = value; *p;) {
            p += strspn(p, " \t,");
            size_t n = strcspn(p, ",");
            size_t end = n;
            while (end && (p[end - 1] == ' ' || p[end - 1] == '\t'))
                end--;
            if ((end == 5 && !strncasecmp(p, "TRACE", 5)) || (end == 5 && !strncasecmp(p, "TRACK", 5)) ||
                (end == 7 && !strncasecmp(p, "CONNECT", 7)))
                return true;
            p += n;
        }
    }
    return false;
}

void qjs_xhr_headers_free(struct xhr_header **headers)
{
    while (*headers != NULL) {
        struct xhr_header *entry = *headers;
        *headers = entry->next;
        free(entry->name);
        free(entry->value);
        free(entry);
    }
}

const char *qjs_xhr_header(struct xhr_header *headers, const char *name)
{
    for (; headers; headers = headers->next)
        if (!strcasecmp(headers->name, name))
            return headers->value;
    return NULL;
}

bool qjs_xhr_header_add(struct xhr_header **headers, const char *name, const char *value)
{
    /* Bound both author and server headers, including repeated appends. */
    size_t size = strlen(name) + strlen(value) + 4;
    if (size > 65536)
        return false;
    for (struct xhr_header *h = *headers; h; h = h->next) {
        size_t bytes = strlen(h->name) + strlen(h->value) + 4;
        if (bytes > 65536 - size)
            return false;
        size += bytes;
    }
    /* Sorted lowercase names give getAllResponseHeaders its specified order. */
    while (*headers && strcasecmp((*headers)->name, name) < 0)
        headers = &(*headers)->next;
    if (*headers && !strcasecmp((*headers)->name, name)) {
        size_t old = strlen((*headers)->value), add = strlen(value);
        char *combined = realloc((*headers)->value, old + add + 3);
        if (combined == NULL)
            return false;
        memcpy(combined + old, ", ", 2);
        memcpy(combined + old + 2, value, add + 1);
        (*headers)->value = combined;
        return true;
    }
    struct xhr_header *entry = calloc(1, sizeof(*entry));
    if (entry == NULL)
        return false;
    entry->name = strdup(name);
    entry->value = strdup(value);
    if (entry->name == NULL || entry->value == NULL) {
        free(entry->name);
        free(entry->value);
        free(entry);
        return false;
    }
    for (char *p = entry->name; *p; p++)
        if (*p >= 'A' && *p <= 'Z')
            *p += 'a' - 'A';
    entry->next = *headers;
    *headers = entry;
    return true;
}

bool qjs_xhr_response_header(struct qjs_xhr *xhr, const uint8_t *bytes, size_t length)
{
    if (length > 65536 || memchr(bytes, 0, length))
        return false;
    char *line = strndup((const char *)bytes, length);
    if (line == NULL)
        return false;
    while (length &&
        (line[length - 1] == '\r' || line[length - 1] == '\n' || line[length - 1] == ' ' || line[length - 1] == '\t'))
        line[--length] = 0;
    bool ok = true;
    if (!strncmp(line, "HTTP/", 5)) {
        qjs_xhr_headers_free(&xhr->response_headers);
        free(xhr->reason);
        xhr->reason = NULL;
        char *status = strchr(line, ' '), *reason = NULL;
        if (status != NULL) {
            xhr->status = strtol(status + 1, &reason, 10);
            while (*reason == ' ')
                reason++;
        }
        xhr->reason = strdup(reason ? reason : "");
        ok = xhr->reason != NULL;
    } else {
        char *colon = strchr(line, ':');
        if (colon != NULL) {
            *colon++ = 0;
            colon += strspn(colon, " \t");
            if (qjs_xhr_token(line) && strcasecmp(line, "set-cookie") && strcasecmp(line, "set-cookie2"))
                ok = qjs_xhr_header_add(&xhr->response_headers, line, colon);
        }
    }
    free(line);
    return ok;
}

static unsigned effective_port(nsurl *url)
{
    lwc_string *part = nsurl_get_component(url, NSURL_PORT);
    unsigned port = nsurl_get_scheme_type(url) == NSURL_SCHEME_HTTPS ? 443 : 80;
    if (part != NULL) {
        port = strtoul(lwc_string_data(part), NULL, 10);
        lwc_string_unref(part);
    }
    return port;
}

bool qjs_xhr_same_origin(nsurl *a, nsurl *b)
{
    if (a == NULL || b == NULL)
        return false;
    enum nsurl_scheme_type scheme = nsurl_get_scheme_type(a);
    return (scheme == NSURL_SCHEME_HTTP || scheme == NSURL_SCHEME_HTTPS) &&
        !nsurl_has_component(b, NSURL_CREDENTIALS) && nsurl_compare(a, b, NSURL_SCHEME | NSURL_HOST) &&
        effective_port(a) == effective_port(b);
}

static size_t invalid_utf8_prefix(const unsigned char *p, size_t length)
{
    unsigned needed = *p >= 0xc2 && *p <= 0xdf ? 2 : *p >= 0xe0 && *p <= 0xef ? 3 : *p >= 0xf0 && *p <= 0xf4 ? 4 : 1;
    size_t i = 1;
    while (i < needed && i < length) {
        unsigned low = 0x80, high = 0xbf;
        if (i == 1) {
            if (*p == 0xe0)
                low = 0xa0;
            if (*p == 0xed)
                high = 0x9f;
            if (*p == 0xf0)
                low = 0x90;
            if (*p == 0xf4)
                high = 0x8f;
        }
        if (p[i] < low || p[i] > high)
            break;
        i++;
    }
    return i;
}

static lwc_string *response_charset(const char *mime)
{
    http_content_type *type = NULL;
    lwc_string *charset = NULL;
    if (mime && http_parse_content_type(mime, &type) == NSERROR_OK && type) {
        http_parameter_list_find_item(type->parameters, corestring_lwc_charset, &charset);
        http_content_type_destroy(type);
    }
    return charset;
}

/* Decode the received byte sequence on demand, caching unchanged responses.
 * Decoding from the beginning makes partial multibyte sequences safe: a later
 * responseText replaces the earlier partial result, never appends mojibake.
 * The existing platform iconv backend supplies legacy encodings; UTF BOMs win. */
JSValue qjs_xhr_text(struct qjs_xhr *xhr)
{
    JSContext *ctx = xhr->thread->ctx;
    if (xhr->state < 3 || xhr->length == 0)
        return JS_NewString(ctx, "");
    if (!JS_IsUndefined(xhr->text) && xhr->text_length == xhr->length)
        return JS_DupValue(ctx, xhr->text);
    lwc_string *charset = response_charset(qjs_xhr_header(xhr->response_headers, "content-type"));
    lwc_string *override = response_charset(xhr->mime);
    /* A MIME override without charset preserves the response's encoding. */
    if (override) {
        if (charset)
            lwc_string_unref(charset);
        charset = override;
    }
    const char *encoding = charset ? lwc_string_data(charset) : "UTF-8";
    if (!strcasecmp(encoding, "iso-8859-1") || !strcasecmp(encoding, "latin1") || !strcasecmp(encoding, "us-ascii"))
        encoding = "WINDOWS-1252";
    if (!strcasecmp(encoding, "utf-16"))
        encoding = "UTF-16LE";
    char *input = (char *)xhr->bytes;
    size_t left = xhr->length;
    if (left >= 3 && !memcmp(input, "\xef\xbb\xbf", 3)) {
        encoding = "UTF-8";
        input += 3;
        left -= 3;
    } else if (left >= 2 && (!memcmp(input, "\xff\xfe", 2) || !memcmp(input, "\xfe\xff", 2))) {
        encoding = (unsigned char)input[0] == 255 ? "UTF-16LE" : "UTF-16BE";
        input += 2;
        left -= 2;
    }
    bool utf8 = !strcasecmp(encoding, "UTF-8") || !strcasecmp(encoding, "UTF8");
    bool utf16 = !strncasecmp(encoding, "UTF-16", 6);
    iconv_t codec = iconv_open("UTF-8", encoding);
    if (codec == (iconv_t)-1) {
        codec = iconv_open("UTF-8", "UTF-8"); /* Unknown labels default to UTF-8. */
        utf8 = true;
        utf16 = false;
    }
    if (charset)
        lwc_string_unref(charset);
    if (codec == (iconv_t)-1)
        return qjs_dom_exception(ctx, DOM_NOT_SUPPORTED_ERR);
    size_t capacity = left * 4 + 4, space = capacity;
    char *buffer = js_malloc(ctx, capacity), *output = buffer;
    if (buffer == NULL) {
        iconv_close(codec);
        return JS_EXCEPTION;
    }
    while (left) {
        if (iconv(codec, &input, &left, &output, &space) != (size_t)-1)
            break;
        if (errno != EILSEQ && errno != EINVAL) {
            js_free(ctx, buffer);
            iconv_close(codec);
            return JS_ThrowInternalError(ctx, "XHR text conversion failed");
        }
        memcpy(output, "\xef\xbf\xbd", 3);
        output += 3;
        space -= 3;
        /* At end of input, one incomplete sequence produces one replacement. */
        size_t skip = errno == EINVAL ? left
            : utf8                    ? invalid_utf8_prefix((const unsigned char *)input, left)
            : utf16 && left >= 2      ? 2
                                      : 1;
        input += skip;
        left -= skip;
    }
    JSValue text = JS_NewStringLen(ctx, buffer, output - buffer);
    js_free(ctx, buffer);
    iconv_close(codec);
    if (JS_IsException(text))
        return text;
    JS_FreeValue(ctx, xhr->text);
    xhr->text = JS_DupValue(ctx, text);
    xhr->text_length = xhr->length;
    return text;
}

/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef WISP_QUICKJS_XHR_INTERNAL_H
#define WISP_QUICKJS_XHR_INTERNAL_H
#include "xhr.h"
#include <wisp/utils/nsurl.h>

struct xhr_header {
    struct xhr_header *next;
    char *name, *value;
};
struct qjs_xhr {
    jsthread *thread; /* Borrowed until close; requests never delay Window load. */
    struct qjs_xhr *next, **previous;
    JSValue object; /* Borrowed identity. */
    JSValue active; /* External root while a fetch or delivery task is pending. */
    JSValue target, text;
    nsurl *url, *origin;
    char *method, *reason, *mime;
    struct xhr_header *request_headers, *response_headers;
    struct fetch *fetch;
    uint8_t *bytes;
    size_t length, capacity, text_length;
    uint64_t generation, started, progress_at;
    uint32_t timeout;
    unsigned redirects;
    int state, status;
    bool sending, scheduled, start_pending, headers_ready, finished, text_type;
    const char *failure; /* Static event type: error or timeout. */
};

struct qjs_xhr *qjs_xhr_get(JSContext *ctx, JSValueConst object);
void qjs_xhr_cancel(struct qjs_xhr *xhr);
void qjs_xhr_clear_response(struct qjs_xhr *xhr);
bool qjs_xhr_event(struct qjs_xhr *xhr, const char *type);
bool qjs_xhr_begin(struct qjs_xhr *xhr);
bool qjs_xhr_timeout_arm(struct qjs_xhr *xhr);
bool qjs_xhr_fail(struct qjs_xhr *xhr, const char *type);

char *qjs_xhr_bytestring(JSContext *ctx, JSValueConst value, size_t *length);
JSValue qjs_xhr_from_bytes(JSContext *ctx, const char *bytes);
bool qjs_xhr_token(const char *text);
bool qjs_xhr_forbidden_header(const char *name, const char *value);
void qjs_xhr_headers_free(struct xhr_header **headers);
bool qjs_xhr_header_add(struct xhr_header **headers, const char *name, const char *value);
const char *qjs_xhr_header(struct xhr_header *headers, const char *name);
bool qjs_xhr_response_header(struct qjs_xhr *xhr, const uint8_t *bytes, size_t length);
JSValue qjs_xhr_text(struct qjs_xhr *xhr);
bool qjs_xhr_same_origin(nsurl *a, nsurl *b);
#endif

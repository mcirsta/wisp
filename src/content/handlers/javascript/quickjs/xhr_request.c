/* SPDX-License-Identifier: GPL-2.0-or-later
 * Async XHR transport and delivery. Fetch callbacks only accumulate native
 * state; the frontend scheduler enters JS after the fetch stack has unwound.
 */
#include "xhr_internal.h"
#include "event.h"
#include <wisp/content/fetch.h>
#include <wisp/desktop/gui_internal.h>
#include <wisp/misc.h>
#include <nsutils/time.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Bound network-owned memory independently of the JS heap budget. Exceeding the
 * limit fails the request, rather than returning a successful truncated body. */
#define XHR_BODY_LIMIT (16u * 1024u * 1024u)
static void xhr_deliver(void *pw);
static void xhr_timeout(void *pw);

static void ignored_fetch(const fetch_msg *message, void *pw)
{
    (void)message;
    (void)pw;
}

static void stop_fetch(struct qjs_xhr *xhr)
{
    if (xhr->fetch != NULL) {
        fetch_change_callback(xhr->fetch, ignored_fetch, NULL);
        fetch_abort(xhr->fetch);
        xhr->fetch = NULL;
    }
}

static void release_active(struct qjs_xhr *xhr)
{
    JSValue active = xhr->active;
    xhr->active = JS_UNDEFINED;
    JS_FreeValue(xhr->thread->ctx, active);
    /* Without a caller root, xhr may have been freed. */
}

void qjs_xhr_cancel(struct qjs_xhr *xhr)
{
    xhr->generation++;
    xhr->sending = false;
    stop_fetch(xhr);
    if (xhr->scheduled)
        guit->misc->schedule(-1, xhr_deliver, xhr);
    if (!JS_IsUndefined(xhr->active))
        guit->misc->schedule(-1, xhr_timeout, xhr);
    xhr->scheduled = xhr->start_pending = false;
    release_active(xhr);
}

void qjs_xhr_clear_response(struct qjs_xhr *xhr)
{
    qjs_xhr_headers_free(&xhr->response_headers);
    free(xhr->bytes);
    free(xhr->reason);
    xhr->bytes = NULL;
    xhr->reason = NULL;
    xhr->length = xhr->capacity = 0;
    xhr->status = 0;
    xhr->finished = xhr->headers_ready = false;
    xhr->failure = NULL;
    JS_FreeValue(xhr->thread->ctx, xhr->text);
    xhr->text = JS_UNDEFINED;
}

bool qjs_xhr_event(struct qjs_xhr *xhr, const char *type)
{
    jsthread *thread = xhr->thread;
    if (thread == NULL || thread->closed)
        return false;
    bool plain = !strcmp(type, "readystatechange");
    const char *length = qjs_xhr_header(xhr->response_headers, "content-length");
    double total = length ? strtod(length, NULL) : 0;
    JSValue event = plain ? qjs_event_create(thread->ctx, type)
                          : qjs_progress_event_create(thread->ctx, type, xhr->length, total);
    qjs_trace(thread->ctx, "xhr.dispatch", "type=%s state=%d status=%d bytes=%zu", type, xhr->state, xhr->status,
        xhr->length);
    bool ok = qjs_dispatch_host_event(thread, xhr->object, event, "xhr");
    JS_FreeValue(thread->ctx, event);
    return ok;
}

static bool current(struct qjs_xhr *xhr, uint64_t generation)
{
    return xhr->thread != NULL && !xhr->thread->closed && xhr->generation == generation;
}

/* Caller roots the object and context across potentially destructive listeners. */
bool qjs_xhr_fail(struct qjs_xhr *xhr, const char *type)
{
    stop_fetch(xhr);
    guit->misc->schedule(-1, xhr_timeout, xhr);
    qjs_xhr_clear_response(xhr);
    xhr->sending = false;
    xhr->state = 4;
    uint64_t generation = xhr->generation;
    if (!qjs_xhr_event(xhr, "readystatechange"))
        return false;
    if (!current(xhr, generation))
        return true;
    if (!qjs_xhr_event(xhr, type))
        return false;
    return !current(xhr, generation) || qjs_xhr_event(xhr, "loadend");
}

static bool schedule_delivery(struct qjs_xhr *xhr, int delay)
{
    if (xhr->scheduled)
        return true;
    if (guit->misc->schedule(delay, xhr_deliver, xhr) != NSERROR_OK) {
        /* Scheduler failure cannot run script on the fetch stack. Cancel all
         * work and roots; never leave a callback pointing at freed storage. */
        qjs_xhr_cancel(xhr);
        return false;
    }
    xhr->scheduled = true;
    return true;
}

static bool append_data(struct qjs_xhr *xhr, const uint8_t *bytes, size_t length)
{
    if (length > XHR_BODY_LIMIT - xhr->length)
        return false;
    size_t needed = xhr->length + length;
    if (needed > xhr->capacity) {
        size_t capacity = xhr->capacity ? xhr->capacity * 2 : 4096;
        if (capacity < needed)
            capacity = needed;
        uint8_t *buffer = realloc(xhr->bytes, capacity);
        if (buffer == NULL)
            return false;
        xhr->bytes = buffer;
        xhr->capacity = capacity;
    }
    if (length)
        memcpy(xhr->bytes + xhr->length, bytes, length);
    xhr->length = needed;
    return true;
}

static void xhr_fetch(const fetch_msg *message, void *pw)
{
    struct qjs_xhr *xhr = pw;
    switch (message->type) {
    case FETCH_HEADER: {
        const uint8_t *bytes = message->data.header_or_data.buf;
        size_t length = message->data.header_or_data.len;
        if (!qjs_xhr_response_header(xhr, bytes, length)) {
            xhr->failure = "error";
            stop_fetch(xhr);
            break;
        }
        bool end = (length == 2 && bytes[0] == '\r' && bytes[1] == '\n') || (length == 1 && bytes[0] == '\n');
        bool redirect = xhr->status == 301 || xhr->status == 302 || xhr->status == 303 || xhr->status == 307 ||
            xhr->status == 308;
        const char *location = qjs_xhr_header(xhr->response_headers, "location");
        /* Headers can arrive long before the first body byte. Only publish a
         * final response: interim and followed-redirect headers stay private. */
        if (!end || xhr->status < 200 || (redirect && location && *location))
            return;
        xhr->headers_ready = true;
        break;
    }
    case FETCH_DATA:
        xhr->headers_ready = true;
        if (!append_data(xhr, message->data.header_or_data.buf, message->data.header_or_data.len)) {
            xhr->failure = "error";
            stop_fetch(xhr);
        }
        break;
    case FETCH_FINISHED:
        xhr->fetch = NULL;
        xhr->headers_ready = xhr->finished = true;
        break;
    case FETCH_REDIRECT: {
        /* The fetcher does not follow redirects itself. Check every hop before
         * sending credentials or exposing headers/body from another origin. */
        xhr->fetch = NULL;
        nsurl *next = NULL;
        nserror err = nsurl_join(xhr->url, message->data.redirect, &next);
        if (err != NSERROR_OK || ++xhr->redirects > 20 || !qjs_xhr_same_origin(xhr->origin, next)) {
            if (next)
                nsurl_unref(next);
            xhr->failure = "error";
            break;
        }
        if (xhr->url)
            nsurl_unref(xhr->url);
        nsurl *clean = NULL;
        if (nsurl_defragment(next, &clean) != NSERROR_OK) {
            nsurl_unref(next);
            xhr->url = NULL;
            xhr->failure = "error";
            break;
        }
        nsurl_unref(next);
        xhr->url = clean;
        qjs_xhr_clear_response(xhr);
        xhr->start_pending = true;
        break;
    }
    case FETCH_TIMEDOUT:
        xhr->fetch = NULL;
        xhr->failure = "timeout";
        break;
    case FETCH_ERROR:
    case FETCH_AUTH:
    case FETCH_CERT_ERR:
    case FETCH_SSL_ERR:
    case FETCH_NOTMODIFIED: /* Raw fetches deliver 304 as a normal response. */
        xhr->fetch = NULL;
        xhr->failure = "error";
        break;
    default:
        return;
    }
    uint64_t now = 0;
    nsu_getmonotonic_ms(&now);
    int delay = !xhr->finished && !xhr->failure && xhr->state == 3 && now < xhr->progress_at + 50
        ? (int)(xhr->progress_at + 50 - now)
        : 0;
    if (xhr->scheduled && (xhr->finished || xhr->failure)) {
        guit->misc->schedule(-1, xhr_deliver, xhr);
        xhr->scheduled = false;
    }
    schedule_delivery(xhr, delay);
}

static void start_fetch(struct qjs_xhr *xhr)
{
    xhr->start_pending = false;
    if (!qjs_xhr_same_origin(xhr->origin, xhr->url)) {
        xhr->failure = "error";
        return;
    }
    size_t count = 0;
    for (struct xhr_header *h = xhr->request_headers; h; h = h->next)
        count++;
    char **headers = calloc(count + 2, sizeof(*headers));
    if (headers == NULL) {
        xhr->failure = "error";
        return;
    }
    size_t i = 0;
    bool ok = true;
    for (struct xhr_header *h = xhr->request_headers; h; h = h->next) {
        size_t n = strlen(h->name) + strlen(h->value) + 3;
        headers[i] = malloc(n);
        if (headers[i] == NULL) {
            ok = false;
            break;
        }
        /* libcurl uses Name; to transmit an empty header, Name: removes it. */
        snprintf(headers[i++], n, *h->value ? "%s: %s" : "%s;", h->name, h->value);
    }
    if (ok && !qjs_xhr_header(xhr->request_headers, "accept")) {
        headers[i] = strdup("Accept: */*");
        ok = headers[i++] != NULL;
    }
    nserror err = ok ? fetch_start_raw(xhr->url, xhr->origin, xhr_fetch, xhr, (const char **)headers, &xhr->fetch)
                     : NSERROR_NOMEM;
    for (size_t j = 0; j < i; j++)
        free(headers[j]);
    free(headers);
    if (err != NSERROR_OK)
        xhr->failure = "error";
}

static void xhr_deliver(void *pw)
{
    struct qjs_xhr *xhr = pw;
    jsthread *thread = xhr->thread;
    qjs_thread_ref(thread);
    JSValue root = JS_DupValue(thread->ctx, xhr->active);
    uint64_t generation = xhr->generation;
    xhr->scheduled = false;
    bool ok = true;
    if (xhr->start_pending)
        start_fetch(xhr);
    if (xhr->failure) {
        ok = qjs_xhr_fail(xhr, xhr->failure);
        goto out;
    }
    if (xhr->headers_ready && xhr->state == 1) {
        xhr->state = 2;
        if (!(ok = qjs_xhr_event(xhr, "readystatechange")) || !current(xhr, generation))
            goto out;
    }
    if (xhr->length) {
        xhr->state = 3;
        if (!(ok = qjs_xhr_event(xhr, "readystatechange")) || !current(xhr, generation))
            goto out;
    }
    if (xhr->length || xhr->finished) {
        nsu_getmonotonic_ms(&xhr->progress_at);
        if (!(ok = qjs_xhr_event(xhr, "progress")) || !current(xhr, generation))
            goto out;
    }
    if (xhr->finished) {
        guit->misc->schedule(-1, xhr_timeout, xhr);
        xhr->sending = false;
        xhr->state = 4;
        if (!(ok = qjs_xhr_event(xhr, "readystatechange")) || !current(xhr, generation))
            goto out;
        if (!(ok = qjs_xhr_event(xhr, "load")) || !current(xhr, generation))
            goto out;
        ok = qjs_xhr_event(xhr, "loadend");
    }
out:
    if (current(xhr, generation) && (!xhr->sending || !ok || thread->heap->interrupted))
        qjs_xhr_cancel(xhr);
    JS_FreeValue(thread->ctx, root);
    qjs_thread_unref(thread);
}

bool qjs_xhr_timeout_arm(struct qjs_xhr *xhr)
{
    if (!xhr->sending)
        return true;
    if (!xhr->timeout) {
        guit->misc->schedule(-1, xhr_timeout, xhr);
        return true;
    }
    uint64_t now = 0;
    if (nsu_getmonotonic_ms(&now) != NSUERROR_OK)
        return false;
    uint64_t due = xhr->started + xhr->timeout;
    uint64_t delay = due > now ? due - now : 0;
    return guit->misc->schedule(delay > INT_MAX ? INT_MAX : (int)delay, xhr_timeout, xhr) == NSERROR_OK;
}

static void xhr_timeout(void *pw)
{
    struct qjs_xhr *xhr = pw;
    uint64_t now = 0;
    nsu_getmonotonic_ms(&now);
    if (now < xhr->started + xhr->timeout) {
        if (qjs_xhr_timeout_arm(xhr))
            return;
    }
    stop_fetch(xhr);
    xhr->failure = "timeout";
    schedule_delivery(xhr, 0);
}

bool qjs_xhr_begin(struct qjs_xhr *xhr)
{
    if (nsu_getmonotonic_ms(&xhr->started) != NSUERROR_OK)
        return false;
    xhr->sending = xhr->start_pending = true;
    xhr->active = JS_DupValue(xhr->thread->ctx, xhr->object);
    xhr->redirects = 0;
    if (!qjs_xhr_timeout_arm(xhr) || !schedule_delivery(xhr, 0)) {
        qjs_xhr_cancel(xhr);
        return false;
    }
    return true;
}

void qjs_xhr_close(jsthread *thread)
{
    while (thread->requests) {
        struct qjs_xhr *xhr = thread->requests;
        JSValue root = JS_DupValue(thread->ctx, xhr->object);
        qjs_xhr_cancel(xhr);
        thread->requests = xhr->next;
        if (xhr->next)
            xhr->next->previous = &thread->requests;
        xhr->previous = NULL;
        xhr->thread = NULL;
        JS_FreeValue(thread->ctx, root);
    }
}

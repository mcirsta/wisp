/*
 * Copyright 2025 Neosurf Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * HTML timers on the existing frontend scheduler. No event pumping or JS thread.
 */
#include "timers.h"
#include "private.h"
#include <assert.h>
#include <limits.h>
#include <stddef.h>
#include <nsutils/time.h>
#include <wisp/desktop/gui_internal.h>
#include <wisp/misc.h>
#include <wisp/utils/log.h>

struct qjs_timer {
    struct qjs_timer *next;
    struct qjs_callback callback;
    int32_t id;
    int delay;
    unsigned nesting;
    uint64_t due_ms;
    bool repeat, cancelled;
};

static void timer_wake(void *pw);

static void timer_free(jsthread *thread, struct qjs_timer *timer)
{
    JSContext *ctx = thread->ctx;
    JS_FreeValue(ctx, timer->callback.handler);
    for (int i = 0; i < timer->callback.argc; i++)
        JS_FreeValue(ctx, timer->callback.argv[i]);
    js_free(ctx, timer->callback.argv);
    js_free(ctx, timer->callback.source);
    js_free(ctx, timer);
}

void qjs_timers_close(jsthread *thread)
{
    if (thread->timer_armed) {
        guit->misc->schedule(-1, timer_wake, thread);
        thread->timer_armed = false;
    }
    while (thread->timers != NULL) {
        struct qjs_timer *timer = thread->timers;
        thread->timers = timer->next;
        qjs_trace(thread->ctx, "timer.cancel", "id=%d reason=queue-closed", timer->id);
        timer_free(thread, timer);
    }
    thread->timer_tail = NULL;
    if (thread->running_timer != NULL)
        thread->running_timer->cancelled = true;
}

/* Frontend schedulers need not preserve insertion order. Keep a stable ordered
 * queue here, with one wakeup per document, and execute one timer per callback. */
static void timer_insert(jsthread *thread, struct qjs_timer *timer)
{
    if (thread->timer_tail != NULL && thread->timer_tail->due_ms <= timer->due_ms) {
        thread->timer_tail->next = timer;
        thread->timer_tail = timer;
        timer->next = NULL;
        return;
    }
    struct qjs_timer **slot = &thread->timers;
    while (*slot != NULL && (*slot)->due_ms <= timer->due_ms)
        slot = &(*slot)->next;
    timer->next = *slot;
    *slot = timer;
    if (timer->next == NULL)
        thread->timer_tail = timer;
}

static nserror timer_arm(jsthread *thread)
{
    /* The active callback will arm once, after its checkpoint and cleanup. */
    if (thread->running_timer != NULL)
        return NSERROR_OK;
    if (thread->timers == NULL) {
        if (thread->timer_armed)
            guit->misc->schedule(-1, timer_wake, thread);
        thread->timer_armed = false;
        return NSERROR_OK;
    }
    uint64_t due = thread->timers->due_ms, now;
    if (thread->timer_armed && thread->timer_due_ms == due)
        return NSERROR_OK;
    if (nsu_getmonotonic_ms(&now) != NSUERROR_OK)
        return NSERROR_UNKNOWN;
    uint64_t remaining = due > now ? due - now : 0;
    nserror err = guit->misc->schedule(remaining > INT_MAX ? INT_MAX : (int)remaining, timer_wake, thread);
    if (err != NSERROR_OK)
        guit->misc->schedule(-1, timer_wake, thread);
    thread->timer_armed = err == NSERROR_OK;
    thread->timer_due_ms = due;
    return err;
}

static void timer_schedule_failed(jsthread *thread, nserror error)
{
    /* A frontend can remove its old wakeup before an allocation failure. Do not
     * strand strong callback references in a queue which can no longer run. */
    NSLOG(wisp, WARNING, "JavaScript timer scheduling failed: %d", error);
    qjs_trace(thread->ctx, "timer.failure", "scheduler_error=%d; cancel queue", error);
    qjs_timers_close(thread);
}

static void timer_wake(void *pw)
{
    jsthread *thread = pw;
    thread->timer_armed = false;
    assert(!thread->closed && thread->running_timer == NULL && thread->timers != NULL);
    uint64_t now;
    if (nsu_getmonotonic_ms(&now) != NSUERROR_OK) {
        timer_schedule_failed(thread, NSERROR_UNKNOWN);
        return;
    }
    if (now < thread->timers->due_ms) {
        nserror err = timer_arm(thread);
        if (err != NSERROR_OK)
            timer_schedule_failed(thread, err);
        return;
    }
    struct qjs_timer *timer = thread->timers;
    thread->timers = timer->next;
    if (thread->timers == NULL)
        thread->timer_tail = NULL;
    timer->next = NULL;
    thread->running_timer = timer;
    qjs_thread_ref(thread);
    unsigned previous_nesting = thread->heap->timer_nesting;
    thread->heap->timer_nesting = timer->nesting;
    enum qjs_result result = qjs_run_callback(thread, &timer->callback, timer->id);
    thread->heap->timer_nesting = previous_nesting;
    thread->running_timer = NULL;

    /* Ordinary thrown exceptions do not cancel intervals. Stop an interrupted
     * interval so it cannot repeatedly monopolize the browser's main thread. */
    if (timer->repeat && !timer->cancelled && !thread->closed && result != QJS_INTERRUPTED) {
        if (timer->nesting > 5 && timer->delay < 4)
            timer->delay = 4;
        if (timer->nesting < 6)
            timer->nesting++;
        if (nsu_getmonotonic_ms(&now) == NSUERROR_OK) {
            timer->due_ms = now + (unsigned)timer->delay;
            timer_insert(thread, timer);
            timer = NULL;
        } else {
            timer_schedule_failed(thread, NSERROR_UNKNOWN);
        }
    }
    if (timer != NULL) {
        qjs_trace(thread->ctx, "timer.finish", "id=%d reason=%s", timer->id,
            thread->closed                  ? "document-closed"
                : timer->cancelled          ? "cancelled"
                : result == QJS_INTERRUPTED ? "interrupted"
                                            : "completed");
        timer_free(thread, timer);
    }
    nserror err = timer_arm(thread);
    if (err != NSERROR_OK)
        timer_schedule_failed(thread, err);
    qjs_thread_unref(thread);
}

static bool timer_id_used(jsthread *thread, int32_t id)
{
    if (thread->running_timer != NULL && thread->running_timer->id == id)
        return true;
    for (struct qjs_timer *timer = thread->timers; timer != NULL; timer = timer->next) {
        if (timer->id == id)
            return true;
    }
    return false;
}

static int32_t timer_next_id(jsthread *thread)
{
    int32_t first = thread->next_timer_id;
    do {
        int32_t id = thread->next_timer_id;
        bool used = thread->timer_ids_wrapped && timer_id_used(thread, id);
        if (id == INT32_MAX) {
            thread->timer_ids_wrapped = true;
            thread->next_timer_id = 1;
        } else {
            thread->next_timer_id++;
        }
        if (!used)
            return id;
    } while (thread->next_timer_id != first);
    return 0;
}

static bool timer_receiver(JSContext *ctx, JSValueConst this_val)
{
    if (JS_IsUndefined(this_val) || JS_IsNull(this_val))
        return true;
    JSValue global = JS_GetGlobalObject(ctx);
    bool valid = JS_IsSameValue(ctx, this_val, global);
    JS_FreeValue(ctx, global);
    if (!valid)
        JS_ThrowTypeError(ctx, "Timer method requires a Window receiver");
    return valid;
}

static JSValue timer_create(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv, bool repeat)
{
    if (!timer_receiver(ctx, this_val))
        return JS_EXCEPTION;
    if (argc == 0)
        return JS_ThrowTypeError(ctx, "Timer requires a handler");
    /* Web IDL: handler conversion precedes long delay conversion. Both can
     * call page code; retain the converted handler and recheck closure after. */
    bool callable = JS_IsFunction(ctx, argv[0]);
    JSValue handler = callable ? JS_DupValue(ctx, argv[0]) : JS_ToString(ctx, argv[0]);
    if (JS_IsException(handler))
        return handler;
    int32_t delay = 0;
    if (argc > 1 && JS_ToInt32(ctx, &delay, argv[1]) < 0) {
        JS_FreeValue(ctx, handler);
        return JS_EXCEPTION;
    }
    jsthread *thread = JS_GetContextOpaque(ctx);
    if (thread == NULL || thread->closed || guit == NULL || guit->misc == NULL || guit->misc->schedule == NULL) {
        JS_FreeValue(ctx, handler);
        return JS_ThrowInternalError(ctx, "Timer context or scheduler is unavailable");
    }
    if (delay < 0)
        delay = 0;
    if (thread->heap->timer_nesting > 5 && delay < 4)
        delay = 4;
    uint64_t now;
    if (nsu_getmonotonic_ms(&now) != NSUERROR_OK) {
        JS_FreeValue(ctx, handler);
        return JS_ThrowInternalError(ctx, "Timer clock is unavailable");
    }
    struct qjs_timer *timer = js_mallocz(ctx, sizeof(*timer));
    if (timer == NULL) {
        JS_FreeValue(ctx, handler);
        return JS_EXCEPTION;
    }
    timer->callback.handler = handler;
    timer->callback.source = js_strdup(ctx, thread->entry_source != NULL ? thread->entry_source : "<timer>");
    int count = callable && argc > 2 ? argc - 2 : 0;
    if (timer->callback.source == NULL ||
        (count > 0 &&
            ((size_t)count > SIZE_MAX / sizeof(JSValue) ||
                (timer->callback.argv = js_malloc(ctx, (size_t)count * sizeof(JSValue))) == NULL))) {
        timer_free(thread, timer);
        return JS_ThrowOutOfMemory(ctx);
    }
    for (int i = 0; i < count; i++)
        timer->callback.argv[i] = JS_DupValue(ctx, argv[i + 2]);
    timer->callback.argc = count;
    timer->callback.initiating_entry = thread->trace_entry;
    timer->id = timer_next_id(thread);
    if (timer->id == 0) {
        timer_free(thread, timer);
        return JS_ThrowInternalError(ctx, "No timer IDs available");
    }
    timer->delay = delay;
    timer->nesting = thread->heap->timer_nesting < 6 ? thread->heap->timer_nesting + 1 : 6;
    timer->due_ms = now + (unsigned)delay;
    timer->repeat = repeat;
    timer_insert(thread, timer);
    nserror err = timer_arm(thread);
    if (err != NSERROR_OK) {
        timer_schedule_failed(thread, err);
        return err == NSERROR_NOMEM ? JS_ThrowOutOfMemory(ctx) : JS_ThrowInternalError(ctx, "Timer scheduling failed");
    }
    qjs_trace(
        ctx, "timer.schedule", "id=%d repeat=%d delay_ms=%d nesting=%u", timer->id, repeat, delay, timer->nesting);
    return JS_NewInt32(ctx, timer->id);
}

static JSValue js_setTimeout(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    return timer_create(ctx, this_val, argc, argv, false);
}

static JSValue js_setInterval(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    return timer_create(ctx, this_val, argc, argv, true);
}

static JSValue js_clearTimer(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    if (!timer_receiver(ctx, this_val))
        return JS_EXCEPTION;
    int32_t id = 0;
    if (argc > 0 && JS_ToInt32(ctx, &id, argv[0]) < 0)
        return JS_EXCEPTION;
    jsthread *thread = JS_GetContextOpaque(ctx);
    if (thread == NULL || thread->closed || id <= 0)
        return JS_UNDEFINED;
    if (thread->running_timer != NULL && thread->running_timer->id == id) {
        thread->running_timer->cancelled = true;
        qjs_trace(ctx, "timer.cancel", "id=%d reason=clear-active", id);
        return JS_UNDEFINED;
    }
    struct qjs_timer **slot = &thread->timers, *previous = NULL;
    while (*slot != NULL && (*slot)->id != id) {
        previous = *slot;
        slot = &(*slot)->next;
    }
    if (*slot != NULL) {
        struct qjs_timer *timer = *slot;
        *slot = timer->next;
        if (thread->timer_tail == timer)
            thread->timer_tail = previous;
        qjs_trace(ctx, "timer.cancel", "id=%d reason=clear", id);
        timer_free(thread, timer);
        nserror err = timer_arm(thread);
        if (err != NSERROR_OK)
            timer_schedule_failed(thread, err);
    }
    return JS_UNDEFINED;
}

int qjs_init_timers(JSContext *ctx)
{
    static const JSCFunctionListEntry properties[] = {
        JS_CFUNC_DEF2("setTimeout", 1, js_setTimeout, JS_PROP_C_W_E),
        JS_CFUNC_DEF2("clearTimeout", 0, js_clearTimer, JS_PROP_C_W_E),
        JS_CFUNC_DEF2("setInterval", 1, js_setInterval, JS_PROP_C_W_E),
        JS_CFUNC_DEF2("clearInterval", 0, js_clearTimer, JS_PROP_C_W_E),
    };
    JSValue global = JS_GetGlobalObject(ctx);
    int ret = JS_SetPropertyFunctionList(ctx, global, properties, sizeof(properties) / sizeof(properties[0]));
    JS_FreeValue(ctx, global);
    return ret < 0 ? -1 : 0;
}

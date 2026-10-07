/* SPDX-License-Identifier: GPL-2.0-or-later
 * Internal QuickJS host state. Not a Web API or a generated binding.
 */
#ifndef WISP_QUICKJS_PRIVATE_H
#define WISP_QUICKJS_PRIVATE_H

#include <stdbool.h>
#include <stdint.h>
#include "quickjs.h"
#include "content/handlers/javascript/js.h"

struct qjs_timer;
struct qjs_rejection;
struct qjs_dom_document;
struct qjs_event_dispatch;
struct qjs_event_target;

/* One runtime per browser window. The window and every document context hold
 * references, so retirement during a callback cannot free the runtime early. */
struct jsheap {
    JSRuntime *rt;
    int timeout;
    int refcount;
    unsigned execution_depth;
    unsigned timer_nesting;
    uint64_t deadline_ms;
    bool interrupted;
    struct jsthread *threads;
    JSClassID event_class[2], event_target_class;
    JSClassID node_class, tokenlist_class, collection_class[2], xhr_class, console_class;
};

/* One context per document; queued timers belong to this owner. Active host
 * callbacks retain it separately so navigation can retire it during execution. */
struct jsthread {
    jsheap *heap;
    JSContext *ctx;
    JSValue error_stack_getter;
    JSValue window_event_target; /* Explicit Window root, released on document close. */
    struct qjs_event_dispatch *event_dispatch;
    struct qjs_event_target *event_targets; /* Borrowed registry for document retirement. */
    uint64_t time_origin_ms, trace_next_listener;
    unsigned refcount;
    struct jsthread *next;
    struct qjs_rejection *rejections;
    bool closed;
    void *win_priv;
    void *doc_priv;

    struct qjs_timer *timers, *timer_tail, *running_timer;
    int32_t next_timer_id;
    bool timer_ids_wrapped;
    bool timer_armed;
    uint64_t timer_due_ms;

    uint64_t trace_document, trace_next_entry, trace_entry;
    const char *entry_source; /* Borrowed for the duration of the active entry. */
    struct qjs_dom_document *dom_document; /* Borrowed from the unforgeable document object. */
    uint64_t dom_changes, dom_first_entry;
};

struct qjs_callback {
    JSValue handler; /* Callable or an already-converted source string. */
    int argc;
    JSValue *argv;
    char *source;
    uint64_t initiating_entry;
};

enum qjs_result { QJS_OK, QJS_EXCEPTION, QJS_INTERRUPTED };

/* The caller retains the thread until it has finished native callback cleanup. */
void qjs_thread_ref(jsthread *thread);
void qjs_thread_unref(jsthread *thread);
enum qjs_result qjs_run_callback(jsthread *thread, const struct qjs_callback *callback, int32_t timer_id);

enum qjs_result qjs_run_event_listener(
    jsthread *thread, JSValueConst callback, JSValueConst target, JSValueConst event, const char *source);

/* No JS coercion or getters. Details are bounded and kept on one log line.
 * Call from actual host operations, including DOM mutations and invalidation. */
void qjs_trace(JSContext *ctx, const char *operation, const char *format, ...) __attribute__((format(printf, 3, 4)));

/* QuickJS keeps static function tables for lazy initialization. Runtime class
 * IDs require dynamic tables: eagerly materialize their string-named members
 * before the table goes out of scope. Use only on fresh, private objects. */
int qjs_define_properties(JSContext *ctx, JSValueConst object, const JSCFunctionListEntry *entries, int count);

#endif

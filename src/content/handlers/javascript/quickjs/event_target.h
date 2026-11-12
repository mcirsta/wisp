/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef WISP_QUICKJS_EVENT_TARGET_H
#define WISP_QUICKJS_EVENT_TARGET_H
#include "private.h"
#include <dom/core/document.h>

/* Shared listener storage/dispatch for script events and the native click adapter. */
int qjs_init_event_target(JSContext *ctx);
void qjs_events_close(jsthread *thread);
JSValue qjs_event_target_create(JSContext *ctx);
bool qjs_event_target_validate(JSContext *ctx, JSValueConst object);
int qjs_event_target_listen(JSContext *ctx, JSValueConst target, const char *type, JSValueConst callback, bool add);
enum qjs_handler_kind {
    QJS_HANDLER_SCROLL, QJS_HANDLER_RESIZE, QJS_HANDLER_LOAD, QJS_HANDLER_ERROR,
    QJS_HANDLER_READYSTATE, QJS_HANDLER_LOADSTART, QJS_HANDLER_PROGRESS,
    QJS_HANDLER_ABORT, QJS_HANDLER_TIMEOUT, QJS_HANDLER_LOADEND, QJS_HANDLER_SCROLLEND, QJS_HANDLERS
};
/* IDL handlers share native listener ordering and GC ownership. The exposing
 * interface validates its receiver before calling these helpers. */
JSValue qjs_event_handler_get(JSContext *ctx, JSValueConst target, int kind);
JSValue qjs_event_handler_set(JSContext *ctx, JSValueConst target, JSValueConst value, int kind);
JSValue qjs_event_target_dispatch(JSContext *ctx, JSValueConst target, JSValueConst event, bool script);
JSValue qjs_event_target_dispatch_load(JSContext *ctx, JSValueConst document, JSValueConst event);
void qjs_events_connect_document(JSContext *ctx, dom_document *document);
#endif

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
JSValue qjs_event_target_dispatch(JSContext *ctx, JSValueConst target, JSValueConst event, bool script);
void qjs_events_connect_document(JSContext *ctx, dom_document *document);
#endif

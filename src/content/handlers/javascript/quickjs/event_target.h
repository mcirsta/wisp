/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef WISP_QUICKJS_EVENT_TARGET_H
#define WISP_QUICKJS_EVENT_TARGET_H
#include "private.h"
#include <dom/core/document.h>

/* Script-dispatched events. Native input/lifecycle delivery is a later adapter. */
int qjs_init_event_target(JSContext *ctx);
void qjs_events_close(jsthread *thread);
void qjs_events_connect_document(JSContext *ctx, dom_document *document);
#endif

/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef WISP_QUICKJS_EVENT_TARGET_H
#define WISP_QUICKJS_EVENT_TARGET_H
#include "private.h"

/* Script-dispatched standalone/Window events. Native DOM interaction is a later adapter. */
int qjs_init_event_target(JSContext *ctx);
void qjs_events_close(jsthread *thread);
#endif

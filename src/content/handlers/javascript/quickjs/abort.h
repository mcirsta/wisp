/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef WISP_QUICKJS_ABORT_H
#define WISP_QUICKJS_ABORT_H
#include "private.h"

struct qjs_abort_algorithm;
int qjs_init_abort(JSContext *ctx, JSValueConst target_proto);
void qjs_abort_close(jsthread *thread);
/* Undefined means this is not an AbortSignal; exceptions reject retired/foreign signals. */
JSValue qjs_abort_target(JSContext *ctx, JSValueConst object);
bool qjs_abort_validate(JSContext *ctx, JSValueConst object);
bool qjs_abort_aborted(JSContext *ctx, JSValueConst object);
/* Host-only callbacks remove their own algorithm and do not invoke JavaScript.
 * owner is retained/traced by the signal until that removal. */
struct qjs_abort_algorithm *
qjs_abort_add(JSContext *ctx, JSValueConst signal, JSValueConst owner, void (*callback)(void *), void *pw);
void qjs_abort_remove(JSRuntime *rt, struct qjs_abort_algorithm *algorithm);
#endif

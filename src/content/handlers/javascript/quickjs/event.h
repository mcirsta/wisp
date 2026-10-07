/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef WISP_QUICKJS_EVENT_H
#define WISP_QUICKJS_EVENT_H
#include "dom.h"
#include <dom/events/events.h>

/* Web IDL operations are enumerable; QuickJS's magic-method macro is not. */
#define QJS_EVENT_METHOD(name, length, function, magic)                                                                \
    {                                                                                                                  \
        name, JS_PROP_C_W_E, JS_DEF_CFUNC, magic,                                                                      \
        {                                                                                                              \
            .func = {length, JS_CFUNC_generic_magic, {.generic_magic = function}}                                      \
        }                                                                                                              \
    }

struct qjs_event {
    dom_event *native;
    JSValue target, detail, path; /* Private frozen JS path, cleared after dispatch. */
    double timestamp;
    bool custom, composed;
};

struct qjs_event *qjs_event_get(JSContext *ctx, JSValueConst value);
int qjs_init_events(JSContext *ctx);
JSValue qjs_event_current_target(JSContext *ctx, struct qjs_event *event);
JSValue qjs_event_new_object(JSContext *ctx, JSValueConst new_target, JSClassID id);
int qjs_event_define_interface(
    JSContext *ctx, JSValueConst global, const char *name, JSValueConst ctor, JSValueConst proto);
#endif

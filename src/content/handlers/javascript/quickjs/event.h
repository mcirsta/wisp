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
    enum qjs_event_kind kind;
    bool composed;
};

/* Inheritance is explicit: adding an Event subtype must not make it a UIEvent
 * merely because its enum value follows the input event kinds. */
static inline bool qjs_event_is_ui(enum qjs_event_kind kind)
{
    return kind == QJS_UI_EVENT || kind == QJS_MOUSE_EVENT || kind == QJS_POINTER_EVENT;
}

struct qjs_event *qjs_event_get(JSContext *ctx, JSValueConst value);
int qjs_init_events(JSContext *ctx);
struct qjs_event_options {
    bool bubbles, cancelable, composed;
};
int qjs_event_read_options(JSContext *ctx, JSValueConst dict, struct qjs_event_options *options);
/* Consumes object; allocates the appropriate native-backed event value. */
JSValue qjs_event_allocate(JSContext *ctx, JSValue object, enum qjs_event_kind kind, bool composed);
/* Host-created trusted base Event, with false bubbles/cancelable/composed. */
JSValue qjs_event_create(JSContext *ctx, const char *type);
/* Host-created trusted submit event; retains the submitter wrapper. */
JSValue qjs_native_submit_event(JSContext *ctx, dom_string *type, JSValueConst document, dom_node *submitter);
JSValue qjs_event_current_target(JSContext *ctx, struct qjs_event *event);
JSValue qjs_event_new_object(JSContext *ctx, JSValueConst new_target, JSClassID id);
int qjs_event_define_interface(
    JSContext *ctx, JSValueConst global, const char *name, JSValueConst ctor, JSValueConst proto);
#endif

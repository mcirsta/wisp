/* SPDX-License-Identifier: GPL-2.0-or-later
 * DOM abort algorithms are host callbacks, separate from the public abort event.
 * Only listener cancellation is connected here; fetch and other APIs are separate.
 */
#include "abort.h"
#include "event.h"
#include "event_target.h"

struct qjs_abort_algorithm {
    struct qjs_abort_algorithm *next, **previous;
    JSValue owner; /* Signal -> operation's EventTarget, traced from the signal. */
    void (*callback)(void *);
    void *pw;
};

struct abort_signal {
    jsthread *thread; /* Borrowed identity, never dereferenced after retirement. */
    JSValue target, reason, handler, handler_listener;
    struct qjs_abort_algorithm *algorithms;
    bool aborted;
};

struct abort_controller {
    JSValue signal;
};

static struct abort_signal *signal_get(JSContext *ctx, JSValueConst object)
{
    jsthread *thread = JS_GetContextOpaque(ctx);
    struct abort_signal *signal = thread != NULL ? JS_GetOpaque(object, thread->heap->abort_signal_class) : NULL;
    if (signal == NULL) {
        JS_ThrowTypeError(ctx, "AbortSignal receiver required");
        return NULL;
    }
    if (signal->thread != thread || thread->closed) {
        qjs_dom_exception(ctx, DOM_INVALID_STATE_ERR);
        return NULL;
    }
    return signal;
}

bool qjs_abort_validate(JSContext *ctx, JSValueConst object)
{
    return signal_get(ctx, object) != NULL;
}

bool qjs_abort_aborted(JSContext *ctx, JSValueConst object)
{
    struct abort_signal *signal = signal_get(ctx, object);
    return signal != NULL && signal->aborted;
}

JSValue qjs_abort_target(JSContext *ctx, JSValueConst object)
{
    jsthread *thread = JS_GetContextOpaque(ctx);
    if (JS_GetOpaque(object, thread->heap->abort_signal_class) == NULL)
        return JS_UNDEFINED;
    struct abort_signal *signal = signal_get(ctx, object);
    return signal != NULL ? JS_DupValue(ctx, signal->target) : JS_EXCEPTION;
}

struct qjs_abort_algorithm *
qjs_abort_add(JSContext *ctx, JSValueConst object, JSValueConst owner, void (*callback)(void *), void *pw)
{
    struct abort_signal *signal = signal_get(ctx, object);
    if (signal == NULL)
        return NULL;
    struct qjs_abort_algorithm *algorithm = js_mallocz(ctx, sizeof(*algorithm));
    if (algorithm == NULL)
        return NULL;
    algorithm->owner = JS_DupValue(ctx, owner);
    algorithm->callback = callback;
    algorithm->pw = pw;
    /* Preserve abort-algorithm registration order. */
    struct qjs_abort_algorithm **slot = &signal->algorithms;
    while (*slot != NULL)
        slot = &(*slot)->next;
    algorithm->previous = slot;
    *slot = algorithm;
    return algorithm;
}

void qjs_abort_remove(JSRuntime *rt, struct qjs_abort_algorithm *algorithm)
{
    if (algorithm == NULL)
        return;
    *algorithm->previous = algorithm->next;
    if (algorithm->next != NULL)
        algorithm->next->previous = algorithm->previous;
    JS_FreeValueRT(rt, algorithm->owner);
    js_free_rt(rt, algorithm);
}

static void signal_finalizer(JSRuntime *rt, JSValue object)
{
    struct abort_signal *signal = JS_GetOpaque(object, JS_GetClassID(object));
    if (signal == NULL)
        return;
    /* GC can finalize either end of an unreachable signal/target cycle first.
     * These callbacks only detach host registrations, never invoke page script. */
    while (signal->algorithms != NULL)
        signal->algorithms->callback(signal->algorithms->pw);
    JS_FreeValueRT(rt, signal->target);
    JS_FreeValueRT(rt, signal->reason);
    JS_FreeValueRT(rt, signal->handler);
    JS_FreeValueRT(rt, signal->handler_listener);
    js_free_rt(rt, signal);
}

static void signal_mark(JSRuntime *rt, JSValueConst object, JS_MarkFunc *mark)
{
    struct abort_signal *signal = JS_GetOpaque(object, JS_GetClassID(object));
    if (signal == NULL)
        return;
    JS_MarkValue(rt, signal->target, mark);
    JS_MarkValue(rt, signal->reason, mark);
    JS_MarkValue(rt, signal->handler, mark);
    JS_MarkValue(rt, signal->handler_listener, mark);
    for (struct qjs_abort_algorithm *a = signal->algorithms; a != NULL; a = a->next)
        JS_MarkValue(rt, a->owner, mark);
}

static void controller_finalizer(JSRuntime *rt, JSValue object)
{
    struct abort_controller *controller = JS_GetOpaque(object, JS_GetClassID(object));
    if (controller != NULL) {
        JS_FreeValueRT(rt, controller->signal);
        js_free_rt(rt, controller);
    }
}

static void controller_mark(JSRuntime *rt, JSValueConst object, JS_MarkFunc *mark)
{
    struct abort_controller *controller = JS_GetOpaque(object, JS_GetClassID(object));
    if (controller != NULL)
        JS_MarkValue(rt, controller->signal, mark);
}

static JSValue signal_create(JSContext *ctx)
{
    jsthread *thread = JS_GetContextOpaque(ctx);
    if (thread == NULL || thread->closed)
        return qjs_dom_exception(ctx, DOM_INVALID_STATE_ERR);
    JSValue object = JS_NewObjectClass(ctx, thread->heap->abort_signal_class);
    if (JS_IsException(object))
        return object;
    struct abort_signal *signal = js_mallocz(ctx, sizeof(*signal));
    if (signal == NULL) {
        JS_FreeValue(ctx, object);
        return JS_EXCEPTION;
    }
    signal->thread = thread;
    signal->reason = JS_UNDEFINED;
    signal->handler = JS_NULL;
    signal->handler_listener = JS_UNDEFINED;
    signal->target = qjs_event_target_create(ctx);
    JS_SetOpaque(object, signal);
    if (JS_IsException(signal->target)) {
        JS_FreeValue(ctx, object);
        return JS_EXCEPTION;
    }
    return object;
}

static JSValue controller_constructor(JSContext *ctx, JSValueConst new_target, int argc, JSValueConst *argv)
{
    jsthread *thread = JS_GetContextOpaque(ctx);
    if (thread == NULL || thread->closed)
        return qjs_dom_exception(ctx, DOM_INVALID_STATE_ERR);
    JSValue object = qjs_event_new_object(ctx, new_target, thread->heap->abort_controller_class);
    if (JS_IsException(object))
        return object;
    struct abort_controller *controller = js_mallocz(ctx, sizeof(*controller));
    if (controller == NULL) {
        JS_FreeValue(ctx, object);
        return JS_EXCEPTION;
    }
    controller->signal = signal_create(ctx); /* Rechecks retirement after prototype access. */
    JS_SetOpaque(object, controller);
    if (JS_IsException(controller->signal)) {
        JS_FreeValue(ctx, object);
        return JS_EXCEPTION;
    }
    return object;
}

static JSValue illegal_signal_constructor(JSContext *ctx, JSValueConst new_target, int argc, JSValueConst *argv)
{
    return JS_ThrowTypeError(ctx, "Illegal AbortSignal constructor");
}

static JSValue abort_reason(JSContext *ctx, JSValueConst reason)
{
    if (!JS_IsUndefined(reason))
        return JS_DupValue(ctx, reason);
    jsthread *thread = JS_GetContextOpaque(ctx);
    JSValue args[] = {JS_NewString(ctx, "The operation was aborted"), JS_NewString(ctx, "AbortError")};
    JSValue result = JS_IsException(args[0]) || JS_IsException(args[1])
        ? JS_EXCEPTION
        : JS_CallConstructor(ctx, thread->abort_error_constructor, 2, args);
    JS_FreeValue(ctx, args[0]);
    JS_FreeValue(ctx, args[1]);
    return result;
}

static JSValue signal_abort_static(JSContext *ctx, JSValueConst object, int argc, JSValueConst *argv)
{
    JSValue result = signal_create(ctx);
    if (JS_IsException(result))
        return result;
    struct abort_signal *signal = signal_get(ctx, result);
    signal->reason = abort_reason(ctx, argc > 0 ? argv[0] : JS_UNDEFINED);
    if (JS_IsException(signal->reason)) {
        JS_FreeValue(ctx, result);
        return JS_EXCEPTION;
    }
    signal->aborted = true;
    return result;
}

static JSValue signal_getter(JSContext *ctx, JSValueConst object, int reason)
{
    struct abort_signal *signal = signal_get(ctx, object);
    if (signal == NULL)
        return JS_EXCEPTION;
    return reason ? JS_DupValue(ctx, signal->reason) : JS_NewBool(ctx, signal->aborted);
}

static JSValue signal_throw(JSContext *ctx, JSValueConst object, int argc, JSValueConst *argv)
{
    struct abort_signal *signal = signal_get(ctx, object);
    if (signal == NULL)
        return JS_EXCEPTION;
    return signal->aborted ? JS_Throw(ctx, JS_DupValue(ctx, signal->reason)) : JS_UNDEFINED;
}

static JSValue signal_handler_get(JSContext *ctx, JSValueConst object)
{
    struct abort_signal *signal = signal_get(ctx, object);
    return signal != NULL ? JS_DupValue(ctx, signal->handler) : JS_EXCEPTION;
}

static JSValue
signal_handler_call(JSContext *ctx, JSValueConst object, int argc, JSValueConst *argv, int magic, JSValue *data)
{
    struct abort_signal *signal = signal_get(ctx, data[0]);
    if (signal == NULL)
        return JS_EXCEPTION;
    /* EventHandler's LegacyTreatNonObjectAsNull also permits non-callable
     * objects. They retain their identity but do nothing when invoked. */
    if (!JS_IsFunction(ctx, signal->handler))
        return JS_UNDEFINED;
    JSValue callback = JS_DupValue(ctx, signal->handler);
    JSValue result = JS_Call(ctx, callback, data[0], argc, argv);
    JS_FreeValue(ctx, callback);
    if (JS_IsBool(result) && !JS_ToBool(ctx, result))
        dom_event_prevent_default(qjs_event_get(ctx, argv[0])->native);
    return result;
}

static JSValue signal_handler_set(JSContext *ctx, JSValueConst object, JSValueConst value)
{
    struct abort_signal *signal = signal_get(ctx, object);
    if (signal == NULL)
        return JS_EXCEPTION;
    bool active = JS_IsObject(value);
    if (active && JS_IsUndefined(signal->handler_listener)) {
        JSValue listener = JS_NewCFunctionData(ctx, signal_handler_call, 1, 0, 1, &object);
        if (JS_IsException(listener))
            return listener;
        if (qjs_event_target_listen(ctx, object, "abort", listener, true) < 0) {
            JS_FreeValue(ctx, listener);
            return JS_EXCEPTION;
        }
        signal->handler_listener = listener;
    } else if (!active && !JS_IsUndefined(signal->handler_listener)) {
        if (qjs_event_target_listen(ctx, object, "abort", signal->handler_listener, false) < 0)
            return JS_EXCEPTION;
        JS_FreeValue(ctx, signal->handler_listener);
        signal->handler_listener = JS_UNDEFINED;
    }
    JSValue previous = signal->handler;
    signal->handler = active ? JS_DupValue(ctx, value) : JS_NULL;
    JS_FreeValue(ctx, previous);
    qjs_trace(ctx, "signal.handler", "active=%d", active);
    return JS_UNDEFINED;
}

static struct abort_controller *controller_get(JSContext *ctx, JSValueConst object)
{
    jsthread *thread = JS_GetContextOpaque(ctx);
    if (thread == NULL) {
        JS_ThrowTypeError(ctx, "AbortController receiver required");
        return NULL;
    }
    return JS_GetOpaque2(ctx, object, thread->heap->abort_controller_class);
}

static JSValue controller_signal(JSContext *ctx, JSValueConst object)
{
    struct abort_controller *controller = controller_get(ctx, object);
    return controller != NULL ? JS_DupValue(ctx, controller->signal) : JS_EXCEPTION;
}

static JSValue controller_abort(JSContext *ctx, JSValueConst object, int argc, JSValueConst *argv)
{
    struct abort_controller *controller = controller_get(ctx, object);
    if (controller == NULL)
        return JS_EXCEPTION;
    struct abort_signal *signal = signal_get(ctx, controller->signal);
    if (signal == NULL)
        return JS_EXCEPTION;
    if (signal->aborted)
        return JS_UNDEFINED;
    JSValue reason = abort_reason(ctx, argc > 0 ? argv[0] : JS_UNDEFINED);
    if (JS_IsException(reason))
        return reason;
    /* Allocate the event before changing state, so OOM cannot strand abort steps. */
    JSValue event = qjs_event_create(ctx, "abort");
    if (JS_IsException(event)) {
        JS_FreeValue(ctx, reason);
        return event;
    }
    signal->reason = reason;
    signal->aborted = true;
    qjs_trace(ctx, "signal.abort", "cancel registered operations");
    while (signal->algorithms != NULL)
        signal->algorithms->callback(signal->algorithms->pw);
    JSValue result = qjs_event_target_dispatch(ctx, controller->signal, event, false);
    JS_FreeValue(ctx, event);
    if (JS_IsException(result))
        return result;
    JS_FreeValue(ctx, result);
    return JS_UNDEFINED;
}

void qjs_abort_close(jsthread *thread)
{
    JS_FreeValue(thread->ctx, thread->abort_error_constructor);
    thread->abort_error_constructor = JS_UNDEFINED;
}

int qjs_init_abort(JSContext *ctx, JSValueConst target_proto)
{
    jsthread *thread = JS_GetContextOpaque(ctx);
    JSClassID local[2] = {0};
    JSClassID *ids[] = {thread != NULL ? &thread->heap->abort_controller_class : &local[0],
        thread != NULL ? &thread->heap->abort_signal_class : &local[1]};
    const JSClassDef defs[] = {
        {.class_name = "AbortController", .finalizer = controller_finalizer, .gc_mark = controller_mark},
        {.class_name = "AbortSignal", .finalizer = signal_finalizer, .gc_mark = signal_mark}};
    static const JSCFunctionListEntry controller_properties[] = {
        JS_CGETSET_DEF2("signal", controller_signal, NULL, JS_PROP_C_W_E),
        JS_CFUNC_DEF2("abort", 0, controller_abort, JS_PROP_C_W_E)};
    static const JSCFunctionListEntry signal_properties[] = {
        JS_CGETSET_MAGIC_DEF2("aborted", signal_getter, NULL, 0, JS_PROP_C_W_E),
        JS_CGETSET_MAGIC_DEF2("reason", signal_getter, NULL, 1, JS_PROP_C_W_E),
        JS_CFUNC_DEF2("throwIfAborted", 0, signal_throw, JS_PROP_C_W_E),
        JS_CGETSET_DEF2("onabort", signal_handler_get, signal_handler_set, JS_PROP_C_W_E)};
    JSValue global = JS_GetGlobalObject(ctx);
    int ret = -1;
    for (unsigned i = 0; i < 2; i++) {
        if (*ids[i] == 0) {
            JS_NewClassID(JS_GetRuntime(ctx), ids[i]);
            if (JS_NewClass(JS_GetRuntime(ctx), *ids[i], &defs[i]) < 0) {
                *ids[i] = 0;
                goto out;
            }
        }
        JSValue proto = i ? JS_NewObjectProto(ctx, target_proto) : JS_NewObject(ctx);
        JSValue ctor = JS_NewCFunction2(ctx, i ? illegal_signal_constructor : controller_constructor,
            defs[i].class_name, 0, JS_CFUNC_constructor, 0);
        const JSCFunctionListEntry *properties = i ? signal_properties : controller_properties;
        int count = i ? sizeof(signal_properties) / sizeof(*signal_properties)
                      : sizeof(controller_properties) / sizeof(*controller_properties);
        bool failed = JS_IsException(proto) || JS_IsException(ctor) ||
            JS_SetPropertyFunctionList(ctx, proto, properties, count) < 0 ||
            qjs_event_define_interface(ctx, global, defs[i].class_name, ctor, proto) < 0;
        if (!failed && i) {
            JSValue parent = JS_GetPropertyStr(ctx, target_proto, "constructor");
            failed = JS_IsException(parent) || JS_SetPrototype(ctx, ctor, parent) < 0;
            JS_FreeValue(ctx, parent);
            static const JSCFunctionListEntry statics[] = {
                JS_CFUNC_DEF2("abort", 0, signal_abort_static, JS_PROP_C_W_E)};
            if (!failed)
                failed = JS_SetPropertyFunctionList(ctx, ctor, statics, 1) < 0;
        }
        if (!failed)
            JS_SetClassProto(ctx, *ids[i], JS_DupValue(ctx, proto));
        JS_FreeValue(ctx, proto);
        JS_FreeValue(ctx, ctor);
        if (failed)
            goto out;
    }
    if (thread != NULL) {
        thread->abort_error_constructor = JS_GetPropertyStr(ctx, global, "DOMException");
        if (JS_IsException(thread->abort_error_constructor))
            goto out;
    }
    ret = 0;
out:
    JS_FreeValue(ctx, global);
    return ret;
}

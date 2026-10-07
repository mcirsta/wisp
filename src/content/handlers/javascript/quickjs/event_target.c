/* SPDX-License-Identifier: GPL-2.0-or-later
 * Duktape's EventTarget.bnd informed the native bridge. Registrations are owned
 * by GC-visible QuickJS targets; libdom owns invocation snapshots and ordering.
 */
#include "event_target.h"
#include "event.h"
#include <inttypes.h>
#include <string.h>

struct listener;
struct qjs_event_target {
    dom_event_target *native;
    struct listener *listeners;
    jsthread *thread;
    struct qjs_event_target *next, **previous;
    JSValue object; /* Borrowed identity; temporarily rooted while closing. */
};

struct listener {
    struct listener *next, **previous;
    struct qjs_event_target *target;
    dom_event_listener *native;
    dom_string *type;
    JSRuntime *rt;
    jsthread *thread; /* Borrowed: callbacks are entered only from an active JS dispatch. */
    JSValue callback;
    char *source;
    uint64_t id, entry;
    bool capture, once, passive;
};

struct qjs_event_dispatch {
    struct qjs_event_dispatch *previous;
    dom_event *native;
    JSValue object; /* Borrowed from dispatchEvent's rooted argument. */
};

static void listener_cleanup(void *pw)
{
    struct listener *listener = pw;
    dom_string_unref(listener->type);
    JS_FreeValueRT(listener->rt, listener->callback);
    js_free_rt(listener->rt, listener->source);
    js_free_rt(listener->rt, listener);
}

static void listener_remove(struct listener *listener)
{
    *listener->previous = listener->next;
    if (listener->next != NULL)
        listener->next->previous = listener->previous;
    dom_event_target_remove_event_listener(
        listener->target->native, listener->type, listener->native, listener->capture);
    dom_event_listener_unref(listener->native);
    /* listener can now be freed, or retained by a native invocation snapshot. */
}

static void target_clear(struct qjs_event_target *target)
{
    while (target->listeners != NULL)
        listener_remove(target->listeners);
}

static void target_detach(struct qjs_event_target *target)
{
    if (target->previous != NULL) {
        *target->previous = target->next;
        if (target->next != NULL)
            target->next->previous = target->previous;
    }
    target->previous = NULL;
    target->thread = NULL;
}

static void target_finalizer(JSRuntime *rt, JSValue object)
{
    struct qjs_event_target *target = JS_GetOpaque(object, JS_GetClassID(object));
    if (target != NULL) {
        target_detach(target);
        target_clear(target);
        dom_event_target_unref(target->native);
        js_free_rt(rt, target);
    }
}

static void target_mark(JSRuntime *rt, JSValueConst object, JS_MarkFunc *mark)
{
    struct qjs_event_target *target = JS_GetOpaque(object, JS_GetClassID(object));
    if (target != NULL)
        for (struct listener *l = target->listeners; l != NULL; l = l->next)
            JS_MarkValue(rt, l->callback, mark);
}

void qjs_events_close(jsthread *thread)
{
    while (thread->event_targets != NULL) {
        struct qjs_event_target *target = thread->event_targets;
        JSValue owner = JS_DupValue(thread->ctx, target->object);
        target_detach(target);
        target_clear(target);
        JS_FreeValue(thread->ctx, owner);
    }
    JS_FreeValue(thread->ctx, thread->window_event_target);
    thread->window_event_target = JS_UNDEFINED;
}

/* Return a rooted owner: closing the document during argument coercion or a
 * callback may drop the thread's Window listener storage. */
static JSValue target_owner(JSContext *ctx, JSValueConst object, bool *window)
{
    jsthread *thread = JS_GetContextOpaque(ctx);
    if (thread == NULL || thread->closed)
        return qjs_dom_exception(ctx, DOM_INVALID_STATE_ERR);
    JSValue global = JS_GetGlobalObject(ctx);
    *window = JS_IsUndefined(object) || JS_IsNull(object) || JS_IsStrictEqual(ctx, object, global);
    JS_FreeValue(ctx, global);
    if (*window)
        return JS_DupValue(ctx, thread->window_event_target);
    struct qjs_event_target *target = JS_GetOpaque2(ctx, object, thread->heap->event_target_class);
    if (target == NULL)
        return JS_EXCEPTION;
    if (target->thread != thread)
        return qjs_dom_exception(ctx, DOM_INVALID_STATE_ERR);
    return JS_DupValue(ctx, object);
}

static int target_initialize(JSContext *ctx, JSValueConst object)
{
    struct qjs_event_target *target = js_mallocz(ctx, sizeof(*target));
    if (target == NULL)
        return -1;
    JS_SetOpaque(object, target);
    dom_exception err = dom_event_target_create(&target->native);
    if (err != DOM_NO_ERR) {
        qjs_dom_exception(ctx, err);
        return -1;
    }
    target->thread = JS_GetContextOpaque(ctx);
    target->object = object;
    target->next = target->thread->event_targets;
    target->previous = &target->thread->event_targets;
    if (target->next != NULL)
        target->next->previous = &target->next;
    target->thread->event_targets = target;
    return 0;
}

static JSValue target_constructor(JSContext *ctx, JSValueConst new_target, int argc, JSValueConst *argv)
{
    jsthread *thread = JS_GetContextOpaque(ctx);
    if (thread == NULL || thread->closed)
        return qjs_dom_exception(ctx, DOM_INVALID_STATE_ERR);
    JSValue object = qjs_event_new_object(ctx, new_target, thread->heap->event_target_class);
    if (JS_IsException(object))
        return object;
    if (thread->closed || target_initialize(ctx, object) < 0) {
        JS_FreeValue(ctx, object);
        return thread->closed ? qjs_dom_exception(ctx, DOM_INVALID_STATE_ERR) : JS_EXCEPTION;
    }
    return object;
}

struct options {
    bool capture, once, passive, passive_present;
};
static int listener_options(JSContext *ctx, JSValueConst object, bool adding, struct options *options)
{
    if (JS_IsNull(object) || JS_IsUndefined(object))
        return 0;
    if (!JS_IsObject(object)) {
        options->capture = JS_ToBool(ctx, object);
        return 0;
    }
    const char *names[] = {"capture", "once", "passive", "signal"};
    for (unsigned i = 0; i < (adding ? 4 : 1); i++) {
        JSValue value = JS_GetPropertyStr(ctx, object, names[i]);
        if (JS_IsException(value))
            return -1;
        bool flag = JS_ToBool(ctx, value);
        switch (i) {
        case 0:
            options->capture = flag;
            break;
        case 1:
            options->once = flag;
            break;
        case 2:
            options->passive = flag;
            options->passive_present = !JS_IsUndefined(value);
            break;
        case 3:
            /* AbortSignal is not exposed yet. No arbitrary object is a valid signal. */
            if (!JS_IsUndefined(value)) {
                JS_FreeValue(ctx, value);
                JS_ThrowTypeError(ctx, "signal requires an AbortSignal");
                return -1;
            }
            break;
        }
        JS_FreeValue(ctx, value);
    }
    return 0;
}

static void listener_invoke(dom_event *event, void *pw)
{
    struct listener *listener = pw;
    jsthread *thread = listener->thread;
    struct qjs_event_dispatch *dispatch = thread->event_dispatch;
    while (dispatch != NULL && dispatch->native != event)
        dispatch = dispatch->previous;
    if (dispatch == NULL || thread->closed || thread->heap->interrupted)
        return;
    struct qjs_event *value = qjs_event_get(thread->ctx, dispatch->object);
    if (listener->once)
        listener_remove(listener); /* Native snapshot still owns the callback record. */
    dom_event_set_passive_listener(event, listener->passive);
    qjs_trace(thread->ctx, "event.listener", "id=%" PRIu64 " registered_by=%" PRIu64, listener->id, listener->entry);
    enum qjs_result result = qjs_run_event_listener(
        thread, listener->callback, value->target, dispatch->object, listener->source);
    dom_event_set_passive_listener(event, false);
    if (thread->closed || result == QJS_INTERRUPTED)
        dom_event_stop_immediate_propagation(event);
}

static bool default_passive(dom_string *type)
{
    const char *names[] = {"touchstart", "touchmove", "wheel", "mousewheel"};
    for (unsigned i = 0; i < sizeof(names) / sizeof(*names); i++)
        if (dom_string_byte_length(type) == strlen(names[i]) &&
            memcmp(dom_string_data(type), names[i], strlen(names[i])) == 0)
            return true;
    return false;
}

static JSValue listener_method(JSContext *ctx, JSValueConst object, int argc, JSValueConst *argv, int adding)
{
    bool window;
    JSValue owner = target_owner(ctx, object, &window), result = JS_EXCEPTION;
    if (JS_IsException(owner))
        return owner;
    dom_string *type = NULL;
    if (argc < 2) {
        JS_ThrowTypeError(ctx, "Event listener requires a type and callback");
        goto out;
    }
    type = qjs_dom_string(ctx, argv[0], false);
    if (type == NULL)
        goto out;
    bool empty = JS_IsNull(argv[1]) || JS_IsUndefined(argv[1]);
    if (!empty && !JS_IsObject(argv[1])) {
        JS_ThrowTypeError(ctx, "Event listener must be a callback object or null");
        goto out;
    }
    struct options options = {0};
    if (argc > 2 && listener_options(ctx, argv[2], adding, &options) < 0)
        goto out;
    jsthread *thread = JS_GetContextOpaque(ctx);
    if (thread->closed) {
        qjs_dom_exception(ctx, DOM_INVALID_STATE_ERR);
        goto out;
    }
    result = JS_UNDEFINED;
    if (empty)
        goto out;
    struct qjs_event_target *target = JS_GetOpaque(owner, thread->heap->event_target_class);
    struct listener **slot = &target->listeners;
    for (; *slot != NULL; slot = &(*slot)->next) {
        struct listener *l = *slot;
        if (l->capture == options.capture && dom_string_isequal(l->type, type) &&
            JS_IsStrictEqual(ctx, l->callback, argv[1])) {
            if (!adding) {
                qjs_trace(ctx, "event.remove", "id=%" PRIu64, l->id);
                listener_remove(l);
            }
            goto out;
        }
    }
    if (!adding)
        goto out;
    struct listener *listener = js_mallocz(ctx, sizeof(*listener));
    if (listener == NULL) {
        result = JS_EXCEPTION;
        goto out;
    }
    listener->rt = JS_GetRuntime(ctx);
    listener->thread = thread;
    listener->target = target;
    listener->type = dom_string_ref(type);
    listener->callback = JS_DupValue(ctx, argv[1]);
    listener->capture = options.capture;
    listener->once = options.once;
    listener->passive = options.passive_present ? options.passive : window && default_passive(type);
    listener->entry = thread->trace_entry;
    listener->id = ++thread->trace_next_listener;
    listener->source = js_strdup(ctx, thread->entry_source != NULL ? thread->entry_source : "<event listener>");
    if (listener->source == NULL) {
        listener_cleanup(listener);
        result = JS_EXCEPTION;
        goto out;
    }
    dom_exception err = dom_event_listener_create_owned(listener_invoke, listener, listener_cleanup, &listener->native);
    if (err != DOM_NO_ERR) {
        listener_cleanup(listener);
        result = qjs_dom_exception(ctx, err);
        goto out;
    }
    err = dom_event_target_add_event_listener(target->native, type, listener->native, listener->capture);
    if (err != DOM_NO_ERR) {
        dom_event_listener_unref(listener->native);
        result = qjs_dom_exception(ctx, err);
        goto out;
    }
    listener->previous = slot;
    *slot = listener;
    qjs_trace(ctx, "event.add", "id=%" PRIu64 " type=%.*s capture=%d once=%d passive=%d", listener->id,
        (int)(dom_string_byte_length(type) > 128 ? 128 : dom_string_byte_length(type)), dom_string_data(type),
        listener->capture, listener->once, listener->passive);
out:
    dom_string_unref(type);
    JS_FreeValue(ctx, owner);
    return result;
}

static JSValue target_dispatch(JSContext *ctx, JSValueConst object, int argc, JSValueConst *argv)
{
    bool window;
    JSValue owner = target_owner(ctx, object, &window);
    if (JS_IsException(owner))
        return owner;
    struct qjs_event *event = argc > 0 ? qjs_event_get(ctx, argv[0]) : NULL;
    if (argc == 0)
        JS_ThrowTypeError(ctx, "dispatchEvent requires an Event");
    JSValue result = JS_EXCEPTION;
    if (event == NULL)
        goto out;
    bool active;
    dom_event_in_dispatch(event->native, &active);
    if (active) {
        qjs_dom_exception(ctx, DOM_INVALID_STATE_ERR);
        goto out;
    }
    jsthread *thread = JS_GetContextOpaque(ctx);
    struct qjs_event_target *target = JS_GetOpaque(owner, thread->heap->event_target_class);
    JS_FreeValue(ctx, event->target);
    event->target = window ? JS_GetGlobalObject(ctx) : JS_DupValue(ctx, object);
    dom_event_set_is_trusted(event->native, false);
    struct qjs_event_dispatch dispatch = {thread->event_dispatch, event->native, argv[0]};
    thread->event_dispatch = &dispatch;
    qjs_thread_ref(thread);
    qjs_trace(ctx, "event.dispatch", "target=%s", window ? "window" : "EventTarget");
    bool success;
    dom_exception err = dom_event_target_dispatch_event(target->native, event->native, &success);
    thread->event_dispatch = dispatch.previous;
    qjs_trace(ctx, "event.complete", "canceled=%d result=%u", !success, err);
    if (thread->heap->interrupted) {
        /* Preserve the original uncatchable interrupt from the listener. */
        result = JS_EXCEPTION;
    } else {
        result = err == DOM_NO_ERR ? JS_NewBool(ctx, success) : qjs_dom_exception(ctx, err);
    }
    qjs_thread_unref(thread);
out:
    JS_FreeValue(ctx, owner);
    return result;
}

static const JSCFunctionListEntry target_properties[] = {QJS_EVENT_METHOD("addEventListener", 2, listener_method, 1),
    QJS_EVENT_METHOD("removeEventListener", 2, listener_method, 0),
    JS_CFUNC_DEF2("dispatchEvent", 1, target_dispatch, JS_PROP_C_W_E)};

int qjs_init_event_target(JSContext *ctx)
{
    if (JS_AddIntrinsicDOMException(ctx) < 0 || qjs_init_events(ctx) < 0)
        return -1;
    jsthread *thread = JS_GetContextOpaque(ctx);
    JSClassID local = 0, *id = thread != NULL ? &thread->heap->event_target_class : &local;
    if (*id == 0) {
        JS_NewClassID(JS_GetRuntime(ctx), id);
        JSClassDef def = {.class_name = "EventTarget", .finalizer = target_finalizer, .gc_mark = target_mark};
        if (JS_NewClass(JS_GetRuntime(ctx), *id, &def) < 0) {
            *id = 0;
            return -1;
        }
    }
    JSValue global = JS_GetGlobalObject(ctx), proto = JS_NewObject(ctx);
    JSValue ctor = JS_NewCFunction2(ctx, target_constructor, "EventTarget", 0, JS_CFUNC_constructor, 0);
    int ret = -1;
    if (JS_IsException(proto) || JS_IsException(ctor) ||
        JS_SetPropertyFunctionList(
            ctx, proto, target_properties, sizeof(target_properties) / sizeof(*target_properties)) < 0 ||
        qjs_event_define_interface(ctx, global, "EventTarget", ctor, proto) < 0 ||
        JS_SetPrototype(ctx, global, proto) < 0)
        goto out;
    JS_SetClassProto(ctx, *id, JS_DupValue(ctx, proto));
    if (thread != NULL) {
        JSValue window = JS_NewObjectClass(ctx, *id);
        if (JS_IsException(window))
            goto out;
        if (target_initialize(ctx, window) < 0) {
            JS_FreeValue(ctx, window);
            goto out;
        }
        thread->window_event_target = window;
    }
    ret = 0;
out:
    JS_FreeValue(ctx, ctor);
    JS_FreeValue(ctx, proto);
    JS_FreeValue(ctx, global);
    return ret;
}

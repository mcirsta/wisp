/* SPDX-License-Identifier: GPL-2.0-or-later
 * Duktape's EventTarget.bnd informed the native bridge. Registrations are owned
 * by GC-visible QuickJS targets; libdom owns invocation snapshots and ordering.
 */
#include "event_target.h"
#include "event.h"
#include "abort.h"
#include "xhr.h"
#include <inttypes.h>
#include <string.h>

struct listener;
struct event_handlers {
    JSValue callback[QJS_HANDLERS], listener[QJS_HANDLERS];
};
struct qjs_event_target {
    dom_event_target *native;
    struct listener *listeners;
    jsthread *thread;
    struct qjs_event_target *next, **previous;
    bool node;
    uint64_t node_id;
    JSValue object; /* Borrowed identity; temporarily rooted while closing. */
    struct event_handlers *event_handlers; /* Allocated only when an IDL handler is set. */
};

struct listener {
    struct listener *next, **previous;
    struct qjs_event_target *target;
    dom_event_listener *native;
    dom_string *type;
    JSRuntime *rt;
    jsthread *thread; /* Borrowed: callbacks are entered only from an active JS dispatch. */
    JSValue callback, signal;
    struct qjs_abort_algorithm *abort_algorithm;
    char *source;
    uint64_t id, entry;
    bool capture, once, passive;
};

struct qjs_event_dispatch {
    struct qjs_event_dispatch *previous;
    dom_event *native;
    JSValue object; /* Borrowed from the caller's rooted event (script or native). */
    JSValue document; /* Owned for DOM dispatch, undefined for parentless targets. */
    bool failed;
};

static void listener_cleanup(void *pw)
{
    struct listener *listener = pw;
    dom_string_unref(listener->type);
    JS_FreeValueRT(listener->rt, listener->callback);
    JS_FreeValueRT(listener->rt, listener->signal);
    js_free_rt(listener->rt, listener->source);
    js_free_rt(listener->rt, listener);
}

static void listener_remove(void *pw)
{
    struct listener *listener = pw;
    *listener->previous = listener->next;
    if (listener->next != NULL)
        listener->next->previous = listener->previous;
    dom_event_target_remove_event_listener(
        listener->target->native, listener->type, listener->native, listener->capture);
    struct qjs_abort_algorithm *algorithm = listener->abort_algorithm;
    JSValue signal = listener->signal;
    listener->abort_algorithm = NULL;
    listener->signal = JS_UNDEFINED;
    qjs_abort_remove(listener->rt, algorithm);
    JS_FreeValueRT(listener->rt, signal);
    dom_event_listener_unref(listener->native);
    /* listener can now be freed, or retained by a native invocation snapshot. */
}

static void target_clear(struct qjs_event_target *target, JSRuntime *rt)
{
    while (target->listeners != NULL)
        listener_remove(target->listeners);
    if (target->event_handlers != NULL) {
        for (unsigned i = 0; i < QJS_HANDLERS; i++) {
            JS_FreeValueRT(rt, target->event_handlers->callback[i]);
            JS_FreeValueRT(rt, target->event_handlers->listener[i]);
        }
        js_free_rt(rt, target->event_handlers);
        target->event_handlers = NULL;
    }
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
        target_clear(target, rt);
        dom_event_target_unref(target->native);
        js_free_rt(rt, target);
    }
}

static void target_mark(JSRuntime *rt, JSValueConst object, JS_MarkFunc *mark)
{
    struct qjs_event_target *target = JS_GetOpaque(object, JS_GetClassID(object));
    if (target != NULL)
        for (struct listener *l = target->listeners; l != NULL; l = l->next) {
            JS_MarkValue(rt, l->callback, mark);
            JS_MarkValue(rt, l->signal, mark);
        }
    if (target != NULL && target->event_handlers != NULL)
        for (unsigned i = 0; i < QJS_HANDLERS; i++) {
            JS_MarkValue(rt, target->event_handlers->callback[i], mark);
            JS_MarkValue(rt, target->event_handlers->listener[i], mark);
        }
}

void qjs_events_close(jsthread *thread)
{
    while (thread->event_targets != NULL) {
        struct qjs_event_target *target = thread->event_targets;
        JSValue owner = JS_DupValue(thread->ctx, target->object);
        target_detach(target);
        target_clear(target, thread->heap->rt);
        dom_event_target_unref(target->native);
        target->native = NULL;
        JS_FreeValue(thread->ctx, owner);
    }
    qjs_abort_close(thread);
    JS_FreeValue(thread->ctx, thread->window_event_target);
    thread->window_event_target = JS_UNDEFINED;
}

void qjs_events_connect_document(JSContext *ctx, dom_document *document)
{
    jsthread *thread = JS_GetContextOpaque(ctx);
    struct qjs_event_target *window = JS_GetOpaque(thread->window_event_target, thread->heap->event_target_class);
    dom_document_set_event_parent(document, window->native);
}

static int target_initialize(JSContext *ctx, JSValueConst object, struct qjs_dom_node *node);

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
    JSValue signal_target = qjs_abort_target(ctx, object);
    if (!JS_IsUndefined(signal_target))
        return signal_target;
    JSValue xhr_target = qjs_xhr_target(ctx, object);
    if (!JS_IsUndefined(xhr_target))
        return xhr_target;
    if (JS_GetOpaque(object, thread->heap->node_class) != NULL) {
        struct qjs_dom_node *node = qjs_dom_get(ctx, object, 0);
        if (node == NULL)
            return JS_EXCEPTION;
        if (JS_IsUndefined(node->event_target)) {
            JSValue storage = JS_NewObjectClass(ctx, thread->heap->event_target_class);
            if (JS_IsException(storage))
                return storage;
            if (target_initialize(ctx, storage, node) < 0) {
                JS_FreeValue(ctx, storage);
                return JS_EXCEPTION;
            }
            node->event_target = storage;
        }
        return JS_DupValue(ctx, node->event_target);
    }
    struct qjs_event_target *target = JS_GetOpaque2(ctx, object, thread->heap->event_target_class);
    if (target == NULL)
        return JS_EXCEPTION;
    if (target->thread != thread)
        return qjs_dom_exception(ctx, DOM_INVALID_STATE_ERR);
    return JS_DupValue(ctx, object);
}

bool qjs_event_target_validate(JSContext *ctx, JSValueConst object)
{
    bool window;
    JSValue owner = target_owner(ctx, object, &window);
    bool valid = !JS_IsException(owner);
    JS_FreeValue(ctx, owner);
    return valid;
}

static int target_initialize(JSContext *ctx, JSValueConst object, struct qjs_dom_node *node)
{
    struct qjs_event_target *target = js_mallocz(ctx, sizeof(*target));
    if (target == NULL)
        return -1;
    JS_SetOpaque(object, target);
    dom_exception err = DOM_NO_ERR;
    if (node != NULL) {
        target->native = (dom_event_target *)node->node;
        dom_event_target_ref(target->native);
        target->node = true;
        target->node_id = node->id;
    } else {
        err = dom_event_target_create(&target->native);
    }
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

JSValue qjs_event_target_create(JSContext *ctx)
{
    jsthread *thread = JS_GetContextOpaque(ctx);
    JSValue object = JS_NewObjectClass(ctx, thread->heap->event_target_class);
    if (JS_IsException(object))
        return object;
    if (target_initialize(ctx, object, NULL) < 0) {
        JS_FreeValue(ctx, object);
        return JS_EXCEPTION;
    }
    return object;
}

static JSValue target_constructor(JSContext *ctx, JSValueConst new_target, int argc, JSValueConst *argv)
{
    jsthread *thread = JS_GetContextOpaque(ctx);
    if (thread == NULL || thread->closed)
        return qjs_dom_exception(ctx, DOM_INVALID_STATE_ERR);
    JSValue object = qjs_event_new_object(ctx, new_target, thread->heap->event_target_class);
    if (JS_IsException(object))
        return object;
    if (thread->closed || target_initialize(ctx, object, NULL) < 0) {
        JS_FreeValue(ctx, object);
        return thread->closed ? qjs_dom_exception(ctx, DOM_INVALID_STATE_ERR) : JS_EXCEPTION;
    }
    return object;
}

struct options {
    bool capture, once, passive, passive_present;
    JSValue signal;
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
            if (!JS_IsUndefined(value)) {
                if (!qjs_abort_validate(ctx, value)) {
                    JS_FreeValue(ctx, value);
                    return -1;
                }
                options->signal = JS_DupValue(ctx, value);
            }
            break;
        }
        JS_FreeValue(ctx, value);
    }
    return 0;
}

static int prepare_path(JSContext *ctx, struct qjs_event_dispatch *dispatch, struct qjs_event *event)
{
    if (!JS_IsUndefined(event->path))
        return 0;
    jsthread *thread = JS_GetContextOpaque(ctx);
    struct qjs_event_target *window = JS_GetOpaque(thread->window_event_target, thread->heap->event_target_class);
    JSValue path = JS_NewArray(ctx);
    if (JS_IsException(path))
        return -1;
    /* This array is private. Use own data properties so page changes to
     * Array.prototype cannot intercept host path construction or reads. */
    for (size_t i = 0; i < dom_event_get_path_length(event->native); i++) {
        dom_event_target *native = dom_event_get_path_target(event->native, i);
        JSValue value = native == window->native ? JS_GetGlobalObject(ctx)
            : JS_IsUndefined(dispatch->document) ? JS_DupValue(ctx, event->target)
                                                 : qjs_dom_wrap(ctx, dispatch->document, (dom_node *)native);
        if (JS_IsException(value) || JS_DefinePropertyValueUint32(ctx, path, i, value, JS_PROP_C_W_E) < 0) {
            JS_FreeValue(ctx, path);
            return -1;
        }
    }
    event->path = path;
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
    if (prepare_path(thread->ctx, dispatch, value) < 0) {
        dispatch->failed = true;
        dom_event_stop_immediate_propagation(event);
        return;
    }
    if (listener->once)
        listener_remove(listener); /* Native snapshot still owns the callback record. */
    dom_event_set_passive_listener(event, listener->passive);
    qjs_trace(thread->ctx, "event.listener", "id=%" PRIu64 " registered_by=%" PRIu64, listener->id, listener->entry);
    JSValue current = qjs_event_current_target(thread->ctx, value);
    enum qjs_result result = qjs_run_event_listener(
        thread, listener->callback, current, dispatch->object, listener->source);
    JS_FreeValue(thread->ctx, current);
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

static bool passive_root(JSContext *ctx, JSValueConst object, bool window)
{
    if (window)
        return true;
    jsthread *thread = JS_GetContextOpaque(ctx);
    struct qjs_dom_node *node = JS_GetOpaque(object, thread->heap->node_class);
    if (node == NULL)
        return false;
    if (node->type == DOM_DOCUMENT_NODE)
        return true;
    dom_document *doc = NULL;
    dom_node *element = NULL, *body = NULL;
    dom_node_get_owner_document(node->node, &doc);
    dom_document_get_document_element(doc, &element);
    dom_html_document_get_body(doc, &body);
    bool result = node->node == element || node->node == body;
    dom_node_unref(element);
    dom_node_unref(body);
    dom_node_unref(doc);
    return result;
}

static JSValue listener_method(JSContext *ctx, JSValueConst object, int argc, JSValueConst *argv, int adding)
{
    bool window;
    JSValue owner = target_owner(ctx, object, &window), result = JS_EXCEPTION;
    if (JS_IsException(owner))
        return owner;
    dom_string *type = NULL;
    struct options options = {.signal = JS_UNDEFINED};
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
    if (argc > 2 && listener_options(ctx, argv[2], adding, &options) < 0)
        goto out;
    jsthread *thread = JS_GetContextOpaque(ctx);
    if (thread->closed) {
        qjs_dom_exception(ctx, DOM_INVALID_STATE_ERR);
        goto out;
    }
    result = JS_UNDEFINED;
    if (empty || (!JS_IsUndefined(options.signal) && qjs_abort_aborted(ctx, options.signal)))
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
    listener->signal = JS_DupValue(ctx, options.signal);
    listener->capture = options.capture;
    listener->once = options.once;
    listener->passive = options.passive_present ? options.passive
                                                : default_passive(type) && passive_root(ctx, object, window);
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
    if (!JS_IsUndefined(options.signal)) {
        JSValue identity = window ? JS_GetGlobalObject(ctx) : JS_DupValue(ctx, object);
        listener->abort_algorithm = qjs_abort_add(ctx, options.signal, identity, listener_remove, listener);
        JS_FreeValue(ctx, identity);
        if (listener->abort_algorithm == NULL) {
            listener_remove(listener);
            result = JS_EXCEPTION;
            goto out;
        }
    }
    qjs_trace(ctx, "event.add", "id=%" PRIu64 " type=%.*s capture=%d once=%d passive=%d", listener->id,
        (int)(dom_string_byte_length(type) > 128 ? 128 : dom_string_byte_length(type)), dom_string_data(type),
        listener->capture, listener->once, listener->passive);
out:
    JS_FreeValue(ctx, options.signal);
    dom_string_unref(type);
    JS_FreeValue(ctx, owner);
    return result;
}

int qjs_event_target_listen(JSContext *ctx, JSValueConst target, const char *type, JSValueConst callback, bool add)
{
    JSValue args[] = {JS_NewString(ctx, type), callback};
    if (JS_IsException(args[0]))
        return -1;
    JSValue result = listener_method(ctx, target, 2, args, add);
    JS_FreeValue(ctx, args[0]);
    JS_FreeValue(ctx, result);
    return JS_IsException(result) ? -1 : 0;
}

/* IDL event handlers are independent registrations, so replacing a
 * callback retains its ordering relative to addEventListener registrations. */
static struct qjs_event_target *handler_target(JSContext *ctx, JSValueConst object)
{
    bool window;
    JSValue owner = target_owner(ctx, object, &window);
    if (JS_IsException(owner))
        return NULL;
    jsthread *thread = JS_GetContextOpaque(ctx);
    struct qjs_event_target *target = JS_GetOpaque(owner, thread->heap->event_target_class);
    JS_FreeValue(ctx, owner);
    return target;
}

static JSValue handler_invoke(JSContext *ctx, JSValueConst object, int argc, JSValueConst *argv, int kind)
{
    struct qjs_event_target *target = handler_target(ctx, object);
    if (target == NULL)
        return JS_EXCEPTION;
    if (target->event_handlers == NULL || !JS_IsFunction(ctx, target->event_handlers->callback[kind]))
        return JS_UNDEFINED; /* Web IDL LegacyTreatNonObjectAsNull: non-callable objects do nothing. */
    JSValue callback = JS_DupValue(ctx, target->event_handlers->callback[kind]);
    JSValue result = JS_Call(ctx, callback, object, argc, argv);
    JS_FreeValue(ctx, callback);
    if (JS_IsBool(result) && !JS_ToBool(ctx, result) && argc > 0) {
        struct qjs_event *event = qjs_event_get(ctx, argv[0]);
        if (event != NULL)
            dom_event_prevent_default(event->native);
    }
    return result;
}

JSValue qjs_event_handler_get(JSContext *ctx, JSValueConst object, int kind)
{
    struct qjs_event_target *target = handler_target(ctx, object);
    return target == NULL                ? JS_EXCEPTION
        : target->event_handlers == NULL ? JS_NULL
                                         : JS_DupValue(ctx, target->event_handlers->callback[kind]);
}

JSValue qjs_event_handler_set(JSContext *ctx, JSValueConst object, JSValueConst value, int kind)
{
    struct qjs_event_target *target = handler_target(ctx, object);
    if (target == NULL)
        return JS_EXCEPTION;
    static const char *types[QJS_HANDLERS] = {"scroll", "resize", "load", "error",
        "readystatechange", "loadstart", "progress", "abort", "timeout", "loadend", "scrollend"};
    const char *type = types[kind];
    if (!JS_IsObject(value))
        value = JS_NULL;
    struct event_handlers *handlers = target->event_handlers;
    if (handlers == NULL) {
        if (JS_IsNull(value))
            return JS_UNDEFINED;
        handlers = js_malloc(ctx, sizeof(*handlers));
        if (handlers == NULL)
            return JS_EXCEPTION;
        for (unsigned i = 0; i < QJS_HANDLERS; i++)
            handlers->callback[i] = handlers->listener[i] = JS_NULL;
        target->event_handlers = handlers;
    }
    if (JS_IsNull(value)) {
        if (!JS_IsNull(handlers->listener[kind])) {
            if (qjs_event_target_listen(ctx, object, type, handlers->listener[kind], false) < 0)
                return JS_EXCEPTION;
            JS_FreeValue(ctx, handlers->listener[kind]);
            handlers->listener[kind] = JS_NULL;
        }
    } else if (JS_IsNull(handlers->listener[kind])) {
        JSValue listener = JS_NewCFunctionMagic(ctx, handler_invoke, "", 1, JS_CFUNC_generic_magic, kind);
        if (JS_IsException(listener))
            return listener;
        if (qjs_event_target_listen(ctx, object, type, listener, true) < 0) {
            JS_FreeValue(ctx, listener);
            return JS_EXCEPTION;
        }
        handlers->listener[kind] = listener;
    }
    JSValue previous = handlers->callback[kind];
    handlers->callback[kind] = JS_DupValue(ctx, value);
    JS_FreeValue(ctx, previous);
    qjs_trace(ctx, "event.handler", "type=%s active=%d", type, !JS_IsNull(value));
    return JS_UNDEFINED;
}

/* Window's accessors retain their Window-only brand check; other interfaces
 * can expose the same storage without making EventTarget itself a handler host. */
static bool window_handler_receiver(JSContext *ctx, JSValueConst object)
{
    JSValue global = JS_GetGlobalObject(ctx);
    bool valid = JS_IsUndefined(object) || JS_IsNull(object) || JS_IsStrictEqual(ctx, object, global);
    JS_FreeValue(ctx, global);
    if (!valid)
        JS_ThrowTypeError(ctx, "Window receiver required");
    return valid;
}

static JSValue window_handler_get(JSContext *ctx, JSValueConst object, int kind)
{
    return window_handler_receiver(ctx, object) ? qjs_event_handler_get(ctx, object, kind) : JS_EXCEPTION;
}

static JSValue window_handler_set(JSContext *ctx, JSValueConst object, JSValueConst value, int kind)
{
    return window_handler_receiver(ctx, object) ? qjs_event_handler_set(ctx, object, value, kind) : JS_EXCEPTION;
}

static JSValue
dispatch_event(JSContext *ctx, JSValueConst object, JSValueConst value, bool script, JSValueConst target_override)
{
    bool window;
    JSValue owner = target_owner(ctx, object, &window);
    if (JS_IsException(owner))
        return owner;
    struct qjs_event *event = qjs_event_get(ctx, value);
    JSValue result = JS_EXCEPTION;
    if (event == NULL)
        goto out;
    bool active, initialized;
    dom_event_in_dispatch(event->native, &active);
    dom_event_is_initialised(event->native, &initialized);
    if (active || !initialized) {
        qjs_dom_exception(ctx, DOM_INVALID_STATE_ERR);
        goto out;
    }
    jsthread *thread = JS_GetContextOpaque(ctx);
    struct qjs_event_target *target = JS_GetOpaque(owner, thread->heap->event_target_class);
    JS_FreeValue(ctx, event->target);
    event->target = !JS_IsUndefined(target_override) ? JS_DupValue(ctx, target_override)
        : window                                     ? JS_GetGlobalObject(ctx)
                                                     : JS_DupValue(ctx, object);
    if (script)
        dom_event_set_is_trusted(event->native, false);
    JSValue document = JS_UNDEFINED;
    if (target->node) {
        struct qjs_dom_node *node = qjs_dom_get(ctx, object, 0);
        if (node == NULL)
            goto out;
        document = qjs_dom_owner(ctx, object, node);
    }
    struct qjs_event_dispatch dispatch = {
        .previous = thread->event_dispatch, .native = event->native, .object = value, .document = document};
    thread->event_dispatch = &dispatch;
    qjs_thread_ref(thread);
    qjs_trace(ctx, "event.dispatch", "target=%s node=%" PRIu64,
        window             ? "window"
            : target->node ? "Node"
                           : "EventTarget",
        target->node_id);
    bool success;
    dom_exception err = dom_event_target_dispatch_event(target->native, event->native, &success);
    thread->event_dispatch = dispatch.previous;
    JS_FreeValue(ctx, event->path);
    event->path = JS_UNDEFINED;
    JS_FreeValue(ctx, dispatch.document);
    qjs_trace(ctx, "event.complete", "canceled=%d result=%u", !success, err);
    if (thread->heap->interrupted || dispatch.failed) {
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

JSValue qjs_event_target_dispatch(JSContext *ctx, JSValueConst object, JSValueConst value, bool script)
{
    return dispatch_event(ctx, object, value, script, JS_UNDEFINED);
}

JSValue qjs_event_target_dispatch_load(JSContext *ctx, JSValueConst document, JSValueConst value)
{
    /* HTML's Window load is dispatched at Window with the legacy target override.
     * The override changes event.target, not the native propagation path. */
    return dispatch_event(ctx, JS_UNDEFINED, value, false, document);
}

static JSValue target_dispatch(JSContext *ctx, JSValueConst object, int argc, JSValueConst *argv)
{
    if (argc == 0)
        return JS_ThrowTypeError(ctx, "dispatchEvent requires an Event");
    return qjs_event_target_dispatch(ctx, object, argv[0], true);
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
    static const JSCFunctionListEntry handlers[] = {
        JS_CGETSET_MAGIC_DEF2(
            "onscroll", window_handler_get, window_handler_set, 0, JS_PROP_CONFIGURABLE | JS_PROP_ENUMERABLE),
        JS_CGETSET_MAGIC_DEF2(
            "onresize", window_handler_get, window_handler_set, 1, JS_PROP_CONFIGURABLE | JS_PROP_ENUMERABLE),
        JS_CGETSET_MAGIC_DEF2("onscrollend", window_handler_get, window_handler_set,
            QJS_HANDLER_SCROLLEND, JS_PROP_CONFIGURABLE | JS_PROP_ENUMERABLE),
    };
    if (JS_IsException(proto) || JS_IsException(ctor) ||
        JS_SetPropertyFunctionList(
            ctx, proto, target_properties, sizeof(target_properties) / sizeof(*target_properties)) < 0 ||
        qjs_event_define_interface(ctx, global, "EventTarget", ctor, proto) < 0 ||
        JS_SetPrototype(ctx, global, proto) < 0 ||
        JS_SetPropertyFunctionList(ctx, global, handlers, sizeof(handlers) / sizeof(*handlers)) < 0)
        goto out;
    JS_SetClassProto(ctx, *id, JS_DupValue(ctx, proto));
    if (thread != NULL) {
        JSValue window = JS_NewObjectClass(ctx, *id);
        if (JS_IsException(window))
            goto out;
        if (target_initialize(ctx, window, NULL) < 0) {
            JS_FreeValue(ctx, window);
            goto out;
        }
        thread->window_event_target = window;
    }
    ret = qjs_init_abort(ctx, proto);
out:
    JS_FreeValue(ctx, ctor);
    JS_FreeValue(ctx, proto);
    JS_FreeValue(ctx, global);
    return ret;
}

/* SPDX-License-Identifier: GPL-2.0-or-later
 * Native-backed Event values. Duktape's Event.bnd was consulted for libdom's
 * accessors; conversion, ownership and prototypes follow current DOM/Web IDL.
 */
#include "event.h"
#include <nsutils/time.h>
#include <string.h>

struct qjs_event *qjs_event_get(JSContext *ctx, JSValueConst value)
{
    jsthread *thread = JS_GetContextOpaque(ctx);
    struct qjs_event *event = NULL;
    if (thread != NULL) {
        event = JS_GetOpaque(value, thread->heap->event_class[0]);
        if (event == NULL)
            event = JS_GetOpaque(value, thread->heap->event_class[1]);
    }
    if (event == NULL)
        JS_ThrowTypeError(ctx, "Event receiver required");
    return event;
}

static void event_finalizer(JSRuntime *rt, JSValue value)
{
    struct qjs_event *event = JS_GetOpaque(value, JS_GetClassID(value));
    if (event == NULL)
        return;
    if (event->native != NULL)
        dom_event_unref(event->native);
    JS_FreeValueRT(rt, event->target);
    JS_FreeValueRT(rt, event->detail);
    JS_FreeValueRT(rt, event->path);
    js_free_rt(rt, event);
}

static void event_mark(JSRuntime *rt, JSValueConst value, JS_MarkFunc *mark)
{
    struct qjs_event *event = JS_GetOpaque(value, JS_GetClassID(value));
    if (event != NULL) {
        JS_MarkValue(rt, event->target, mark);
        JS_MarkValue(rt, event->detail, mark);
        JS_MarkValue(rt, event->path, mark);
    }
}

JSValue qjs_event_new_object(JSContext *ctx, JSValueConst new_target, JSClassID id)
{
    JSValue proto = JS_GetPropertyStr(ctx, new_target, "prototype");
    if (JS_IsException(proto))
        return proto;
    if (!JS_IsObject(proto)) {
        JS_FreeValue(ctx, proto);
        proto = JS_GetClassProto(ctx, id);
    }
    JSValue object = JS_NewObjectProtoClass(ctx, proto, id);
    JS_FreeValue(ctx, proto);
    return object;
}

static int boolean_member(JSContext *ctx, JSValueConst dict, const char *key, bool *value)
{
    JSValue member = JS_GetPropertyStr(ctx, dict, key);
    if (JS_IsException(member))
        return -1;
    *value = JS_ToBool(ctx, member);
    JS_FreeValue(ctx, member);
    return 0;
}

enum event_property {
    TYPE,
    TARGET,
    CURRENT,
    PHASE,
    BUBBLES,
    CANCELABLE,
    PREVENTED,
    TRUSTED,
    TIMESTAMP,
    COMPOSED,
    DETAIL,
    CANCEL_BUBBLE,
    RETURN_VALUE
};

static JSValue event_get(JSContext *ctx, JSValueConst object, int property)
{
    struct qjs_event *event = qjs_event_get(ctx, object);
    if (event == NULL)
        return JS_EXCEPTION;
    bool value;
    dom_event_flow_phase phase;
    switch (property) {
    case TYPE: {
        dom_string *type;
        dom_event_get_type(event->native, &type);
        JSValue result = qjs_dom_from_string(ctx, type, false);
        dom_string_unref(type);
        return result;
    }
    case TARGET:
        return JS_DupValue(ctx, event->target);
    case CURRENT:
        return qjs_event_current_target(ctx, event);
    case PHASE:
        dom_event_get_event_phase(event->native, &phase);
        return JS_NewInt32(ctx, phase);
    case BUBBLES:
        dom_event_get_bubbles(event->native, &value);
        break;
    case CANCELABLE:
        dom_event_get_cancelable(event->native, &value);
        break;
    case PREVENTED:
        dom_event_is_default_prevented(event->native, &value);
        break;
    case TRUSTED:
        dom_event_get_is_trusted(event->native, &value);
        break;
    case CANCEL_BUBBLE:
        dom_event_get_cancel_bubble(event->native, &value);
        break;
    case RETURN_VALUE:
        dom_event_is_default_prevented(event->native, &value);
        value = !value;
        break;
    case TIMESTAMP:
        return JS_NewFloat64(ctx, event->timestamp);
    case COMPOSED:
        return JS_NewBool(ctx, event->composed);
    case DETAIL:
        if (!event->custom)
            return JS_ThrowTypeError(ctx, "CustomEvent receiver required");
        return JS_DupValue(ctx, event->detail);
    default:
        return JS_UNDEFINED;
    }
    return JS_NewBool(ctx, value);
}

JSValue qjs_event_current_target(JSContext *ctx, struct qjs_event *event)
{
    dom_event_target *current;
    dom_event_get_current_target(event->native, &current);
    JSValue result = JS_NULL;
    for (size_t i = 0; current != NULL && i < dom_event_get_path_length(event->native); i++) {
        if (dom_event_get_path_target(event->native, i) == current) {
            result = JS_GetPropertyUint32(ctx, event->path, i);
            break;
        }
    }
    dom_event_target_unref(current);
    return result;
}

static JSValue event_set(JSContext *ctx, JSValueConst object, JSValueConst value, int property)
{
    struct qjs_event *event = qjs_event_get(ctx, object);
    if (event == NULL)
        return JS_EXCEPTION;
    bool flag = JS_ToBool(ctx, value);
    if (property == CANCEL_BUBBLE && flag)
        dom_event_stop_propagation(event->native);
    if (property == RETURN_VALUE && !flag)
        dom_event_prevent_default(event->native);
    return JS_UNDEFINED;
}

static JSValue event_allocate(JSContext *ctx, JSValue object, bool custom, bool composed, JSValueConst detail)
{
    if (JS_IsException(object))
        return object;
    jsthread *thread = JS_GetContextOpaque(ctx);
    struct qjs_event *event = js_mallocz(ctx, sizeof(*event));
    if (event == NULL)
        goto fail_object;
    event->target = JS_NULL;
    event->path = JS_UNDEFINED;
    event->detail = JS_DupValue(ctx, detail);
    event->custom = custom;
    event->composed = composed;
    uint64_t now;
    if (nsu_getmonotonic_ms(&now) != NSUERROR_OK)
        now = thread->time_origin_ms;
    event->timestamp = now >= thread->time_origin_ms ? (double)(now - thread->time_origin_ms) : 0;
    JS_SetOpaque(object, event);
    dom_exception err = dom_event_create(&event->native);
    if (err != DOM_NO_ERR) {
        qjs_dom_exception(ctx, err);
        goto fail_object;
    }
    /* isTrusted is a legacy unforgeable own accessor. */
    JSAtom key = JS_NewAtom(ctx, "isTrusted");
    JSValue getter = JS_NewCFunctionMagic(
        ctx, (JSCFunctionMagic *)event_get, "get isTrusted", 0, JS_CFUNC_getter_magic, TRUSTED);
    if (key == JS_ATOM_NULL || JS_IsException(getter)) {
        JS_FreeValue(ctx, getter);
        JS_FreeAtom(ctx, key);
        goto fail_object;
    }
    int ret = JS_DefinePropertyGetSet(ctx, object, key, getter, JS_UNDEFINED, JS_PROP_ENUMERABLE);
    JS_FreeAtom(ctx, key);
    if (ret < 0)
        goto fail_object;
    return object;
fail_object:
    JS_FreeValue(ctx, object);
    return JS_EXCEPTION;
}

JSValue qjs_event_create(JSContext *ctx, const char *type)
{
    jsthread *thread = JS_GetContextOpaque(ctx);
    if (thread == NULL || thread->closed)
        return qjs_dom_exception(ctx, DOM_INVALID_STATE_ERR);
    dom_string *name;
    dom_exception err = dom_string_create((const uint8_t *)type, strlen(type), &name);
    if (err != DOM_NO_ERR)
        return qjs_dom_exception(ctx, err);
    JSValue object = event_allocate(ctx, JS_NewObjectClass(ctx, thread->heap->event_class[0]), false, false, JS_NULL);
    if (!JS_IsException(object))
        dom_event_init(qjs_event_get(ctx, object)->native, name, false, false);
    dom_string_unref(name);
    return object;
}

static JSValue event_constructor(JSContext *ctx, JSValueConst new_target, int argc, JSValueConst *argv, int custom)
{
    jsthread *thread = JS_GetContextOpaque(ctx);
    if (thread == NULL || argc == 0)
        return JS_ThrowTypeError(ctx, "Event requires a type and a document context");
    dom_string *type = qjs_dom_string(ctx, argv[0], false);
    if (type == NULL)
        return JS_EXCEPTION;
    JSValue detail = JS_NULL, object = JS_EXCEPTION;
    bool bubbles = false, cancelable = false, composed = false;
    if (argc > 1 && !JS_IsNull(argv[1]) && !JS_IsUndefined(argv[1])) {
        if (!JS_IsObject(argv[1])) {
            JS_ThrowTypeError(ctx, "Event options must be a dictionary");
            goto out;
        }
        /* Web IDL dictionary members are converted in inheritance/alphabetical order. */
        if (boolean_member(ctx, argv[1], "bubbles", &bubbles) < 0 ||
            boolean_member(ctx, argv[1], "cancelable", &cancelable) < 0 ||
            boolean_member(ctx, argv[1], "composed", &composed) < 0)
            goto out;
        if (custom) {
            detail = JS_GetPropertyStr(ctx, argv[1], "detail");
            if (JS_IsException(detail))
                goto out;
            if (JS_IsUndefined(detail))
                detail = JS_NULL;
        }
    }
    object = event_allocate(
        ctx, qjs_event_new_object(ctx, new_target, thread->heap->event_class[custom]), custom, composed, detail);
    if (!JS_IsException(object)) {
        struct qjs_event *event = qjs_event_get(ctx, object);
        dom_event_init(event->native, type, bubbles, cancelable);
        dom_event_set_is_trusted(event->native, false);
    }
out:
    dom_string_unref(type);
    JS_FreeValue(ctx, detail);
    return object;
}

enum event_method { STOP, STOP_NOW, PREVENT, INIT, INIT_CUSTOM, PATH };
static JSValue event_method(JSContext *ctx, JSValueConst object, int argc, JSValueConst *argv, int method)
{
    struct qjs_event *event = qjs_event_get(ctx, object);
    if (event == NULL)
        return JS_EXCEPTION;
    switch (method) {
    case STOP:
        dom_event_stop_propagation(event->native);
        break;
    case STOP_NOW:
        dom_event_stop_immediate_propagation(event->native);
        break;
    case PREVENT:
        dom_event_prevent_default(event->native);
        break;
    case PATH: {
        JSValue result = JS_NewArray(ctx);
        if (JS_IsException(result))
            return result;
        for (size_t i = 0; i < dom_event_get_path_length(event->native); i++) {
            JSValue target = JS_GetPropertyUint32(ctx, event->path, i);
            if (JS_IsException(target) || JS_DefinePropertyValueUint32(ctx, result, i, target, JS_PROP_C_W_E) < 0) {
                JS_FreeValue(ctx, result);
                return JS_EXCEPTION;
            }
        }
        return result;
    }
    case INIT:
    case INIT_CUSTOM: {
        if (method == INIT_CUSTOM && !event->custom)
            return JS_ThrowTypeError(ctx, "CustomEvent receiver required");
        if (argc == 0)
            return JS_ThrowTypeError(ctx, "Event initialization requires a type");
        dom_string *type = qjs_dom_string(ctx, argv[0], false);
        if (type == NULL)
            return JS_EXCEPTION;
        bool bubbles = argc > 1 && JS_ToBool(ctx, argv[1]);
        bool cancelable = argc > 2 && JS_ToBool(ctx, argv[2]);
        bool active;
        dom_event_in_dispatch(event->native, &active);
        if (!active) {
            dom_event_init(event->native, type, bubbles, cancelable);
            dom_event_set_is_trusted(event->native, false);
            JS_FreeValue(ctx, event->target);
            event->target = JS_NULL;
            if (method == INIT_CUSTOM) {
                JSValue detail = argc > 3 && !JS_IsUndefined(argv[3]) ? JS_DupValue(ctx, argv[3]) : JS_NULL;
                JS_FreeValue(ctx, event->detail);
                event->detail = detail;
            }
        }
        dom_string_unref(type);
        break;
    }
    }
    return JS_UNDEFINED;
}

int qjs_event_define_interface(
    JSContext *ctx, JSValueConst global, const char *name, JSValueConst ctor, JSValueConst proto)
{
    JSValue symbol = JS_GetPropertyStr(ctx, global, "Symbol");
    JSValue tag = JS_IsException(symbol) ? JS_EXCEPTION : JS_GetPropertyStr(ctx, symbol, "toStringTag");
    JS_FreeValue(ctx, symbol);
    JSAtom key = JS_IsException(tag) ? JS_ATOM_NULL : JS_ValueToAtom(ctx, tag);
    JS_FreeValue(ctx, tag);
    if (key == JS_ATOM_NULL)
        return -1;
    int ret = JS_DefinePropertyValue(ctx, proto, key, JS_NewString(ctx, name), JS_PROP_CONFIGURABLE);
    JS_FreeAtom(ctx, key);
    if (ret < 0 || JS_SetConstructor(ctx, ctor, proto) < 0)
        return -1;
    return JS_DefinePropertyValueStr(
        ctx, global, name, JS_DupValue(ctx, ctor), JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE);
}

#define EVENT_GET(name, key)                                                                                           \
    JS_CGETSET_MAGIC_DEF2(name, event_get, NULL, key, JS_PROP_CONFIGURABLE | JS_PROP_ENUMERABLE)
#define EVENT_METHOD(name, length, key) QJS_EVENT_METHOD(name, length, event_method, key)
static const JSCFunctionListEntry event_properties[] = {
    EVENT_GET("type", TYPE),
    EVENT_GET("target", TARGET),
    EVENT_GET("srcElement", TARGET),
    EVENT_GET("currentTarget", CURRENT),
    EVENT_GET("eventPhase", PHASE),
    EVENT_GET("bubbles", BUBBLES),
    EVENT_GET("cancelable", CANCELABLE),
    EVENT_GET("defaultPrevented", PREVENTED),
    EVENT_GET("timeStamp", TIMESTAMP),
    EVENT_GET("composed", COMPOSED),
    JS_CGETSET_MAGIC_DEF2(
        "cancelBubble", event_get, event_set, CANCEL_BUBBLE, JS_PROP_CONFIGURABLE | JS_PROP_ENUMERABLE),
    JS_CGETSET_MAGIC_DEF2("returnValue", event_get, event_set, RETURN_VALUE, JS_PROP_CONFIGURABLE | JS_PROP_ENUMERABLE),
    EVENT_METHOD("stopPropagation", 0, STOP),
    EVENT_METHOD("stopImmediatePropagation", 0, STOP_NOW),
    EVENT_METHOD("preventDefault", 0, PREVENT),
    EVENT_METHOD("initEvent", 1, INIT),
    EVENT_METHOD("composedPath", 0, PATH),
};
static const JSCFunctionListEntry custom_properties[] = {
    EVENT_GET("detail", DETAIL), EVENT_METHOD("initCustomEvent", 1, INIT_CUSTOM)};

int qjs_init_events(JSContext *ctx)
{
    jsthread *thread = JS_GetContextOpaque(ctx);
    JSClassID local[2] = {0};
    JSClassID *ids = thread != NULL ? thread->heap->event_class : local;
    JSValue global = JS_GetGlobalObject(ctx), parent = JS_UNDEFINED, parent_ctor = JS_UNDEFINED;
    int ret = -1;
    for (unsigned i = 0; i < 2; i++) {
        const char *name = i ? "CustomEvent" : "Event";
        if (ids[i] == 0) {
            JS_NewClassID(JS_GetRuntime(ctx), &ids[i]);
            JSClassDef def = {.class_name = name, .finalizer = event_finalizer, .gc_mark = event_mark};
            if (JS_NewClass(JS_GetRuntime(ctx), ids[i], &def) < 0) {
                ids[i] = 0;
                goto out;
            }
        }
        JSValue proto = i ? JS_NewObjectProto(ctx, parent) : JS_NewObject(ctx);
        JSValue ctor = JS_NewCFunctionMagic(ctx, event_constructor, name, 1, JS_CFUNC_constructor_magic, i);
        const JSCFunctionListEntry *properties = i ? custom_properties : event_properties;
        int count = i ? sizeof(custom_properties) / sizeof(custom_properties[0])
                      : sizeof(event_properties) / sizeof(event_properties[0]);
        bool failed = JS_IsException(proto) || JS_IsException(ctor) ||
            JS_SetPropertyFunctionList(ctx, proto, properties, count) < 0 ||
            (i && JS_SetPrototype(ctx, ctor, parent_ctor) < 0) ||
            qjs_event_define_interface(ctx, global, name, ctor, proto) < 0;
        if (!failed && !i) {
            const char *constants[] = {"NONE", "CAPTURING_PHASE", "AT_TARGET", "BUBBLING_PHASE"};
            for (unsigned n = 0; n < 4; n++) {
                if (JS_DefinePropertyValueStr(ctx, proto, constants[n], JS_NewInt32(ctx, n), JS_PROP_ENUMERABLE) < 0 ||
                    JS_DefinePropertyValueStr(ctx, ctor, constants[n], JS_NewInt32(ctx, n), JS_PROP_ENUMERABLE) < 0) {
                    failed = true;
                    break;
                }
            }
        }
        if (!failed)
            JS_SetClassProto(ctx, ids[i], JS_DupValue(ctx, proto));
        JS_FreeValue(ctx, parent);
        JS_FreeValue(ctx, parent_ctor);
        parent = proto;
        parent_ctor = ctor;
        if (failed)
            goto out;
    }
    ret = 0;
out:
    JS_FreeValue(ctx, global);
    JS_FreeValue(ctx, parent);
    JS_FreeValue(ctx, parent_ctor);
    return ret;
}

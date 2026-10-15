/* SPDX-License-Identifier: GPL-2.0-or-later
 * Geometry Interfaces: value rectangles, independent of DOM/layout lifetime. */
#include "dom.h"
#include "event.h"
#include <math.h>
#include <string.h>

struct dom_rect {
    double value[4];
};

static struct dom_rect *rect_get(JSContext *ctx, JSValueConst object, bool writable)
{
    jsthread *thread = JS_GetContextOpaque(ctx);
    struct dom_rect *rect = thread ? JS_GetOpaque(object, thread->heap->rect_class[1]) : NULL;
    if (rect == NULL && thread != NULL && !writable)
        rect = JS_GetOpaque(object, thread->heap->rect_class[0]);
    if (rect == NULL)
        JS_ThrowTypeError(ctx, "DOMRect receiver required");
    return rect;
}

static void rect_free(JSRuntime *rt, JSValue object)
{
    js_free_rt(rt, JS_GetOpaque(object, JS_GetClassID(object)));
}

/* fmin/fmax suppress NaN; Web IDL's unrestricted-double geometry preserves it. */
static double rect_value(const struct dom_rect *rect, int property)
{
    if (property < 4)
        return rect->value[property];
    int axis = property == 4 || property == 6 ? 1 : 0; /* top, right, bottom, left */
    double start = rect->value[axis], end = start + rect->value[axis + 2];
    if (isnan(start) || isnan(end))
        return NAN;
    return property == 4 || property == 7 ? fmin(start, end) : fmax(start, end);
}

static JSValue rect_getter(JSContext *ctx, JSValueConst object, int property)
{
    struct dom_rect *rect = rect_get(ctx, object, false);
    return rect ? JS_NewFloat64(ctx, rect_value(rect, property)) : JS_EXCEPTION;
}

static JSValue rect_setter(JSContext *ctx, JSValueConst object, JSValueConst value, int property)
{
    struct dom_rect *rect = rect_get(ctx, object, true);
    double number;
    if (rect == NULL || JS_ToFloat64(ctx, &number, value) < 0)
        return JS_EXCEPTION;
    rect->value[property] = number;
    return JS_UNDEFINED;
}

static JSValue rect_create(JSContext *ctx, JSValueConst new_target, const double *values, int kind)
{
    jsthread *thread = JS_GetContextOpaque(ctx);
    JSClassID id = thread->heap->rect_class[kind];
    JSValue object = JS_IsUndefined(new_target) ? JS_NewObjectClass(ctx, id)
                                                : qjs_event_new_object(ctx, new_target, id);
    if (JS_IsException(object))
        return object;
    struct dom_rect *rect = js_malloc(ctx, sizeof(*rect));
    if (rect == NULL) {
        JS_FreeValue(ctx, object);
        return JS_EXCEPTION;
    }
    memcpy(rect->value, values, sizeof(rect->value));
    JS_SetOpaque(object, rect);
    return object;
}

JSValue qjs_rect_new(JSContext *ctx, double x, double y, double width, double height)
{
    const double values[] = {x, y, width, height};
    return rect_create(ctx, JS_UNDEFINED, values, 1);
}

static JSValue rect_constructor(JSContext *ctx, JSValueConst new_target, int argc, JSValueConst *argv, int kind)
{
    double values[4] = {0};
    for (int i = 0; i < 4 && i < argc; i++) {
        if (!JS_IsUndefined(argv[i]) && JS_ToFloat64(ctx, &values[i], argv[i]) < 0)
            return JS_EXCEPTION;
    }
    return rect_create(ctx, new_target, values, kind);
}

static JSValue rect_from(JSContext *ctx, JSValueConst object, int argc, JSValueConst *argv, int kind)
{
    (void)object;
    double values[4] = {0};
    JSValueConst dict = argc ? argv[0] : JS_UNDEFINED;
    if (!JS_IsNull(dict) && !JS_IsUndefined(dict)) {
        if (!JS_IsObject(dict))
            return JS_ThrowTypeError(ctx, "DOMRectInit dictionary required");
        static const char *names[] = {"x", "y", "width", "height"};
        /* Web IDL dictionary members are converted in lexicographic order. */
        const int order[] = {3, 2, 0, 1};
        for (int j = 0; j < 4; j++) {
            int i = order[j];
            JSValue value = JS_GetPropertyStr(ctx, dict, names[i]);
            bool failed = JS_IsException(value) || (!JS_IsUndefined(value) && JS_ToFloat64(ctx, &values[i], value) < 0);
            JS_FreeValue(ctx, value);
            if (failed)
                return JS_EXCEPTION;
        }
    }
    return rect_create(ctx, JS_UNDEFINED, values, kind);
}

static JSValue rect_json(JSContext *ctx, JSValueConst object, int argc, JSValueConst *argv)
{
    (void)argc;
    (void)argv;
    struct dom_rect *rect = rect_get(ctx, object, false);
    if (rect == NULL)
        return JS_EXCEPTION;
    const char *names[] = {"x", "y", "width", "height", "top", "right", "bottom", "left"};
    JSValue result = JS_NewObject(ctx);
    if (JS_IsException(result))
        return result;
    for (int i = 0; i < 8; i++) {
        if (JS_DefinePropertyValueStr(ctx, result, names[i], JS_NewFloat64(ctx, rect_value(rect, i)), JS_PROP_C_W_E) <
            0) {
            JS_FreeValue(ctx, result);
            return JS_EXCEPTION;
        }
    }
    return result;
}

int qjs_init_rect(JSContext *ctx)
{
    jsthread *thread = JS_GetContextOpaque(ctx);
    JSClassID local_ids[2] = {0};
    JSClassID *ids = thread ? thread->heap->rect_class : local_ids;
    static const JSClassDef classes[] = {
        {.class_name = "DOMRectReadOnly", .finalizer = rect_free}, {.class_name = "DOMRect", .finalizer = rect_free}};
#define RECT_GET(name, index)                                                                                          \
    JS_CGETSET_MAGIC_DEF2(name, rect_getter, NULL, index, JS_PROP_CONFIGURABLE | JS_PROP_ENUMERABLE)
#define RECT_SET(name, index)                                                                                          \
    JS_CGETSET_MAGIC_DEF2(name, rect_getter, rect_setter, index, JS_PROP_CONFIGURABLE | JS_PROP_ENUMERABLE)
    static const JSCFunctionListEntry readonly[] = {RECT_GET("x", 0), RECT_GET("y", 1), RECT_GET("width", 2),
        RECT_GET("height", 3), RECT_GET("top", 4), RECT_GET("right", 5), RECT_GET("bottom", 6), RECT_GET("left", 7),
        JS_CFUNC_DEF2("toJSON", 0, rect_json, JS_PROP_C_W_E)};
    static const JSCFunctionListEntry writable[] = {
        RECT_SET("x", 0), RECT_SET("y", 1), RECT_SET("width", 2), RECT_SET("height", 3)};
#undef RECT_GET
#undef RECT_SET
    JSValue global = JS_GetGlobalObject(ctx), parent = JS_UNDEFINED, parent_ctor = JS_UNDEFINED;
    int result = -1;
    for (int kind = 0; kind < 2; kind++) {
        JSClassID *id = &ids[kind];
        if (*id == 0) {
            JS_NewClassID(JS_GetRuntime(ctx), id);
            if (JS_NewClass(JS_GetRuntime(ctx), *id, &classes[kind]) < 0) {
                *id = 0;
                goto out;
            }
        }
        JSValue proto = kind ? JS_NewObjectProto(ctx, parent) : JS_NewObject(ctx);
        JSValue ctor = JS_NewCFunction2(
            ctx, (JSCFunction *)rect_constructor, classes[kind].class_name, 0, JS_CFUNC_constructor_magic, kind);
        bool failed = JS_IsException(proto) || JS_IsException(ctor) ||
            JS_SetPropertyFunctionList(ctx, proto, kind ? writable : readonly,
                kind ? sizeof(writable) / sizeof(*writable) : sizeof(readonly) / sizeof(*readonly)) < 0 ||
            qjs_event_define_interface(ctx, global, classes[kind].class_name, ctor, proto) < 0;
        if (!failed && kind)
            failed = JS_SetPrototype(ctx, ctor, parent_ctor) < 0;
        if (!failed) {
            JSValue from = JS_NewCFunctionMagic(ctx, rect_from, "fromRect", 0, JS_CFUNC_generic_magic, kind);
            failed = JS_IsException(from) || JS_DefinePropertyValueStr(ctx, ctor, "fromRect", from, JS_PROP_C_W_E) < 0;
        }
        if (!failed)
            JS_SetClassProto(ctx, *id, JS_DupValue(ctx, proto));
        JS_FreeValue(ctx, parent);
        JS_FreeValue(ctx, parent_ctor);
        parent = proto;
        parent_ctor = ctor;
        if (failed)
            goto out;
    }
    result = 0;
out:
    JS_FreeValue(ctx, parent);
    JS_FreeValue(ctx, parent_ctor);
    JS_FreeValue(ctx, global);
    return result;
}

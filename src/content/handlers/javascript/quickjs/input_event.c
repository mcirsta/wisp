/* SPDX-License-Identifier: GPL-2.0-or-later
 * UI Events / MouseEvent values. Duktape's Event and KeyboardEvent bindings
 * informed the adapter boundary; conversion and lifetime follow current IDL.
 */
#include "input_event.h"
#include "event_target.h"
#include <math.h>
#include <string.h>

/* EventModifierInit member order is alphabetical within this dictionary. */
static const struct {
    const char *member, *key;
} modifiers[] = {{"altKey", "Alt"}, {"ctrlKey", "Control"}, {"metaKey", "Meta"}, {"modifierAltGraph", "AltGraph"},
    {"modifierCapsLock", "CapsLock"}, {"modifierFn", "Fn"}, {"modifierFnLock", "FnLock"}, {"modifierHyper", "Hyper"},
    {"modifierNumLock", "NumLock"}, {"modifierScrollLock", "ScrollLock"}, {"modifierSuper", "Super"},
    {"modifierSymbol", "Symbol"}, {"modifierSymbolLock", "SymbolLock"}, {"shiftKey", "Shift"}};
enum { ALT = 0, CTRL = 1, META = 2, SHIFT = 13 };
#define PRIMARY_MODIFIERS ((1u << ALT) | (1u << CTRL) | (1u << META) | (1u << SHIFT))

enum input_property { VIEW, DETAIL, SCREEN_X, SCREEN_Y, CLIENT_X, CLIENT_Y, BUTTON, BUTTONS, RELATED };

static struct qjs_input_data *input_get(JSContext *ctx, JSValueConst object, bool mouse)
{
    struct qjs_event *event = qjs_event_get(ctx, object);
    if (event == NULL)
        return NULL;
    if (mouse ? (event->kind != QJS_MOUSE_EVENT && event->kind != QJS_POINTER_EVENT) : !qjs_event_is_ui(event->kind)) {
        JS_ThrowTypeError(ctx, mouse ? "MouseEvent receiver required" : "UIEvent receiver required");
        return NULL;
    }
    return &((struct qjs_input_event *)event)->data;
}

static JSValue input_getter(JSContext *ctx, JSValueConst object, int property)
{
    struct qjs_input_data *data = input_get(ctx, object, property >= SCREEN_X);
    if (data == NULL)
        return JS_EXCEPTION;
    switch (property) {
    case VIEW:
        return JS_DupValue(ctx, data->view);
    case DETAIL:
        return JS_NewInt32(ctx, data->detail);
    case SCREEN_X:
        return JS_NewFloat64(ctx, data->screen_x);
    case SCREEN_Y:
        return JS_NewFloat64(ctx, data->screen_y);
    case CLIENT_X:
        return JS_NewFloat64(ctx, data->client_x);
    case CLIENT_Y:
        return JS_NewFloat64(ctx, data->client_y);
    case BUTTON:
        return JS_NewInt32(ctx, data->button);
    case BUTTONS:
        return JS_NewInt32(ctx, data->buttons);
    case RELATED:
        return JS_DupValue(ctx, data->related);
    }
    return JS_UNDEFINED;
}

static JSValue modifier_getter(JSContext *ctx, JSValueConst object, int modifier)
{
    struct qjs_input_data *data = input_get(ctx, object, true);
    return data != NULL ? JS_NewBool(ctx, data->modifiers & (1u << modifier)) : JS_EXCEPTION;
}

static JSValue modifier_state(JSContext *ctx, JSValueConst object, int argc, JSValueConst *argv)
{
    struct qjs_input_data *data = input_get(ctx, object, true);
    if (data == NULL)
        return JS_EXCEPTION;
    if (argc == 0)
        return JS_ThrowTypeError(ctx, "getModifierState requires a key");
    dom_string *key = qjs_dom_string(ctx, argv[0], false);
    if (key == NULL)
        return JS_EXCEPTION;
    bool active = false;
    for (unsigned i = 0; i < sizeof(modifiers) / sizeof(*modifiers); i++) {
        size_t length = strlen(modifiers[i].key);
        if (dom_string_byte_length(key) == length && memcmp(dom_string_data(key), modifiers[i].key, length) == 0) {
            active = (data->modifiers & (1u << i)) != 0;
            break;
        }
    }
    dom_string_unref(key);
    return JS_NewBool(ctx, active);
}

/* Nullable interface conversion, with the current document's Window identity.
 * Cross-document WindowProxy objects are not exposed by these bindings. */
static JSValue input_reference(JSContext *ctx, JSValueConst value, bool view)
{
    if (JS_IsNull(value) || JS_IsUndefined(value))
        return JS_NULL;
    if (view) {
        JSValue global = JS_GetGlobalObject(ctx);
        bool same = JS_IsStrictEqual(ctx, value, global);
        JS_FreeValue(ctx, global);
        if (!same)
            return JS_ThrowTypeError(ctx, "view requires a Window or null");
    } else if (!qjs_event_target_validate(ctx, value)) {
        return JS_EXCEPTION;
    }
    return JS_DupValue(ctx, value);
}

static int integer_member(JSContext *ctx, JSValueConst dict, const char *name, int32_t *result)
{
    JSValue value = JS_GetPropertyStr(ctx, dict, name);
    if (JS_IsException(value))
        return -1;
    int ret = JS_ToInt32(ctx, result, value);
    JS_FreeValue(ctx, value);
    return ret;
}

static int coordinate_member(JSContext *ctx, JSValueConst dict, const char *name, double *result)
{
    JSValue value = JS_GetPropertyStr(ctx, dict, name);
    if (JS_IsException(value))
        return -1;
    int ret = 0;
    if (!JS_IsUndefined(value)) {
        ret = JS_ToFloat64(ctx, result, value);
        if (ret == 0 && !isfinite(*result)) {
            JS_ThrowTypeError(ctx, "MouseEvent coordinates must be finite");
            ret = -1;
        }
    }
    JS_FreeValue(ctx, value);
    return ret;
}

static int reference_member(JSContext *ctx, JSValueConst dict, const char *name, JSValue *result, bool view)
{
    JSValue value = JS_GetPropertyStr(ctx, dict, name);
    if (JS_IsException(value))
        return -1;
    *result = input_reference(ctx, value, view);
    JS_FreeValue(ctx, value);
    return JS_IsException(*result) ? -1 : 0;
}

static int16_t signed_short(int32_t value)
{
    int32_t low = (uint32_t)value & 0xffffu;
    return (int16_t)(low >= 0x8000 ? low - 0x10000 : low);
}

static int input_options(JSContext *ctx, JSValueConst dict, struct qjs_input_data *data, bool mouse)
{
    if (!JS_IsObject(dict))
        return 0; /* EventInit has already checked the dictionary. */
    if (integer_member(ctx, dict, "detail", &data->detail) < 0 ||
        reference_member(ctx, dict, "view", &data->view, true) < 0)
        return -1;
    if (!mouse)
        return 0;
    for (unsigned i = 0; i < sizeof(modifiers) / sizeof(*modifiers); i++) {
        JSValue value = JS_GetPropertyStr(ctx, dict, modifiers[i].member);
        if (JS_IsException(value))
            return -1;
        if (JS_ToBool(ctx, value))
            data->modifiers |= 1u << i;
        JS_FreeValue(ctx, value);
    }
    int32_t button, buttons;
    if (integer_member(ctx, dict, "button", &button) < 0 || integer_member(ctx, dict, "buttons", &buttons) < 0 ||
        coordinate_member(ctx, dict, "clientX", &data->client_x) < 0 ||
        coordinate_member(ctx, dict, "clientY", &data->client_y) < 0 ||
        reference_member(ctx, dict, "relatedTarget", &data->related, false) < 0 ||
        coordinate_member(ctx, dict, "screenX", &data->screen_x) < 0 ||
        coordinate_member(ctx, dict, "screenY", &data->screen_y) < 0)
        return -1;
    data->button = signed_short(button);
    data->buttons = (uint16_t)((uint32_t)buttons & 0xffffu);
    return 0;
}

enum pointer_property {
    POINTER_ID,
    WIDTH,
    HEIGHT,
    PRESSURE,
    TANGENTIAL_PRESSURE,
    TILT_X,
    TILT_Y,
    TWIST,
    POINTER_TYPE,
    IS_PRIMARY
};

static JSValue pointer_getter(JSContext *ctx, JSValueConst object, int property)
{
    struct qjs_event *base = qjs_event_get(ctx, object);
    if (base == NULL)
        return JS_EXCEPTION;
    if (base->kind != QJS_POINTER_EVENT)
        return JS_ThrowTypeError(ctx, "PointerEvent receiver required");
    struct qjs_pointer_event *event = (struct qjs_pointer_event *)base;
    switch (property) {
    case POINTER_ID:
        return JS_NewInt32(ctx, event->pointer_id);
    case WIDTH:
        return JS_NewFloat64(ctx, event->width);
    case HEIGHT:
        return JS_NewFloat64(ctx, event->height);
    case PRESSURE:
        return JS_NewFloat64(ctx, event->pressure);
    case TANGENTIAL_PRESSURE:
        return JS_NewFloat64(ctx, event->tangential_pressure);
    case TILT_X:
        return JS_NewInt32(ctx, event->tilt_x);
    case TILT_Y:
        return JS_NewInt32(ctx, event->tilt_y);
    case TWIST:
        return JS_NewInt32(ctx, event->twist);
    case POINTER_TYPE:
        return JS_DupValue(ctx, event->pointer_type);
    case IS_PRIMARY:
        return JS_NewBool(ctx, event->primary);
    }
    return JS_UNDEFINED;
}

static int pointer_options(JSContext *ctx, JSValueConst dict, struct qjs_pointer_event *data)
{
    data->width = data->height = 1;
    data->pointer_type = JS_NewString(ctx, "");
    if (JS_IsException(data->pointer_type))
        return -1;
    if (JS_IsUndefined(dict) || JS_IsNull(dict))
        return 0;
    const char *names[] = {"height", "isPrimary", "pointerId", "pointerType", "pressure", "tangentialPressure", "tiltX",
        "tiltY", "twist", "width"};
    for (unsigned i = 0; i < sizeof(names) / sizeof(*names); i++) {
        JSValue value = JS_GetPropertyStr(ctx, dict, names[i]);
        if (JS_IsException(value))
            return -1;
        int result = 0;
        if (!JS_IsUndefined(value)) {
            switch (i) {
            case 1:
                data->primary = JS_ToBool(ctx, value);
                break;
            case 2:
                result = JS_ToInt32(ctx, &data->pointer_id, value);
                break;
            case 3:
                JS_FreeValue(ctx, data->pointer_type);
                data->pointer_type = JS_ToString(ctx, value);
                result = JS_IsException(data->pointer_type) ? -1 : 0;
                break;
            case 6:
                result = JS_ToInt32(ctx, &data->tilt_x, value);
                break;
            case 7:
                result = JS_ToInt32(ctx, &data->tilt_y, value);
                break;
            case 8:
                result = JS_ToInt32(ctx, &data->twist, value);
                break;
            default: {
                double *number = i == 0 ? &data->height
                    : i == 4            ? &data->pressure
                    : i == 5            ? &data->tangential_pressure
                                        : &data->width;
                result = JS_ToFloat64(ctx, number, value);
                if (result == 0 && (i == 4 || i == 5))
                    *number = (float)*number; /* Web IDL float, unlike double width/height. */
                if (result == 0 && !isfinite(*number)) {
                    JS_ThrowTypeError(ctx, "PointerEvent numbers must be finite");
                    result = -1;
                }
                break;
            }
            }
        }
        JS_FreeValue(ctx, value);
        if (result < 0)
            return -1;
    }
    return 0;
}

JSValue qjs_input_event_constructor(JSContext *ctx, JSValueConst new_target, int argc, JSValueConst *argv, int kind)
{
    jsthread *thread = JS_GetContextOpaque(ctx);
    if (thread == NULL || argc == 0)
        return JS_ThrowTypeError(ctx, "UIEvent requires a type and a document context");
    dom_string *type = qjs_dom_string(ctx, argv[0], false);
    if (type == NULL)
        return JS_EXCEPTION;
    struct qjs_event_options options = {0};
    struct qjs_input_data data = {.view = JS_NULL, .related = JS_NULL};
    struct qjs_pointer_event pointer = {.pointer_type = JS_NULL};
    JSValue result = JS_EXCEPTION;
    JSValueConst dict = argc > 1 ? argv[1] : JS_UNDEFINED;
    if (qjs_event_read_options(ctx, dict, &options) < 0 || input_options(ctx, dict, &data, kind != QJS_UI_EVENT) < 0)
        goto out;
    if (kind == QJS_POINTER_EVENT && pointer_options(ctx, dict, &pointer) < 0)
        goto out;
    JSValue object = qjs_event_new_object(ctx, new_target, thread->heap->event_class[kind]);
    if (JS_IsException(object))
        goto out;
    result = qjs_event_allocate(ctx, object, kind, options.composed);
    if (!JS_IsException(result)) {
        struct qjs_input_event *event = (struct qjs_input_event *)qjs_event_get(ctx, result);
        if (kind == QJS_POINTER_EVENT) {
            struct qjs_input_event base = *event;
            *(struct qjs_pointer_event *)event = pointer;
            *event = base;
            pointer.pointer_type = JS_NULL;
        }
        event->data = data;
        data.view = data.related = JS_NULL; /* Ownership transferred. */
        dom_event_init(event->base.native, type, options.bubbles, options.cancelable);
        dom_event_set_is_trusted(event->base.native, false);
    }
out:
    JS_FreeValue(ctx, pointer.pointer_type);
    JS_FreeValue(ctx, data.view);
    JS_FreeValue(ctx, data.related);
    dom_string_unref(type);
    return result;
}

static JSValue input_init(JSContext *ctx, JSValueConst object, int argc, JSValueConst *argv, int mouse)
{
    struct qjs_input_data *current = input_get(ctx, object, mouse);
    if (current == NULL)
        return JS_EXCEPTION;
    if (argc == 0)
        return JS_ThrowTypeError(ctx, "Event initialization requires a type");
    dom_string *type = qjs_dom_string(ctx, argv[0], false);
    if (type == NULL)
        return JS_EXCEPTION;
    struct qjs_input_data data = {.view = JS_NULL, .related = JS_NULL};
    bool bubbles = argc > 1 && JS_ToBool(ctx, argv[1]), cancelable = argc > 2 && JS_ToBool(ctx, argv[2]);
    JSValue result = JS_EXCEPTION;
    data.view = input_reference(ctx, argc > 3 ? argv[3] : JS_NULL, true);
    if (JS_IsException(data.view) || (argc > 4 && JS_ToInt32(ctx, &data.detail, argv[4]) < 0))
        goto out;
    if (mouse) {
        /* Legacy initMouseEvent still takes long coordinates, not doubles. */
        double *coordinates[] = {&data.screen_x, &data.screen_y, &data.client_x, &data.client_y};
        for (unsigned i = 0; i < 4; i++) {
            int32_t value = 0;
            if (argc > (int)i + 5 && JS_ToInt32(ctx, &value, argv[i + 5]) < 0)
                goto out;
            *coordinates[i] = value;
        }
        const unsigned bits[] = {CTRL, ALT, SHIFT, META};
        for (unsigned i = 0; i < 4; i++)
            if (argc > (int)i + 9 && JS_ToBool(ctx, argv[i + 9]))
                data.modifiers |= 1u << bits[i];
        int32_t button = 0;
        if (argc > 13 && JS_ToInt32(ctx, &button, argv[13]) < 0)
            goto out;
        data.button = signed_short(button);
        data.related = input_reference(ctx, argc > 14 ? argv[14] : JS_NULL, false);
        if (JS_IsException(data.related))
            goto out;
    }
    struct qjs_event *event = qjs_event_get(ctx, object);
    bool active;
    dom_event_in_dispatch(event->native, &active);
    if (!active) {
        dom_event_init(event->native, type, bubbles, cancelable);
        dom_event_set_is_trusted(event->native, false);
        JS_FreeValue(ctx, event->target);
        event->target = JS_NULL;
        JS_FreeValue(ctx, current->view);
        current->view = data.view;
        data.view = JS_NULL;
        current->detail = data.detail;
        if (mouse) {
            current->screen_x = data.screen_x;
            current->screen_y = data.screen_y;
            current->client_x = data.client_x;
            current->client_y = data.client_y;
            current->modifiers = (current->modifiers & ~PRIMARY_MODIFIERS) | data.modifiers;
            current->button = data.button;
            JS_FreeValue(ctx, current->related);
            current->related = data.related;
            data.related = JS_NULL;
            /* No parameters initialize buttons or additional modifiers. */
        }
    }
    result = JS_UNDEFINED;
out:
    JS_FreeValue(ctx, data.view);
    JS_FreeValue(ctx, data.related);
    dom_string_unref(type);
    return result;
}

#define INPUT_GET(name, key) JS_CGETSET_MAGIC_DEF2(name, input_getter, NULL, key, JS_PROP_C_W_E)
#define MODIFIER_GET(name, key) JS_CGETSET_MAGIC_DEF2(name, modifier_getter, NULL, key, JS_PROP_C_W_E)
int qjs_input_event_properties(JSContext *ctx, JSValueConst proto, enum qjs_event_kind kind)
{
    static const JSCFunctionListEntry ui[] = {
        INPUT_GET("view", VIEW), INPUT_GET("detail", DETAIL), QJS_EVENT_METHOD("initUIEvent", 1, input_init, false)};
    static const JSCFunctionListEntry mouse[] = {INPUT_GET("screenX", SCREEN_X), INPUT_GET("screenY", SCREEN_Y),
        INPUT_GET("clientX", CLIENT_X), INPUT_GET("clientY", CLIENT_Y), INPUT_GET("x", CLIENT_X),
        INPUT_GET("y", CLIENT_Y), INPUT_GET("button", BUTTON), INPUT_GET("buttons", BUTTONS),
        INPUT_GET("relatedTarget", RELATED), MODIFIER_GET("ctrlKey", CTRL), MODIFIER_GET("altKey", ALT),
        MODIFIER_GET("shiftKey", SHIFT), MODIFIER_GET("metaKey", META),
        JS_CFUNC_DEF2("getModifierState", 1, modifier_state, JS_PROP_C_W_E),
        QJS_EVENT_METHOD("initMouseEvent", 1, input_init, true)};
    if (kind == QJS_POINTER_EVENT) {
#define POINTER_GET(name, key) JS_CGETSET_MAGIC_DEF2(name, pointer_getter, NULL, key, JS_PROP_C_W_E)
        static const JSCFunctionListEntry pointer[] = {POINTER_GET("pointerId", POINTER_ID),
            POINTER_GET("width", WIDTH), POINTER_GET("height", HEIGHT), POINTER_GET("pressure", PRESSURE),
            POINTER_GET("tangentialPressure", TANGENTIAL_PRESSURE), POINTER_GET("tiltX", TILT_X),
            POINTER_GET("tiltY", TILT_Y), POINTER_GET("twist", TWIST), POINTER_GET("pointerType", POINTER_TYPE),
            POINTER_GET("isPrimary", IS_PRIMARY)};
        return JS_SetPropertyFunctionList(ctx, proto, pointer, sizeof(pointer) / sizeof(*pointer));
    }
    return kind == QJS_UI_EVENT ? JS_SetPropertyFunctionList(ctx, proto, ui, sizeof(ui) / sizeof(*ui))
                                : JS_SetPropertyFunctionList(ctx, proto, mouse, sizeof(mouse) / sizeof(*mouse));
}

/* Construct host values without invoking constructors or page-controlled getters. */
JSValue qjs_native_input_event(JSContext *ctx, dom_string *type, const struct js_mouse_event *mouse)
{
    jsthread *thread = JS_GetContextOpaque(ctx);
    bool click = dom_string_byte_length(type) == 5 && memcmp(dom_string_data(type), "click", 5) == 0;
    bool composed = click || (dom_string_byte_length(type) == 5 && memcmp(dom_string_data(type), "input", 5) == 0);
    enum qjs_event_kind kind = click ? QJS_POINTER_EVENT : QJS_EVENT;
    JSValue object = qjs_event_allocate(ctx, JS_NewObjectClass(ctx, thread->heap->event_class[kind]), kind, composed);
    if (JS_IsException(object))
        return object;
    struct qjs_event *event = qjs_event_get(ctx, object);
    dom_event_init(event->native, type, true, click);
    dom_event_set_is_trusted(event->native, true);
    if (click) {
        struct qjs_pointer_event *pointer = (struct qjs_pointer_event *)event;
        pointer->pointer_id = mouse != NULL ? 1 : -1;
        pointer->width = pointer->height = 1;
        pointer->primary = mouse != NULL;
        pointer->pointer_type = JS_NewString(ctx, mouse != NULL ? "mouse" : "");
        pointer->input.data.view = JS_GetGlobalObject(ctx);
        if (JS_IsException(pointer->pointer_type)) {
            JS_FreeValue(ctx, object);
            return JS_EXCEPTION;
        }
        if (mouse != NULL) {
            struct qjs_input_data *data = &pointer->input.data;
            data->client_x = mouse->client_x;
            data->client_y = mouse->client_y;
            data->screen_x = mouse->screen_x;
            data->screen_y = mouse->screen_y;
            data->buttons = mouse->buttons;
            data->detail = mouse->detail;
            data->modifiers = (mouse->ctrl << CTRL) | (mouse->alt << ALT) | (mouse->shift << SHIFT) |
                (mouse->meta << META);
            pointer->pressure = mouse->buttons != 0 ? 0.5 : 0;
        }
    }
    return object;
}

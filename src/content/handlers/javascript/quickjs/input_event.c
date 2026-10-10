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
    if (mouse ? event->kind != QJS_MOUSE_EVENT : !qjs_event_is_ui(event->kind)) {
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
    JSValue result = JS_EXCEPTION;
    JSValueConst dict = argc > 1 ? argv[1] : JS_UNDEFINED;
    if (qjs_event_read_options(ctx, dict, &options) < 0 || input_options(ctx, dict, &data, kind == QJS_MOUSE_EVENT) < 0)
        goto out;
    JSValue object = qjs_event_new_object(ctx, new_target, thread->heap->event_class[kind]);
    if (JS_IsException(object))
        goto out;
    result = qjs_event_allocate(ctx, object, kind, options.composed);
    if (!JS_IsException(result)) {
        struct qjs_input_event *event = (struct qjs_input_event *)qjs_event_get(ctx, result);
        event->data = data;
        data.view = data.related = JS_NULL; /* Ownership transferred. */
        dom_event_init(event->base.native, type, options.bubbles, options.cancelable);
        dom_event_set_is_trusted(event->base.native, false);
    }
out:
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
    return kind == QJS_UI_EVENT ? JS_SetPropertyFunctionList(ctx, proto, ui, sizeof(ui) / sizeof(*ui))
                                : JS_SetPropertyFunctionList(ctx, proto, mouse, sizeof(mouse) / sizeof(*mouse));
}

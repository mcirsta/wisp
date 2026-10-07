/* SPDX-License-Identifier: GPL-2.0-or-later
 * Shared Web IDL property-key and iterator setup for live collections.
 */
#include "dom.h"

/* Web IDL indexed properties use array indices (canonical decimal strings,
 * excluding 2^32-1); symbols and noncanonical strings remain ordinary keys. */
int qjs_dom_property_index(JSContext *ctx, JSAtom key, uint32_t *index)
{
    JSValue value = JS_AtomToValue(ctx, key);
    if (JS_IsException(value))
        return -1;
    if (!JS_IsString(value)) {
        JS_FreeValue(ctx, value);
        return 0;
    }
    size_t length;
    const char *text = JS_ToCStringLen(ctx, &length, value);
    JS_FreeValue(ctx, value);
    if (text == NULL)
        return -1;
    bool valid = length > 0 && length <= 10 && !(length > 1 && text[0] == '0');
    uint64_t number = 0;
    for (size_t i = 0; valid && i < length; i++) {
        if (text[i] < '0' || text[i] > '9')
            valid = false;
        else
            number = number * 10 + text[i] - '0';
    }
    JS_FreeCString(ctx, text);
    *index = (uint32_t)number;
    return valid && number < UINT32_MAX;
}

/* Only called during context initialization, before page code can replace the
 * intrinsics. Web IDL explicitly uses Array's methods for value iterables. */
int qjs_dom_install_iteration(JSContext *ctx, JSValueConst proto, JSValueConst global, const char *name, bool iterable)
{
    JSValue array = JS_GetPropertyStr(ctx, global, "Array");
    JSValue array_proto = JS_IsException(array) ? JS_EXCEPTION : JS_GetPropertyStr(ctx, array, "prototype");
    JS_FreeValue(ctx, array);
    if (JS_IsException(array_proto))
        return -1;
    int ret = -1;
    const char *methods[] = {"entries", "keys", "values", "forEach"};
    for (unsigned i = 0; iterable && i < sizeof(methods) / sizeof(*methods); i++) {
        JSValue method = JS_GetPropertyStr(ctx, array_proto, methods[i]);
        if (JS_IsException(method) || JS_DefinePropertyValueStr(ctx, proto, methods[i], method, JS_PROP_C_W_E) < 0)
            goto out;
    }
    JSValue symbol = JS_GetPropertyStr(ctx, global, "Symbol");
    if (JS_IsException(symbol))
        goto out;
    const char *symbols[] = {"iterator", "toStringTag"};
    for (unsigned i = 0; i < 2; i++) {
        JSValue key = JS_GetPropertyStr(ctx, symbol, symbols[i]);
        JSAtom atom = JS_IsException(key) ? JS_ATOM_NULL : JS_ValueToAtom(ctx, key);
        JS_FreeValue(ctx, key);
        if (atom == JS_ATOM_NULL) {
            JS_FreeValue(ctx, symbol);
            goto out;
        }
        JSValue value = i == 0 ? JS_GetPropertyStr(ctx, array_proto, "values") : JS_NewString(ctx, name);
        int defined = JS_IsException(value)
            ? -1
            : JS_DefinePropertyValue(ctx, proto, atom, value, JS_PROP_CONFIGURABLE | (i == 0 ? JS_PROP_WRITABLE : 0));
        JS_FreeAtom(ctx, atom);
        if (defined < 0) {
            JS_FreeValue(ctx, symbol);
            goto out;
        }
    }
    JS_FreeValue(ctx, symbol);
    ret = 0;
out:
    JS_FreeValue(ctx, array_proto);
    return ret;
}

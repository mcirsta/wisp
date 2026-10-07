/* SPDX-License-Identifier: GPL-2.0-or-later
 * Live DOMTokenList bindings. Duktape's Element/DOMTokenList bindings informed
 * the libdom ownership review; conversions and indexed properties follow Web IDL.
 */
#include "dom.h"
#include <string.h>

struct tokenlist {
    JSValue element;
    /* Keep expandos separate so ownKeys can return live indices before strings
     * and symbols. QuickJS otherwise appends exotic keys after ordinary keys. */
    JSValue properties;
};

static void list_finalizer(JSRuntime *rt, JSValue object)
{
    struct tokenlist *list = JS_GetOpaque(object, JS_GetClassID(object));
    if (list != NULL) {
        JS_FreeValueRT(rt, list->element);
        JS_FreeValueRT(rt, list->properties);
        js_free_rt(rt, list);
    }
}

static void list_mark(JSRuntime *rt, JSValueConst object, JS_MarkFunc *mark)
{
    struct tokenlist *list = JS_GetOpaque(object, JS_GetClassID(object));
    if (list != NULL) {
        JS_MarkValue(rt, list->element, mark);
        JS_MarkValue(rt, list->properties, mark);
    }
}

static struct tokenlist *list_receiver(JSContext *ctx, JSValueConst object)
{
    jsthread *thread = JS_GetContextOpaque(ctx);
    if (thread == NULL) {
        JS_ThrowTypeError(ctx, "DOMTokenList has no document context");
        return NULL;
    }
    return JS_GetOpaque2(ctx, object, thread->heap->tokenlist_class);
}

static struct qjs_dom_node *list_element(JSContext *ctx, JSValueConst object)
{
    struct tokenlist *list = list_receiver(ctx, object);
    return list == NULL ? NULL : qjs_dom_get(ctx, list->element, DOM_ELEMENT_NODE);
}

JSValue qjs_class_list_get(JSContext *ctx, JSValueConst object)
{
    struct qjs_dom_node *node = qjs_dom_get(ctx, object, DOM_ELEMENT_NODE);
    if (node == NULL)
        return JS_EXCEPTION;
    if (!JS_IsUndefined(node->class_list))
        return JS_DupValue(ctx, node->class_list);
    if (node->class_tokens == NULL) {
        dom_string *name = NULL;
        dom_exception err = dom_string_create((const uint8_t *)"class", 5, &name);
        if (err == DOM_NO_ERR)
            err = dom_tokenlist_create((dom_element *)node->node, name, &node->class_tokens);
        dom_string_unref(name);
        if (err != DOM_NO_ERR)
            return qjs_dom_exception(ctx, err);
    }
    jsthread *thread = JS_GetContextOpaque(ctx);
    JSValue object_list = JS_NewObjectClass(ctx, thread->heap->tokenlist_class);
    if (JS_IsException(object_list))
        return object_list;
    struct tokenlist *list = js_mallocz(ctx, sizeof(*list));
    if (list == NULL) {
        JS_FreeValue(ctx, object_list);
        return JS_EXCEPTION;
    }
    list->element = JS_DupValue(ctx, object);
    list->properties = JS_NewObjectProto(ctx, JS_NULL);
    JS_SetOpaque(object_list, list);
    if (JS_IsException(list->properties)) {
        JS_FreeValue(ctx, object_list);
        return JS_EXCEPTION;
    }
    node->class_list = JS_DupValue(ctx, object_list);
    return object_list;
}

JSValue qjs_class_list_set(JSContext *ctx, JSValueConst object, JSValueConst value)
{
    if (qjs_dom_get(ctx, object, DOM_ELEMENT_NODE) == NULL)
        return JS_EXCEPTION;
    /* PutForwards=value uses ordinary property access, including a page's
     * replacement getter/setter, rather than bypassing it via a native write. */
    JSValue list = JS_GetPropertyStr(ctx, object, "classList");
    if (JS_IsException(list))
        return list;
    if (!JS_IsObject(list)) {
        JS_FreeValue(ctx, list);
        return JS_ThrowTypeError(ctx, "classList forwarding target must be an object");
    }
    JSValue key = JS_NewString(ctx, "value");
    int ret = JS_IsException(key) ? -1 : JS_SetPropertyValue(ctx, list, key, JS_DupValue(ctx, value), 0);
    JS_FreeValue(ctx, list);
    return ret < 0 ? JS_EXCEPTION : JS_UNDEFINED;
}

static JSValue list_length(JSContext *ctx, JSValueConst object)
{
    struct qjs_dom_node *node = list_element(ctx, object);
    if (node == NULL)
        return JS_EXCEPTION;
    uint32_t length;
    dom_exception err = dom_tokenlist_get_length(node->class_tokens, &length);
    return err == DOM_NO_ERR ? JS_NewUint32(ctx, length) : qjs_dom_exception(ctx, err);
}

static JSValue list_value(JSContext *ctx, JSValueConst object)
{
    struct qjs_dom_node *node = list_element(ctx, object);
    if (node == NULL)
        return JS_EXCEPTION;
    dom_string *value = NULL;
    dom_exception err = dom_tokenlist_get_value(node->class_tokens, &value);
    JSValue result = err == DOM_NO_ERR ? qjs_dom_from_string(ctx, value, false) : qjs_dom_exception(ctx, err);
    dom_string_unref(value);
    return result;
}

static JSValue list_set_value(JSContext *ctx, JSValueConst object, JSValueConst value)
{
    if (list_element(ctx, object) == NULL)
        return JS_EXCEPTION;
    dom_string *text = qjs_dom_string(ctx, value, false);
    if (text == NULL)
        return JS_EXCEPTION;
    struct qjs_dom_node *node = list_element(ctx, object);
    if (node == NULL) {
        dom_string_unref(text);
        return JS_EXCEPTION;
    }
    dom_exception err = dom_tokenlist_set_value(node->class_tokens, text);
    dom_string_unref(text);
    qjs_dom_changed(ctx, node, "classList.value", NULL, err);
    return err == DOM_NO_ERR ? JS_UNDEFINED : qjs_dom_exception(ctx, err);
}

static JSValue list_string(JSContext *ctx, JSValueConst object, int argc, JSValueConst *argv)
{
    return list_value(ctx, object);
}

static JSValue list_item(JSContext *ctx, JSValueConst object, int argc, JSValueConst *argv)
{
    if (list_element(ctx, object) == NULL)
        return JS_EXCEPTION;
    if (argc == 0)
        return JS_ThrowTypeError(ctx, "item requires an index");
    uint32_t index;
    if (JS_ToUint32(ctx, &index, argv[0]) < 0)
        return JS_EXCEPTION;
    struct qjs_dom_node *node = list_element(ctx, object);
    if (node == NULL)
        return JS_EXCEPTION;
    dom_string *value = NULL;
    dom_exception err = dom_tokenlist_item(node->class_tokens, index, &value);
    JSValue result = err == DOM_NO_ERR ? qjs_dom_from_string(ctx, value, true) : qjs_dom_exception(ctx, err);
    dom_string_unref(value);
    return result;
}

enum list_method { CONTAINS, ADD, REMOVE, TOGGLE, REPLACE, SUPPORTS };

static JSValue list_method(JSContext *ctx, JSValueConst object, int argc, JSValueConst *argv, int method)
{
    if (list_element(ctx, object) == NULL)
        return JS_EXCEPTION;
    bool variadic = method == ADD || method == REMOVE;
    int count = variadic ? argc : method == REPLACE ? 2 : 1;
    if (argc < count)
        return JS_ThrowTypeError(ctx, "DOMTokenList method is missing arguments");
    dom_string *local[2] = {NULL, NULL};
    dom_string **tokens = count <= 2 ? local : js_mallocz(ctx, (size_t)count * sizeof(*tokens));
    if (tokens == NULL)
        return JS_EXCEPTION;
    JSValue result = JS_EXCEPTION;
    /* Web IDL converts every argument before the DOM algorithm validates any
     * token. Coercion may edit the attribute, throw, or retire the document. */
    for (int i = 0; i < count; i++) {
        tokens[i] = qjs_dom_string(ctx, argv[i], false);
        if (tokens[i] == NULL)
            goto out;
    }
    int force = -1;
    if (method == TOGGLE && argc > 1 && !JS_IsUndefined(argv[1])) {
        force = JS_ToBool(ctx, argv[1]);
        if (force < 0)
            goto out;
    }
    struct qjs_dom_node *node = list_element(ctx, object);
    if (node == NULL)
        goto out;
    bool value = false, updated = false;
    dom_exception err = DOM_NO_ERR;
    static const char *const operations[] = {"classList.contains", "classList.add", "classList.remove",
        "classList.toggle", "classList.replace", "classList.supports"};
    switch (method) {
    case CONTAINS:
        err = dom_tokenlist_contains(node->class_tokens, tokens[0], &value);
        break;
    case ADD:
        err = dom_tokenlist_add_tokens(node->class_tokens, tokens, count, &updated);
        break;
    case REMOVE:
        err = dom_tokenlist_remove_tokens(node->class_tokens, tokens, count, &updated);
        break;
    case TOGGLE:
        err = dom_tokenlist_toggle(node->class_tokens, tokens[0], force, &value, &updated);
        break;
    case REPLACE:
        err = dom_tokenlist_replace(node->class_tokens, tokens[0], tokens[1], &value, &updated);
        break;
    case SUPPORTS:
        /* class has no specification-defined supported token vocabulary. */
        JS_ThrowTypeError(ctx, "classList has no supported tokens");
        goto out;
    }
    if (updated)
        qjs_dom_changed(ctx, node, operations[method], NULL, err);
    result = err != DOM_NO_ERR ? qjs_dom_exception(ctx, err) : variadic ? JS_UNDEFINED : JS_NewBool(ctx, value);
out:
    for (int i = 0; i < count; i++)
        dom_string_unref(tokens[i]);
    if (tokens != local)
        js_free(ctx, tokens);
    return result;
}

/* Web IDL indexed properties use array indices (canonical decimal strings,
 * excluding 2^32-1); symbols and noncanonical strings remain ordinary keys. */
static int property_index(JSContext *ctx, JSAtom key, uint32_t *index)
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

static int list_get_own(JSContext *ctx, JSPropertyDescriptor *desc, JSValueConst object, JSAtom key)
{
    struct tokenlist *list = list_receiver(ctx, object);
    if (list == NULL)
        return -1;
    uint32_t index;
    int indexed = property_index(ctx, key, &index);
    if (indexed < 0)
        return -1;
    if (!indexed)
        return JS_GetOwnProperty(ctx, desc, list->properties, key);
    struct qjs_dom_node *node = list_element(ctx, object);
    if (node == NULL)
        return -1;
    dom_string *token = NULL;
    dom_exception err = dom_tokenlist_item(node->class_tokens, index, &token);
    if (err != DOM_NO_ERR) {
        qjs_dom_exception(ctx, err);
        return -1;
    }
    if (token == NULL)
        return 0;
    if (desc != NULL) {
        desc->flags = JS_PROP_ENUMERABLE | JS_PROP_CONFIGURABLE;
        desc->getter = desc->setter = JS_UNDEFINED;
        desc->value = qjs_dom_from_string(ctx, token, false);
    }
    dom_string_unref(token);
    return desc != NULL && JS_IsException(desc->value) ? -1 : 1;
}

static int list_keys(JSContext *ctx, JSPropertyEnum **keys, uint32_t *count, JSValueConst object)
{
    *keys = NULL;
    *count = 0;
    struct qjs_dom_node *node = list_element(ctx, object);
    if (node == NULL)
        return -1;
    uint32_t length;
    dom_exception err = dom_tokenlist_get_length(node->class_tokens, &length);
    if (err != DOM_NO_ERR) {
        qjs_dom_exception(ctx, err);
        return -1;
    }
    struct tokenlist *list = list_receiver(ctx, object);
    JSPropertyEnum *extra = NULL;
    uint32_t extras = 0;
    if (JS_GetOwnPropertyNames(ctx, &extra, &extras, list->properties, JS_GPN_STRING_MASK | JS_GPN_SYMBOL_MASK) < 0)
        return -1;
    if (length > UINT32_MAX - extras || (size_t)length + extras >= SIZE_MAX / sizeof(**keys)) {
        JS_FreePropertyEnum(ctx, extra, extras);
        JS_ThrowOutOfMemory(ctx);
        return -1;
    }
    JSPropertyEnum *result = js_mallocz(ctx, ((size_t)length + extras + 1) * sizeof(*result));
    if (result == NULL) {
        JS_FreePropertyEnum(ctx, extra, extras);
        return -1;
    }
    for (uint32_t i = 0; i < length; i++) {
        result[i].atom = JS_NewAtomUInt32(ctx, i);
        if (result[i].atom == JS_ATOM_NULL) {
            JS_FreePropertyEnum(ctx, result, i);
            JS_FreePropertyEnum(ctx, extra, extras);
            return -1;
        }
    }
    if (extras != 0)
        memcpy(result + length, extra, extras * sizeof(*extra));
    js_free(ctx, extra); /* Atom references were transferred to result. */
    *keys = result;
    *count = length + extras;
    return 0;
}

static int list_define(JSContext *ctx, JSValueConst object, JSAtom key, JSValueConst value, JSValueConst getter,
    JSValueConst setter, int flags)
{
    uint32_t index;
    int indexed = property_index(ctx, key, &index);
    if (indexed != 0)
        return indexed < 0 ? -1 : 0; /* No indexed setter, including absent indices. */
    struct tokenlist *list = list_receiver(ctx, object);
    if (list == NULL)
        return -1;
    return JS_DefineProperty(ctx, list->properties, key, value, getter, setter, flags);
}

static int list_delete(JSContext *ctx, JSValueConst object, JSAtom key)
{
    uint32_t index;
    int indexed = property_index(ctx, key, &index);
    if (indexed < 0)
        return -1;
    if (indexed) {
        int present = list_get_own(ctx, NULL, object, key);
        return present < 0 ? -1 : !present;
    }
    struct tokenlist *list = list_receiver(ctx, object);
    return list == NULL ? -1 : JS_DeleteProperty(ctx, list->properties, key, 0);
}

static int list_prevent_extensions(JSContext *ctx, JSValueConst object)
{
    /* Web IDL legacy platform objects must remain extensible: their supported
     * indices can change when the DOM attribute changes. */
    return 0;
}

static JSClassExoticMethods list_exotic = {.get_own_property = list_get_own,
    .get_own_property_names = list_keys,
    .define_own_property = list_define,
    .delete_property = list_delete,
    .prevent_extensions = list_prevent_extensions};

static JSValue list_constructor(JSContext *ctx, JSValueConst target, int argc, JSValueConst *argv)
{
    return JS_ThrowTypeError(ctx, "Illegal DOMTokenList constructor");
}

/* Only called during context initialization, before page code can replace the
 * intrinsics. Web IDL explicitly uses Array's methods for value iterables. */
static int install_iteration(JSContext *ctx, JSValueConst proto, JSValueConst global)
{
    JSValue array = JS_GetPropertyStr(ctx, global, "Array");
    JSValue array_proto = JS_IsException(array) ? JS_EXCEPTION : JS_GetPropertyStr(ctx, array, "prototype");
    JS_FreeValue(ctx, array);
    if (JS_IsException(array_proto))
        return -1;
    int ret = -1;
    const char *methods[] = {"entries", "keys", "values", "forEach"};
    for (unsigned i = 0; i < sizeof(methods) / sizeof(*methods); i++) {
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
        JSValue value = i == 0 ? JS_GetPropertyStr(ctx, array_proto, "values") : JS_NewString(ctx, "DOMTokenList");
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

#define METHOD(name, count, operation)                                                                                 \
    {                                                                                                                  \
        name, JS_PROP_C_W_E, JS_DEF_CFUNC, operation,                                                                  \
        {                                                                                                              \
            .func = {count, JS_CFUNC_generic_magic, {.generic_magic = list_method}}                                    \
        }                                                                                                              \
    }

int qjs_init_tokenlist(JSContext *ctx)
{
    jsthread *thread = JS_GetContextOpaque(ctx);
    JSClassID local = 0;
    JSClassID *id = thread == NULL ? &local : &thread->heap->tokenlist_class;
    if (*id == 0) {
        JS_NewClassID(JS_GetRuntime(ctx), id);
        JSClassDef definition = {
            .class_name = "DOMTokenList", .finalizer = list_finalizer, .gc_mark = list_mark, .exotic = &list_exotic};
        if (JS_NewClass(JS_GetRuntime(ctx), *id, &definition) < 0) {
            *id = 0;
            return -1;
        }
    }
    static const JSCFunctionListEntry methods[] = {
        JS_CGETSET_DEF2("length", list_length, NULL, JS_PROP_CONFIGURABLE | JS_PROP_ENUMERABLE),
        JS_CGETSET_DEF2("value", list_value, list_set_value, JS_PROP_CONFIGURABLE | JS_PROP_ENUMERABLE),
        JS_CFUNC_DEF2("item", 1, list_item, JS_PROP_C_W_E),
        JS_CFUNC_DEF2("toString", 0, list_string, JS_PROP_C_W_E),
        METHOD("contains", 1, CONTAINS),
        METHOD("add", 0, ADD),
        METHOD("remove", 0, REMOVE),
        METHOD("toggle", 1, TOGGLE),
        METHOD("replace", 2, REPLACE),
        METHOD("supports", 1, SUPPORTS),
    };
    JSValue proto = JS_NewObject(ctx);
    JSValue ctor = JS_NewCFunction2(ctx, list_constructor, "DOMTokenList", 0, JS_CFUNC_constructor, 0);
    JSValue global = JS_GetGlobalObject(ctx);
    int ret = -1;
    if (JS_IsException(proto) || JS_IsException(ctor) ||
        JS_SetPropertyFunctionList(ctx, proto, methods, sizeof(methods) / sizeof(*methods)) < 0 ||
        install_iteration(ctx, proto, global) < 0 || JS_SetConstructor(ctx, ctor, proto) < 0)
        goto out;
    ret = JS_SetPropertyStr(ctx, global, "DOMTokenList", JS_DupValue(ctx, ctor));
    if (ret >= 0)
        JS_SetClassProto(ctx, *id, JS_DupValue(ctx, proto));
out:
    JS_FreeValue(ctx, global);
    JS_FreeValue(ctx, ctor);
    JS_FreeValue(ctx, proto);
    return ret < 0 ? -1 : 0;
}

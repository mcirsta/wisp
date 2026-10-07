/* SPDX-License-Identifier: GPL-2.0-or-later
 * Live Node.childNodes and ParentNode.children. Native references are borrowed
 * only for an operation; the traced root wrapper owns the collection's lifetime.
 * DOM 4.2.10 and Web IDL 3.9 define the indexed/named property behavior.
 */
#include "dom.h"
#include <string.h>

enum collection_kind { CHILD_NODES, CHILD_ELEMENTS };

struct collection {
    JSValue root, properties;
    enum collection_kind kind;
};

static struct collection *collection_get(JSContext *ctx, JSValueConst object, int kind)
{
    jsthread *thread = JS_GetContextOpaque(ctx);
    if (thread != NULL) {
        JSClassID id = JS_GetClassID(object);
        for (unsigned i = 0; i < 2; i++) {
            if (id == thread->heap->collection_class[i] && (kind < 0 || (unsigned)kind == i)) {
                struct collection *list = JS_GetOpaque(object, id);
                if (list != NULL)
                    return list;
            }
        }
    }
    JS_ThrowTypeError(ctx, "Invalid collection receiver");
    return NULL;
}

static void collection_finalizer(JSRuntime *rt, JSValue object)
{
    struct collection *list = JS_GetOpaque(object, JS_GetClassID(object));
    if (list != NULL) {
        JS_FreeValueRT(rt, list->root);
        JS_FreeValueRT(rt, list->properties);
        js_free_rt(rt, list);
    }
}

static void collection_mark(JSRuntime *rt, JSValueConst object, JS_MarkFunc *mark)
{
    struct collection *list = JS_GetOpaque(object, JS_GetClassID(object));
    if (list != NULL) {
        JS_MarkValue(rt, list->root, mark);
        JS_MarkValue(rt, list->properties, mark);
    }
}

static JSValue children_get(JSContext *ctx, JSValueConst object, int type)
{
    int kind = type == 0 ? CHILD_NODES : CHILD_ELEMENTS;
    struct qjs_dom_node *root = qjs_dom_get(ctx, object, type);
    if (root == NULL)
        return JS_EXCEPTION;
    if (!JS_IsUndefined(root->child_lists[kind]))
        return JS_DupValue(ctx, root->child_lists[kind]);
    jsthread *thread = JS_GetContextOpaque(ctx);
    JSValue value = JS_NewObjectClass(ctx, thread->heap->collection_class[kind]);
    if (JS_IsException(value))
        return value;
    struct collection *list = js_mallocz(ctx, sizeof(*list));
    if (list == NULL) {
        JS_FreeValue(ctx, value);
        return JS_EXCEPTION;
    }
    list->kind = kind;
    list->root = JS_DupValue(ctx, object);
    list->properties = JS_NewObjectProto(ctx, JS_NULL);
    JS_SetOpaque(value, list);
    if (JS_IsException(list->properties)) {
        JS_FreeValue(ctx, value);
        return JS_EXCEPTION;
    }
    root->child_lists[kind] = JS_DupValue(ctx, value);
    return value;
}

/* Direct siblings only. No snapshots/cursors survive an operation: parser
 * writes, textContent replacement and detached-tree edits are immediately live.
 * Reading collections neither registers DOM listeners nor requests rendering. */
static dom_exception next_child(dom_node *from, bool first, int kind, dom_node **result)
{
    dom_exception err = first ? dom_node_get_first_child(from, result) : dom_node_get_next_sibling(from, result);
    while (err == DOM_NO_ERR && *result != NULL && kind == CHILD_ELEMENTS) {
        dom_node_type type;
        err = dom_node_get_node_type(*result, &type);
        if (err != DOM_NO_ERR || type == DOM_ELEMENT_NODE)
            break;
        dom_node *next = NULL;
        err = dom_node_get_next_sibling(*result, &next);
        dom_node_unref(*result);
        *result = next;
    }
    return err;
}

static dom_exception attribute_name(dom_node *node, bool is_name, dom_string **value)
{
    *value = NULL;
    if (is_name && !qjs_dom_is_html(node))
        return DOM_NO_ERR;
    const char *key = is_name ? "name" : "id";
    dom_string *attribute = NULL;
    dom_exception err = dom_string_create((const uint8_t *)key, strlen(key), &attribute);
    if (err == DOM_NO_ERR)
        err = dom_element_get_attribute(node, attribute, value);
    dom_string_unref(attribute);
    return err;
}

/* A null result requests length; name selects namedItem instead of an index. */
static int collection_scan(
    JSContext *ctx, struct collection *list, uint32_t index, dom_string *name, dom_node **result, uint32_t *length)
{
    if (result != NULL)
        *result = NULL;
    if (length != NULL)
        *length = 0;
    struct qjs_dom_node *root = qjs_dom_get(ctx, list->root, 0);
    if (root == NULL)
        return -1;
    if (name != NULL && dom_string_byte_length(name) == 0)
        return 0;
    dom_node *child = NULL;
    dom_exception err = next_child(root->node, true, list->kind, &child);
    uint32_t count = 0;
    while (err == DOM_NO_ERR && child != NULL) {
        bool matches = result != NULL && name == NULL && count == index;
        if (name != NULL) {
            for (unsigned i = 0; i < 2 && !matches && err == DOM_NO_ERR; i++) {
                dom_string *value = NULL;
                err = attribute_name(child, i != 0, &value);
                matches = value != NULL && dom_string_isequal(value, name);
                dom_string_unref(value);
            }
        }
        if (matches && err == DOM_NO_ERR) {
            *result = child;
            return 0;
        }
        count++;
        dom_node *next = NULL;
        if (err == DOM_NO_ERR)
            err = next_child(child, false, list->kind, &next);
        dom_node_unref(child);
        child = next;
    }
    dom_node_unref(child);
    if (err != DOM_NO_ERR) {
        qjs_dom_exception(ctx, err);
        return -1;
    }
    if (length != NULL)
        *length = count;
    return 0;
}

static JSValue wrap_child(JSContext *ctx, struct collection *list, dom_node *child)
{
    struct qjs_dom_node *root = qjs_dom_get(ctx, list->root, 0);
    JSValue owner = root == NULL ? JS_EXCEPTION : qjs_dom_owner(ctx, list->root, root);
    JSValue result = JS_IsException(owner) ? JS_EXCEPTION : qjs_dom_wrap(ctx, owner, child);
    JS_FreeValue(ctx, owner);
    dom_node_unref(child);
    return result;
}

static JSValue collection_length(JSContext *ctx, JSValueConst object, int kind)
{
    struct collection *list = collection_get(ctx, object, kind);
    uint32_t length;
    if (list == NULL || collection_scan(ctx, list, 0, NULL, NULL, &length) < 0)
        return JS_EXCEPTION;
    return JS_NewUint32(ctx, length);
}

static JSValue collection_item(JSContext *ctx, JSValueConst object, int argc, JSValueConst *argv, int kind)
{
    struct collection *list = collection_get(ctx, object, kind);
    if (list == NULL)
        return JS_EXCEPTION;
    if (argc == 0)
        return JS_ThrowTypeError(ctx, "item requires an index");
    uint32_t index;
    if (JS_ToUint32(ctx, &index, argv[0]) < 0)
        return JS_EXCEPTION;
    dom_node *child;
    if (collection_scan(ctx, list, index, NULL, &child, NULL) < 0)
        return JS_EXCEPTION;
    return wrap_child(ctx, list, child);
}

static JSValue collection_named_item(JSContext *ctx, JSValueConst object, int argc, JSValueConst *argv)
{
    struct collection *list = collection_get(ctx, object, CHILD_ELEMENTS);
    if (list == NULL)
        return JS_EXCEPTION;
    if (argc == 0)
        return JS_ThrowTypeError(ctx, "namedItem requires a name");
    dom_string *name = qjs_dom_string(ctx, argv[0], false);
    if (name == NULL)
        return JS_EXCEPTION;
    dom_node *child;
    int ret = collection_scan(ctx, list, 0, name, &child, NULL);
    dom_string_unref(name);
    return ret < 0 ? JS_EXCEPTION : wrap_child(ctx, list, child);
}

static int named_node(JSContext *ctx, struct collection *list, JSAtom key, dom_node **child)
{
    *child = NULL;
    if (list->kind != CHILD_ELEMENTS)
        return 0;
    JSValue value = JS_AtomToValue(ctx, key);
    if (JS_IsException(value))
        return -1;
    if (!JS_IsString(value)) {
        JS_FreeValue(ctx, value);
        return 0;
    }
    dom_string *name = qjs_dom_string(ctx, value, false);
    JS_FreeValue(ctx, value);
    if (name == NULL)
        return -1;
    int ret = collection_scan(ctx, list, 0, name, child, NULL);
    dom_string_unref(name);
    return ret < 0 ? -1 : *child != NULL;
}

/* HTMLCollection has no LegacyOverrideBuiltIns: ordinary own properties and
 * any prototype's own properties hide its supported names. Proxy prototypes
 * may run script here, so no native reference is retained across this walk. */
static int named_visible(JSContext *ctx, JSValueConst object, struct collection *list, JSAtom key)
{
    if (list->kind != CHILD_ELEMENTS)
        return 0;
    /* These two objects are ordinary objects created by this binding; reading
     * their own descriptors cannot run page code. Avoid scanning every child's
     * attributes just to resolve the usual length/item/iterator members. Custom
     * prototype chains still use the specification's ordered checks below. */
    int own = JS_GetOwnProperty(ctx, NULL, list->properties, key);
    if (own != 0)
        return own < 0 ? -1 : 0;
    JSValue proto = JS_GetPrototype(ctx, object);
    JSValue intrinsic = JS_GetClassProto(ctx, JS_GetClassID(object));
    int builtin = JS_IsStrictEqual(ctx, proto, intrinsic) ? JS_GetOwnProperty(ctx, NULL, intrinsic, key) : 0;
    JS_FreeValue(ctx, intrinsic);
    JS_FreeValue(ctx, proto);
    if (builtin != 0)
        return builtin < 0 ? -1 : 0;
    dom_node *child;
    int ret = named_node(ctx, list, key, &child);
    dom_node_unref(child);
    if (ret <= 0)
        return ret;
    ret = JS_GetOwnProperty(ctx, NULL, list->properties, key);
    if (ret != 0)
        return ret < 0 ? -1 : 0;
    proto = JS_GetPrototype(ctx, object);
    while (!JS_IsNull(proto) && !JS_IsException(proto)) {
        ret = JS_GetOwnProperty(ctx, NULL, proto, key);
        if (ret != 0) {
            JS_FreeValue(ctx, proto);
            return ret < 0 ? -1 : 0;
        }
        JSValue next = JS_GetPrototype(ctx, proto);
        JS_FreeValue(ctx, proto);
        proto = next;
    }
    ret = JS_IsException(proto) ? -1 : 1;
    JS_FreeValue(ctx, proto);
    return ret;
}

static int
collection_descriptor(JSContext *ctx, JSPropertyDescriptor *desc, JSValueConst object, JSAtom key, bool ignore_names)
{
    struct collection *list = collection_get(ctx, object, -1);
    if (list == NULL)
        return -1;
    uint32_t index;
    int indexed = qjs_dom_property_index(ctx, key, &index);
    if (indexed < 0)
        return -1;
    dom_node *child = NULL;
    int present;
    if (indexed) {
        if (collection_scan(ctx, list, index, NULL, &child, NULL) < 0)
            return -1;
        present = child != NULL;
    } else {
        present = ignore_names ? 0 : named_visible(ctx, object, list, key);
        if (present < 0)
            return -1;
        if (!present)
            return JS_GetOwnProperty(ctx, desc, list->properties, key);
        /* Re-read after potentially reentrant prototype inspection. */
        if (named_node(ctx, list, key, &child) < 0)
            return -1;
    }
    if (!present)
        return 0;
    if (desc == NULL) {
        dom_node_unref(child);
        return 1;
    }
    desc->flags = JS_PROP_CONFIGURABLE | (indexed ? JS_PROP_ENUMERABLE : 0);
    desc->getter = desc->setter = JS_UNDEFINED;
    desc->value = wrap_child(ctx, list, child);
    return JS_IsException(desc->value) ? -1 : 1;
}

static int collection_get_own(JSContext *ctx, JSPropertyDescriptor *desc, JSValueConst object, JSAtom key)
{
    return collection_descriptor(ctx, desc, object, key, false);
}

static int collection_get_for_set(JSContext *ctx, JSPropertyDescriptor *desc, JSValueConst object, JSAtom key)
{
    return collection_descriptor(ctx, desc, object, key, true);
}

static int collection_define(JSContext *ctx, JSValueConst object, JSAtom key, JSValueConst value, JSValueConst getter,
    JSValueConst setter, int flags)
{
    struct collection *list = collection_get(ctx, object, -1);
    if (list == NULL)
        return -1;
    uint32_t index;
    int ret = qjs_dom_property_index(ctx, key, &index);
    if (ret != 0)
        return ret < 0 ? -1 : 0;
    ret = JS_GetOwnProperty(ctx, NULL, list->properties, key);
    if (ret < 0)
        return -1;
    if (!ret) {
        dom_node *child;
        ret = named_node(ctx, list, key, &child);
        dom_node_unref(child);
        if (ret != 0)
            return ret < 0 ? -1 : 0;
    }
    return JS_DefineProperty(ctx, list->properties, key, value, getter, setter, flags);
}

static int collection_delete(JSContext *ctx, JSValueConst object, JSAtom key)
{
    struct collection *list = collection_get(ctx, object, -1);
    if (list == NULL)
        return -1;
    uint32_t index;
    int indexed = qjs_dom_property_index(ctx, key, &index);
    if (indexed < 0)
        return -1;
    int present = indexed ? collection_get_own(ctx, NULL, object, key) : named_visible(ctx, object, list, key);
    if (present != 0)
        return present < 0 ? -1 : 0;
    return JS_DeleteProperty(ctx, list->properties, key, 0);
}

struct collection_keys {
    JSPropertyEnum *items;
    uint32_t count, capacity;
    JSValue seen;
};

static int add_key(JSContext *ctx, struct collection_keys *keys, JSAtom key)
{
    int present = JS_HasProperty(ctx, keys->seen, key);
    if (present != 0)
        return present < 0 ? -1 : 0;
    if (keys->count == keys->capacity) {
        size_t capacity = (size_t)keys->capacity * 2 + 16;
        if (capacity > UINT32_MAX || capacity > SIZE_MAX / sizeof(*keys->items)) {
            JS_ThrowOutOfMemory(ctx);
            return -1;
        }
        JSPropertyEnum *items = js_realloc(ctx, keys->items, capacity * sizeof(*items));
        if (items == NULL)
            return -1;
        keys->items = items;
        keys->capacity = capacity;
    }
    if (JS_DefinePropertyValue(ctx, keys->seen, key, JS_TRUE, JS_PROP_C_W_E) < 0)
        return -1;
    keys->items[keys->count++] = (JSPropertyEnum){.atom = JS_DupAtom(ctx, key)};
    return 0;
}

static int collect_names(JSContext *ctx, struct collection *list, struct collection_keys *keys)
{
    struct qjs_dom_node *root = qjs_dom_get(ctx, list->root, 0);
    if (root == NULL)
        return -1;
    dom_node *child = NULL;
    dom_exception err = next_child(root->node, true, list->kind, &child);
    int ret = 0;
    while (err == DOM_NO_ERR && ret == 0 && child != NULL) {
        for (unsigned i = 0; i < 2 && ret == 0 && err == DOM_NO_ERR; i++) {
            dom_string *name = NULL;
            err = attribute_name(child, i != 0, &name);
            if (err == DOM_NO_ERR && name != NULL && dom_string_byte_length(name) != 0) {
                JSAtom key = JS_NewAtomLen(ctx, dom_string_data(name), dom_string_byte_length(name));
                ret = key == JS_ATOM_NULL ? -1 : add_key(ctx, keys, key);
                JS_FreeAtom(ctx, key);
            }
            dom_string_unref(name);
        }
        dom_node *next = NULL;
        if (err == DOM_NO_ERR && ret == 0)
            err = next_child(child, false, list->kind, &next);
        dom_node_unref(child);
        child = next;
    }
    dom_node_unref(child);
    if (err != DOM_NO_ERR) {
        qjs_dom_exception(ctx, err);
        return -1;
    }
    return ret;
}

static int collection_own_keys(JSContext *ctx, JSPropertyEnum **result, uint32_t *count, JSValueConst object)
{
    *result = NULL;
    *count = 0;
    struct collection *list = collection_get(ctx, object, -1);
    uint32_t length;
    if (list == NULL || collection_scan(ctx, list, 0, NULL, NULL, &length) < 0)
        return -1;
    struct collection_keys keys = {.seen = JS_NewObjectProto(ctx, JS_NULL)};
    int ret = -1;
    if (JS_IsException(keys.seen))
        goto out;
    for (uint32_t i = 0; i < length; i++) {
        JSAtom key = JS_NewAtomUInt32(ctx, i);
        int added = key == JS_ATOM_NULL ? -1 : add_key(ctx, &keys, key);
        JS_FreeAtom(ctx, key);
        if (added < 0)
            goto out;
    }
    if (list->kind == CHILD_ELEMENTS) {
        if (collect_names(ctx, list, &keys) < 0)
            goto out;
        /* Inspect prototypes only after releasing all traversal references. */
        for (uint32_t i = length; i < keys.count;) {
            int visible = named_visible(ctx, object, list, keys.items[i].atom);
            if (visible < 0)
                goto out;
            if (visible) {
                i++;
            } else {
                JS_DeleteProperty(ctx, keys.seen, keys.items[i].atom, 0);
                JS_FreeAtom(ctx, keys.items[i].atom);
                memmove(keys.items + i, keys.items + i + 1, (keys.count - i - 1) * sizeof(*keys.items));
                keys.count--;
            }
        }
    }
    JSPropertyEnum *extra = NULL;
    uint32_t extras = 0;
    if (JS_GetOwnPropertyNames(ctx, &extra, &extras, list->properties, JS_GPN_STRING_MASK | JS_GPN_SYMBOL_MASK) < 0)
        goto out;
    ret = 0;
    for (uint32_t i = 0; i < extras && ret == 0; i++)
        ret = add_key(ctx, &keys, extra[i].atom);
    JS_FreePropertyEnum(ctx, extra, extras);
out:
    JS_FreeValue(ctx, keys.seen);
    if (ret < 0) {
        JS_FreePropertyEnum(ctx, keys.items, keys.count);
    } else {
        *result = keys.items;
        *count = keys.count;
    }
    return ret;
}

static int collection_prevent_extensions(JSContext *ctx, JSValueConst object)
{
    return 0; /* Web IDL live platform objects must remain extensible. */
}

static JSClassExoticMethods collection_exotic = {.get_own_property = collection_get_own,
    .get_own_property_names = collection_own_keys,
    .define_own_property = collection_define,
    .delete_property = collection_delete,
    .prevent_extensions = collection_prevent_extensions,
    .get_own_property_for_set = collection_get_for_set};

static JSValue collection_constructor(JSContext *ctx, JSValueConst target, int argc, JSValueConst *argv)
{
    return JS_ThrowTypeError(ctx, "Illegal collection constructor");
}

int qjs_dom_install_collections(JSContext *ctx, JSValue *prototypes)
{
    jsthread *thread = JS_GetContextOpaque(ctx);
    JSClassID local[2] = {0, 0};
    JSClassID *ids = thread == NULL ? local : thread->heap->collection_class;
    JSValue global = JS_GetGlobalObject(ctx);
    int ret = -1;
    for (unsigned kind = 0; kind < 2; kind++) {
        const char *name = kind == CHILD_NODES ? "NodeList" : "HTMLCollection";
        if (ids[kind] == 0) {
            JS_NewClassID(JS_GetRuntime(ctx), &ids[kind]);
            JSClassDef definition = {.class_name = name,
                .finalizer = collection_finalizer,
                .gc_mark = collection_mark,
                .exotic = &collection_exotic};
            if (JS_NewClass(JS_GetRuntime(ctx), ids[kind], &definition) < 0) {
                ids[kind] = 0;
                goto out;
            }
        }
        const JSCFunctionListEntry methods[] = {
            JS_CGETSET_MAGIC_DEF2("length", collection_length, NULL, kind, JS_PROP_CONFIGURABLE | JS_PROP_ENUMERABLE),
            {"item", JS_PROP_C_W_E, JS_DEF_CFUNC, kind,
                {.func = {1, JS_CFUNC_generic_magic, {.generic_magic = collection_item}}}},
            JS_CFUNC_DEF2("namedItem", 1, collection_named_item, JS_PROP_C_W_E),
        };
        JSValue proto = JS_NewObject(ctx);
        JSValue ctor = JS_NewCFunction2(ctx, collection_constructor, name, 0, JS_CFUNC_constructor, 0);
        int installed = JS_IsException(proto) || JS_IsException(ctor) ||
            qjs_define_properties(ctx, proto, methods, kind == CHILD_NODES ? 2 : 3) < 0 ||
            qjs_dom_install_iteration(ctx, proto, global, name, kind == CHILD_NODES) < 0 ||
            JS_SetConstructor(ctx, ctor, proto) < 0 || JS_SetPropertyStr(ctx, global, name, JS_DupValue(ctx, ctor)) < 0;
        JS_FreeValue(ctx, ctor);
        if (installed) {
            JS_FreeValue(ctx, proto);
            goto out;
        }
        JS_SetClassProto(ctx, ids[kind], proto);
    }
    static const JSCFunctionListEntry nodes[] = {
        JS_CGETSET_MAGIC_DEF2("childNodes", children_get, NULL, CHILD_NODES, JS_PROP_CONFIGURABLE | JS_PROP_ENUMERABLE),
    };
    if (JS_SetPropertyFunctionList(ctx, prototypes[QJS_NODE], nodes, 1) < 0)
        goto out;
    static const struct {
        int prototype, type;
    } parents[] = {
        {QJS_ELEMENT, DOM_ELEMENT_NODE},
        {QJS_DOCUMENT, DOM_DOCUMENT_NODE},
        {QJS_DOCUMENT_FRAGMENT, DOM_DOCUMENT_FRAGMENT_NODE},
    };
    for (unsigned i = 0; i < sizeof(parents) / sizeof(*parents); i++) {
        const JSCFunctionListEntry entry[] = {
            JS_CGETSET_MAGIC_DEF2(
                "children", children_get, NULL, parents[i].type, JS_PROP_CONFIGURABLE | JS_PROP_ENUMERABLE),
        };
        if (qjs_define_properties(ctx, prototypes[parents[i].prototype], entry, 1) < 0)
            goto out;
    }
    ret = 0;
out:
    JS_FreeValue(ctx, global);
    return ret;
}

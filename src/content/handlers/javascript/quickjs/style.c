/* SPDX-License-Identifier: GPL-2.0-or-later
 * CSSOM inline declarations. The old Duktape style getter was a placeholder;
 * this wrapper uses DOM-owned native declarations, with Web IDL conversions.
 */
#include "dom.h"
#include "content/handlers/css/inline.h"
#include "content/handlers/html/css.h"
#include <wisp/content/handlers/html/private.h>
#include <wisp/utils/corestrings.h>
#include <wisp/utils/nsurl.h>
#include <string.h>

struct style_object {
    JSValue element, properties;
    bool computed;
    int pseudo; /* CSS_PSEUDO_ELEMENT_COUNT denotes an invalid pseudo argument. */
};

static void style_finalizer(JSRuntime *rt, JSValue object)
{
    struct style_object *style = JS_GetOpaque(object, JS_GetClassID(object));
    if (style != NULL) {
        JS_FreeValueRT(rt, style->element);
        JS_FreeValueRT(rt, style->properties);
        js_free_rt(rt, style);
    }
}

static void style_mark(JSRuntime *rt, JSValueConst object, JS_MarkFunc *mark)
{
    struct style_object *style = JS_GetOpaque(object, JS_GetClassID(object));
    if (style != NULL) {
        JS_MarkValue(rt, style->element, mark);
        JS_MarkValue(rt, style->properties, mark);
    }
}

static struct style_object *style_receiver(JSContext *ctx, JSValueConst object)
{
    jsthread *thread = JS_GetContextOpaque(ctx);
    if (thread == NULL) {
        JS_ThrowTypeError(ctx, "CSSStyleProperties has no document context");
        return NULL;
    }
    return JS_GetOpaque2(ctx, object, thread->heap->style_class);
}

static struct qjs_dom_node *style_element(JSContext *ctx, JSValueConst object)
{
    struct style_object *style = style_receiver(ctx, object);
    return style == NULL ? NULL : qjs_dom_get(ctx, style->element, DOM_ELEMENT_NODE);
}

static struct nscss_inline_style *style_native(JSContext *ctx, struct qjs_dom_node *node, css_stylesheet_params *params)
{
    jsthread *thread = JS_GetContextOpaque(ctx);
    html_content *html = thread->doc_priv;
    dom_document *owner = NULL;
    dom_string *uri = NULL;
    dom_document_quirks_mode quirks = html->quirks;
    dom_exception error = dom_node_get_owner_document(node->node, &owner);
    const char *url = html->base_url != NULL ? nsurl_access(html->base_url) : "about:blank";
    if (error == DOM_NO_ERR && owner != html->document) {
        error = dom_document_get_quirks_mode(owner, &quirks);
        if (error == DOM_NO_ERR)
            error = dom_document_get_uri(owner, &uri);
        url = uri != NULL ? dom_string_data(uri) : "about:blank";
    }
    nscss_inline_params(params, url, quirks != DOM_DOCUMENT_QUIRKS_MODE_NONE);
    /* params borrows the URL for synchronous CSS calls below. The node keeps
     * its native owner (and that owner's URI) alive; no JS runs during parsing. */
    dom_string_unref(uri);
    dom_node_unref(owner);
    struct nscss_inline_style *style;
    if (error == DOM_NO_ERR)
        error = nscss_inline_style_get(node->node, params, &style);
    if (error != DOM_NO_ERR) {
        qjs_dom_exception(ctx, error);
        return NULL;
    }
    return style;
}

static JSValue style_css_error(JSContext *ctx, css_error error)
{
    return error == CSS_NOMEM ? JS_ThrowOutOfMemory(ctx) : JS_ThrowInternalError(ctx, "CSS declarations: %d", error);
}

static JSValue style_string(JSContext *ctx, css_error error, lwc_string *text)
{
    if (error != CSS_OK)
        return style_css_error(ctx, error);
    JSValue value = JS_NewStringLen(ctx, lwc_string_data(text), lwc_string_length(text));
    lwc_string_unref(text);
    return value;
}

static struct qjs_dom_node *element_receiver(JSContext *ctx, JSValueConst object, int kind)
{
    struct qjs_dom_node *node = qjs_dom_get(ctx, object, DOM_ELEMENT_NODE);
    if (node != NULL && !(kind == QJS_HTML_ELEMENT ? qjs_dom_is_html(node->node) : qjs_dom_is_svg(node->node))) {
        JS_ThrowTypeError(ctx, "Incorrect inline style receiver");
        return NULL;
    }
    return node;
}

static JSValue style_create(JSContext *ctx, JSValueConst object, bool computed, int pseudo)
{
    jsthread *thread = JS_GetContextOpaque(ctx);
    JSValue result = JS_NewObjectClass(ctx, thread->heap->style_class);
    if (JS_IsException(result))
        return result;
    struct style_object *style = js_mallocz(ctx, sizeof(*style));
    if (style == NULL) {
        JS_FreeValue(ctx, result);
        return JS_EXCEPTION;
    }
    style->element = JS_DupValue(ctx, object);
    style->properties = JS_NewObjectProto(ctx, JS_NULL);
    style->computed = computed;
    style->pseudo = pseudo;
    JS_SetOpaque(result, style);
    if (JS_IsException(style->properties)) {
        JS_FreeValue(ctx, result);
        return JS_EXCEPTION;
    }
    return result;
}

static JSValue element_style(JSContext *ctx, JSValueConst object, int kind)
{
    struct qjs_dom_node *node = element_receiver(ctx, object, kind);
    if (node == NULL)
        return JS_EXCEPTION;
    if (!JS_IsUndefined(node->style))
        return JS_DupValue(ctx, node->style);
    css_stylesheet_params params;
    if (style_native(ctx, node, &params) == NULL)
        return JS_EXCEPTION;
    JSValue result = style_create(ctx, object, false, CSS_PSEUDO_ELEMENT_NONE);
    if (!JS_IsException(result))
        node->style = JS_DupValue(ctx, result);
    return result;
}

static JSValue window_computed_style(JSContext *ctx, JSValueConst object, int argc, JSValueConst *argv)
{
    JSValue global = JS_GetGlobalObject(ctx);
    bool window = JS_IsUndefined(object) || JS_IsNull(object) || JS_IsStrictEqual(ctx, object, global);
    JS_FreeValue(ctx, global);
    if (!window)
        return JS_ThrowTypeError(ctx, "Window receiver required");
    if (argc == 0)
        return JS_ThrowTypeError(ctx, "getComputedStyle requires an Element");
    if (qjs_dom_get(ctx, argv[0], DOM_ELEMENT_NODE) == NULL)
        return JS_EXCEPTION;
    css_pseudo_element pseudo = CSS_PSEUDO_ELEMENT_NONE;
    if (argc > 1 && !JS_IsUndefined(argv[1]) && !JS_IsNull(argv[1])) {
        dom_string *text = qjs_dom_string(ctx, argv[1], false);
        if (text == NULL)
            return JS_EXCEPTION;
        css_error error = CSS_OK;
        if (dom_string_byte_length(text) != 0 && dom_string_data(text)[0] == ':')
            error = css_parse_pseudo_element((const uint8_t *)dom_string_data(text), dom_string_byte_length(text), &pseudo);
        dom_string_unref(text);
        if (error == CSS_INVALID)
            pseudo = CSS_PSEUDO_ELEMENT_COUNT;
        else if (error != CSS_OK)
            return style_css_error(ctx, error);
    }
    if (qjs_dom_get(ctx, argv[0], DOM_ELEMENT_NODE) == NULL)
        return JS_EXCEPTION;
    return style_create(ctx, argv[0], true, pseudo);
}

static int computed_connected(JSContext *ctx, const struct style_object *style, const struct qjs_dom_node *node)
{
    jsthread *thread = JS_GetContextOpaque(ctx);
    html_content *html = thread->doc_priv;
    if (style->pseudo == CSS_PSEUDO_ELEMENT_COUNT || html == NULL || !qjs_dom_is_active_document(ctx, node->node))
        return 0;
    bool connected = false;
    dom_exception error = dom_node_contains(html->document, node->node, &connected);
    if (error != DOM_NO_ERR) {
        qjs_dom_exception(ctx, error);
        return -1;
    }
    return connected;
}

static JSValue computed_value(JSContext *ctx, struct style_object *style, struct qjs_dom_node *node,
    const char *name, size_t length)
{
    int connected = computed_connected(ctx, style, node);
    if (connected <= 0)
        return connected < 0 ? JS_EXCEPTION : JS_NewString(ctx, "");
    jsthread *thread = JS_GetContextOpaque(ctx);
    html_content *html = thread->doc_priv;
    qjs_dom_flush(thread);
    nserror error = html_update_layout(html);
    struct html_computed_style native = {0};
    if (error == NSERROR_OK)
        error = html_computed_style_get(html, node->node, style->pseudo, &native);
    if (error != NSERROR_OK)
        return error == NSERROR_NOMEM ? JS_ThrowOutOfMemory(ctx) : JS_ThrowInternalError(ctx, "Style unavailable");
    lwc_string *text = NULL;
    css_error css_error = native.style != NULL ? html_computed_style_value(html, &native, name, length, &text)
                                              : CSS_INVALID;
    html_computed_style_fini(&native);
    /* An unimplemented family is an explicit limitation, never an empty value
     * pretending that an implemented CSS property is absent. */
    if (css_error == CSS_INVALID)
        return qjs_dom_exception(ctx, DOM_NOT_SUPPORTED_ERR);
    return style_string(ctx, css_error, text);
}

static JSValue element_style_set(JSContext *ctx, JSValueConst object, JSValueConst value, int kind)
{
    if (element_receiver(ctx, object, kind) == NULL)
        return JS_EXCEPTION;
    /* PutForwards uses ordinary Get/Set, including author replacements. */
    JSValue style = JS_GetPropertyStr(ctx, object, "style");
    if (JS_IsException(style))
        return style;
    if (!JS_IsObject(style)) {
        JS_FreeValue(ctx, style);
        return JS_ThrowTypeError(ctx, "style forwarding target must be an object");
    }
    JSValue key = JS_NewString(ctx, "cssText");
    int result = JS_IsException(key) ? -1 : JS_SetPropertyValue(ctx, style, key, JS_DupValue(ctx, value), 0);
    JS_FreeValue(ctx, style);
    return result < 0 ? JS_EXCEPTION : JS_UNDEFINED;
}

enum { STYLE_TEXT, STYLE_LENGTH, STYLE_PARENT };
static JSValue style_get(JSContext *ctx, JSValueConst object, int property)
{
    struct qjs_dom_node *node = style_element(ctx, object);
    if (node == NULL)
        return JS_EXCEPTION;
    if (property == STYLE_PARENT)
        return JS_NULL;
    struct style_object *wrapper = style_receiver(ctx, object);
    if (wrapper->computed) {
        if (property == STYLE_TEXT)
            return JS_NewString(ctx, "");
        int connected = computed_connected(ctx, wrapper, node);
        size_t count;
        css_declarations_supported_longhands(&count);
        return connected < 0 ? JS_EXCEPTION : JS_NewUint32(ctx, connected ? count : 0);
    }
    css_stylesheet_params params;
    struct nscss_inline_style *style = style_native(ctx, node, &params);
    if (style == NULL)
        return JS_EXCEPTION;
    const css_declarations *d = nscss_inline_style_declarations(style);
    if (property == STYLE_LENGTH)
        return JS_NewUint32(ctx, css_declarations_count(d));
    lwc_string *text = NULL;
    css_error error = css_declarations_serialize(d, &text);
    return style_string(ctx, error, text);
}

static JSValue style_publish(JSContext *ctx, struct qjs_dom_node *node, struct nscss_inline_style *style,
    css_declarations *candidate, const char *operation)
{
    bool changed;
    dom_exception error = nscss_inline_style_commit(node->node, style, &candidate, &changed);
    css_declarations_destroy(candidate);
    if (changed)
        qjs_dom_changed(ctx, node, operation, corestring_dom_style, error);
    return error == DOM_NO_ERR ? JS_UNDEFINED : qjs_dom_exception(ctx, error);
}

static JSValue style_text_set(JSContext *ctx, JSValueConst object, JSValueConst value, int property)
{
    (void)property;
    if (style_element(ctx, object) == NULL)
        return JS_EXCEPTION;
    dom_string *text = qjs_dom_string(ctx, value, false);
    if (text == NULL)
        return JS_EXCEPTION;
    struct qjs_dom_node *node = style_element(ctx, object);
    if (node != NULL && style_receiver(ctx, object)->computed) {
        dom_string_unref(text);
        return qjs_dom_exception(ctx, DOM_NO_MODIFICATION_ALLOWED_ERR);
    }
    css_stylesheet_params params;
    struct nscss_inline_style *style = node == NULL ? NULL : style_native(ctx, node, &params);
    css_declarations *candidate = NULL;
    css_error error = style == NULL ? CSS_INVALID
                                    : css_declarations_parse(&params, (const uint8_t *)dom_string_data(text),
                                          dom_string_byte_length(text), &candidate);
    dom_string_unref(text);
    if (style == NULL)
        return JS_EXCEPTION;
    if (error != CSS_OK)
        return style_css_error(ctx, error);
    return style_publish(ctx, node, style, candidate, "style.cssText");
}

enum { STYLE_VALUE, STYLE_PRIORITY, STYLE_SET, STYLE_REMOVE };
static JSValue style_method(JSContext *ctx, JSValueConst object, int argc, JSValueConst *argv, int method)
{
    if (style_element(ctx, object) == NULL)
        return JS_EXCEPTION;
    if (argc < (method == STYLE_SET ? 2 : 1))
        return JS_ThrowTypeError(ctx, "CSSStyleDeclaration method is missing arguments");
    dom_string *args[3] = {NULL, NULL, NULL};
    JSValue result = JS_EXCEPTION;
    unsigned count = method == STYLE_SET ? 3 : 1;
    for (unsigned i = 0; i < count; i++) {
        JSValueConst argument = i < (unsigned)argc ? argv[i] : JS_UNDEFINED;
        /* LegacyNullToEmptyString affects null, not undefined. Optional priority
         * uses its IDL default for either a missing argument or undefined. */
        if ((i != 0 && JS_IsNull(argument)) || (i == 2 && JS_IsUndefined(argument))) {
            if (dom_string_create(NULL, 0, &args[i]) != DOM_NO_ERR)
                JS_ThrowOutOfMemory(ctx);
        } else {
            args[i] = qjs_dom_string(ctx, argument, false);
        }
        if (args[i] == NULL)
            goto out;
    }
    struct qjs_dom_node *node = style_element(ctx, object);
    if (node == NULL)
        goto out;
    struct style_object *wrapper = style_receiver(ctx, object);
    if (wrapper->computed) {
        if (method == STYLE_SET || method == STYLE_REMOVE)
            result = qjs_dom_exception(ctx, DOM_NO_MODIFICATION_ALLOWED_ERR);
        else if (method == STYLE_PRIORITY)
            result = JS_NewString(ctx, "");
        else
            result = computed_value(ctx, wrapper, node, dom_string_data(args[0]), dom_string_byte_length(args[0]));
        goto out;
    }
    css_stylesheet_params params;
    struct nscss_inline_style *style = node == NULL ? NULL : style_native(ctx, node, &params);
    if (style == NULL)
        goto out;
    const css_declarations *d = nscss_inline_style_declarations(style);
    const char *name = dom_string_data(args[0]);
    size_t length = dom_string_byte_length(args[0]);
    css_error error;
    if (method == STYLE_PRIORITY) {
        bool important;
        error = css_declarations_priority(d, name, length, &important);
        result = error == CSS_OK ? JS_NewString(ctx, important ? "important" : "") : style_css_error(ctx, error);
        goto out;
    }
    if (method != STYLE_SET) {
        lwc_string *text = NULL;
        error = css_declarations_value(d, name, length, &text);
        result = style_string(ctx, error, text);
        if (method == STYLE_VALUE || JS_IsException(result))
            goto out;
    }
    css_declarations *candidate = NULL;
    error = css_declarations_clone(d, &candidate);
    bool changed = false;
    if (error == CSS_OK) {
        if (method == STYLE_REMOVE)
            error = css_declarations_remove(candidate, name, length, &changed);
        else
            error = css_declarations_set(candidate, &params, name, length, dom_string_data(args[1]),
                dom_string_byte_length(args[1]), dom_string_data(args[2]), dom_string_byte_length(args[2]), &changed);
    }
    JSValue update = error != CSS_OK ? style_css_error(ctx, error) : JS_UNDEFINED;
    if (error == CSS_OK && changed) {
        update = style_publish(
            ctx, node, style, candidate, method == STYLE_REMOVE ? "style.removeProperty" : "style.setProperty");
        candidate = NULL;
    }
    css_declarations_destroy(candidate);
    if (method == STYLE_SET || JS_IsException(update)) {
        JS_FreeValue(ctx, result);
        result = update;
    }
out:
    for (unsigned i = 0; i < 3; i++)
        dom_string_unref(args[i]);
    return result;
}

static JSValue style_item(JSContext *ctx, JSValueConst object, int argc, JSValueConst *argv)
{
    if (style_element(ctx, object) == NULL)
        return JS_EXCEPTION;
    if (argc == 0)
        return JS_ThrowTypeError(ctx, "item requires an index");
    uint32_t index;
    if (JS_ToUint32(ctx, &index, argv[0]) < 0)
        return JS_EXCEPTION;
    struct qjs_dom_node *node = style_element(ctx, object);
    if (node == NULL)
        return JS_EXCEPTION;
    struct style_object *wrapper = style_receiver(ctx, object);
    if (wrapper->computed) {
        int connected = computed_connected(ctx, wrapper, node);
        size_t count;
        const char *const *names = css_declarations_supported_longhands(&count);
        return connected < 0 ? JS_EXCEPTION : JS_NewString(ctx, connected && index < count ? names[index] : "");
    }
    css_stylesheet_params params;
    struct nscss_inline_style *style = node == NULL ? NULL : style_native(ctx, node, &params);
    if (style == NULL)
        return JS_EXCEPTION;
    const char *name = css_declarations_item(nscss_inline_style_declarations(style), index);
    return JS_NewString(ctx, name != NULL ? name : "");
}

static JSValue property_get(JSContext *ctx, JSValueConst object, int index)
{
    size_t count;
    const char *const *names = css_declarations_supported_properties(&count);
    JSValue name = JS_NewString(ctx, names[index]);
    if (JS_IsException(name))
        return name;
    JSValue result = style_method(ctx, object, 1, &name, STYLE_VALUE);
    JS_FreeValue(ctx, name);
    return result;
}

static JSValue property_set(JSContext *ctx, JSValueConst object, JSValueConst value, int index)
{
    size_t count;
    const char *const *names = css_declarations_supported_properties(&count);
    JSValue args[2] = {JS_NewString(ctx, names[index]), value};
    if (JS_IsException(args[0]))
        return args[0];
    JSValue result = style_method(ctx, object, 2, args, STYLE_SET);
    JS_FreeValue(ctx, args[0]);
    return result;
}

static int style_get_own(JSContext *ctx, JSPropertyDescriptor *desc, JSValueConst object, JSAtom key)
{
    struct style_object *style = style_receiver(ctx, object);
    if (style == NULL)
        return -1;
    uint32_t index;
    int indexed = qjs_dom_property_index(ctx, key, &index);
    if (indexed <= 0)
        return indexed < 0 ? -1 : JS_GetOwnProperty(ctx, desc, style->properties, key);
    struct qjs_dom_node *node = style_element(ctx, object);
    if (node == NULL)
        return -1;
    if (style->computed) {
        int connected = computed_connected(ctx, style, node);
        size_t count;
        const char *const *names = css_declarations_supported_longhands(&count);
        if (connected < 0)
            return -1;
        if (!connected || index >= count)
            return 0;
        if (desc != NULL) {
            desc->flags = JS_PROP_CONFIGURABLE | JS_PROP_ENUMERABLE;
            desc->getter = desc->setter = JS_UNDEFINED;
            desc->value = JS_NewString(ctx, names[index]);
            if (JS_IsException(desc->value))
                return -1;
        }
        return 1;
    }
    css_stylesheet_params params;
    struct nscss_inline_style *native = node == NULL ? NULL : style_native(ctx, node, &params);
    if (native == NULL)
        return -1;
    const char *name = css_declarations_item(nscss_inline_style_declarations(native), index);
    if (name == NULL)
        return 0;
    if (desc != NULL) {
        desc->flags = JS_PROP_CONFIGURABLE | JS_PROP_ENUMERABLE;
        desc->getter = desc->setter = JS_UNDEFINED;
        desc->value = JS_NewString(ctx, name);
        if (JS_IsException(desc->value))
            return -1;
    }
    return 1;
}

static int style_keys(JSContext *ctx, JSPropertyEnum **keys, uint32_t *count, JSValueConst object)
{
    *keys = NULL;
    *count = 0;
    JSValue length_value = style_get(ctx, object, STYLE_LENGTH);
    uint32_t length;
    if (JS_IsException(length_value))
        return -1;
    int converted = JS_ToUint32(ctx, &length, length_value);
    JS_FreeValue(ctx, length_value);
    if (converted < 0)
        return -1;
    struct style_object *style = style_receiver(ctx, object);
    JSPropertyEnum *extra = NULL;
    uint32_t extras;
    if (JS_GetOwnPropertyNames(ctx, &extra, &extras, style->properties, JS_GPN_STRING_MASK | JS_GPN_SYMBOL_MASK) < 0)
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
    js_free(ctx, extra);
    *keys = result;
    *count = length + extras;
    return 0;
}

static int style_define(JSContext *ctx, JSValueConst object, JSAtom key, JSValueConst value, JSValueConst getter,
    JSValueConst setter, int flags)
{
    uint32_t index;
    int indexed = qjs_dom_property_index(ctx, key, &index);
    if (indexed != 0)
        return indexed < 0 ? -1 : 0;
    struct style_object *style = style_receiver(ctx, object);
    return style == NULL ? -1 : JS_DefineProperty(ctx, style->properties, key, value, getter, setter, flags);
}

static int style_delete(JSContext *ctx, JSValueConst object, JSAtom key)
{
    uint32_t index;
    int indexed = qjs_dom_property_index(ctx, key, &index);
    if (indexed < 0)
        return -1;
    if (indexed) {
        int present = style_get_own(ctx, NULL, object, key);
        return present < 0 ? -1 : !present;
    }
    struct style_object *style = style_receiver(ctx, object);
    return style == NULL ? -1 : JS_DeleteProperty(ctx, style->properties, key, 0);
}

static int style_prevent_extensions(JSContext *ctx, JSValueConst object)
{
    return 0;
}
static JSClassExoticMethods style_exotic = {.get_own_property = style_get_own,
    .get_own_property_names = style_keys,
    .define_own_property = style_define,
    .delete_property = style_delete,
    .prevent_extensions = style_prevent_extensions};
static JSValue style_constructor(JSContext *ctx, JSValueConst target, int argc, JSValueConst *argv)
{
    return JS_ThrowTypeError(ctx, "Illegal CSS style constructor");
}

static int install_property(JSContext *ctx, JSValueConst proto, const char *name, int index)
{
    JSCFunctionListEntry entry = JS_CGETSET_MAGIC_DEF2(
        name, property_get, property_set, index, JS_PROP_CONFIGURABLE | JS_PROP_ENUMERABLE);
    return JS_SetPropertyFunctionList(ctx, proto, &entry, 1);
}

int qjs_dom_install_style(JSContext *ctx, JSValue *prototypes)
{
    jsthread *thread = JS_GetContextOpaque(ctx);
    JSClassID local = 0;
    JSClassID *id = thread == NULL ? &local : &thread->heap->style_class;
    if (*id == 0) {
        JS_NewClassID(JS_GetRuntime(ctx), id);
        JSClassDef definition = {.class_name = "CSSStyleProperties",
            .finalizer = style_finalizer,
            .gc_mark = style_mark,
            .exotic = &style_exotic};
        if (JS_NewClass(JS_GetRuntime(ctx), *id, &definition) < 0) {
            *id = 0;
            return -1;
        }
    }
    JSValue global = JS_GetGlobalObject(ctx);
    JSValue base = JS_NewObject(ctx);
    JSValue proto = JS_IsException(base) ? JS_EXCEPTION : JS_NewObjectProto(ctx, base);
    JSValue ctor = JS_NewCFunction2(ctx, style_constructor, "CSSStyleDeclaration", 0, JS_CFUNC_constructor, 0);
    JSValue derived = JS_NewCFunction2(ctx, style_constructor, "CSSStyleProperties", 0, JS_CFUNC_constructor, 0);
    int result = -1;
#define METHOD(name, length, operation)                                                                                \
    {                                                                                                                  \
        name, JS_PROP_C_W_E, JS_DEF_CFUNC, operation,                                                                  \
        {                                                                                                              \
            .func = {length, JS_CFUNC_generic_magic, {.generic_magic = style_method}}                                  \
        }                                                                                                              \
    }
    static const JSCFunctionListEntry methods[] = {
        JS_CGETSET_MAGIC_DEF2(
            "cssText", style_get, style_text_set, STYLE_TEXT, JS_PROP_CONFIGURABLE | JS_PROP_ENUMERABLE),
        JS_CGETSET_MAGIC_DEF2("length", style_get, NULL, STYLE_LENGTH, JS_PROP_CONFIGURABLE | JS_PROP_ENUMERABLE),
        JS_CGETSET_MAGIC_DEF2("parentRule", style_get, NULL, STYLE_PARENT, JS_PROP_CONFIGURABLE | JS_PROP_ENUMERABLE),
        JS_CFUNC_DEF2("item", 1, style_item, JS_PROP_C_W_E),
        METHOD("getPropertyValue", 1, STYLE_VALUE),
        METHOD("getPropertyPriority", 1, STYLE_PRIORITY),
        METHOD("setProperty", 2, STYLE_SET),
        METHOD("removeProperty", 1, STYLE_REMOVE),
    };
#undef METHOD
    if (JS_IsException(global) || JS_IsException(base) || JS_IsException(proto) || JS_IsException(ctor) ||
        JS_IsException(derived) || JS_SetConstructor(ctx, ctor, base) < 0 ||
        JS_SetConstructor(ctx, derived, proto) < 0 || JS_SetPrototype(ctx, derived, ctor) < 0 ||
        JS_SetPropertyFunctionList(ctx, base, methods, sizeof(methods) / sizeof(*methods)) < 0 ||
        qjs_dom_install_iteration(ctx, base, global, "CSSStyleDeclaration", false) < 0 ||
        qjs_dom_install_iteration(ctx, proto, global, "CSSStyleProperties", false) < 0)
        goto out;
    size_t count;
    const char *const *names = css_declarations_supported_properties(&count);
    for (size_t i = 0; i < count; i++) {
        const char *name = names[i];
        size_t length = strlen(name);
        char *camel = js_malloc(ctx, length + 1);
        if (camel == NULL)
            goto out;
        size_t end = 0;
        bool upper = false;
        for (size_t j = 0; j < length; j++) {
            if (name[j] == '-') {
                upper = true;
                continue;
            }
            camel[end++] = upper && name[j] >= 'a' && name[j] <= 'z' ? name[j] - 'a' + 'A' : name[j];
            upper = false;
        }
        camel[end] = '\0';
        int error = install_property(ctx, proto, camel, i);
        if (error >= 0 && strcmp(name, camel) != 0)
            error = install_property(ctx, proto, name, i);
        if (error >= 0 && strncmp(name, "-webkit-", 8) == 0) {
            camel[0] = 'w';
            error = install_property(ctx, proto, camel, i);
        }
        if (error >= 0 && strcmp(name, "float") == 0)
            error = install_property(ctx, proto, "cssFloat", i);
        js_free(ctx, camel);
        if (error < 0)
            goto out;
    }
    static const JSCFunctionListEntry accessor[] = {
        JS_CGETSET_MAGIC_DEF2(
            "style", element_style, element_style_set, QJS_HTML_ELEMENT, JS_PROP_CONFIGURABLE | JS_PROP_ENUMERABLE),
        JS_CGETSET_MAGIC_DEF2(
            "style", element_style, element_style_set, QJS_SVG_ELEMENT, JS_PROP_CONFIGURABLE | JS_PROP_ENUMERABLE),
    };
    if (JS_SetPropertyFunctionList(ctx, prototypes[QJS_HTML_ELEMENT], accessor, 1) < 0 ||
        JS_SetPropertyFunctionList(ctx, prototypes[QJS_SVG_ELEMENT], accessor + 1, 1) < 0 ||
        JS_SetPropertyStr(ctx, global, "CSSStyleDeclaration", JS_DupValue(ctx, ctor)) < 0 ||
        JS_SetPropertyStr(ctx, global, "CSSStyleProperties", JS_DupValue(ctx, derived)) < 0)
        goto out;
    static const JSCFunctionListEntry computed[] = {
        JS_CFUNC_DEF2("getComputedStyle", 1, window_computed_style, JS_PROP_C_W_E),
    };
    if (JS_SetPropertyFunctionList(ctx, global, computed, sizeof(computed) / sizeof(*computed)) < 0)
        goto out;
    JS_SetClassProto(ctx, *id, JS_DupValue(ctx, proto));
    result = 0;
out:
    JS_FreeValue(ctx, derived);
    JS_FreeValue(ctx, ctor);
    JS_FreeValue(ctx, proto);
    JS_FreeValue(ctx, base);
    JS_FreeValue(ctx, global);
    return result;
}

/* SPDX-License-Identifier: GPL-2.0-or-later
 * Same-document node creation and tree edits. libdom owns tree validity and
 * links; this layer owns Web IDL conversion, wrappers and render attribution.
 */
#include "dom.h"
#include <inttypes.h>

enum creation { CREATE_ELEMENT, CREATE_TEXT, CREATE_COMMENT, CREATE_FRAGMENT };
enum mutation { APPEND, INSERT, REPLACE, REMOVE };

/* Custom element construction needs a registry and lifecycle implementation.
 * Accept the default options and the legacy string argument; do not silently
 * claim to construct customized built-ins. Read dictionary members in IDL
 * order, preserving getter/conversion exceptions. */
static int element_options(JSContext *ctx, JSValueConst options)
{
    if (JS_IsNull(options) || JS_IsUndefined(options))
        return 0;
    if (!JS_IsObject(options)) {
        const char *legacy = JS_ToCString(ctx, options);
        if (legacy == NULL)
            return -1;
        JS_FreeCString(ctx, legacy);
        return 0;
    }
    JSValue registry = JS_GetPropertyStr(ctx, options, "customElementRegistry");
    if (JS_IsException(registry))
        return -1;
    bool unsupported = !JS_IsUndefined(registry) && !JS_IsNull(registry);
    JS_FreeValue(ctx, registry);
    if (unsupported) {
        JS_ThrowTypeError(ctx, "CustomElementRegistry is not supported");
        return -1;
    }
    JSValue is = JS_GetPropertyStr(ctx, options, "is");
    if (JS_IsException(is))
        return -1;
    if (JS_IsUndefined(is))
        return 0;
    dom_string *name = qjs_dom_string(ctx, is, false);
    JS_FreeValue(ctx, is);
    if (name == NULL)
        return -1;
    dom_string_unref(name);
    JS_ThrowDOMException(ctx, "NotSupportedError", "Customized built-in elements are not supported");
    return -1;
}

static JSValue create(JSContext *ctx, JSValueConst object, int argc, JSValueConst *argv, int method)
{
    if (qjs_dom_get(ctx, object, DOM_DOCUMENT_NODE) == NULL)
        return JS_EXCEPTION;
    dom_string *text = NULL;
    if (method != CREATE_FRAGMENT) {
        if (argc == 0)
            return JS_ThrowTypeError(ctx, "Node creation requires a string argument");
        text = qjs_dom_string(ctx, argv[0], false);
        if (text == NULL)
            return JS_EXCEPTION;
    }
    if (method == CREATE_ELEMENT && argc > 1 && element_options(ctx, argv[1]) < 0) {
        dom_string_unref(text);
        return JS_EXCEPTION;
    }
    /* String conversion and option getters can retire the document. */
    struct qjs_dom_node *self = qjs_dom_get(ctx, object, DOM_DOCUMENT_NODE);
    if (self == NULL) {
        dom_string_unref(text);
        return JS_EXCEPTION;
    }
    dom_node *node = NULL;
    dom_exception err;
    switch (method) {
    case CREATE_ELEMENT:
        err = dom_document_create_element(self->node, text, &node);
        break;
    case CREATE_TEXT:
        err = dom_document_create_text_node(self->node, text, &node);
        break;
    case CREATE_COMMENT:
        err = dom_document_create_comment(self->node, text, &node);
        break;
    default:
        err = dom_document_create_document_fragment(self->node, &node);
        break;
    }
    dom_string_unref(text);
    JSValue result = err == DOM_NO_ERR ? qjs_dom_wrap(ctx, object, node) : qjs_dom_exception(ctx, err);
    dom_node_unref(node);
    return result;
}

static JSValue mutate(JSContext *ctx, JSValueConst object, int argc, JSValueConst *argv, int method)
{
    struct qjs_dom_node *self = qjs_dom_get(ctx, object, 0);
    if (self == NULL)
        return JS_EXCEPTION;
    if (argc < (method == INSERT || method == REPLACE ? 2 : 1))
        return JS_ThrowTypeError(ctx, "Tree operation is missing required arguments");
    struct qjs_dom_node *child = qjs_dom_get(ctx, argv[0], 0), *reference = NULL, *former = NULL;
    if (child == NULL)
        return JS_EXCEPTION;
    if (method == REPLACE || (method == INSERT && !JS_IsNull(argv[1]) && !JS_IsUndefined(argv[1]))) {
        reference = qjs_dom_get(ctx, argv[1], 0);
        if (reference == NULL)
            return JS_EXCEPTION;
    }

    /* Moving a connected node into a detached tree changes the old parent's
     * rendered subtree too. Retain/wrap that parent before mutation, so a JS
     * allocation failure cannot leave an unreported native edit behind. */
    dom_node *parent = NULL, *result = NULL;
    JSValue former_object = JS_UNDEFINED;
    dom_exception err = DOM_NO_ERR;
    bool changes = !((method == INSERT || method == REPLACE) && child == reference);
    if ((method == APPEND || method == INSERT) && child->type == DOM_DOCUMENT_FRAGMENT_NODE)
        err = dom_node_has_child_nodes(child->node, &changes);
    if (err == DOM_NO_ERR && method != REMOVE)
        err = dom_node_get_parent_node(child->node, &parent);
    if (err != DOM_NO_ERR)
        return qjs_dom_exception(ctx, err);
    if (parent != NULL && parent != self->node) {
        JSValue owner = qjs_dom_owner(ctx, object, self);
        former_object = qjs_dom_wrap(ctx, owner, parent);
        JS_FreeValue(ctx, owner);
        dom_node_unref(parent);
        if (JS_IsException(former_object))
            return JS_EXCEPTION;
        former = qjs_dom_get(ctx, former_object, 0);
        if (former == NULL) {
            JS_FreeValue(ctx, former_object);
            return JS_EXCEPTION;
        }
    } else {
        dom_node_unref(parent);
    }

    static const char *const operations[] = {"appendChild", "insertBefore", "replaceChild", "removeChild"};
    switch (method) {
    case APPEND:
        err = dom_node_append_child(self->node, child->node, &result);
        break;
    case INSERT:
        err = dom_node_insert_before(self->node, child->node, reference != NULL ? reference->node : NULL, &result);
        break;
    case REPLACE:
        err = dom_node_replace_child(self->node, child->node, reference->node, &result);
        break;
    default:
        err = dom_node_remove_child(self->node, child->node, &result);
        break;
    }
    dom_node_unref(result);
    qjs_trace(ctx, "dom.tree",
        "operation=%s parent=%" PRIu64 " child=%" PRIu64 " reference=%" PRIu64 " former_parent=%" PRIu64 " result=%u",
        operations[method], self->id, child->id, reference != NULL ? reference->id : 0, former != NULL ? former->id : 0,
        err);
    /* Native allocation failures can occur after linking. Validation errors
     * leave both trees intact and must not request a page rebuild. */
    if ((err == DOM_NO_ERR && changes) || err == DOM_NO_MEM_ERR) {
        qjs_dom_changed(ctx, self, operations[method], NULL, err);
        if (former != NULL)
            qjs_dom_changed(ctx, former, "moveFrom", NULL, err);
    }
    JS_FreeValue(ctx, former_object);
    return err == DOM_NO_ERR ? JS_DupValue(ctx, argv[method == REPLACE ? 1 : 0]) : qjs_dom_exception(ctx, err);
}

#define METHOD(name, count, callback, method)                                                                          \
    {                                                                                                                  \
        name, JS_PROP_C_W_E, JS_DEF_CFUNC, method,                                                                     \
        {                                                                                                              \
            .func = {count, JS_CFUNC_generic_magic, {.generic_magic = callback}}                                       \
        }                                                                                                              \
    }

int qjs_dom_install_tree(JSContext *ctx, JSValue *prototypes)
{
    static const JSCFunctionListEntry document[] = {
        METHOD("createElement", 1, create, CREATE_ELEMENT),
        METHOD("createTextNode", 1, create, CREATE_TEXT),
        METHOD("createComment", 1, create, CREATE_COMMENT),
        METHOD("createDocumentFragment", 0, create, CREATE_FRAGMENT),
    };
    static const JSCFunctionListEntry node[] = {
        METHOD("appendChild", 1, mutate, APPEND),
        METHOD("insertBefore", 2, mutate, INSERT),
        METHOD("replaceChild", 2, mutate, REPLACE),
        METHOD("removeChild", 1, mutate, REMOVE),
    };
    return JS_SetPropertyFunctionList(ctx, prototypes[QJS_DOCUMENT], document, sizeof(document) / sizeof(*document)) <
                0 ||
            JS_SetPropertyFunctionList(ctx, prototypes[QJS_NODE], node, sizeof(node) / sizeof(*node)) < 0
        ? -1
        : 0;
}

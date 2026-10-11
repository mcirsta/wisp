/* SPDX-License-Identifier: GPL-2.0-or-later
 * The supported Node/Element/Document operations use libdom directly. Missing
 * APIs are omitted instead of returning dummy nodes, collections or success.
 */
#include "dom.h"
#include <wisp/content/handlers/html/private.h>
#include <string.h>

/* Accessors shared by Node and its derived interfaces. */
enum node_property {
    NODE_TYPE,
    NODE_NAME,
    NODE_VALUE,
    TEXT_CONTENT,
    PARENT,
    PARENT_ELEMENT,
    FIRST_CHILD,
    LAST_CHILD,
    NEXT_SIBLING,
    PREVIOUS_SIBLING,
    OWNER_DOCUMENT,
    CONNECTED,
    TAG_NAME,
    ID,
    CLASS_NAME,
    DATA,
    DOCUMENT_ELEMENT,
    HEAD,
    BODY,
    READY_STATE
};

static JSValue wrap_result(JSContext *ctx, JSValueConst object, struct qjs_dom_node *self, dom_node *node)
{
    JSValue owner = qjs_dom_owner(ctx, object, self);
    JSValue value = qjs_dom_wrap(ctx, owner, node);
    JS_FreeValue(ctx, owner);
    dom_node_unref(node);
    return value;
}

static dom_node_type required_type(int property)
{
    if (property >= DOCUMENT_ELEMENT)
        return DOM_DOCUMENT_NODE;
    if (property >= TAG_NAME && property <= CLASS_NAME)
        return DOM_ELEMENT_NODE;
    return 0;
}

static struct qjs_dom_node *property_receiver(JSContext *ctx, JSValueConst object, int property)
{
    struct qjs_dom_node *node = qjs_dom_get(ctx, object, required_type(property));
    if (node != NULL && property == DATA && node->type != DOM_TEXT_NODE && node->type != DOM_COMMENT_NODE &&
        node->type != DOM_CDATA_SECTION_NODE && node->type != DOM_PROCESSING_INSTRUCTION_NODE) {
        JS_ThrowTypeError(ctx, "CharacterData receiver required");
        return NULL;
    }
    return node;
}

/* HTML's head/body are direct children of the document element, not the first
 * matching descendant. body additionally accepts a frameset element. */
static dom_exception document_child(dom_node *document, int property, dom_node **result)
{
    dom_node *root = NULL, *child = NULL;
    *result = NULL;
    dom_exception err = dom_document_get_document_element(document, &root);
    if (err != DOM_NO_ERR || root == NULL)
        return err;
    if (property == DOCUMENT_ELEMENT) {
        *result = root;
        return DOM_NO_ERR;
    }
    dom_html_element_type tag = DOM_HTML_ELEMENT_TYPE__UNKNOWN;
    if (qjs_dom_is_html(root))
        err = dom_html_element_get_tag_type(root, &tag);
    bool html_root = tag == DOM_HTML_ELEMENT_TYPE_HTML;
    if (err == DOM_NO_ERR && html_root)
        err = dom_node_get_first_child(root, &child);
    dom_node_unref(root);
    while (err == DOM_NO_ERR && child != NULL) {
        dom_node_type type;
        err = dom_node_get_node_type(child, &type);
        if (err == DOM_NO_ERR && type == DOM_ELEMENT_NODE && qjs_dom_is_html(child)) {
            err = dom_html_element_get_tag_type(child, &tag);
            if (err == DOM_NO_ERR) {
                bool match = property == HEAD
                    ? tag == DOM_HTML_ELEMENT_TYPE_HEAD
                    : tag == DOM_HTML_ELEMENT_TYPE_BODY || tag == DOM_HTML_ELEMENT_TYPE_FRAMESET;
                if (match) {
                    *result = child;
                    return DOM_NO_ERR;
                }
            }
        }
        dom_node *next = NULL;
        if (err == DOM_NO_ERR)
            err = dom_node_get_next_sibling(child, &next);
        dom_node_unref(child);
        child = next;
    }
    dom_node_unref(child);
    return err;
}

static JSValue node_get(JSContext *ctx, JSValueConst object, int property)
{
    struct qjs_dom_node *self = property_receiver(ctx, object, property);
    if (self == NULL)
        return JS_EXCEPTION;
    dom_exception err = DOM_NO_ERR;
    dom_string *text = NULL;
    dom_node *node = NULL;
    bool nullable = true;
    switch (property) {
    case READY_STATE: {
        jsthread *thread = JS_GetContextOpaque(ctx);
        html_content *html = thread->doc_priv;
        const char *state = html->readiness == HTML_DOCUMENT_COMPLETE ? "complete"
            : html->readiness == HTML_DOCUMENT_INTERACTIVE            ? "interactive"
                                                                      : "loading";
        return JS_NewString(ctx, state);
    }
    case NODE_TYPE:
        return JS_NewInt32(ctx, self->type);
    case OWNER_DOCUMENT:
        return self->type == DOM_DOCUMENT_NODE ? JS_NULL : JS_DupValue(ctx, self->owner);
    case CONNECTED: {
        JSValue owner = qjs_dom_owner(ctx, object, self);
        struct qjs_dom_node *doc = qjs_dom_get(ctx, owner, DOM_DOCUMENT_NODE);
        bool connected = false;
        err = doc == NULL ? DOM_INVALID_STATE_ERR : dom_node_contains(doc->node, self->node, &connected);
        JS_FreeValue(ctx, owner);
        return err == DOM_NO_ERR ? JS_NewBool(ctx, connected) : qjs_dom_exception(ctx, err);
    }
    case PARENT:
    case PARENT_ELEMENT:
        err = dom_node_get_parent_node(self->node, &node);
        break;
    case FIRST_CHILD:
        err = dom_node_get_first_child(self->node, &node);
        break;
    case LAST_CHILD:
        err = dom_node_get_last_child(self->node, &node);
        break;
    case NEXT_SIBLING:
        err = dom_node_get_next_sibling(self->node, &node);
        break;
    case PREVIOUS_SIBLING:
        err = dom_node_get_previous_sibling(self->node, &node);
        break;
    case DOCUMENT_ELEMENT:
    case HEAD:
    case BODY:
        err = document_child(self->node, property, &node);
        break;
    case NODE_NAME:
    case TAG_NAME:
        err = dom_node_get_node_name(self->node, &text);
        nullable = false;
        break;
    case NODE_VALUE:
    case DATA:
        err = dom_node_get_node_value(self->node, &text);
        nullable = property != DATA;
        break;
    case TEXT_CONTENT:
        err = dom_node_get_text_content(self->node, &text);
        nullable = self->type == DOM_DOCUMENT_NODE || self->type == DOM_DOCUMENT_TYPE_NODE;
        break;
    case ID:
    case CLASS_NAME: {
        dom_string *name = NULL;
        const char *key = property == ID ? "id" : "class";
        err = dom_string_create((const uint8_t *)key, strlen(key), &name);
        if (err == DOM_NO_ERR)
            err = dom_element_get_attribute(self->node, name, &text);
        dom_string_unref(name);
        nullable = false;
        break;
    }
    }
    if (err != DOM_NO_ERR) {
        dom_node_unref(node);
        dom_string_unref(text);
        return qjs_dom_exception(ctx, err);
    }
    if ((property >= PARENT && property <= PREVIOUS_SIBLING) || property >= DOCUMENT_ELEMENT) {
        if (property == PARENT_ELEMENT && node != NULL) {
            dom_node_type type;
            err = dom_node_get_node_type(node, &type);
            if (err != DOM_NO_ERR || type != DOM_ELEMENT_NODE) {
                dom_node_unref(node);
                node = NULL;
            }
        }
        return err == DOM_NO_ERR ? wrap_result(ctx, object, self, node) : qjs_dom_exception(ctx, err);
    }
    JSValue value = qjs_dom_from_string(ctx, text, nullable);
    dom_string_unref(text);
    return value;
}

static JSValue node_set(JSContext *ctx, JSValueConst object, JSValueConst value, int property)
{
    struct qjs_dom_node *self = property_receiver(ctx, object, property);
    if (self == NULL)
        return JS_EXCEPTION;
    /* CharacterData.data has LegacyNullToEmptyString: unlike the nullable Node
     * attributes, undefined still becomes the literal string "undefined". */
    dom_string *text = qjs_dom_string(
        ctx, value, property == NODE_VALUE || property == TEXT_CONTENT || (property == DATA && JS_IsNull(value)));
    if (text == NULL)
        return JS_EXCEPTION;
    /* Conversion can run JS and retire the document. */
    self = property_receiver(ctx, object, property);
    if (self == NULL) {
        dom_string_unref(text);
        return JS_EXCEPTION;
    }
    dom_exception err = DOM_NO_ERR;
    const char *operation = NULL;
    if (property == ID || property == CLASS_NAME) {
        dom_string *name = NULL;
        operation = property == ID ? "id" : "className";
        const char *key = property == ID ? "id" : "class";
        err = dom_string_create((const uint8_t *)key, strlen(key), &name);
        if (err == DOM_NO_ERR)
            err = dom_element_set_attribute(self->node, name, text);
        dom_string_unref(name);
    } else if (self->type == DOM_TEXT_NODE || self->type == DOM_COMMENT_NODE || self->type == DOM_CDATA_SECTION_NODE ||
        self->type == DOM_PROCESSING_INSTRUCTION_NODE) {
        operation = property == TEXT_CONTENT ? "textContent" : property == DATA ? "data" : "nodeValue";
        err = dom_node_set_node_value(self->node, text);
    } else if (property == TEXT_CONTENT &&
        (self->type == DOM_ELEMENT_NODE || self->type == DOM_DOCUMENT_FRAGMENT_NODE)) {
        operation = "textContent";
        err = dom_node_set_text_content(self->node, text);
    }
    dom_string_unref(text);
    if (operation != NULL)
        qjs_dom_changed(ctx, self, operation, NULL, err);
    return err == DOM_NO_ERR ? JS_UNDEFINED : qjs_dom_exception(ctx, err);
}

static JSValue document_id(JSContext *ctx, JSValueConst object, int argc, JSValueConst *argv)
{
    if (qjs_dom_get(ctx, object, DOM_DOCUMENT_NODE) == NULL)
        return JS_EXCEPTION;
    if (argc == 0)
        return JS_ThrowTypeError(ctx, "getElementById requires an ID");
    dom_string *id = qjs_dom_string(ctx, argv[0], false);
    if (id == NULL)
        return JS_EXCEPTION;
    struct qjs_dom_node *self = qjs_dom_get(ctx, object, DOM_DOCUMENT_NODE);
    if (self == NULL) {
        dom_string_unref(id);
        return JS_EXCEPTION;
    }
    dom_node *node = NULL;
    dom_exception err = dom_document_get_element_by_id(self->node, id, &node);
    dom_string_unref(id);
    if (err != DOM_NO_ERR)
        return qjs_dom_exception(ctx, err);
    return wrap_result(ctx, object, self, node);
}

enum attribute_method { GET_ATTRIBUTE, HAS_ATTRIBUTE, SET_ATTRIBUTE, REMOVE_ATTRIBUTE };

static JSValue attribute(JSContext *ctx, JSValueConst object, int argc, JSValueConst *argv, int method)
{
    if (qjs_dom_get(ctx, object, DOM_ELEMENT_NODE) == NULL)
        return JS_EXCEPTION;
    if (argc < (method == SET_ATTRIBUTE ? 2 : 1))
        return JS_ThrowTypeError(ctx, "Attribute method is missing required arguments");
    dom_string *name = qjs_dom_string(ctx, argv[0], false), *value = NULL;
    if (name == NULL)
        return JS_EXCEPTION;
    if (method == SET_ATTRIBUTE) {
        value = qjs_dom_string(ctx, argv[1], false);
        if (value == NULL) {
            dom_string_unref(name);
            return JS_EXCEPTION;
        }
    }
    struct qjs_dom_node *self = qjs_dom_get(ctx, object, DOM_ELEMENT_NODE);
    if (self == NULL) {
        dom_string_unref(name);
        dom_string_unref(value);
        return JS_EXCEPTION;
    }
    dom_exception err = DOM_NO_ERR;
    if (qjs_dom_is_html(self->node)) {
        dom_string *lower = NULL;
        err = dom_string_tolower(name, true, &lower);
        dom_string_unref(name);
        name = lower;
    }
    bool present = false;
    dom_string *result = NULL;
    JSValue ret = JS_UNDEFINED;
    if (err == DOM_NO_ERR) {
        switch (method) {
        case GET_ATTRIBUTE:
            err = dom_element_get_attribute(self->node, name, &result);
            break;
        case HAS_ATTRIBUTE:
            err = dom_element_has_attribute(self->node, name, &present);
            break;
        case SET_ATTRIBUTE:
            err = dom_element_set_attribute(self->node, name, value);
            if (err == DOM_NO_ERR || err == DOM_NO_MEM_ERR)
                qjs_dom_changed(ctx, self, "setAttribute", name, err);
            break;
        case REMOVE_ATTRIBUTE:
            err = dom_element_has_attribute(self->node, name, &present);
            if (err == DOM_NO_ERR && present) {
                err = dom_element_remove_attribute(self->node, name);
                qjs_dom_changed(ctx, self, "removeAttribute", name, err);
            }
            break;
        }
    }
    if (err != DOM_NO_ERR)
        ret = qjs_dom_exception(ctx, err);
    else if (method == GET_ATTRIBUTE)
        ret = qjs_dom_from_string(ctx, result, true);
    else if (method == HAS_ATTRIBUTE)
        ret = JS_NewBool(ctx, present);
    dom_string_unref(name);
    dom_string_unref(value);
    dom_string_unref(result);
    return ret;
}

static JSValue node_has_children(JSContext *ctx, JSValueConst object, int argc, JSValueConst *argv)
{
    struct qjs_dom_node *self = qjs_dom_get(ctx, object, 0);
    if (self == NULL)
        return JS_EXCEPTION;
    bool result;
    dom_exception err = dom_node_has_child_nodes(self->node, &result);
    return err == DOM_NO_ERR ? JS_NewBool(ctx, result) : qjs_dom_exception(ctx, err);
}

#define GET(name, property)                                                                                            \
    JS_CGETSET_MAGIC_DEF2(name, node_get, NULL, property, JS_PROP_CONFIGURABLE | JS_PROP_ENUMERABLE)
#define ACCESS(name, property)                                                                                         \
    JS_CGETSET_MAGIC_DEF2(name, node_get, node_set, property, JS_PROP_CONFIGURABLE | JS_PROP_ENUMERABLE)
#define METHOD(name, count, method)                                                                                    \
    {                                                                                                                  \
        name, JS_PROP_C_W_E, JS_DEF_CFUNC, method,                                                                     \
        {                                                                                                              \
            .func = {count, JS_CFUNC_generic_magic, {.generic_magic = attribute}}                                      \
        }                                                                                                              \
    }

int qjs_dom_install_properties(JSContext *ctx, JSValue *prototypes)
{
    static const JSCFunctionListEntry node[] = {
        GET("nodeType", NODE_TYPE),
        GET("nodeName", NODE_NAME),
        ACCESS("nodeValue", NODE_VALUE),
        ACCESS("textContent", TEXT_CONTENT),
        GET("parentNode", PARENT),
        GET("parentElement", PARENT_ELEMENT),
        GET("firstChild", FIRST_CHILD),
        GET("lastChild", LAST_CHILD),
        GET("nextSibling", NEXT_SIBLING),
        GET("previousSibling", PREVIOUS_SIBLING),
        GET("ownerDocument", OWNER_DOCUMENT),
        GET("isConnected", CONNECTED),
        JS_CFUNC_DEF2("hasChildNodes", 0, node_has_children, JS_PROP_C_W_E),
    };
    static const JSCFunctionListEntry element[] = {
        GET("tagName", TAG_NAME),
        ACCESS("id", ID),
        ACCESS("className", CLASS_NAME),
        JS_CGETSET_DEF2("classList", qjs_class_list_get, qjs_class_list_set, JS_PROP_CONFIGURABLE | JS_PROP_ENUMERABLE),
        METHOD("getAttribute", 1, GET_ATTRIBUTE),
        METHOD("hasAttribute", 1, HAS_ATTRIBUTE),
        METHOD("setAttribute", 2, SET_ATTRIBUTE),
        METHOD("removeAttribute", 1, REMOVE_ATTRIBUTE),
    };
    static const JSCFunctionListEntry document[] = {
        GET("readyState", READY_STATE),
        GET("documentElement", DOCUMENT_ELEMENT),
        GET("head", HEAD),
        GET("body", BODY),
        JS_CFUNC_DEF2("getElementById", 1, document_id, JS_PROP_C_W_E),
    };
    static const JSCFunctionListEntry data[] = {ACCESS("data", DATA)};
    static const JSCFunctionListEntry constants[] = {
        JS_PROP_INT32_DEF("ELEMENT_NODE", 1, JS_PROP_ENUMERABLE),
        JS_PROP_INT32_DEF("ATTRIBUTE_NODE", 2, JS_PROP_ENUMERABLE),
        JS_PROP_INT32_DEF("TEXT_NODE", 3, JS_PROP_ENUMERABLE),
        JS_PROP_INT32_DEF("CDATA_SECTION_NODE", 4, JS_PROP_ENUMERABLE),
        JS_PROP_INT32_DEF("ENTITY_REFERENCE_NODE", 5, JS_PROP_ENUMERABLE),
        JS_PROP_INT32_DEF("ENTITY_NODE", 6, JS_PROP_ENUMERABLE),
        JS_PROP_INT32_DEF("PROCESSING_INSTRUCTION_NODE", 7, JS_PROP_ENUMERABLE),
        JS_PROP_INT32_DEF("COMMENT_NODE", 8, JS_PROP_ENUMERABLE),
        JS_PROP_INT32_DEF("DOCUMENT_NODE", 9, JS_PROP_ENUMERABLE),
        JS_PROP_INT32_DEF("DOCUMENT_TYPE_NODE", 10, JS_PROP_ENUMERABLE),
        JS_PROP_INT32_DEF("DOCUMENT_FRAGMENT_NODE", 11, JS_PROP_ENUMERABLE),
        JS_PROP_INT32_DEF("NOTATION_NODE", 12, JS_PROP_ENUMERABLE),
    };
    JSValue ctor = JS_GetPropertyStr(ctx, prototypes[QJS_NODE], "constructor");
    int err = JS_IsException(ctor)
        ? -1
        : JS_SetPropertyFunctionList(ctx, ctor, constants, sizeof(constants) / sizeof(*constants));
    JS_FreeValue(ctx, ctor);
    if (err < 0 ||
        JS_SetPropertyFunctionList(ctx, prototypes[QJS_NODE], constants, sizeof(constants) / sizeof(*constants)) < 0)
        return -1;
    return JS_SetPropertyFunctionList(ctx, prototypes[QJS_NODE], node, sizeof(node) / sizeof(*node)) < 0 ||
            JS_SetPropertyFunctionList(ctx, prototypes[QJS_ELEMENT], element, sizeof(element) / sizeof(*element)) < 0 ||
            JS_SetPropertyFunctionList(ctx, prototypes[QJS_DOCUMENT], document, sizeof(document) / sizeof(*document)) <
                0 ||
            JS_SetPropertyFunctionList(ctx, prototypes[QJS_CHARACTER_DATA], data, sizeof(data) / sizeof(*data)) < 0
        ? -1
        : 0;
}

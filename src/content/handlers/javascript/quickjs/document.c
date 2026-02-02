/*
 * Copyright 2025 Neosurf Contributors
 *
 * This file is part of NeoSurf, http://www.netsurf-browser.org/
 */

#include "document.h"
#include <wisp/utils/log.h>
#include "quickjs.h"
#include <stdlib.h>

/* Forward declarations for element methods */
static JSValue js_element_appendChild(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
static JSValue js_element_removeChild(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
static JSValue js_element_insertBefore(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
static JSValue js_element_cloneNode(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
static JSValue js_element_getAttribute(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
static JSValue js_element_setAttribute(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
static JSValue js_element_hasAttribute(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
static JSValue js_element_removeAttribute(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
static JSValue js_element_addEventListener(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
static JSValue js_element_removeEventListener(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);

/**
 * Create a dummy style object that accepts any property without error.
 */
static JSValue create_style_object(JSContext *ctx)
{
    NSLOG(wisp, DEBUG, "Creating dummy style object for element");
    return JS_NewObject(ctx);
}

/**
 * Create a dummy classList object with add/remove/contains/toggle methods.
 */
static JSValue create_classlist_object(JSContext *ctx)
{
    JSValue classList = JS_NewObject(ctx);
    /* Stubs that do nothing but don't crash */
    JS_SetPropertyStr(ctx, classList, "add", JS_NewCFunction(ctx, (JSCFunction *)js_element_setAttribute, "add", 1));
    JS_SetPropertyStr(
        ctx, classList, "remove", JS_NewCFunction(ctx, (JSCFunction *)js_element_removeAttribute, "remove", 1));
    JS_SetPropertyStr(
        ctx, classList, "contains", JS_NewCFunction(ctx, (JSCFunction *)js_element_hasAttribute, "contains", 1));
    JS_SetPropertyStr(
        ctx, classList, "toggle", JS_NewCFunction(ctx, (JSCFunction *)js_element_hasAttribute, "toggle", 1));
    return classList;
}

/**
 * Create a dummy element object with common properties.
 * Elements need: style, classList, tagName, parentNode, childNodes, and
 * methods.
 */
static JSValue create_element_object(JSContext *ctx, const char *tag)
{
    JSValue element = JS_NewObject(ctx);

    /* Add style property */
    JS_SetPropertyStr(ctx, element, "style", create_style_object(ctx));

    /* Add classList */
    JS_SetPropertyStr(ctx, element, "classList", create_classlist_object(ctx));

    /* Add tagName and nodeName */
    if (tag) {
        JS_SetPropertyStr(ctx, element, "tagName", JS_NewString(ctx, tag));
        JS_SetPropertyStr(ctx, element, "nodeName", JS_NewString(ctx, tag));
    }

    /* Node properties */
    JS_SetPropertyStr(ctx, element, "nodeType", JS_NewInt32(ctx, 1)); /* ELEMENT_NODE */
    JS_SetPropertyStr(ctx, element, "parentNode", JS_NULL);
    JS_SetPropertyStr(ctx, element, "parentElement", JS_NULL);
    JS_SetPropertyStr(ctx, element, "firstChild", JS_NULL);
    JS_SetPropertyStr(ctx, element, "lastChild", JS_NULL);
    JS_SetPropertyStr(ctx, element, "nextSibling", JS_NULL);
    JS_SetPropertyStr(ctx, element, "previousSibling", JS_NULL);
    JS_SetPropertyStr(ctx, element, "ownerDocument", JS_NULL);

    /* Empty child arrays */
    JS_SetPropertyStr(ctx, element, "childNodes", JS_NewArray(ctx));
    JS_SetPropertyStr(ctx, element, "children", JS_NewArray(ctx));

    /* Content properties */
    JS_SetPropertyStr(ctx, element, "innerHTML", JS_NewString(ctx, ""));
    JS_SetPropertyStr(ctx, element, "outerHTML", JS_NewString(ctx, ""));
    JS_SetPropertyStr(ctx, element, "textContent", JS_NewString(ctx, ""));
    JS_SetPropertyStr(ctx, element, "innerText", JS_NewString(ctx, ""));
    JS_SetPropertyStr(ctx, element, "id", JS_NewString(ctx, ""));
    JS_SetPropertyStr(ctx, element, "className", JS_NewString(ctx, ""));

    /* Dimension stubs */
    JS_SetPropertyStr(ctx, element, "offsetWidth", JS_NewInt32(ctx, 0));
    JS_SetPropertyStr(ctx, element, "offsetHeight", JS_NewInt32(ctx, 0));
    JS_SetPropertyStr(ctx, element, "offsetTop", JS_NewInt32(ctx, 0));
    JS_SetPropertyStr(ctx, element, "offsetLeft", JS_NewInt32(ctx, 0));
    JS_SetPropertyStr(ctx, element, "clientWidth", JS_NewInt32(ctx, 0));
    JS_SetPropertyStr(ctx, element, "clientHeight", JS_NewInt32(ctx, 0));
    JS_SetPropertyStr(ctx, element, "scrollWidth", JS_NewInt32(ctx, 0));
    JS_SetPropertyStr(ctx, element, "scrollHeight", JS_NewInt32(ctx, 0));
    JS_SetPropertyStr(ctx, element, "scrollTop", JS_NewInt32(ctx, 0));
    JS_SetPropertyStr(ctx, element, "scrollLeft", JS_NewInt32(ctx, 0));

    /* Element methods */
    JS_SetPropertyStr(ctx, element, "appendChild", JS_NewCFunction(ctx, js_element_appendChild, "appendChild", 1));
    JS_SetPropertyStr(ctx, element, "removeChild", JS_NewCFunction(ctx, js_element_removeChild, "removeChild", 1));
    JS_SetPropertyStr(ctx, element, "insertBefore", JS_NewCFunction(ctx, js_element_insertBefore, "insertBefore", 2));
    JS_SetPropertyStr(ctx, element, "cloneNode", JS_NewCFunction(ctx, js_element_cloneNode, "cloneNode", 1));
    JS_SetPropertyStr(ctx, element, "getAttribute", JS_NewCFunction(ctx, js_element_getAttribute, "getAttribute", 1));
    JS_SetPropertyStr(ctx, element, "setAttribute", JS_NewCFunction(ctx, js_element_setAttribute, "setAttribute", 2));
    JS_SetPropertyStr(ctx, element, "hasAttribute", JS_NewCFunction(ctx, js_element_hasAttribute, "hasAttribute", 1));
    JS_SetPropertyStr(
        ctx, element, "removeAttribute", JS_NewCFunction(ctx, js_element_removeAttribute, "removeAttribute", 1));
    JS_SetPropertyStr(
        ctx, element, "addEventListener", JS_NewCFunction(ctx, js_element_addEventListener, "addEventListener", 2));
    JS_SetPropertyStr(ctx, element, "removeEventListener",
        JS_NewCFunction(ctx, js_element_removeEventListener, "removeEventListener", 2));

    NSLOG(wisp, DEBUG, "Created element stub with DOM properties, tagName='%s'", tag ? tag : "(null)");

    return element;
}

/* Element method stubs */
static JSValue js_element_appendChild(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    NSLOG(wisp, DEBUG, "element.appendChild() called (stub)");
    if (argc > 0) {
        return JS_DupValue(ctx, argv[0]); /* Return the appended child */
    }
    return JS_UNDEFINED;
}

static JSValue js_element_removeChild(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    NSLOG(wisp, DEBUG, "element.removeChild() called (stub)");
    if (argc > 0) {
        return JS_DupValue(ctx, argv[0]); /* Return the removed child */
    }
    return JS_UNDEFINED;
}

static JSValue js_element_insertBefore(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    NSLOG(wisp, DEBUG, "element.insertBefore() called (stub)");
    if (argc > 0) {
        return JS_DupValue(ctx, argv[0]); /* Return the inserted node */
    }
    return JS_UNDEFINED;
}

static JSValue js_element_cloneNode(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    NSLOG(wisp, DEBUG, "element.cloneNode() called (stub)");
    /* Return a new empty element as a "clone" */
    return create_element_object(ctx, NULL);
}

static JSValue js_element_getAttribute(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    if (argc > 0) {
        const char *name = JS_ToCString(ctx, argv[0]);
        NSLOG(wisp, DEBUG, "element.getAttribute('%s') -> null (stub)", name ? name : "(null)");
        if (name)
            JS_FreeCString(ctx, name);
    }
    return JS_NULL;
}

static JSValue js_element_setAttribute(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    if (argc >= 2) {
        const char *name = JS_ToCString(ctx, argv[0]);
        const char *value = JS_ToCString(ctx, argv[1]);
        NSLOG(wisp, DEBUG, "element.setAttribute('%s', '%s') (stub)", name ? name : "(null)",
            value ? value : "(null)");
        if (name)
            JS_FreeCString(ctx, name);
        if (value)
            JS_FreeCString(ctx, value);
    }
    return JS_UNDEFINED;
}

static JSValue js_element_hasAttribute(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    if (argc > 0) {
        const char *name = JS_ToCString(ctx, argv[0]);
        NSLOG(wisp, DEBUG, "element.hasAttribute('%s') -> false (stub)", name ? name : "(null)");
        if (name)
            JS_FreeCString(ctx, name);
    }
    return JS_FALSE;
}

static JSValue js_element_removeAttribute(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    if (argc > 0) {
        const char *name = JS_ToCString(ctx, argv[0]);
        NSLOG(wisp, DEBUG, "element.removeAttribute('%s') (stub)", name ? name : "(null)");
        if (name)
            JS_FreeCString(ctx, name);
    }
    return JS_UNDEFINED;
}

static JSValue js_element_addEventListener(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    if (argc >= 2) {
        const char *type = JS_ToCString(ctx, argv[0]);
        NSLOG(wisp, DEBUG, "element.addEventListener('%s', fn) (stub)", type ? type : "(null)");
        if (type)
            JS_FreeCString(ctx, type);
    }
    return JS_UNDEFINED;
}

static JSValue js_element_removeEventListener(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    if (argc >= 2) {
        const char *type = JS_ToCString(ctx, argv[0]);
        NSLOG(wisp, DEBUG, "element.removeEventListener('%s', fn) (stub)", type ? type : "(null)");
        if (type)
            JS_FreeCString(ctx, type);
    }
    return JS_UNDEFINED;
}


static JSValue js_document_getElementById(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    if (argc > 0) {
        const char *id = JS_ToCString(ctx, argv[0]);
        NSLOG(wisp, DEBUG, "document.getElementById called with: '%s' -> returning null (stub)", id ? id : "(null)");
        if (id)
            JS_FreeCString(ctx, id);
    } else {
        NSLOG(wisp, DEBUG, "document.getElementById called with no args -> null");
    }
    return JS_NULL;
}

static JSValue js_document_getElementsByTagName(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    if (argc > 0) {
        const char *tag = JS_ToCString(ctx, argv[0]);
        NSLOG(wisp, DEBUG, "document.getElementsByTagName('%s') -> returning empty array (stub)",
            tag ? tag : "(null)");
        if (tag)
            JS_FreeCString(ctx, tag);
    }
    /* Return empty array */
    return JS_NewArray(ctx);
}

static JSValue js_document_getElementsByClassName(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    if (argc > 0) {
        const char *cls = JS_ToCString(ctx, argv[0]);
        NSLOG(wisp, DEBUG, "document.getElementsByClassName('%s') -> returning empty array (stub)",
            cls ? cls : "(null)");
        if (cls)
            JS_FreeCString(ctx, cls);
    }
    /* Return empty array */
    return JS_NewArray(ctx);
}

static JSValue js_document_querySelector(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    if (argc > 0) {
        const char *sel = JS_ToCString(ctx, argv[0]);
        NSLOG(wisp, DEBUG, "document.querySelector('%s') -> returning null (stub)", sel ? sel : "(null)");
        if (sel)
            JS_FreeCString(ctx, sel);
    }
    return JS_NULL;
}

static JSValue js_document_querySelectorAll(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    if (argc > 0) {
        const char *sel = JS_ToCString(ctx, argv[0]);
        NSLOG(wisp, DEBUG, "document.querySelectorAll('%s') -> returning empty array (stub)", sel ? sel : "(null)");
        if (sel)
            JS_FreeCString(ctx, sel);
    }
    /* Return empty array-like object */
    return JS_NewArray(ctx);
}

static JSValue js_document_createElement(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    const char *tag = NULL;
    if (argc > 0) {
        tag = JS_ToCString(ctx, argv[0]);
        NSLOG(wisp, DEBUG, "document.createElement('%s') -> creating element stub", tag ? tag : "(null)");
    } else {
        NSLOG(wisp, DEBUG, "document.createElement() with no args");
    }

    /* Create element with style property and common attributes */
    JSValue element = create_element_object(ctx, tag);

    if (tag)
        JS_FreeCString(ctx, tag);
    return element;
}

static JSValue js_document_createTextNode(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    if (argc > 0) {
        const char *text = JS_ToCString(ctx, argv[0]);
        NSLOG(wisp, DEBUG, "document.createTextNode('%s')", text ? text : "(null)");
        if (text)
            JS_FreeCString(ctx, text);
    }
    /* Return a simple text node object */
    JSValue node = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, node, "nodeType", JS_NewInt32(ctx, 3)); /* TEXT_NODE */
    return node;
}

static JSValue js_document_write(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    if (argc > 0) {
        const char *str = JS_ToCString(ctx, argv[0]);
        NSLOG(wisp, DEBUG, "document.write('%s') (ignored)", str ? str : "(null)");
        if (str)
            JS_FreeCString(ctx, str);
    }
    return JS_UNDEFINED;
}

static JSValue js_document_body_getter(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    NSLOG(wisp, DEBUG, "document.body getter -> returning stub element");
    /* Return an element with style property */
    return create_element_object(ctx, "BODY");
}

static JSValue js_document_documentElement_getter(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    NSLOG(wisp, DEBUG, "document.documentElement getter -> returning stub element");
    return create_element_object(ctx, "HTML");
}

static JSValue js_document_head_getter(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    NSLOG(wisp, DEBUG, "document.head getter -> returning stub element");
    return create_element_object(ctx, "HEAD");
}

static JSValue js_document_readyState_getter(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    NSLOG(wisp, DEBUG, "document.readyState getter -> 'complete'");
    return JS_NewString(ctx, "complete");
}

static JSValue js_document_cookie_getter(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    NSLOG(wisp, DEBUG, "document.cookie getter -> ''");
    return JS_NewString(ctx, "");
}

static JSValue js_document_cookie_setter(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    if (argc > 0) {
        const char *cookie = JS_ToCString(ctx, argv[0]);
        NSLOG(wisp, DEBUG, "document.cookie setter: '%s' (ignored)", cookie ? cookie : "(null)");
        if (cookie)
            JS_FreeCString(ctx, cookie);
    }
    return JS_UNDEFINED;
}

static void define_getter(JSContext *ctx, JSValue obj, const char *name, JSCFunction *func)
{
    JSAtom atom = JS_NewAtom(ctx, name);
    JS_DefinePropertyGetSet(
        ctx, obj, atom, JS_NewCFunction(ctx, func, name, 0), JS_UNDEFINED, JS_PROP_ENUMERABLE | JS_PROP_CONFIGURABLE);
    JS_FreeAtom(ctx, atom);
}

static void
define_getter_setter(JSContext *ctx, JSValue obj, const char *name, JSCFunction *getter, JSCFunction *setter)
{
    JSAtom atom = JS_NewAtom(ctx, name);
    JS_DefinePropertyGetSet(ctx, obj, atom, JS_NewCFunction(ctx, getter, name, 0),
        JS_NewCFunction(ctx, setter, name, 1), JS_PROP_ENUMERABLE | JS_PROP_CONFIGURABLE);
    JS_FreeAtom(ctx, atom);
}

int qjs_init_document(JSContext *ctx)
{
    NSLOG(wisp, DEBUG, "Initializing document binding");

    JSValue global_obj = JS_GetGlobalObject(ctx);
    JSValue document = JS_NewObject(ctx);

    /* Methods */
    JS_SetPropertyStr(
        ctx, document, "getElementById", JS_NewCFunction(ctx, js_document_getElementById, "getElementById", 1));
    JS_SetPropertyStr(ctx, document, "getElementsByTagName",
        JS_NewCFunction(ctx, js_document_getElementsByTagName, "getElementsByTagName", 1));
    JS_SetPropertyStr(ctx, document, "getElementsByClassName",
        JS_NewCFunction(ctx, js_document_getElementsByClassName, "getElementsByClassName", 1));
    JS_SetPropertyStr(
        ctx, document, "querySelector", JS_NewCFunction(ctx, js_document_querySelector, "querySelector", 1));
    JS_SetPropertyStr(
        ctx, document, "querySelectorAll", JS_NewCFunction(ctx, js_document_querySelectorAll, "querySelectorAll", 1));
    JS_SetPropertyStr(
        ctx, document, "createElement", JS_NewCFunction(ctx, js_document_createElement, "createElement", 1));
    JS_SetPropertyStr(
        ctx, document, "createTextNode", JS_NewCFunction(ctx, js_document_createTextNode, "createTextNode", 1));
    JS_SetPropertyStr(ctx, document, "write", JS_NewCFunction(ctx, js_document_write, "write", 1));

    /* Properties */
    define_getter(ctx, document, "body", js_document_body_getter);
    define_getter(ctx, document, "documentElement", js_document_documentElement_getter);
    define_getter(ctx, document, "head", js_document_head_getter);
    define_getter(ctx, document, "readyState", js_document_readyState_getter);
    define_getter_setter(ctx, document, "cookie", js_document_cookie_getter, js_document_cookie_setter);

    /* Attach document to global object and window.document */
    JS_SetPropertyStr(ctx, global_obj, "document", document);

    JS_FreeValue(ctx, global_obj);

    NSLOG(wisp, DEBUG, "Document binding initialized with element stubs");
    return 0;
}

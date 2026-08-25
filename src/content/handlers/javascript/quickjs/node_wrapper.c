#include "content/handlers/javascript/quickjs/node_wrapper.h"
#include "content/handlers/javascript/quickjs/document.h"
#include <wisp/utils/log.h>
#include "dom/core/element.h"
#include "dom/core/exceptions.h"
#include "dom/core/node.h"
#include "dom/core/string.h"
#include "dom/core/document.h"
#include "dom/html/html_element.h"
#include "libwapcaplet/libwapcaplet.h"
#include "quickjs.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

//Only Class ID for the Node class. JS_NewClassID assing a one time.
static JSClassID qjs_node_class_id = 0;

//Internal struct that save the dom_element
typedef struct {
    struct dom_element *element;
} js_dom_node_data_t;

//Finalizer: it is called when the GC from QuickJS free the object.
//Free the reference that we used to have over the dom_node.
static void js_node_finalizer(JSRuntime *rt, JSValue val){
    js_dom_node_data_t *data = JS_GetOpaque(val, qjs_node_class_id);
    if(data){
        if(data->element){
            NSLOG(wisp, DEBUG, "Finalizing DOM node wrapper for element %p", data->element);
            dom_node_unref((struct dom_node *)data->element);
        }
        free(data);
    }
}

//gc_mark: if we would have childs JSValues encode, we would mark here.
//How we save only a dom_node (that it not contain JSValues), this stay empty.

static void js_node_gc_mark(JSRuntime *rt, JSValueConst val, JS_MarkFunc *mark_func){
    (void)rt; (void)val; (void)mark_func;
    //This not have sub-objects JS that mark.
}

//JSClassDef: this describe the class for QuickJS
static const JSClassDef js_node_class_def = {
    .class_name = "Node",
    .finalizer = js_node_finalizer,
    .gc_mark = js_node_gc_mark,
    //.call and .exotic is don´t use for DOM nodes.
};

int qjs_init_node_wrapper(JSContext *ctx){
    JSRuntime *rt = JS_GetRuntime(ctx);
    if(qjs_node_class_id == 0){
        JS_NewClassID(rt, &qjs_node_class_id);
    }

    if(!JS_IsRegisteredClass(rt, qjs_node_class_id)){
        if(JS_NewClass(rt, qjs_node_class_id, &js_node_class_def) < 0){
            NSLOG(wisp, ERROR, "Failed to register DOMNode class");
            return -1;
        }
    }
    NSLOG(wisp, DEBUG, "DOMNode class initialized");
    return 0;
}

//Wrap / Unwrap
JSValue qjs_wrap_dom_element(JSContext *ctx, struct dom_element *el, const char *tag){
    if(!el){
        return JS_NULL;
    }
    //Create the JSObject with the DOMNode Class.
    JSValue obj = JS_NewObjectClass(ctx, qjs_node_class_id);
    if(JS_IsException(obj)){
        dom_node_unref((struct dom_node *)el);
        return JS_EXCEPTION;
    }

    //Create and allocate the internal structure.
    js_dom_node_data_t *data = calloc(1, sizeof(js_dom_node_data_t));
    if(!data){
        JS_FreeValue(ctx, obj);
        dom_node_unref((struct dom_node *)el);
        return JS_EXCEPTION;
    }
    data->element = el;
    JS_SetOpaque(obj, data);

    //Get the real ID of the element
    struct dom_string *id_str = NULL;
    if(dom_html_element_get_id(el, &id_str) == DOM_NO_ERR && id_str){
        const char *id_cstr = dom_string_data(id_str);
        if(id_cstr){
            JS_SetPropertyStr(ctx, obj, "id", JS_NewString(ctx, id_cstr));
            dom_string_unref(id_str);
        } else {
            JS_SetPropertyStr(ctx, obj, "id", JS_NewString(ctx, ""));
        }
    }

    //Get the className
    struct dom_string *class_str = NULL;

    if(dom_html_element_get_class_name(el, &class_str) == DOM_NO_ERR && class_str){
        const char *class_cstr = dom_string_data(class_str);
        if(class_cstr){
            JS_SetPropertyStr(ctx, obj, "class", JS_NewString(ctx, class_cstr));
        }
        dom_string_unref(class_str);
    } else {
        JS_SetPropertyStr(ctx, obj, "class", JS_NewString(ctx, ""));
    }

    obj = create_element_object(ctx, tag, obj);
    return obj;
}

struct dom_element *qjs_unwrap_dom_element(JSContext *ctx, JSValueConst obj){
    if (JS_VALUE_GET_TAG(obj) != JS_TAG_OBJECT){
        return NULL;
    }
    js_dom_node_data_t *data = JS_GetOpaque(obj, qjs_node_class_id);
    if (data == NULL) {
        return NULL;
    }
    return data->element;
}


//Prototype Node methods.

//Getter from nodeType

JSValue js_node_nodeType_getter(JSContext *ctx, JSValueConst this_val){
    struct dom_node *n = (dom_node *)qjs_unwrap_dom_element(ctx, this_val);
    if (n == NULL){
        return JS_UNDEFINED;
    }
    dom_node_type t = 0;
    dom_node_get_node_type((dom_node *)n, &t);
    return JS_NewInt32(ctx, (int32_t)t);
}

//Getter from nodeName
/*
static JSValue js_node_nodeName_getter(JSContext *ctx, JSValueConst this_val){
    struct dom_node *n = qjs_unwrap_dom_node(ctx, this_val);
    if(n == NULL){
        return JS_NewString(ctx, "");
    }
    struct dom_string *s = NULL;
    //Get content from dom_string
    const char *cstr = dom_string_data(s);
    JSValue r = JS_NewString(ctx, cstr);
    dom_string_unref(s);
    return r;
}
*/

/*
struct dom_string js_node_id_getter(JSContext *ctx, JSValueConst this_val){
    struct dom_node *n = (dom_node *)qjs_unwrap_dom_element(ctx, this_val);
    struct dom_node_type *nt = 0;
    dom_node_get_node_type(n, nt);

    if(n == NULL || (int *)nt != (int *)DOM_ELEMENT_NODE){
        return JS_NewString(ctx, "");
    }
    struct dom_string *s = NULL;
    struct dom_document *doc;
    dom_document_get_element_by_id(doc, s, struct dom_element **result)
    if( != DOM_NO_ERR || s == NULL){

    }
}
*/

#ifndef WISP_QUICKJS_NODE_H
#define WISP_QUICKJS_NODE_H
#include "dom/core/string.h"
#include "quickjs.h"
#include <dom/dom.h>
enum TYPE_STRING{
    ID,
    CLASS,
};
//Init ClassJS "Node" in the runtime.
//Must called a one time for JSContext.
int qjs_init_node_wrapper(JSContext *ctx);

//Wrap a dom_element in a JSObject
//The resulting object have every propertys of create_element_object()
//more a internal reference to real dom_element.
JSValue qjs_wrap_dom_element(JSContext *ctx, struct dom_element *el, const char *tag);

//Recovery the dom_element since a JSObject wrap.
struct dom_element *qjs_unwrap_dom_element(JSContext *ctx, JSValueConst val);
//Get nodeType
JSValue js_node_nodeType_getter(JSContext *ctx, JSValueConst this_val);
#endif

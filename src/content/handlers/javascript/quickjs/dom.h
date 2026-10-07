/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef WISP_QUICKJS_DOM_H
#define WISP_QUICKJS_DOM_H

#include <dom/dom.h>
#include <dom/core/tokenlist.h>
#include "private.h"

enum qjs_dom_prototype {
    QJS_NODE,
    QJS_ELEMENT,
    QJS_HTML_ELEMENT,
    QJS_DOCUMENT,
    QJS_HTML_DOCUMENT,
    QJS_CHARACTER_DATA,
    QJS_TEXT,
    QJS_COMMENT,
    QJS_DOCUMENT_TYPE,
    QJS_DOCUMENT_FRAGMENT,
    QJS_DOM_PROTOTYPES
};

struct qjs_dom_node {
    dom_node *node;
    dom_node_type type;
    uint64_t id;
    dom_tokenlist *class_tokens;
    JSValue event_target; /* Lazy listener storage; Node inherits EventTarget. */
    JSValue class_list;
    JSValue child_lists[2]; /* SameObject childNodes and children, allocated lazily. */
    JSValue owner; /* Document wrapper; undefined on the document itself. */
    struct qjs_dom_document *document;
    struct qjs_dom_node *next, **previous;
};

/* Brand-check and connect the document lazily (the parser creates the context
 * before publishing its document pointer). type == 0 accepts every Node. */
struct qjs_dom_node *qjs_dom_get(JSContext *ctx, JSValueConst object, dom_node_type type);
JSValue qjs_dom_owner(JSContext *ctx, JSValueConst object, struct qjs_dom_node *node);
JSValue qjs_dom_wrap(JSContext *ctx, JSValueConst document, dom_node *node);
JSValue qjs_dom_exception(JSContext *ctx, dom_exception error);
dom_string *qjs_dom_string(JSContext *ctx, JSValueConst value, bool nullable);
JSValue qjs_dom_from_string(JSContext *ctx, dom_string *value, bool nullable);
bool qjs_dom_is_html(dom_node *node);
void qjs_dom_changed(
    JSContext *ctx, struct qjs_dom_node *node, const char *operation, dom_string *attribute, dom_exception error);
void qjs_dom_flush(jsthread *thread);
void qjs_dom_close(jsthread *thread);
int qjs_dom_install_collections(JSContext *ctx, JSValue *prototypes);
int qjs_dom_property_index(JSContext *ctx, JSAtom key, uint32_t *index);
int qjs_dom_install_iteration(JSContext *ctx, JSValueConst proto, JSValueConst global, const char *name, bool iterable);
int qjs_init_tokenlist(JSContext *ctx);
JSValue qjs_class_list_get(JSContext *ctx, JSValueConst object);
JSValue qjs_class_list_set(JSContext *ctx, JSValueConst object, JSValueConst value);
int qjs_dom_install_properties(JSContext *ctx, JSValue *prototypes);
int qjs_dom_install_tree(JSContext *ctx, JSValue *prototypes);

#endif

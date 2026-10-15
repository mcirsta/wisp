/* SPDX-License-Identifier: GPL-2.0-or-later
 * https://html.spec.whatwg.org/multipage/dynamic-markup-insertion.html#the-innerhtml-property
 * The historical Duktape setter informed the libdom calls, not the parsing or
 * ownership model. Conversion and the complete inert parse precede replacement.
 */
#include "dom.h"
#include <libdom/bindings/hubbub/parser.h>

static JSValue inner_html_get(JSContext *ctx, JSValueConst object)
{
    struct qjs_dom_node *self = qjs_dom_get(ctx, object, DOM_ELEMENT_NODE);
    if (self == NULL || !qjs_execution_check(ctx))
        return JS_EXCEPTION;
    dom_string *markup = NULL;
    /* Documents without a browsing context have scripting disabled. */
    dom_exception err = dom_html_serialize_fragment(
        self->node, qjs_dom_is_active_document(ctx, self->node), &markup);
    JSValue result = err != DOM_NO_ERR ? qjs_dom_exception(ctx, err)
        : !qjs_execution_check(ctx)    ? JS_EXCEPTION
                                       : qjs_dom_from_string(ctx, markup, false);
    dom_string_unref(markup);
    return result;
}

static JSValue parser_exception(JSContext *ctx, dom_hubbub_error error)
{
    if (error == DOM_HUBBUB_NOMEM || error == DOM_HUBBUB_HUBBUB_ERR_NOMEM)
        return JS_ThrowOutOfMemory(ctx);
    if (error == DOM_HUBBUB_HUBBUB_ERR_INVALID)
        return qjs_dom_exception(ctx, DOM_NOT_SUPPORTED_ERR);
    /* Malformed HTML is recovered by the parser, not a SyntaxError. */
    return JS_ThrowInternalError(ctx, "HTML fragment parser failed (%u)", error);
}

static JSValue inner_html_set(JSContext *ctx, JSValueConst object, JSValueConst value)
{
    if (qjs_dom_get(ctx, object, DOM_ELEMENT_NODE) == NULL)
        return JS_EXCEPTION;
    /* LegacyNullToEmptyString applies to null, not undefined. */
    dom_string *markup = qjs_dom_string(ctx, value, JS_IsNull(value));
    if (markup == NULL)
        return JS_EXCEPTION;
    struct qjs_dom_node *self = qjs_dom_get(ctx, object, DOM_ELEMENT_NODE);
    if (self == NULL || !qjs_execution_check(ctx)) {
        dom_string_unref(markup);
        return JS_EXCEPTION;
    }
    dom_document_fragment *contents = NULL, *fragment = NULL;
    dom_exception err = dom_html_template_element_get_content((dom_html_template_element *)self->node, &contents);
    if (err != DOM_NO_ERR && err != DOM_NOT_SUPPORTED_ERR) {
        dom_string_unref(markup);
        return qjs_dom_exception(ctx, err);
    }
    dom_node *target = contents != NULL ? (dom_node *)contents : self->node;
    /* Scripting syntax follows the context document, while all fragment scripts
     * are inert. The parser uses the actual template contents owner for nodes. */
    dom_hubbub_parser_params params = {.enable_script = qjs_dom_is_active_document(ctx, self->node)};
    dom_hubbub_parser *parser = NULL;
    dom_hubbub_error error = dom_hubbub_fragment_parser_create(&params, (dom_element *)self->node, &parser, &fragment);
    bool running = true;
    size_t length = dom_string_byte_length(markup);
    for (size_t offset = 0; error == DOM_HUBBUB_OK && offset < length;) {
        if (!(running = qjs_execution_check(ctx)))
            break;
        size_t take = length - offset;
        if (take > 16384)
            take = 16384;
        error = dom_hubbub_parser_parse_chunk(parser, (const uint8_t *)dom_string_data(markup) + offset, take);
        offset += take;
    }
    if (error == DOM_HUBBUB_OK && running)
        error = dom_hubbub_parser_completed(parser);
    if (parser != NULL)
        dom_hubbub_parser_destroy(parser);
    dom_string_unref(markup);
    JSValue result;
    if (!running) {
        result = JS_EXCEPTION;
    } else if (error != DOM_HUBBUB_OK) {
        qjs_trace(ctx, "dom.parse", "operation=innerHTML result=%u", error);
        result = parser_exception(ctx, error);
    } else if (!qjs_execution_check(ctx)) {
        result = JS_EXCEPTION;
    } else {
        err = dom_node_replace_all(target, (dom_node *)fragment);
        qjs_dom_changed_target(ctx, self, target, "innerHTML", NULL, err);
        result = err == DOM_NO_ERR ? JS_UNDEFINED : qjs_dom_exception(ctx, err);
    }
    dom_node_unref(fragment);
    dom_node_unref(contents);
    return result;
}

int qjs_dom_install_markup(JSContext *ctx, JSValue *prototypes)
{
    static const JSCFunctionListEntry properties[] = {
        JS_CGETSET_DEF2("innerHTML", inner_html_get, inner_html_set, JS_PROP_CONFIGURABLE | JS_PROP_ENUMERABLE),
    };
    return JS_SetPropertyFunctionList(
        ctx, prototypes[QJS_ELEMENT], properties, sizeof(properties) / sizeof(*properties));
}

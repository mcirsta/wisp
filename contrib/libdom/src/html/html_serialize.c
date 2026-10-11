/* SPDX-License-Identifier: MIT
 * https://html.spec.whatwg.org/multipage/parsing.html#serialising-html-fragments
 */
#include <stdlib.h>
#include <string.h>
#include <dom/dom.h>
#include "core/document.h"
#include "core/element.h"
#include "html/html_template_element.h"

struct serializer {
    uint8_t *data;
    size_t length, capacity;
    dom_exception error;
    bool scripting;
};

static void append(struct serializer *s, const void *data, size_t length)
{
    if (s->error != DOM_NO_ERR || length == 0)
        return;
    /* Leave room for the terminator added by dom_string_create. */
    if (length > SIZE_MAX - s->length - 1) {
        s->error = DOM_NO_MEM_ERR;
        return;
    }
    size_t need = s->length + length;
    if (need > s->capacity) {
        size_t capacity = s->capacity ? s->capacity : 256;
        while (capacity < need) {
            if (capacity > SIZE_MAX / 2) {
                capacity = need;
                break;
            }
            capacity *= 2;
        }
        uint8_t *buffer = realloc(s->data, capacity);
        if (buffer == NULL) {
            s->error = DOM_NO_MEM_ERR;
            return;
        }
        s->data = buffer;
        s->capacity = capacity;
    }
    memcpy(s->data + s->length, data, length);
    s->length += length;
}

static void literal(struct serializer *s, const char *text)
{
    append(s, text, strlen(text));
}

static void string(struct serializer *s, const dom_string *text)
{
    if (text != NULL)
        append(s, dom_string_data(text), dom_string_byte_length(text));
}

static bool name_is(const dom_node_internal *node, const char *name)
{
    size_t length = strlen(name);
    return dom_string_byte_length(node->name) == length && memcmp(dom_string_data(node->name), name, length) == 0;
}

static bool in_namespace(const dom_node_internal *node, dom_namespace ns)
{
    return node->namespace != NULL && dom_string_isequal(node->namespace, dom_namespaces[ns]);
}

static bool serializes_as_void(const dom_node_internal *node)
{
    if (node->type != DOM_ELEMENT_NODE || !in_namespace(node, DOM_NAMESPACE_HTML))
        return false;
    switch (_dom_html_node_type(node)) {
    case DOM_HTML_ELEMENT_TYPE_AREA:
    case DOM_HTML_ELEMENT_TYPE_BASE:
    case DOM_HTML_ELEMENT_TYPE_BASEFONT:
    case DOM_HTML_ELEMENT_TYPE_BGSOUND:
    case DOM_HTML_ELEMENT_TYPE_BR:
    case DOM_HTML_ELEMENT_TYPE_COL:
    case DOM_HTML_ELEMENT_TYPE_EMBED:
    case DOM_HTML_ELEMENT_TYPE_FRAME:
    case DOM_HTML_ELEMENT_TYPE_HR:
    case DOM_HTML_ELEMENT_TYPE_IMG:
    case DOM_HTML_ELEMENT_TYPE_INPUT:
    case DOM_HTML_ELEMENT_TYPE_KEYGEN:
    case DOM_HTML_ELEMENT_TYPE_LINK:
    case DOM_HTML_ELEMENT_TYPE_META:
    case DOM_HTML_ELEMENT_TYPE_PARAM:
    case DOM_HTML_ELEMENT_TYPE_SOURCE:
    case DOM_HTML_ELEMENT_TYPE_TRACK:
    case DOM_HTML_ELEMENT_TYPE_WBR:
        return true;
    default:
        return false;
    }
}

static bool literal_text(const dom_node_internal *parent, bool scripting)
{
    if (parent == NULL || parent->type != DOM_ELEMENT_NODE || !in_namespace(parent, DOM_NAMESPACE_HTML))
        return false;
    if (name_is(parent, "noscript"))
        return scripting && !parent->owner->is_template_document;
    return name_is(parent, "style") || name_is(parent, "script") || name_is(parent, "xmp") ||
        name_is(parent, "iframe") || name_is(parent, "noembed") || name_is(parent, "noframes") ||
        name_is(parent, "plaintext");
}

static void escaped(struct serializer *s, const dom_string *value, bool attribute)
{
    if (value == NULL)
        return;
    const uint8_t *text = (const uint8_t *)dom_string_data(value);
    size_t length = dom_string_byte_length(value), start = 0;
    for (size_t i = 0; i < length && s->error == DOM_NO_ERR; i++) {
        const char *replacement = NULL;
        size_t consumed = 1;
        switch (text[i]) {
        case '&':
            replacement = "&amp;";
            break;
        case '<':
            replacement = "&lt;";
            break;
        case '>':
            replacement = "&gt;";
            break;
        case '"':
            if (attribute)
                replacement = "&quot;";
            break;
        case 0xc2:
            if (i + 1 < length && text[i + 1] == 0xa0) {
                replacement = "&nbsp;";
                consumed = 2;
            }
            break;
        }
        if (replacement != NULL) {
            append(s, text + start, i - start);
            literal(s, replacement);
            i += consumed - 1;
            start = i + 1;
        }
    }
    append(s, text + start, length - start);
}

/* HTML/SVG/MathML use the local name. Other namespaces retain the prefix.
 * Do not use nodeName/tagName, which uppercase HTML names in HTML documents. */
static void element_name(struct serializer *s, const dom_node_internal *node)
{
    if (!in_namespace(node, DOM_NAMESPACE_HTML) && !in_namespace(node, DOM_NAMESPACE_SVG) &&
        !in_namespace(node, DOM_NAMESPACE_MATHML) && node->prefix != NULL) {
        string(s, node->prefix);
        literal(s, ":");
    }
    string(s, node->name);
}

static dom_exception attribute(dom_attr *attr, void *pw)
{
    struct serializer *s = pw;
    const dom_node_internal *node = (const dom_node_internal *)attr;
    literal(s, " ");
    if (in_namespace(node, DOM_NAMESPACE_XML))
        literal(s, "xml:");
    else if (in_namespace(node, DOM_NAMESPACE_XLINK))
        literal(s, "xlink:");
    else if (in_namespace(node, DOM_NAMESPACE_XMLNS)) {
        if (!name_is(node, "xmlns"))
            literal(s, "xmlns:");
    } else if (node->namespace != NULL && node->prefix != NULL) {
        string(s, node->prefix);
        literal(s, ":");
    }
    string(s, node->name);
    literal(s, "=\"");
    if (s->error != DOM_NO_ERR)
        return s->error;
    dom_string *value = NULL;
    dom_exception err = dom_attr_get_value(attr, &value);
    if (err != DOM_NO_ERR)
        return err;
    escaped(s, value, true);
    dom_string_unref(value);
    literal(s, "\"");
    return s->error;
}

static void enter(struct serializer *s, const dom_node_internal *node)
{
    switch (node->type) {
    case DOM_ELEMENT_NODE:
        literal(s, "<");
        element_name(s, node);
        if (s->error == DOM_NO_ERR)
            s->error = _dom_element_foreach_attribute((const dom_element *)node, attribute, s);
        literal(s, ">");
        break;
    case DOM_TEXT_NODE:
    case DOM_CDATA_SECTION_NODE: /* CDATASection inherits Text. */
        if (literal_text(node->parent, s->scripting))
            string(s, node->value);
        else
            escaped(s, node->value, false);
        break;
    case DOM_COMMENT_NODE:
        literal(s, "<!--");
        string(s, node->value);
        literal(s, "-->");
        break;
    case DOM_PROCESSING_INSTRUCTION_NODE:
        literal(s, "<?");
        string(s, node->name);
        literal(s, " ");
        string(s, node->value);
        literal(s, "?>");
        break;
    case DOM_DOCUMENT_TYPE_NODE:
        literal(s, "<!DOCTYPE ");
        string(s, node->name);
        literal(s, ">");
        break;
    default:
        s->error = DOM_NOT_SUPPORTED_ERR;
    }
}

static const dom_node_internal *content_root(const dom_node_internal *node)
{
    if (_dom_html_is_template(node))
        return (const dom_node_internal *)((const dom_html_template_element *)node)->content;
    return node;
}

dom_exception dom_html_serialize_fragment(dom_node *node, bool scripting, dom_string **result)
{
    if (result == NULL)
        return DOM_INVALID_ACCESS_ERR;
    *result = NULL;
    if (node == NULL)
        return DOM_INVALID_ACCESS_ERR;
    const dom_node_internal *root = (const dom_node_internal *)node;
    if (root->type != DOM_ELEMENT_NODE && root->type != DOM_DOCUMENT_NODE && root->type != DOM_DOCUMENT_FRAGMENT_NODE)
        return DOM_NOT_SUPPORTED_ERR;
    if (serializes_as_void(root))
        return dom_string_create(NULL, 0, result);
    root = content_root(root);
    struct serializer s = {.scripting = scripting};
    const dom_node_internal *current = root->first_child;
    while (current != NULL && s.error == DOM_NO_ERR) {
        enter(&s, current);
        if (current->type == DOM_ELEMENT_NODE && !serializes_as_void(current)) {
            const dom_node_internal *child = content_root(current)->first_child;
            if (child != NULL) {
                current = child;
                continue;
            }
        }
        /* Read-only traversal needs no recursive C calls or per-node stack.
         * Cross a contents fragment's host link only when it is inside this
         * traversal: serializing the fragment itself must stop at that root. */
        while (current != NULL && s.error == DOM_NO_ERR) {
            if (current->type == DOM_ELEMENT_NODE && !serializes_as_void(current)) {
                literal(&s, "</");
                element_name(&s, current);
                literal(&s, ">");
            }
            if (current->next != NULL) {
                current = current->next;
                break;
            }
            current = current->parent;
            if (current == root)
                current = NULL;
            else if (current != NULL && current->type == DOM_DOCUMENT_FRAGMENT_NODE)
                current = ((const dom_document_fragment *)current)->host;
        }
    }
    dom_exception err = s.error;
    if (err == DOM_NO_ERR)
        err = dom_string_create(s.data, s.length, result);
    free(s.data);
    return err;
}

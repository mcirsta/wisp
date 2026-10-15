/*
 * This file is part of libdom.
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 * Copyright 2007 John-Mark Bell <jmb@netsurf-browser.org>
 * Copyright 2009 Bo Yang <struggleyb.nku@gmail.com>
 */

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <dom/dom.h>

#include <dom/core/document.h>
#include <dom/core/node.h>
#include <dom/core/nodelist.h>
#include <dom/core/string.h>

#include "core/document.h"
#include "core/element.h"
#include "core/node.h"
#include "core/nodelist.h"

#include "utils/utils.h"

struct class_token {
    size_t start, length;
};

/**
 * DOM node list
 */
struct dom_nodelist {
    dom_document *owner; /**< Owning document */

    dom_node_internal *root;
    /**< Root of applicable subtree */

    nodelist_type type; /**< Type of this list */

    union {
        struct {
            dom_string *name;
            dom_string *lower; /**< ASCII-lowercased HTML query, not an element name. */
            /**< Tag name to match */
            bool any_name; /**< The name is '*' */
        } n;
        struct {
            bool any_namespace; /**< The namespace is '*' */
            bool any_localname; /**< The localname is '*' */
            dom_string *namespace; /**< Namespace */
            dom_string *localname; /**< Localname */
        } ns; /**< Data for namespace matching */
        struct {
            dom_string *query;
            struct class_token *tokens;
            size_t count;
        } classes;
    } data;

    uint32_t refcnt; /**< Reference count */
};

static bool class_space(unsigned char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
}

/* Store slices of the retained query: no string allocation or interning while
 * visiting elements. Duplicate query tokens need not be matched twice. */
static dom_exception class_query_init(dom_nodelist *list, dom_string *query)
{
    const char *bytes = dom_string_data(query);
    size_t length = dom_string_byte_length(query), capacity = 0;
    for (size_t i = 0; i < length; i++) {
        if (!class_space(bytes[i]) && (i == 0 || class_space(bytes[i - 1])))
            capacity++;
    }
    if (capacity != 0) {
        list->data.classes.tokens = calloc(capacity, sizeof(struct class_token));
        if (list->data.classes.tokens == NULL)
            return DOM_NO_MEM_ERR;
    }
    for (size_t i = 0; i < length;) {
        if (class_space(bytes[i])) {
            i++;
            continue;
        }
        size_t start = i;
        while (i < length && !class_space(bytes[i]))
            i++;
        size_t n = i - start, j;
        for (j = 0; j < list->data.classes.count; j++) {
            struct class_token token = list->data.classes.tokens[j];
            if (token.length == n && memcmp(bytes + token.start, bytes + start, n) == 0)
                break;
        }
        if (j == list->data.classes.count)
            list->data.classes.tokens[list->data.classes.count++] = (struct class_token){start, n};
    }
    list->data.classes.query = dom_string_ref(query);
    return DOM_NO_ERR;
}

/**
 * Create a nodelist
 *
 * \param doc        Owning document
 * \param type	     The type of the NodeList
 * \param root       Root node of subtree that list applies to
 * \param tagname    Name of nodes in list (or NULL)
 * \param namespace  Namespace part of nodes in list (or NULL)
 * \param localname  Local part of nodes in list (or NULL)
 * \param list       Pointer to location to receive list
 * \return DOM_NO_ERR on success, DOM_NO_MEM_ERR on memory exhaustion
 *
 * ::root must be a node owned by ::doc
 *
 * The returned list will already be referenced, so the client need not
 * do so explicitly. The client must unref the list once finished with it.
 */
dom_exception _dom_nodelist_create(dom_document *doc, nodelist_type type, dom_node_internal *root, dom_string *tagname,
    dom_string *namespace, dom_string *localname, dom_nodelist **list)
{
    dom_nodelist *l;

    l = calloc(1, sizeof(dom_nodelist));
    if (l == NULL)
        return DOM_NO_MEM_ERR;

    dom_node_ref(doc);
    l->owner = doc;

    dom_node_ref(root);
    l->root = root;

    l->type = type;

    if (type == DOM_NODELIST_BY_NAME) {
        assert(tagname != NULL);
        l->data.n.any_name = false;
        if (dom_string_byte_length(tagname) == 1) {
            const char *ch = dom_string_data(tagname);
            if (*ch == '*') {
                l->data.n.any_name = true;
            }
        }

        l->data.n.name = dom_string_ref(tagname);
        if (doc->html && !l->data.n.any_name) {
            dom_exception err = dom_string_tolower(tagname, true, &l->data.n.lower);
            if (err != DOM_NO_ERR) {
                dom_string_unref(l->data.n.name);
                dom_node_unref(root);
                dom_node_unref(doc);
                free(l);
                return err;
            }
        }
    } else if (type == DOM_NODELIST_BY_CLASS) {
        dom_exception err = class_query_init(l, tagname);
        if (err != DOM_NO_ERR) {
            dom_node_unref(root);
            dom_node_unref(doc);
            free(l);
            return err;
        }
    } else if (type == DOM_NODELIST_BY_NAMESPACE) {
        l->data.ns.any_localname = false;
        l->data.ns.any_namespace = false;
        if (localname != NULL) {
            if (dom_string_byte_length(localname) == 1) {
                const char *ch = dom_string_data(localname);
                if (*ch == '*') {
                    l->data.ns.any_localname = true;
                }
            }
            dom_string_ref(localname);
        }
        if (namespace != NULL) {
            if (dom_string_byte_length(namespace) == 1) {
                const char *ch = dom_string_data(namespace);
                if (*ch == '*') {
                    l->data.ns.any_namespace = true;
                }
            }
            dom_string_ref(namespace);
        }

        l->data.ns.namespace = namespace;
        l->data.ns.localname = localname;
    }

    l->refcnt = 1;

    *list = l;

    return DOM_NO_ERR;
}

/**
 * Claim a reference on a DOM node list
 *
 * \param list  The list to claim a reference on
 */
void dom_nodelist_ref(dom_nodelist *list)
{
    assert(list != NULL);
    list->refcnt++;
}

/**
 * Release a reference on a DOM node list
 *
 * \param list  The list to release the reference from
 *
 * If the reference count reaches zero, any memory claimed by the
 * list will be released
 */
void dom_nodelist_unref(dom_nodelist *list)
{
    if (list == NULL)
        return;

    if (--list->refcnt == 0) {
        dom_node_internal *owner = (dom_node_internal *)list->owner;
        switch (list->type) {
        case DOM_NODELIST_CHILDREN:
            /* Nothing to do */
            break;
        case DOM_NODELIST_BY_NAMESPACE:
            if (list->data.ns.namespace != NULL)
                dom_string_unref(list->data.ns.namespace);
            if (list->data.ns.localname != NULL)
                dom_string_unref(list->data.ns.localname);
            break;
        case DOM_NODELIST_BY_CLASS:
            dom_string_unref(list->data.classes.query);
            free(list->data.classes.tokens);
            break;
        case DOM_NODELIST_BY_NAME:
            assert(list->data.n.name != NULL);
            dom_string_unref(list->data.n.name);
            dom_string_unref(list->data.n.lower);
            break;
        }

        dom_node_unref(list->root);

        /* Remove list from document */
        _dom_document_remove_nodelist(list->owner, list);

        /* Destroy the list object */
        free(list);

        /* And release our reference on the owning document
         * This must be last as, otherwise, it's possible that
         * the document is destroyed before we are */
        dom_node_unref(owner);
    }
}

/* Compare the qualified name without allocating prefix:local strings. */
static bool qualified_name_matches(const dom_node_internal *node, dom_string *name)
{
    if (node->prefix == NULL)
        return dom_string_isequal(node->name, name);
    size_t prefix = dom_string_byte_length(node->prefix), local = dom_string_byte_length(node->name);
    if (dom_string_byte_length(name) != prefix + 1 + local)
        return false;
    const char *bytes = dom_string_data(name);
    return memcmp(bytes, dom_string_data(node->prefix), prefix) == 0 && bytes[prefix] == ':' &&
        memcmp(bytes + prefix + 1, dom_string_data(node->name), local) == 0;
}

static bool class_token_matches(const char *a, const char *b, size_t length, bool quirks)
{
    if (!quirks)
        return memcmp(a, b, length) == 0;
    for (size_t i = 0; i < length; i++) {
        unsigned char x = a[i], y = b[i];
        if (x >= 'A' && x <= 'Z')
            x += 'a' - 'A';
        if (y >= 'A' && y <= 'Z')
            y += 'a' - 'A';
        if (x != y)
            return false;
    }
    return true;
}

static bool class_query_matches(const dom_nodelist *list, const dom_element *element)
{
    if (list->data.classes.count == 0)
        return false;
    /* Read the root's current document, including after subtree adoption.
     * Limited quirks does not make class matching case-insensitive. */
    bool quirks = list->root->owner->quirks == DOM_DOCUMENT_QUIRKS_MODE_FULL;
    const char *query = dom_string_data(list->data.classes.query);
    for (size_t i = 0; i < list->data.classes.count; i++) {
        struct class_token token = list->data.classes.tokens[i];
        uint32_t j;
        for (j = 0; j < element->n_classes; j++) {
            lwc_string *name = element->classes[j];
            if (lwc_string_length(name) == token.length &&
                class_token_matches(query + token.start, lwc_string_data(name), token.length, quirks))
                break;
        }
        if (j == element->n_classes)
            return false;
    }
    return true;
}

/* DOM's list-of-elements-with-class-names algorithm uses the same live walk as
 * tag queries. The JS binding presents it as an HTMLCollection. */
dom_exception dom_node_get_elements_by_class_name(dom_node *root, dom_string *classes, dom_nodelist **result)
{
    dom_node_internal *node = (dom_node_internal *)root;
    *result = NULL;
    if (node->type != DOM_DOCUMENT_NODE && node->type != DOM_ELEMENT_NODE)
        return DOM_NOT_SUPPORTED_ERR;
    return _dom_document_get_nodelist(node->owner, DOM_NODELIST_BY_CLASS, node, classes, NULL, NULL, result);
}

/* Shared by length, item and the one-pass visitor: all observe the same live
 * filter. DOM's tag query folds the query only for HTML-namespace elements in
 * an HTML document; namespace/local-name queries are always exact. */
static bool nodelist_matches(const dom_nodelist *list, const dom_node_internal *node)
{
    if (list->type == DOM_NODELIST_CHILDREN)
        return true;
    if (node->type != DOM_ELEMENT_NODE)
        return false;
    if (list->type == DOM_NODELIST_BY_CLASS)
        return class_query_matches(list, (const dom_element *)node);
    if (list->type == DOM_NODELIST_BY_NAME) {
        if (list->data.n.any_name)
            return true;
        bool html = list->data.n.lower != NULL && node->namespace != NULL &&
            dom_string_isequal(node->namespace, dom_namespaces[DOM_NAMESPACE_HTML]);
        return qualified_name_matches(node, html ? list->data.n.lower : list->data.n.name);
    }
    return (list->data.ns.any_namespace || dom_string_isequal(node->namespace, list->data.ns.namespace)) &&
        (list->data.ns.any_localname || dom_string_isequal(node->name, list->data.ns.localname));
}

static dom_node_internal *nodelist_next(const dom_nodelist *list, dom_node_internal *node)
{
    if (list->type == DOM_NODELIST_CHILDREN)
        return node->next;
    if (node->first_child != NULL)
        return node->first_child;
    while (node != list->root) {
        if (node->next != NULL)
            return node->next;
        node = node->parent;
    }
    return NULL;
}

/** Visit borrowed matching nodes once, in tree order; false stops the walk.
 * The visitor must not mutate the DOM or release the list. No cursor persists
 * across calls, so parser writes and detached subtree edits are immediately live. */
void dom_nodelist_foreach(dom_nodelist *list, dom_nodelist_callback callback, void *ctx)
{
    for (dom_node_internal *node = list->root->first_child; node != NULL; node = nodelist_next(list, node)) {
        if (nodelist_matches(list, node) && !callback((dom_node *)node, ctx))
            break;
    }
}

dom_exception dom_nodelist_get_length(dom_nodelist *list, uint32_t *length)
{
    *length = 0;
    for (dom_node_internal *node = list->root->first_child; node != NULL; node = nodelist_next(list, node)) {
        if (nodelist_matches(list, node))
            (*length)++;
    }
    return DOM_NO_ERR;
}

/* Return a referenced node, or NULL for an out-of-range index. Test the index
 * only on matching nodes, without index + 1 wrapping for UINT32_MAX. */
dom_exception _dom_nodelist_item(dom_nodelist *list, uint32_t index, dom_node **result)
{
    *result = NULL;
    for (dom_node_internal *node = list->root->first_child; node != NULL; node = nodelist_next(list, node)) {
        if (nodelist_matches(list, node) && index-- == 0) {
            *result = (dom_node *)dom_node_ref(node);
            break;
        }
    }
    return DOM_NO_ERR;
}

/**
 * Match a nodelist instance against a set of nodelist creation parameters
 *
 * \param list       List to match
 * \param type	     The type of the NodeList
 * \param root       Root node of subtree that list applies to
 * \param tagname    Name of nodes in list (or NULL)
 * \param namespace  Namespace part of nodes in list (or NULL)
 * \param localname  Local part of nodes in list (or NULL)
 * \return true if list matches, false otherwise
 */
bool _dom_nodelist_match(dom_nodelist *list, nodelist_type type, dom_node_internal *root, dom_string *tagname,
    dom_string *namespace, dom_string *localname)
{
    if (list->root != root)
        return false;

    if (list->type != type)
        return false;

    switch (list->type) {
    case DOM_NODELIST_CHILDREN:
        return true;
    case DOM_NODELIST_BY_CLASS:
        return dom_string_isequal(list->data.classes.query, tagname);
    case DOM_NODELIST_BY_NAME:
        return dom_string_isequal(list->data.n.name, tagname);
    case DOM_NODELIST_BY_NAMESPACE:
        return dom_string_isequal(list->data.ns.namespace, namespace) &&
            dom_string_isequal(list->data.ns.localname, localname);
    }

    return false;
}

/**
 * Test whether the two NodeList are equal
 *
 * \param l1  One list
 * \param l2  The other list
 * \reutrn true for equal, false otherwise.
 */
bool _dom_nodelist_equal(dom_nodelist *l1, dom_nodelist *l2)
{
    if (l1->type != l2->type)
        return false;
    if (l2->type == DOM_NODELIST_BY_CLASS)
        return _dom_nodelist_match(l1, l2->type, l2->root, l2->data.classes.query, NULL, NULL);
    if (l2->type == DOM_NODELIST_BY_NAME)
        return _dom_nodelist_match(l1, l2->type, l2->root, l2->data.n.name, NULL, NULL);
    if (l2->type == DOM_NODELIST_BY_NAMESPACE)
        return _dom_nodelist_match(l1, l2->type, l2->root, NULL, l2->data.ns.namespace, l2->data.ns.localname);
    return l1->root == l2->root;
}

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
#include "core/node.h"
#include "core/nodelist.h"

#include "utils/utils.h"

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
    } data;

    uint32_t refcnt; /**< Reference count */
};

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

/* Shared by length, item and the one-pass visitor: all observe the same live
 * filter. DOM's tag query folds the query only for HTML-namespace elements in
 * an HTML document; namespace/local-name queries are always exact. */
static bool nodelist_matches(const dom_nodelist *list, const dom_node_internal *node)
{
    if (list->type == DOM_NODELIST_CHILDREN)
        return true;
    if (node->type != DOM_ELEMENT_NODE)
        return false;
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
    if (l2->type == DOM_NODELIST_BY_NAME)
        return _dom_nodelist_match(l1, l2->type, l2->root, l2->data.n.name, NULL, NULL);
    if (l2->type == DOM_NODELIST_BY_NAMESPACE)
        return _dom_nodelist_match(l1, l2->type, l2->root, NULL, l2->data.ns.namespace, l2->data.ns.localname);
    return l1->root == l2->root;
}

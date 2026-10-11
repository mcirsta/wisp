/*
 * This file is part of libdom.
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 * Copyright 2009 Bo Yang <struggleyb.nku@gmail.com>
 */

#include <assert.h>
#include <stdlib.h>

#include <libwapcaplet/libwapcaplet.h>

#include "html/html_collection.h"
#include "html/html_document.h"

#include "core/element.h"
#include "core/node.h"
#include "core/string.h"

/*-----------------------------------------------------------------------*/
/* Constructor and destructor */

/**
 * Create a dom_html_collection
 *
 * \param doc   The document
 * \param root  The root element of the collection
 * \param ic    The callback function used to determin whether certain node
 *              belongs to the collection
 * \param col   The result collection object
 * \return DOM_NO_ERR on success, appropriate dom_exception on failure.
 */
dom_exception _dom_html_collection_create(struct dom_document *doc, struct dom_node_internal *root,
    dom_callback_is_in_collection ic, void *ctx, struct dom_html_collection **col)
{
    *col = malloc(sizeof(dom_html_collection));
    if (*col == NULL)
        return DOM_NO_MEM_ERR;

    return _dom_html_collection_initialise(doc, *col, root, ic, ctx);
}

/**
 * Intialiase a dom_html_collection
 *
 * \param doc   The document
 * \param col   The collection object to be initialised
 * \param root  The root element of the collection
 * \param ic    The callback function used to determin whether certain node
 *              beint32_ts to the collection
 * \return DOM_NO_ERR on success.
 */
dom_exception _dom_html_collection_initialise(struct dom_document *doc, struct dom_html_collection *col,
    struct dom_node_internal *root, dom_callback_is_in_collection ic, void *ctx)
{
    assert(doc != NULL);
    assert(ic != NULL);
    assert(root != NULL);

    col->doc = doc;
    dom_node_ref(doc);

    col->whole_tree = false;
    col->root = root;
    dom_node_ref(root);

    col->ic = ic;
    col->ctx = ctx;
    col->refcnt = 1;

    return DOM_NO_ERR;
}

/**
 * Finalise a dom_html_collection object
 *
 * \param col  The dom_html_collection object
 */
void _dom_html_collection_finalise(struct dom_html_collection *col)
{
    dom_node_unref(col->doc);
    col->doc = NULL;

    dom_node_unref(col->root);
    col->root = NULL;

    col->ic = NULL;
}

/**
 * Destroy a dom_html_collection object
 * \param col  The dom_html_collection object
 */
void _dom_html_collection_destroy(struct dom_html_collection *col)
{
    _dom_html_collection_finalise(col);

    free(col);
}


/*-----------------------------------------------------------------------*/
/* Public API */

/* Form collections retain the form, and follow its current tree even after it
 * is detached/reinserted. Other collections retain their fixed subtree root. */
static dom_node_internal *collection_root(dom_html_collection *col)
{
    dom_node_internal *root = col->root;
    if (col->whole_tree) {
        while (root->parent != NULL)
            root = root->parent;
    }
    return root;
}

static dom_node_internal *collection_next(dom_node_internal *node, dom_node_internal *root)
{
    if (node->first_child != NULL)
        return node->first_child;
    while (node != root && node->next == NULL)
        node = node->parent;
    return node == root ? NULL : node->next;
}


/**
 * Get the length of this dom_html_collection
 *
 * \param col  The dom_html_collection object
 * \param len  The returned length of this collection
 * \return DOM_NO_ERR on success.
 */
dom_exception dom_html_collection_get_length(dom_html_collection *col, uint32_t *len)
{
    struct dom_node_internal *root = collection_root(col), *node = root;
    *len = 0;

    while (node != NULL) {
        if (node->type == DOM_ELEMENT_NODE && col->ic(node, col->ctx) == true)
            (*len)++;

        node = collection_next(node, root);
    }

    return DOM_NO_ERR;
}

/**
 * Get the node with certain index
 *
 * \param col  The dom_html_collection object
 * \param index  The index number based on zero
 * \param node   The returned node object
 * \return DOM_NO_ERR on success.
 */
dom_exception dom_html_collection_item(dom_html_collection *col, uint32_t index, struct dom_node **node)
{
    struct dom_node_internal *root = collection_root(col), *n = root;
    uint32_t len = 0;

    while (n != NULL) {
        if (n->type == DOM_ELEMENT_NODE && col->ic(n, col->ctx) == true)
            len++;

        if (len == index + 1) {
            dom_node_ref(n);
            *node = (struct dom_node *)n;
            return DOM_NO_ERR;
        }

        n = collection_next(n, root);
    }

    /* Not find the node */
    *node = NULL;
    return DOM_NO_ERR;
}

/**
 * Get the node in the collection according name
 *
 * \param col   The collection
 * \param name  The name of target node
 * \param node  The returned node object
 * \return DOM_NO_ERR on success.
 */
dom_exception dom_html_collection_named_item(dom_html_collection *col, dom_string *name, struct dom_node **node)
{
    struct dom_node_internal *root = collection_root(col), *n = root;
    dom_document *doc = (dom_document *)dom_node_get_owner(n);
    dom_exception err;

    while (n != NULL) {
        if (n->type == DOM_ELEMENT_NODE && col->ic(n, col->ctx) == true) {
            dom_string *id = NULL;
            dom_string *id_name = NULL;

            err = _dom_element_get_id((struct dom_element *)n, &id);
            if (err != DOM_NO_ERR) {
                return err;
            }

            if (id != NULL) {
                if (dom_string_isequal(name, id)) {
                    *node = dom_node_ref(n);
                    dom_string_unref(id);

                    return DOM_NO_ERR;
                }
                dom_string_unref(id);
            }

            /* Check for Name attr if id not matched/found */
            err = _dom_element_get_attribute((dom_element *)n, doc->memoised[hds_name], &id_name);
            if (err != DOM_NO_ERR) {
                return err;
            }

            if (id_name != NULL) {
                if (dom_string_isequal(name, id_name)) {
                    *node = dom_node_ref(n);
                    dom_string_unref(id_name);

                    return DOM_NO_ERR;
                }
                dom_string_unref(id_name);
            }
        }

        n = collection_next(n, root);
    }

    /* Not found the target node */
    *node = NULL;

    return DOM_NO_ERR;
}

/**
 * Claim a reference on this collection
 *
 * \param col  The collection object
 */
void dom_html_collection_ref(dom_html_collection *col)
{
    if (col == NULL)
        return;

    col->refcnt++;
}

/**
 * Relese a reference on this collection
 *
 * \param col  The collection object
 */
void dom_html_collection_unref(dom_html_collection *col)
{
    if (col == NULL)
        return;

    if (col->refcnt > 0)
        col->refcnt--;

    if (col->refcnt == 0)
        _dom_html_collection_destroy(col);
}

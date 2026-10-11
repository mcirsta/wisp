/* SPDX-License-Identifier: MIT */
#include <stdlib.h>

#include "core/document.h"
#include "html/html_template_element.h"

/* HTML's template contents live in a separate inert document and are not
 * children of the element: https://html.spec.whatwg.org/#the-template-element */
static dom_exception template_contents_create(dom_html_template_element *element)
{
    dom_node_internal *node = (dom_node_internal *)element;
    dom_document *owner;
    dom_exception err = _dom_document_template_owner(node->owner, &owner);
    if (err != DOM_NO_ERR)
        return err;
    err = dom_document_create_document_fragment(owner, &element->content);
    if (err == DOM_NO_ERR)
        element->content->host = node;
    return err;
}

static void template_destroy(dom_node_internal *node)
{
    dom_html_template_element *element = (dom_html_template_element *)node;
    /* Contents can have been independently adopted into this document. Hold
     * the owner while releasing both trees, as either can remove pending nodes. */
    dom_document *owner = (dom_document *)dom_node_ref(node->owner);
    if (element->content != NULL) {
        element->content->host = NULL;
        dom_node_unref(element->content);
    }
    _dom_html_element_finalise(&element->base);
    free(element);
    dom_node_unref(owner);
}

static dom_exception template_copy(dom_node_internal *source, dom_node_internal **result)
{
    dom_html_template_element *copy = malloc(sizeof(*copy));
    if (copy == NULL)
        return DOM_NO_MEM_ERR;
    copy->content = NULL;
    dom_exception err = dom_html_element_copy_internal(source, copy);
    if (err != DOM_NO_ERR) {
        free(copy);
        return err;
    }
    /* The shared clone path establishes contents in the destination document,
     * after ownership transfer, even for a shallow clone. */
    *result = (dom_node_internal *)copy;
    return DOM_NO_ERR;
}

static const struct dom_element_protected_vtable template_vtable = {
    {template_destroy, template_copy}, DOM_ELEMENT_PROTECT_VTABLE};

dom_exception
_dom_html_template_element_create(struct dom_html_element_create_params *params, dom_html_template_element **result)
{
    *result = NULL;
    dom_html_template_element *element = malloc(sizeof(*element));
    if (element == NULL)
        return DOM_NO_MEM_ERR;
    dom_node_internal *node = (dom_node_internal *)element;
    node->base.vtable = &_dom_html_element_vtable;
    node->vtable = &template_vtable;
    element->content = NULL;
    dom_exception err = _dom_html_element_initialise(params, &element->base);
    if (err != DOM_NO_ERR) {
        free(element);
        return err;
    }
    err = template_contents_create(element);
    if (err != DOM_NO_ERR) {
        dom_node_unref(element);
        return err;
    }
    *result = element;
    return DOM_NO_ERR;
}

dom_exception dom_html_template_element_get_content(dom_html_template_element *element, dom_document_fragment **content)
{
    *content = NULL;
    if (!_dom_html_is_template((dom_node_internal *)element))
        return DOM_NOT_SUPPORTED_ERR;
    *content = (dom_document_fragment *)dom_node_ref(element->content);
    return DOM_NO_ERR;
}

dom_exception _dom_html_template_clone_contents(
    dom_html_template_element *source, dom_html_template_element *copy, bool deep, dom_node_operation operation)
{
    dom_exception err = template_contents_create(copy);
    if (err != DOM_NO_ERR || !deep)
        return err;
    return _dom_node_clone_children(&source->content->base, &copy->content->base, operation);
}

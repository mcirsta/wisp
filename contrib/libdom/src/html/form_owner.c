/* SPDX-License-Identifier: MIT */
#include <dom/dom.h>
#include "html/form_owner.h"
#include "html/html_button_element.h"
#include "html/html_input_element.h"
#include "html/html_select_element.h"
#include "html/html_text_area_element.h"
#include "html/html_document.h"
#include "core/attr.h"

static struct dom_html_form_association *association(dom_node_internal *node)
{
    switch (_dom_html_node_type(node)) {
    case DOM_HTML_ELEMENT_TYPE_INPUT:
        return &((dom_html_input_element *)node)->association;
    case DOM_HTML_ELEMENT_TYPE_BUTTON:
        return &((dom_html_button_element *)node)->association;
    case DOM_HTML_ELEMENT_TYPE_SELECT:
        return &((dom_html_select_element *)node)->association;
    case DOM_HTML_ELEMENT_TYPE_TEXTAREA:
        return &((dom_html_text_area_element *)node)->association;
    default:
        return NULL;
    }
}

static dom_node_internal *tree_root(dom_node_internal *node)
{
    while (node->parent != NULL)
        node = node->parent;
    return node;
}

static bool ancestor(dom_node_internal *node, dom_html_form_element *form)
{
    for (node = node->parent; node != NULL; node = node->parent) {
        if (node == (dom_node_internal *)form)
            return true;
    }
    return false;
}

void _dom_html_form_association_clear(struct dom_html_form_association *state)
{
    dom_html_form_element *retained = state->ancestor ? NULL : state->parser_form;
    *state = (struct dom_html_form_association){0};
    dom_node_unref(retained);
}

/* Hubbub calls this after inserting the control. Preserve its historical
 * non-ancestor association, but never override explicit form= ownership.
 * https://html.spec.whatwg.org/multipage/parsing.html#create-an-element-for-the-token */
dom_exception _dom_html_form_associate(dom_node_internal *control, dom_html_form_element *form)
{
    struct dom_html_form_association *state = association(control);
    if (state == NULL)
        return DOM_NO_ERR;
    bool explicit_form = false;
    dom_html_document *doc = (dom_html_document *)control->owner;
    dom_exception err = dom_element_has_attribute(control, doc->memoised[hds_form], &explicit_form);
    if (err != DOM_NO_ERR)
        return err;
    bool valid = !explicit_form && form != NULL && tree_root(control) == tree_root((dom_node_internal *)form);
    bool is_ancestor = valid && ancestor(control, form);
    if (valid && !is_ancestor)
        dom_node_ref(form);
    _dom_html_form_association_clear(state);
    if (valid)
        *state = (struct dom_html_form_association){.parser_form = form, .ancestor = is_ancestor};
    return DOM_NO_ERR;
}

/* The reset algorithm's ordinary result can be read directly, without a cache
 * or rescanning every control on ID changes. Parser-only identity is cleared at
 * the relevant mutation boundaries. This is shared by IDL and form collections.
 * https://html.spec.whatwg.org/multipage/form-control-infrastructure.html#reset-the-form-owner */
dom_exception _dom_html_form_owner(dom_node_internal *control, dom_html_form_element **form)
{
    dom_html_document *doc = (dom_html_document *)control->owner;
    dom_string *id = NULL;
    *form = NULL;
    dom_exception err = dom_element_get_attribute(control, doc->memoised[hds_form], &id);
    if (err != DOM_NO_ERR)
        return err;
    if (id != NULL && tree_root(control)->type == DOM_DOCUMENT_NODE) {
        dom_node_internal *candidate = NULL;
        err = dom_document_get_element_by_id(control->owner, id, &candidate);
        dom_string_unref(id);
        if (_dom_html_node_type(candidate) == DOM_HTML_ELEMENT_TYPE_FORM)
            *form = (dom_html_form_element *)candidate;
        else
            dom_node_unref(candidate);
        return err;
    }
    dom_string_unref(id);

    struct dom_html_form_association *state = association(control);
    if (state != NULL && state->parser_form != NULL) {
        /* A borrowed ancestor is compared with live ancestors before it can
         * ever be dereferenced, including after document teardown. */
        if (!state->ancestor || ancestor(control, state->parser_form)) {
            *form = (dom_html_form_element *)dom_node_ref(state->parser_form);
            return DOM_NO_ERR;
        }
        _dom_html_form_association_clear(state);
    }
    for (dom_node_internal *node = control->parent; node != NULL; node = node->parent) {
        if (_dom_html_node_type(node) == DOM_HTML_ELEMENT_TYPE_FORM) {
            *form = (dom_html_form_element *)dom_node_ref(node);
            break;
        }
    }
    return DOM_NO_ERR;
}

void _dom_html_form_attribute_changed(dom_node_internal *node, dom_string *namespace, dom_string *name)
{
    struct dom_html_form_association *state = association(node);
    if (state == NULL || namespace != NULL)
        return;
    dom_html_document *doc = (dom_html_document *)node->owner;
    if (dom_string_isequal(name, doc->memoised[hds_form]))
        _dom_html_form_association_clear(state);
}

void _dom_html_form_attribute_value_changed(dom_attr *attribute)
{
    dom_node_internal *attr = (dom_node_internal *)attribute;
    struct dom_html_form_association *state = association(attr->parent);
    if (state == NULL || state->parser_form == NULL || attr->namespace != NULL)
        return;
    dom_html_document *doc = (dom_html_document *)attr->owner;
    if (!dom_string_isequal(attr->name, doc->memoised[hds_form]))
        return;
    /* Attribute construction sets parent before installing it in the list. A
     * failed installation must not discard the existing parser association. */
    dom_attr *installed = NULL;
    dom_exception err = dom_element_get_attribute_node(attr->parent, attr->name, &installed);
    if (err == DOM_NO_ERR && installed == attribute)
        _dom_html_form_association_clear(state);
    dom_node_unref(installed);
}

/* Keep parser identity only when control and owner travel in the same subtree.
 * On removal that subtree is the new tree; on fragment insertion each child was
 * a separate tree before attachment. Hubbub associates after initial insertion.
 * Borrowed ancestors are validated before following any owner pointers. */
void _dom_html_form_subtree_changed(dom_node_internal *root)
{
    for (dom_node_internal *node = root; node != NULL;) {
        struct dom_html_form_association *state = association(node);
        if (state != NULL && state->parser_form != NULL) {
            dom_node_internal *owner = NULL;
            if (!state->ancestor || ancestor(node, state->parser_form)) {
                owner = (dom_node_internal *)state->parser_form;
                while (owner != NULL && owner != root)
                    owner = owner->parent;
            }
            if (owner == NULL)
                _dom_html_form_association_clear(state);
        }
        if (node->first_child != NULL) {
            node = node->first_child;
        } else {
            while (node != root && node->next == NULL)
                node = node->parent;
            node = node == root ? NULL : node->next;
        }
    }
}

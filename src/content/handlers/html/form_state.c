/* SPDX-License-Identifier: GPL-2.0-only */
#include <string.h>
#include <wisp/utils/corestrings.h>
#include <wisp/content/handlers/html/private.h>
#include "utils/libdom.h"
#include "content/handlers/html/form_state.h"

static dom_html_element_type html_tag(dom_node *node)
{
    dom_node_type type;
    dom_string *ns = NULL;
    dom_html_element_type tag = DOM_HTML_ELEMENT_TYPE__UNKNOWN;
    if (node == NULL || dom_node_get_node_type(node, &type) != DOM_NO_ERR || type != DOM_ELEMENT_NODE ||
        dom_node_get_namespace(node, &ns) != DOM_NO_ERR)
        return tag;
    if (ns == NULL || dom_string_isequal(ns, corestring_dom_html_namespace))
        dom_html_element_get_tag_type(node, &tag);
    dom_string_unref(ns);
    return tag;
}

static bool input_type(dom_node *node, lwc_string *wanted)
{
    dom_string *type = NULL;
    if (html_tag(node) != DOM_HTML_ELEMENT_TYPE_INPUT ||
        dom_html_input_element_get_type((dom_html_input_element *)node, &type) != DOM_NO_ERR)
        return false;
    bool matches = type != NULL && dom_string_caseless_lwc_isequal(type, wanted);
    dom_string_unref(type);
    return matches;
}

static bool checkable(dom_node *node)
{
    return input_type(node, corestring_lwc_radio) || input_type(node, corestring_lwc_checkbox);
}

/* Associations are confined to one tree, including detached subtrees. */
static dom_node *tree_root(dom_node *node)
{
    dom_node *root = dom_node_ref(node), *parent = NULL;
    while (dom_node_get_parent_node(root, &parent) == DOM_NO_ERR && parent != NULL) {
        dom_node_unref(root);
        root = parent;
    }
    return root;
}

static bool labelable(dom_node *node)
{
    switch (html_tag(node)) {
    case DOM_HTML_ELEMENT_TYPE_INPUT:
        return !input_type(node, corestring_lwc_hidden);
    case DOM_HTML_ELEMENT_TYPE_BUTTON:
    case DOM_HTML_ELEMENT_TYPE_SELECT:
    case DOM_HTML_ELEMENT_TYPE_TEXTAREA:
    case DOM_HTML_ELEMENT_TYPE_METER:
    case DOM_HTML_ELEMENT_TYPE_OUTPUT:
    case DOM_HTML_ELEMENT_TYPE_PROGRESS:
        return true;
    default:
        return false;
    }
}

/* Label activation must ignore interactive descendants, including their text. */
static bool interactive(dom_node *node)
{
    dom_string *attribute = NULL;
    switch (html_tag(node)) {
    case DOM_HTML_ELEMENT_TYPE_INPUT:
        return !input_type(node, corestring_lwc_hidden);
    case DOM_HTML_ELEMENT_TYPE_BUTTON:
    case DOM_HTML_ELEMENT_TYPE_SELECT:
    case DOM_HTML_ELEMENT_TYPE_TEXTAREA:
    case DOM_HTML_ELEMENT_TYPE_DETAILS:
    case DOM_HTML_ELEMENT_TYPE_EMBED:
    case DOM_HTML_ELEMENT_TYPE_IFRAME:
        return true;
    case DOM_HTML_ELEMENT_TYPE_A:
        attribute = corestring_dom_href;
        break;
    case DOM_HTML_ELEMENT_TYPE_AUDIO:
    case DOM_HTML_ELEMENT_TYPE_VIDEO:
        attribute = corestring_dom_controls;
        break;
    case DOM_HTML_ELEMENT_TYPE_IMG:
        attribute = corestring_dom_usemap;
        break;
    default:
        return false;
    }
    bool present = false;
    return dom_element_has_attribute(node, attribute, &present) != DOM_NO_ERR || present;
}

struct label_id_search {
    dom_string *id;
    dom_node *found;
};

static nserror label_find_id(dom_node *node, void *pw)
{
    struct label_id_search *search = pw;
    dom_string *id = NULL;
    if (dom_element_get_attribute(node, corestring_dom_id, &id) != DOM_NO_ERR)
        return NSERROR_DOM;
    bool match = id != NULL && dom_string_isequal(id, search->id);
    dom_string_unref(id);
    if (!match)
        return NSERROR_OK;
    search->found = dom_node_ref(node);
    return NSERROR_STOPPED;
}

static dom_node *label_control(dom_node *label)
{
    dom_string *target = NULL;
    dom_node *control = NULL;
    if (dom_html_label_element_get_html_for((dom_html_label_element *)label, &target) != DOM_NO_ERR)
        return NULL;
    if (target != NULL) {
        dom_node *root = tree_root(label);
        dom_node_type type;
        if (dom_string_byte_length(target) != 0 && dom_node_get_node_type(root, &type) == DOM_NO_ERR) {
            if (type == DOM_DOCUMENT_NODE) {
                dom_document_get_element_by_id(root, target, &control);
            } else {
                /* A detached label cannot target its owner document. */
                struct label_id_search search = {.id = target};
                if (libdom_iterate_elements(root, label_find_id, &search) == NSERROR_STOPPED)
                    control = search.found;
            }
        }
        dom_node_unref(root);
        dom_string_unref(target);
    } else {
        dom_string *all;
        dom_nodelist *nodes;
        if (dom_string_create((const uint8_t *)"*", 1, &all) != DOM_NO_ERR)
            return NULL;
        dom_exception err = dom_element_get_elements_by_tag_name(label, all, &nodes);
        dom_string_unref(all);
        if (err != DOM_NO_ERR)
            return NULL;
        uint32_t count = 0;
        dom_nodelist_get_length(nodes, &count);
        for (uint32_t i = 0; i < count; i++) {
            dom_nodelist_item(nodes, i, &control);
            if (control != NULL && labelable(control))
                break;
            dom_node_unref(control);
            control = NULL;
        }
        dom_nodelist_unref(nodes);
    }
    if (control != NULL && !checkable(control)) {
        dom_node_unref(control);
        control = NULL;
    }
    return control;
}

/* Freeze the activation element before callbacks, as DOM dispatch does. The
 * label's associated control is deliberately resolved later, during activation. */
static dom_node *checkable_activation_target(dom_node *clicked)
{
    dom_node *node = dom_node_ref(clicked);
    while (node != NULL) {
        if (checkable(node) || html_tag(node) == DOM_HTML_ELEMENT_TYPE_LABEL)
            return node;
        if (interactive(node)) {
            dom_node_unref(node);
            return NULL;
        }
        dom_node *parent = NULL;
        dom_node_get_parent_node(node, &parent);
        dom_node_unref(node);
        node = parent;
    }
    return NULL;
}

dom_node *html_checkable_target(dom_node *clicked)
{
    dom_node *target = checkable_activation_target(clicked);
    if (html_tag(target) == DOM_HTML_ELEMENT_TYPE_LABEL) {
        dom_node *control = label_control(target);
        dom_node_unref(target);
        return control;
    }
    return target;
}

bool html_form_is_submit_button(dom_node *node)
{
    dom_html_element_type tag = html_tag(node);
    if (tag == DOM_HTML_ELEMENT_TYPE_INPUT)
        return input_type(node, corestring_lwc_submit) || input_type(node, corestring_lwc_image);
    if (tag != DOM_HTML_ELEMENT_TYPE_BUTTON)
        return false;
    dom_string *type = NULL;
    if (dom_element_get_attribute(node, corestring_dom_type, &type) != DOM_NO_ERR)
        return false;
    bool submit = dom_string_caseless_isequal(type, corestring_dom_submit);
    bool automatic = !submit && !dom_string_caseless_isequal(type, corestring_dom_reset) &&
        !dom_string_caseless_isequal(type, corestring_dom_button);
    dom_string_unref(type);
    if (automatic) {
        bool command = false, commandfor = false;
        dom_node *parent = NULL;
        if (dom_element_has_attribute(node, corestring_dom_command, &command) == DOM_NO_ERR &&
            dom_element_has_attribute(node, corestring_dom_commandfor, &commandfor) == DOM_NO_ERR &&
            dom_node_get_parent_node(node, &parent) == DOM_NO_ERR)
            submit = !command && !commandfor && html_tag(parent) != DOM_HTML_ELEMENT_TYPE_SELECT;
        dom_node_unref(parent);
    }
    return submit;
}

dom_node *html_form_submit_target(dom_node *clicked)
{
    dom_node *node = dom_node_ref(clicked);
    while (node != NULL) {
        dom_html_element_type tag = html_tag(node);
        if (tag == DOM_HTML_ELEMENT_TYPE_INPUT || tag == DOM_HTML_ELEMENT_TYPE_BUTTON)
            return node;
        if (interactive(node)) {
            dom_node_unref(node);
            return NULL;
        }
        dom_node *parent = NULL;
        dom_node_get_parent_node(node, &parent);
        dom_node_unref(node);
        node = parent;
    }
    return NULL;
}

bool html_form_control_disabled(dom_node *input)
{
    bool disabled = true;
    if (dom_element_has_attribute(input, corestring_dom_disabled, &disabled) != DOM_NO_ERR || disabled)
        return true;
    dom_node *child = dom_node_ref(input), *parent = NULL;
    while (dom_node_get_parent_node(child, &parent) == DOM_NO_ERR && parent != NULL) {
        if (html_tag(parent) == DOM_HTML_ELEMENT_TYPE_FIELDSET) {
            if (dom_element_has_attribute(parent, corestring_dom_disabled, &disabled) != DOM_NO_ERR) {
                dom_node_unref(child);
                dom_node_unref(parent);
                return true;
            }
            if (disabled) {
                /* Descendants of the first legend are exempt. */
                dom_node *legend = NULL;
                dom_node_get_first_child(parent, &legend);
                while (legend != NULL && html_tag(legend) != DOM_HTML_ELEMENT_TYPE_LEGEND) {
                    dom_node *next = NULL;
                    dom_node_get_next_sibling(legend, &next);
                    dom_node_unref(legend);
                    legend = next;
                }
                disabled = legend != child;
                dom_node_unref(legend);
                if (disabled) {
                    dom_node_unref(child);
                    dom_node_unref(parent);
                    return true;
                }
            }
        }
        dom_node_unref(child);
        child = parent;
    }
    dom_node_unref(child);
    return false;
}

struct radio_activation {
    dom_node *input;
    dom_node **previous;
    dom_string *name;
    dom_html_form_element *form;
    html_checkable_changed_cb notify;
    void *pw;
};

static nserror uncheck_radio_peer(dom_node *other, void *pw)
{
    struct radio_activation *activation = pw;
    dom_string *name = NULL;
    dom_html_form_element *form = NULL;
    bool checked = false;
    nserror result = NSERROR_OK;
    if (other == activation->input || !input_type(other, corestring_lwc_radio))
        return NSERROR_OK;
    if (dom_html_input_element_get_name((dom_html_input_element *)other, &name) != DOM_NO_ERR) {
        result = NSERROR_DOM;
        goto out;
    }
    if (name == NULL || !dom_string_isequal(name, activation->name))
        goto out;
    if (dom_html_input_element_get_form((dom_html_input_element *)other, &form) != DOM_NO_ERR) {
        result = NSERROR_DOM;
        goto out;
    }
    if (form == activation->form) {
        /* Preserve the existing live-state bookkeeping even for an unchecked
         * peer, but only report changes that can affect selector matching. */
        if (dom_html_input_element_get_checked((dom_html_input_element *)other, &checked) != DOM_NO_ERR) {
            result = NSERROR_DOM;
            goto out;
        }
        if (checked && activation->previous != NULL && *activation->previous == NULL)
            *activation->previous = dom_node_ref(other);
        if (dom_html_input_element_set_checked((dom_html_input_element *)other, false) != DOM_NO_ERR) {
            result = NSERROR_DOM;
            goto out;
        }
        if (checked && activation->notify != NULL)
            activation->notify(other, activation->pw);
    }
out:
    dom_node_unref(form);
    dom_string_unref(name);
    return result;
}

static void select_radio(dom_node *input, dom_node **previous, html_checkable_changed_cb changed, void *pw)
{
    dom_node *root = tree_root(input);
    struct radio_activation activation = {.input = input, .previous = previous, .notify = changed, .pw = pw};
    if (dom_html_input_element_get_name((dom_html_input_element *)input, &activation.name) != DOM_NO_ERR ||
        dom_html_input_element_get_form((dom_html_input_element *)input, &activation.form) != DOM_NO_ERR)
        goto out;
    if (activation.name != NULL && dom_string_byte_length(activation.name) != 0 &&
        libdom_iterate_elements(root, uncheck_radio_peer, &activation) != NSERROR_OK)
        goto out;
    bool checked = false;
    if (dom_html_input_element_get_checked((dom_html_input_element *)input, &checked) == DOM_NO_ERR &&
        dom_html_input_element_set_checked((dom_html_input_element *)input, true) == DOM_NO_ERR && !checked) {
        if (changed != NULL)
            changed(input, pw);
    }
out:
    dom_node_unref(root);
    dom_node_unref(activation.form);
    dom_string_unref(activation.name);
}

static void set_checkbox(dom_node *input, bool checked, bool indeterminate, html_checkable_changed_cb changed, void *pw)
{
    bool was_checked, was_indeterminate;
    if (dom_html_input_element_get_checked((dom_html_input_element *)input, &was_checked) != DOM_NO_ERR ||
        dom_html_input_element_get_indeterminate((dom_html_input_element *)input, &was_indeterminate) != DOM_NO_ERR ||
        dom_html_input_element_set_checked((dom_html_input_element *)input, checked) != DOM_NO_ERR ||
        dom_html_input_element_set_indeterminate((dom_html_input_element *)input, indeterminate) != DOM_NO_ERR)
        return;
    if (was_checked == checked && was_indeterminate == indeterminate)
        return;
    if (changed != NULL)
        changed(input, pw);
}

static bool same_radio_group(dom_node *input, dom_node *previous)
{
    if (!input_type(previous, corestring_lwc_radio))
        return false;
    if (previous == input)
        return true;
    dom_node *root = tree_root(input), *other_root = tree_root(previous);
    dom_string *name = NULL, *other_name = NULL;
    dom_html_form_element *form = NULL, *other_form = NULL;
    bool same = root == other_root &&
        dom_html_input_element_get_name((dom_html_input_element *)input, &name) == DOM_NO_ERR &&
        dom_html_input_element_get_name((dom_html_input_element *)previous, &other_name) == DOM_NO_ERR &&
        name != NULL && other_name != NULL && dom_string_byte_length(name) != 0 &&
        dom_string_isequal(name, other_name) && dom_html_input_element_get_form((dom_html_input_element *)input, &form) == DOM_NO_ERR &&
        dom_html_input_element_get_form((dom_html_input_element *)previous, &other_form) == DOM_NO_ERR && form == other_form;
    dom_node_unref(root);
    dom_node_unref(other_root);
    dom_node_unref(form);
    dom_node_unref(other_form);
    dom_string_unref(name);
    dom_string_unref(other_name);
    return same;
}

static bool click_dispatch(const struct html_form_click_context *context, dom_node *target, dom_string *type)
{
    if (context != NULL && context->dispatch != NULL)
        return context->dispatch(target, type, context->pw);
    return fire_generic_dom_event(type, target, true, dom_string_isequal(type, corestring_dom_click));
}

static bool input_click(dom_node *input, dom_node *clicked, const struct html_form_click_context *context)
{
    html_checkable_changed_cb changed = context != NULL ? context->changed : NULL;
    void *pw = context != NULL ? context->pw : NULL;
    bool was_checked, was_indeterminate;
    if (!checkable(input) || html_form_control_disabled(input) ||
        dom_html_input_element_get_checked((dom_html_input_element *)input, &was_checked) != DOM_NO_ERR ||
        dom_html_input_element_get_indeterminate((dom_html_input_element *)input, &was_indeterminate) != DOM_NO_ERR)
        return false;
    dom_node_ref(input);
    dom_node *previous = NULL;
    /* HTML legacy-pre-activation: listeners see the provisional checked state. */
    if (input_type(input, corestring_lwc_checkbox)) {
        set_checkbox(input, !was_checked, false, changed, pw);
    } else {
        if (was_checked)
            previous = dom_node_ref(input);
        select_radio(input, &previous, changed, pw);
    }
    bool accepted = click_dispatch(context, clicked, corestring_dom_click);
    /* Re-read the type and group: listeners can edit, detach or move controls. */
    if (!accepted) {
        if (input_type(input, corestring_lwc_checkbox)) {
            set_checkbox(input, was_checked, was_indeterminate, changed, pw);
        } else if (input_type(input, corestring_lwc_radio)) {
            if (previous != NULL && same_radio_group(input, previous)) {
                /* Restoration also clears any peer selected during the callback.
                 * Disabledness does not block restoring the saved live state. */
                select_radio(previous, NULL, changed, pw);
            } else {
                bool checked = false;
                dom_html_input_element_get_checked((dom_html_input_element *)input, &checked);
                dom_html_input_element_set_checked((dom_html_input_element *)input, false);
                if (checked && changed != NULL)
                    changed(input, pw);
            }
        }
    } else if (checkable(input)) {
        /* HTML's checkbox/radio activation steps still apply if the listener
         * disables the control. They require a connected, uncanceled activation,
         * not another checkedness comparison after the provisional update. */
        dom_node *root = tree_root(input);
        dom_node_type type;
        bool connected = dom_node_get_node_type(root, &type) == DOM_NO_ERR && type == DOM_DOCUMENT_NODE;
        dom_node_unref(root);
        if (connected) {
            click_dispatch(context, input, corestring_dom_input);
            click_dispatch(context, input, corestring_dom_change);
        }
    }
    dom_node_unref(previous);
    dom_node_unref(input);
    return accepted;
}

/* The original activation element stays frozen, but HTML's interactive-content
 * exclusion uses the target's current position/attributes when activation runs. */
static bool label_ignores_click(dom_node *label, dom_node *clicked)
{
    bool blocked = false;
    dom_node *node = dom_node_ref(clicked);
    while (node != NULL && node != label) {
        blocked |= interactive(node);
        dom_node *parent = NULL;
        dom_node_get_parent_node(node, &parent);
        dom_node_unref(node);
        node = parent;
    }
    bool ignore = node == label && blocked;
    dom_node_unref(node);
    return ignore;
}

struct html_form_click_result html_form_click(dom_node *clicked, const struct html_form_click_context *context)
{
    struct html_form_click_result result = {0};
    if (clicked == NULL)
        return result;
    dom_node_ref(clicked);
    dom_node *activation = checkable_activation_target(clicked);
    result.handled = activation != NULL;
    if (activation != NULL && checkable(activation)) {
        result.accepted = input_click(activation, clicked, context);
    } else {
        result.accepted = click_dispatch(context, clicked, corestring_dom_click);
        if (result.accepted && activation != NULL && !label_ignores_click(activation, clicked)) {
            dom_node *control = label_control(activation);
            if (control != NULL) {
                /* Canceling this forwarded click rolls back the input only;
                 * it does not cancel the already dispatched label click. */
                input_click(control, control, context);
                dom_node_unref(control);
            }
        }
    }
    dom_node_unref(activation);
    dom_node_unref(clicked);
    return result;
}

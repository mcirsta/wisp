/* SPDX-License-Identifier: GPL-2.0-only */
#include <limits.h>
#include <assert.h>
#include <wisp/content/handlers/html/private.h>
#include <wisp/content/handlers/html/form_internal.h>
#include "content/handlers/html/form_bindings.h"
#include "utils/talloc.h"

struct control_binding {
    struct form_control *control;
    struct box *box;
    struct form *form;
};

struct form_binding {
    struct form *form;
    hashmap_t *old_index, *index;
    struct form_control **old_controls, **controls;
    size_t old_count, count;
};

struct html_form_binding_plan {
    html_content *html;
    struct form *forms;
    struct control_binding *old, *fresh;
    size_t old_count, new_count;
    struct form_binding *bindings;
    size_t count;
    bool applied;
};

/* Physical box traversal, bounded by the region. Clones borrow their gadget;
 * only the original owning box participates in registration. */
static struct box *next_box(struct box *root, struct box *box)
{
    if (box->children != NULL)
        return box->children;
    while (box != root && box->next == NULL)
        box = box->parent;
    return box == root ? NULL : box->next;
}

static bool native_control(const struct form_control *control)
{
    switch (control->type) {
    case GADGET_RADIO:
    case GADGET_CHECKBOX:
    case GADGET_HIDDEN:
    case GADGET_IMAGE:
    case GADGET_SUBMIT:
    case GADGET_RESET:
    case GADGET_BUTTON:
        return true;
    default:
        return false;
    }
}

/* Use the same libdom parser association as ordinary control construction.
 * These getters only return references; validation does not allocate. */
static nserror control_owner(html_content *html, struct form_control *control, struct form **result)
{
    dom_html_element_type tag;
    dom_html_form_element *owner = NULL;
    dom_exception err;
    *result = NULL;
    if (dom_html_element_get_tag_type(control->node, &tag) != DOM_NO_ERR)
        return NSERROR_DOM;
    if (tag == DOM_HTML_ELEMENT_TYPE_INPUT)
        err = dom_html_input_element_get_form(control->node, &owner);
    else if (tag == DOM_HTML_ELEMENT_TYPE_BUTTON)
        err = dom_html_button_element_get_form(control->node, &owner);
    else
        return NSERROR_NOT_IMPLEMENTED;
    if (err != DOM_NO_ERR)
        return NSERROR_DOM;
    if (owner == NULL)
        return NSERROR_OK;
    for (struct form *f = html->forms; f != NULL; f = f->prev) {
        if (f->node == owner) {
            *result = f;
            break;
        }
    }
    dom_node_unref(owner);
    return *result == NULL ? NSERROR_NOT_IMPLEMENTED : NSERROR_OK;
}

static nserror collect_controls(struct html_form_binding_plan *plan, struct box *root, bool detached,
    struct control_binding **result, size_t *count)
{
    *count = 0;
    for (struct box *box = root; box != NULL; box = next_box(root, box))
        if (box->gadget != NULL && !(box->flags & CLONE))
            (*count)++;
    if (*count == 0)
        return NSERROR_OK;
    if (*count > UINT_MAX)
        return NSERROR_NOMEM;
    *result = talloc_array(plan, struct control_binding, *count);
    if (*result == NULL)
        return NSERROR_NOMEM;
    size_t i = 0;
    for (struct box *box = root; box != NULL; box = next_box(root, box)) {
        struct form_control *control = box->gadget;
        if (control == NULL || (box->flags & CLONE))
            continue;
        if (!native_control(control))
            return NSERROR_NOT_IMPLEMENTED;
        if (control->box != box || control->node != box->node || control->html != plan->html)
            return NSERROR_INVALID;
        struct form *form;
        nserror err = control_owner(plan->html, control, &form);
        if (err != NSERROR_OK)
            return err;
        if ((detached && (control->form != NULL || control->prev != NULL || control->next != NULL)) ||
            (!detached && control->form != form))
            return NSERROR_INVALID;
        if (!detached && form != NULL) {
            struct form_control *member = form->controls;
            while (member != NULL && member != control)
                member = member->next;
            if (member == NULL)
                return NSERROR_INVALID;
        }
        (*result)[i++] = (struct control_binding){control, box, form};
    }
    return NSERROR_OK;
}

static bool retiring(const struct html_form_binding_plan *plan, const struct form_control *control)
{
    for (size_t i = 0; i < plan->old_count; i++)
        if (plan->old[i].control == control)
            return true;
    return false;
}

static int discard_plan(struct html_form_binding_plan *plan)
{
    for (size_t i = 0; i < plan->count; i++)
        if (plan->bindings[i].index != NULL)
            hashmap_destroy(plan->bindings[i].index);
    return 0;
}

static nserror prepare_form(struct html_form_binding_plan *plan, struct form_binding *binding)
{
    struct form *form = binding->form;
    binding->old_index = form->control_index;
    for (struct form_control *c = form->controls; c != NULL; c = c->next)
        binding->old_count++;
    size_t capacity = binding->old_count + plan->new_count;
    if (capacity > UINT_MAX)
        return NSERROR_NOMEM;
    binding->old_controls = talloc_array(plan, struct form_control *, binding->old_count);
    binding->controls = talloc_array(plan, struct form_control *, capacity);
    if (binding->old_controls == NULL || binding->controls == NULL)
        return NSERROR_NOMEM;
    size_t i = 0;
    struct form_control *prev = NULL;
    for (struct form_control *c = form->controls; c != NULL; c = c->next) {
        if (c->form != form || c->prev != prev)
            return NSERROR_INVALID;
        binding->old_controls[i++] = c;
        prev = c;
        if (!retiring(plan, c)) {
            binding->controls[binding->count++] = c;
            continue;
        }
        /* Preserve existing positions where the same node remains visible. */
        for (size_t j = 0; j < plan->new_count; j++) {
            const struct control_binding *fresh = &plan->fresh[j];
            if (fresh->form == form && fresh->control->node == c->node) {
                binding->controls[binding->count++] = fresh->control;
                break;
            }
        }
    }
    if (prev != form->last_control)
        return NSERROR_INVALID;
    /* Newly visible controls append to this lookup list. Submission order comes
     * from the DOM form collection, not from the gadget registration list. */
    for (size_t j = 0; j < plan->new_count; j++) {
        const struct control_binding *fresh = &plan->fresh[j];
        if (fresh->form != form)
            continue;
        for (i = 0; i < binding->count && binding->controls[i] != fresh->control; i++)
            ;
        if (i == binding->count)
            binding->controls[binding->count++] = fresh->control;
    }
    /* Preserve the existing list-only mode if an index was unavailable. */
    if (binding->old_index == NULL)
        return NSERROR_OK;
    binding->index = form_control_index_create();
    if (binding->index == NULL)
        return NSERROR_NOMEM;
    for (i = 0; i < binding->count; i++) {
        struct form_control *c = binding->controls[i];
        if (hashmap_lookup(binding->index, c->node) != NULL)
            return NSERROR_INVALID;
        void **slot = hashmap_insert(binding->index, c->node);
        if (slot == NULL)
            return NSERROR_NOMEM;
        *slot = c;
    }
    return NSERROR_OK;
}

nserror html_form_prepare_bindings(
    html_content *html, struct box *old_root, struct box *new_root, void *owner, struct html_form_binding_plan **result)
{
    *result = NULL;
    struct html_form_binding_plan *plan = talloc_zero(owner, struct html_form_binding_plan);
    if (plan == NULL)
        return NSERROR_NOMEM;
    talloc_set_destructor(plan, discard_plan);
    plan->html = html;
    plan->forms = html->forms;
    nserror err = collect_controls(plan, old_root, false, &plan->old, &plan->old_count);
    if (err == NSERROR_OK)
        err = collect_controls(plan, new_root, true, &plan->fresh, &plan->new_count);
    if (err != NSERROR_OK)
        goto failed;
    size_t capacity = plan->old_count + plan->new_count;
    if (capacity > UINT_MAX) {
        err = NSERROR_NOMEM;
        goto failed;
    }
    if (capacity > 0) {
        plan->bindings = talloc_zero_array(plan, struct form_binding, capacity);
        if (plan->bindings == NULL) {
            err = NSERROR_NOMEM;
            goto failed;
        }
    }
    for (size_t j = 0; j < capacity; j++) {
        struct form *form = j < plan->old_count ? plan->old[j].form : plan->fresh[j - plan->old_count].form;
        if (form == NULL)
            continue;
        size_t i = 0;
        while (i < plan->count && plan->bindings[i].form != form)
            i++;
        if (i == plan->count)
            plan->bindings[plan->count++].form = form;
    }
    for (size_t i = 0; i < plan->count; i++) {
        err = prepare_form(plan, &plan->bindings[i]);
        if (err != NSERROR_OK)
            goto failed;
    }
    *result = plan;
    return NSERROR_OK;

failed:
    talloc_free(plan);
    return err;
}

static bool controls_unchanged(
    const struct html_form_binding_plan *plan, const struct control_binding *controls, size_t count, bool detached)
{
    for (size_t i = 0; i < count; i++) {
        const struct control_binding *b = &controls[i];
        /* Check the owning box before dereferencing a possibly retired gadget. */
        if (b->box->gadget != b->control)
            return false;
        struct form_control *c = b->control;
        struct form *owner;
        if (c->box != b->box || c->node != b->box->node || c->html != plan->html || !native_control(c) ||
            control_owner(plan->html, c, &owner) != NSERROR_OK || owner != b->form ||
            c->form != (detached ? NULL : b->form) || (detached && (c->prev != NULL || c->next != NULL)))
            return false;
    }
    return true;
}

nserror html_form_check_bindings(const struct html_form_binding_plan *plan)
{
    if (plan->applied || plan->html->forms != plan->forms)
        return NSERROR_INVALID;
    for (size_t i = 0; i < plan->count; i++) {
        const struct form_binding *b = &plan->bindings[i];
        struct form *form = plan->html->forms;
        while (form != NULL && form != b->form)
            form = form->prev;
        if (form == NULL || form->control_index != b->old_index)
            return NSERROR_INVALID;
        struct form_control *control = form->controls, *prev = NULL;
        for (size_t j = 0; j < b->old_count; j++) {
            if (control == NULL || control != b->old_controls[j] || control->form != form || control->prev != prev)
                return NSERROR_INVALID;
            prev = control;
            control = control->next;
        }
        if (control != NULL || form->last_control != prev)
            return NSERROR_INVALID;
    }
    if (!controls_unchanged(plan, plan->old, plan->old_count, false) ||
        !controls_unchanged(plan, plan->fresh, plan->new_count, true))
        return NSERROR_INVALID;
    return NSERROR_OK;
}

nserror html_form_apply_bindings(struct html_form_binding_plan *plan)
{
    if (!plan->html->base.locked || !plan->html->restyling || html_form_check_bindings(plan) != NSERROR_OK)
        return NSERROR_INVALID;
    html_form_commit_bindings(plan);
    return NSERROR_OK;
}

void html_form_commit_bindings(struct html_form_binding_plan *plan)
{
    assert(plan->html->base.locked && plan->html->restyling && !plan->applied);
    for (size_t i = 0; i < plan->old_count; i++) {
        struct form_control *c = plan->old[i].control;
        c->form = NULL;
        c->prev = c->next = NULL;
    }
    for (size_t i = 0; i < plan->count; i++) {
        struct form_binding *b = &plan->bindings[i];
        struct form *form = b->form;
        form->controls = b->count == 0 ? NULL : b->controls[0];
        form->last_control = b->count == 0 ? NULL : b->controls[b->count - 1];
        for (size_t j = 0; j < b->count; j++) {
            struct form_control *c = b->controls[j];
            c->form = form;
            c->prev = j == 0 ? NULL : b->controls[j - 1];
            c->next = j + 1 == b->count ? NULL : b->controls[j + 1];
        }
        form->control_index = b->index;
        b->index = NULL; /* Ownership transferred; discarding the plan is safe. */
        if (b->old_index != NULL)
            hashmap_destroy(b->old_index);
    }
    plan->applied = true;
}

void html_form_discard_bindings(struct html_form_binding_plan *plan)
{
    talloc_free(plan);
}

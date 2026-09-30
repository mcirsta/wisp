/* SPDX-License-Identifier: GPL-2.0-only */
#include <limits.h>
#include <stdlib.h>
#include <wisp/content/handlers/html/private.h>
#include <wisp/utils/corestrings.h>
#include "content/handlers/html/box_construct.h"
#include "content/handlers/html/box_mappings.h"
#include "utils/libdom.h"
#include "utils/talloc.h"

#ifdef WISP_ENABLE_PARTIAL_UPDATES
struct mapping {
    dom_node *node, *parent;
    struct box *old, *fresh;
    struct dom_user_data *prepared;
    size_t order;
};

struct html_box_mapping_plan {
    html_content *html;
    dom_node *scope;
    struct box *old_root, *new_root, *layout, *parent;
    const struct box_construct_region *candidate;
    void *bctx;
    uint64_t generation;
    struct mapping *entries;
    size_t count, capacity;
    bool applied;
};

static int discard_plan(struct html_box_mapping_plan *plan)
{
    for (size_t i = 0; i < plan->count; i++) {
        dom_node_discard_prepared_user_data(plan->entries[i].prepared);
        dom_node_unref(plan->entries[i].parent);
        dom_node_unref(plan->entries[i].node);
    }
    return 0;
}

static bool within(const struct box *root, const struct box *box)
{
    while (box != NULL && box != root)
        box = box->parent;
    return root != NULL && box == root;
}

static nserror collect_mapping(dom_node *node, void *pw)
{
    struct html_box_mapping_plan *plan = pw;
    if (plan->count == plan->capacity) {
        if (plan->capacity > UINT_MAX / 2)
            return NSERROR_NOMEM;
        size_t capacity = plan->capacity == 0 ? 64 : plan->capacity * 2;
        struct mapping *entries = talloc_realloc(plan, plan->entries, struct mapping, capacity);
        if (entries == NULL)
            return NSERROR_NOMEM;
        plan->entries = entries;
        plan->capacity = capacity;
    }
    struct mapping *m = &plan->entries[plan->count];
    *m = (struct mapping){.node = dom_node_ref(node), .order = plan->count};
    plan->count++;
    if (dom_node_get_parent_node(node, &m->parent) != DOM_NO_ERR ||
        dom_node_get_user_data(node, corestring_dom___ns_key_box_node_data, &m->old) != DOM_NO_ERR)
        return NSERROR_DOM;
    return m->old == NULL || within(plan->old_root, m->old) ? NSERROR_OK : NSERROR_NOT_IMPLEMENTED;
}

static int compare_mapping(const void *a, const void *b)
{
    uintptr_t x = (uintptr_t)((const struct mapping *)a)->node;
    uintptr_t y = (uintptr_t)((const struct mapping *)b)->node;
    return (x > y) - (x < y);
}

static struct mapping *find_mapping(const struct html_box_mapping_plan *plan, dom_node *node)
{
    struct mapping key = {.node = node};
    return bsearch(&key, plan->entries, plan->count, sizeof(key), compare_mapping);
}

/* Box-tree membership is distinct from DOM ancestry because inlines flatten.
 * Only owning element boxes publish mappings; clones borrow their source node. */
static nserror walk_boxes(const struct html_box_mapping_plan *plan, struct box *root, bool fresh, bool populate)
{
    size_t seen = 0;
    for (struct box *box = root; box != NULL;) {
        if (box->node != NULL && !(box->flags & CLONE)) {
            seen++;
            struct mapping *m = find_mapping(plan, box->node);
            if (m == NULL)
                return NSERROR_NOT_IMPLEMENTED;
            if (populate) {
                if (m->fresh != NULL)
                    return NSERROR_INVALID;
                m->fresh = box;
            } else if ((fresh ? m->fresh : m->old) != box) {
                return NSERROR_INVALID;
            }
        }
        if (box->children != NULL) {
            if (box->children->parent != box || box->children->prev != NULL)
                return NSERROR_INVALID;
            box = box->children;
            continue;
        }
        while (box != root && box->next == NULL) {
            if (box->parent == NULL || box->parent->last != box)
                return NSERROR_INVALID;
            box = box->parent;
        }
        if (box == root)
            break;
        if (box->next->prev != box || box->next->parent != box->parent)
            return NSERROR_INVALID;
        box = box->next;
    }
    if (!populate) {
        size_t expected = 0;
        for (size_t i = 0; i < plan->count; i++)
            expected += (fresh ? plan->entries[i].fresh : plan->entries[i].old) != NULL;
        if (seen != expected)
            return NSERROR_INVALID;
    }
    return NSERROR_OK;
}

nserror html_box_prepare_mappings(html_content *html, dom_node *scope, struct box *old_root,
    const struct box_construct_region *candidate, void *owner, struct html_box_mapping_plan **result)
{
    *result = NULL;
    if (html == NULL || scope == NULL || candidate == NULL)
        return NSERROR_BAD_PARAMETER;
    dom_node_type type;
    if (dom_node_get_node_type(scope, &type) != DOM_NO_ERR)
        return NSERROR_DOM;
    if (type != DOM_ELEMENT_NODE || (old_root != NULL && old_root->node != scope) ||
        (candidate->root != NULL &&
            (candidate->root->node != scope || candidate->root->parent != NULL || candidate->root->prev != NULL ||
                candidate->root->next != NULL)))
        return NSERROR_NOT_IMPLEMENTED;
    struct html_box_mapping_plan *plan = talloc_zero(owner, struct html_box_mapping_plan);
    if (plan == NULL)
        return NSERROR_NOMEM;
    talloc_set_destructor(plan, discard_plan);
    plan->html = html;
    plan->scope = scope; /* collect_mapping retains it, including a hidden root. */
    plan->old_root = old_root;
    plan->new_root = candidate->root;
    plan->candidate = candidate;
    plan->parent = old_root == NULL ? NULL : old_root->parent;
    plan->layout = html->layout;
    plan->bctx = html->bctx;
    plan->generation = html->restyle.dom_generation;
    nserror err = libdom_iterate_elements(scope, collect_mapping, plan);
    if (err != NSERROR_OK)
        goto failed;
    qsort(plan->entries, plan->count, sizeof(*plan->entries), compare_mapping);
    err = walk_boxes(plan, old_root, false, false);
    if (err == NSERROR_OK)
        err = walk_boxes(plan, candidate->root, true, true);
    if (err != NSERROR_OK)
        goto failed;
    for (size_t i = 0; i < plan->count; i++) {
        struct mapping *m = &plan->entries[i];
        if (m->old == m->fresh)
            continue;
        if (dom_node_prepare_user_data(corestring_dom___ns_key_box_node_data, m->fresh, NULL, &m->prepared) !=
            DOM_NO_ERR) {
            err = NSERROR_NOMEM;
            goto failed;
        }
    }
    *result = plan;
    return NSERROR_OK;
failed:
    talloc_free(plan);
    return err;
}

struct check_context {
    const struct html_box_mapping_plan *plan;
    size_t visited;
};

static nserror check_mapping(dom_node *node, void *pw)
{
    struct check_context *ctx = pw;
    const struct mapping *m = find_mapping(ctx->plan, node);
    if (m == NULL || m->order != ctx->visited++)
        return NSERROR_INVALID;
    dom_node *parent = NULL;
    if (dom_node_get_parent_node(node, &parent) != DOM_NO_ERR)
        return NSERROR_DOM;
    bool same = parent == m->parent;
    dom_node_unref(parent);
    struct box *mapped = NULL;
    if (dom_node_get_user_data(node, corestring_dom___ns_key_box_node_data, &mapped) != DOM_NO_ERR)
        return NSERROR_DOM;
    return same && mapped == m->old ? NSERROR_OK : NSERROR_INVALID;
}

nserror html_box_check_mappings(const struct html_box_mapping_plan *plan)
{
    if (plan->applied || plan->html->bctx != plan->bctx || plan->html->layout != plan->layout ||
        plan->html->restyle.dom_generation != plan->generation || plan->candidate->root != plan->new_root ||
        (plan->old_root != NULL && plan->old_root->parent != plan->parent) ||
        (plan->new_root != NULL &&
            (plan->new_root->parent != NULL || plan->new_root->prev != NULL || plan->new_root->next != NULL)))
        return NSERROR_INVALID;
    struct check_context ctx = {.plan = plan};
    nserror err = libdom_iterate_elements(plan->scope, check_mapping, &ctx);
    if (err != NSERROR_OK)
        return err;
    if (ctx.visited != plan->count)
        return NSERROR_INVALID;
    err = walk_boxes(plan, plan->old_root, false, false);
    return err == NSERROR_OK ? walk_boxes(plan, plan->new_root, true, false) : err;
}

nserror html_box_apply_mappings(struct html_box_mapping_plan *plan)
{
    if (!plan->html->base.locked || !plan->html->restyling || html_box_check_mappings(plan) != NSERROR_OK)
        return NSERROR_INVALID;
    for (size_t i = 0; i < plan->count; i++) {
        struct mapping *m = &plan->entries[i];
        if (m->prepared != NULL) {
            void *old;
            dom_node_set_prepared_user_data(m->node, m->prepared, &old);
            m->prepared = NULL;
        }
    }
    plan->applied = true;
    return NSERROR_OK;
}

void html_box_discard_mappings(struct html_box_mapping_plan *plan)
{
    talloc_free(plan);
}
#endif

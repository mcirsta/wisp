/* SPDX-License-Identifier: GPL-2.0-only */
#include <wisp/content/handlers/html/private.h>
#include <wisp/utils/nsurl.h>
#include "content/handlers/html/box_boundary.h"
#include "content/handlers/html/box_construct.h"
#include "content/handlers/html/box_mappings.h"
#include "content/handlers/html/box_publish.h"
#include "content/handlers/html/form_bindings.h"
#include "content/handlers/html/object.h"
#include "content/handlers/html/restyle.h"
#include "desktop/selection.h"
#include "utils/talloc.h"

/* These states can hold box pointers, text offsets or a frontend caret. Their
 * normal cleanup broadcasts callbacks. Keep that work in the existing fallback,
 * outside the no-callback publication interval. Unfocused outside widgets keep
 * their boxes, values and registration untouched. */
static bool interaction_idle(const html_content *html)
{
    return html->drag_type == HTML_DRAG_NONE && html->selection_type == HTML_SELECTION_NONE &&
        html->focus_type == HTML_FOCUS_SELF && html->visible_select_menu == NULL &&
        html->base.textsearch.context == NULL &&
        (html->sel == NULL || (!selection_active(html->sel) && !selection_dragging(html->sel)));
}

nserror html_box_publish_region(html_content *html, struct box_construct_region **candidate, const char **reason)
{
    const char *why = "publication_state";
    nserror err = NSERROR_NOT_IMPLEMENTED;
    void *preparation = NULL;
    if (html == NULL || candidate == NULL || *candidate == NULL) {
        err = NSERROR_BAD_PARAMETER;
        goto done;
    }
    struct box_construct_region *region = *candidate;
    if (!html->base.locked || !html->restyling || html->reflowing || html->box_conversion_context != NULL ||
        html->restyle_old_bctx != NULL || !html->restyle.partial || html->enable_scripting ||
        html->base.status != CONTENT_STATUS_DONE || html->base.active != 0)
        goto done;
    why = "multiple_regions";
    if (html->restyle.active.count != 1)
        goto done;
    /* Removing the boundary itself can join anonymous inline containers in its
     * parent. Replace a stable visible ancestor instead; panel descendants may
     * still hide/reveal freely inside that ancestor. */
    why = "hidden_boundary";
    if (region->root == NULL)
        goto done;
    why = "inputs_changed";
    if (html_restyle_needs_restart(html))
        goto done;
    why = "active_interaction";
    if (!interaction_idle(html))
        goto done;
    why = "frames";
    if (html->iframe != NULL || html->frameset != NULL || region->iframes != NULL)
        goto done;

    dom_node *scope = html->restyle.active.roots[0];
    struct box *old = box_for_node(scope);
    /* A hidden old root has no owned insertion boundary: widen before building
     * or fall back. The usual tab scope itself stays visible in both states. */
    err = html_box_check_boundary(html, old, region, &why);
    if (err != NSERROR_OK)
        goto done;
    void *old_owner = box_construct_region_owner(old);
    struct box *parent = old->parent, *prev = old->prev, *next = old->next;
    struct html_box_mapping_plan *mappings;
    struct html_form_binding_plan *forms;
    struct html_object_binding_plan *objects;
    preparation = talloc_new(NULL);
    if (preparation == NULL) {
        err = NSERROR_NOMEM;
        goto done;
    }
    why = "mappings";
    err = html_box_prepare_mappings(html, scope, old, region, preparation, &mappings);
    if (err != NSERROR_OK)
        goto done;
    why = "forms";
    err = html_form_prepare_bindings(html, old, region->root, preparation, &forms);
    if (err != NSERROR_OK)
        goto done;
    why = "resources";
    err = html_object_prepare_bindings(html, old, region, preparation, &objects);
    if (err != NSERROR_OK)
        goto done;

    /* No callbacks, fetches or yields occur during preparation. Preflight every
     * binding before the first write; each subsequent commit is infallible and
     * does not invalidate another plan's input. Resources are installed last,
     * as an object can change its candidate box type/dimension flags. */
    why = "stale_bindings";
    if ((err = html_box_check_mappings(mappings)) != NSERROR_OK ||
        (err = html_form_check_bindings(forms)) != NSERROR_OK ||
        (err = html_object_check_bindings(objects)) != NSERROR_OK)
        goto done;
    html->sticky_layout_valid = false;
    html_box_commit_mappings(mappings);
    html_form_commit_bindings(forms);
    html_object_commit_bindings(objects);

    struct box *fresh = region->root;
    talloc_steal(html->bctx, region);
    fresh->flags |= OWNS_REGION;
    fresh->parent = parent;
    fresh->prev = prev;
    fresh->next = next;
    if (prev != NULL)
        prev->next = fresh;
    else
        parent->children = fresh;
    if (next != NULL)
        next->prev = fresh;
    else
        parent->last = fresh;
    *candidate = NULL;
    talloc_free(preparation);
    preparation = NULL;
    /* Published resources use the document's existing records. Their staging
     * descriptors (including copied SVG bytes) have no further purpose. */
    while (region->objects != NULL) {
        struct box_construct_object *request = region->objects;
        region->objects = request->next;
        talloc_free(request);
    }
    talloc_free(old_owner);
    why = "ok";
done:
    talloc_free(preparation);
    if (reason != NULL)
        *reason = err == NSERROR_NOMEM ? "no_memory" : why;
    return err;
}

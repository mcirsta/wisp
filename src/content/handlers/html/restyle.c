/* SPDX-License-Identifier: GPL-2.0-only */
#include <string.h>
#include "content/handlers/html/restyle.h"
#include "content/handlers/html/restyle_profile.h"

#ifdef WISP_ENABLE_PARTIAL_UPDATES
static void clear_roots(struct html_restyle_changes *changes)
{
    for (uint32_t i = 0; i < changes->count; i++)
        dom_node_unref(changes->roots[i]);
    changes->count = 0;
}

/* Inclusive DOM ancestry; errors must widen the refresh rather than lose a root. */
static bool contains(dom_node *root, dom_node *node, bool *match)
{
    dom_node *current = dom_node_ref(node);
    *match = false;
    while (current != NULL) {
        if (current == root) {
            *match = true;
            dom_node_unref(current);
            return true;
        }
        dom_node *parent = NULL;
        dom_exception err = dom_node_get_parent_node(current, &parent);
        dom_node_unref(current);
        if (err != DOM_NO_ERR)
            return false;
        current = parent;
    }
    return true;
}
#endif

void html_restyle_record_full(html_content *html, const char *reason)
{
#ifdef WISP_ENABLE_PARTIAL_UPDATES
    clear_roots(&html->restyle.pending);
    html->restyle.pending.full_reason = reason;
#endif
}

void html_restyle_record_checked(html_content *html, dom_node *node)
{
#ifdef WISP_ENABLE_PARTIAL_UPDATES
    struct html_restyle_changes *changes = &html->restyle.pending;
    dom_node *parent = NULL;
    changes->changed_controls++;
    if (changes->full_reason != NULL)
        return;
    if (dom_node_get_parent_node(node, &parent) != DOM_NO_ERR || parent == NULL) {
        dom_node_unref(parent);
        html_restyle_record_full(html, "invalid_scope");
        return;
    }
    for (uint32_t i = 0; i < changes->count;) {
        bool match;
        if (!contains(changes->roots[i], parent, &match))
            goto invalid;
        if (match) {
            dom_node_unref(parent);
            return;
        }
        if (!contains(parent, changes->roots[i], &match))
            goto invalid;
        if (match) {
            dom_node_unref(changes->roots[i]);
            changes->roots[i] = changes->roots[--changes->count];
        } else {
            i++;
        }
    }
    if (changes->count == HTML_RESTYLE_MAX_ROOTS) {
        dom_node_unref(parent);
        html_restyle_record_full(html, "scope_capacity");
        return;
    }
    changes->roots[changes->count++] = parent;
    return;
invalid:
    dom_node_unref(parent);
    html_restyle_record_full(html, "invalid_scope");
#endif
}

void html_restyle_note_dom_change(html_content *html)
{
#ifdef WISP_ENABLE_PARTIAL_UPDATES
    if (html != NULL && html->had_initial_layout)
        html->restyle.dom_generation++;
#endif
}

void html_restyle_discard_pending(html_content *html)
{
#ifdef WISP_ENABLE_PARTIAL_UPDATES
    clear_roots(&html->restyle.pending);
    memset(&html->restyle.pending, 0, sizeof(html->restyle.pending));
#endif
}

void html_restyle_begin(html_content *html)
{
    HTML_PROFILE(html->restyle_profile.update_mode = "full");
#ifdef WISP_ENABLE_PARTIAL_UPDATES
    HTML_PROFILE(const char *reason = html->restyle.pending.full_reason;
        if (html->restyle.force_full) reason = "forced_full";
        else if (reason == NULL && html->restyle.dom_generation != html->restyle.styled_generation)
            reason = "dom_changed";
        else if (reason == NULL && html->enable_scripting) reason = "scripting";
        html->restyle_profile.fallback_reason = reason != NULL ? reason : "partial_not_implemented";
        html->restyle_profile.dirty_roots = html->restyle.pending.count;
        html->restyle_profile.changed_controls = html->restyle.pending.changed_controls);
    /* Until selective CSS is implemented every batch uses the full path. */
    html_restyle_discard_pending(html);
#else
    HTML_PROFILE(html->restyle_profile.fallback_reason = "feature_disabled");
#endif
}

void html_restyle_capture_inputs(html_content *html)
{
#ifdef WISP_ENABLE_PARTIAL_UPDATES
    /* Detachment may have synchronized additional edits. Construction follows. */
    html->restyle.active_generation = html->restyle.dom_generation;
#endif
}

void html_restyle_end(html_content *html, bool success)
{
#ifdef WISP_ENABLE_PARTIAL_UPDATES
    if (success)
        html->restyle.styled_generation = html->restyle.active_generation;
#endif
}

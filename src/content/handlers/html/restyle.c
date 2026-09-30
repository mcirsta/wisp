/* SPDX-License-Identifier: GPL-2.0-only */
#include <string.h>
#include <stdlib.h>
#include <wisp/utils/nsoption.h>
#include <wisp/content/handlers/html/box.h>
#include "content/handlers/html/box_construct.h"
#include "content/handlers/html/font_face.h"
#include "content/handlers/css/select.h"
#include "utils/libdom.h"
#include "content/urldb.h"
#include "content/handlers/html/restyle.h"
#include "content/handlers/html/restyle_profile.h"

#ifdef WISP_ENABLE_PARTIAL_UPDATES
struct html_restyle_entry {
    dom_node *node;
    const css_select_results *styles; /* Borrowed until the old bctx is retired. */
};

static struct html_restyle_inputs inputs(html_content *html)
{
    struct html_restyle_inputs value = {0};
    value.ctx = html->select_ctx;
    value.base_url = html->base_url;
    value.visited_generation = urldb_visited_generation();
    memcpy(value.media, &html->media, sizeof(value.media));
    const css_unit_ctx *u = &html->unit_len_ctx;
    const css_fixed lengths[] = {u->viewport_width, u->viewport_height, u->container_width, u->container_height,
        u->font_size_default, u->font_size_minimum, u->device_dpi};
    memcpy(value.units, lengths, sizeof(lengths));
    value.width = html->base.available_width;
    value.height = html->base.available_height;
    value.font_default = nsoption_int(font_default);
    value.font_size = nsoption_int(font_size);
    value.font_min_size = nsoption_int(font_min_size);
    value.author_css = nsoption_bool(author_level_css);
    value.font_generation = html_font_face_generation();
    if (css_select_ctx_checked_parent_scope(html->select_ctx, &value.valid, &value.css_revision) != CSS_OK)
        value.valid = false;
    /* There is no client font-measure callback today. A future callback needs
     * its own invalidation inputs before its composed values can be reused. */
    if (u->measure != NULL)
        value.valid = false;
    return value;
}

static bool same_inputs(const struct html_restyle_inputs *a, const struct html_restyle_inputs *b)
{
    /* Copy media bytes directly from the same content object, preserving every
     * field (including future additions). Padding can only widen invalidation. */
    return a->valid && b->valid && a->ctx == b->ctx && a->base_url == b->base_url &&
        a->css_revision == b->css_revision && a->font_generation == b->font_generation &&
        a->visited_generation == b->visited_generation && a->width == b->width && a->height == b->height &&
        a->font_default == b->font_default && a->font_size == b->font_size && a->font_min_size == b->font_min_size &&
        a->author_css == b->author_css && memcmp(a->units, b->units, sizeof(a->units)) == 0 &&
        memcmp(a->media, b->media, sizeof(a->media)) == 0;
}

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

#ifdef WISP_ENABLE_PARTIAL_UPDATES
static void discard_active(html_content *html)
{
    clear_roots(&html->restyle.active);
    memset(&html->restyle.active, 0, sizeof(html->restyle.active));
    free(html->restyle.entries);
    html->restyle.entries = NULL;
    html->restyle.count = html->restyle.capacity = 0;
    html->restyle.partial = false;
}

static int compare_entries(const void *a, const void *b)
{
    uintptr_t x = (uintptr_t)((const struct html_restyle_entry *)a)->node;
    uintptr_t y = (uintptr_t)((const struct html_restyle_entry *)b)->node;
    return (x > y) - (x < y);
}

static nserror collect_style(dom_node *node, void *pw)
{
    html_content *html = pw;
    struct html_restyle_state *state = &html->restyle;
    struct box *box = box_for_node(node);
    if (box == NULL || box->styles == NULL)
        return NSERROR_OK;
    for (uint32_t i = 0; i < state->active.count; i++) {
        bool inside;
        if (!contains(state->active.roots[i], node, &inside))
            return NSERROR_DOM;
        if (inside)
            return NSERROR_OK;
    }
    if (state->count == state->capacity) {
        if (state->capacity > SIZE_MAX / 2 / sizeof(*state->entries))
            return NSERROR_NOMEM;
        size_t capacity = state->capacity ? state->capacity * 2 : 128;
        struct html_restyle_entry *entries = realloc(state->entries, capacity * sizeof(*entries));
        if (entries == NULL)
            return NSERROR_NOMEM;
        state->entries = entries;
        state->capacity = capacity;
        HTML_PROFILE(html->restyle_profile.temporary_bytes = capacity * sizeof(*entries));
    }
    state->entries[state->count++] = (struct html_restyle_entry){node, box->styles};
    return NSERROR_OK;
}
#endif

void html_restyle_reset(html_content *html)
{
    html_restyle_discard_pending(html);
#ifdef WISP_ENABLE_PARTIAL_UPDATES
    discard_active(html);
    html->restyle.styled_inputs.valid = false;
#endif
}

void html_restyle_close(html_content *html)
{
    /* Content with remaining users can finish after its window closes. Keep the
     * active table alive until the next slice restarts fully or the build is
     * cancelled (on destruction or the last user's removal). Simply clearing
     * partial would leave uninvalidated node data. Advance the generation before
     * dropping any newer checked-state batch. */
    html_restyle_note_dom_change(html);
    html_restyle_discard_pending(html);
    if (!html->restyling)
        html_restyle_reset(html);
}

void html_restyle_use_full(html_content *html, const char *reason)
{
#ifdef WISP_ENABLE_PARTIAL_UPDATES
    discard_active(html);
#endif
    HTML_PROFILE(html->restyle_profile.update_mode = "full"; html->restyle_profile.fallback_reason = reason);
}

void html_restyle_begin(html_content *html)
{
    HTML_PROFILE(html->restyle_profile.update_mode = "full");
#ifdef WISP_ENABLE_PARTIAL_UPDATES
    struct html_restyle_state *state = &html->restyle;
    discard_active(html);
    state->active = state->pending;
    memset(&state->pending, 0, sizeof(state->pending));
    HTML_PROFILE(html->restyle_profile.dirty_roots = state->active.count;
        html->restyle_profile.changed_controls = state->active.changed_controls);
    struct html_restyle_inputs current = inputs(html);
    const char *reason = state->active.full_reason;
    if (state->force_full)
        reason = "forced_full";
    else if (reason == NULL && state->dom_generation != state->styled_generation)
        reason = "dom_changed";
    else if (reason == NULL && html->enable_scripting)
        reason = "scripting";
    else if (reason == NULL && !current.valid)
        reason = "selector_scope";
    else if (reason == NULL && (!html->had_initial_layout || html->base.status != CONTENT_STATUS_DONE ||
                                 html->base.active != 0 || html_font_face_has_pending()))
        reason = "loading";
    else if (reason == NULL && !state->styled_inputs.valid)
        reason = "no_style_snapshot";
    else if (reason == NULL && !same_inputs(&current, &state->styled_inputs))
        reason = "inputs_changed";
    else if (reason == NULL && state->active.count == 0)
        reason = "no_scope";
    if (reason == NULL) {
        nserror err = libdom_iterate_elements((dom_node *)html->document, collect_style, html);
        if (err != NSERROR_OK)
            reason = "reuse_table_failed";
        else if (state->count == 0)
            reason = "no_reusable_styles";
    }
    if (reason != NULL) {
        html_restyle_use_full(html, reason);
        return;
    }
    qsort(state->entries, state->count, sizeof(*state->entries), compare_entries);
    state->partial = true;
    HTML_PROFILE(html->restyle_profile.update_mode = "partial_css";
        html->restyle_profile.fallback_reason = NULL;
        html->restyle_profile.temporary_bytes = state->capacity * sizeof(*state->entries));
#else
    HTML_PROFILE(html->restyle_profile.fallback_reason = "feature_disabled");
#endif
}

nserror html_restyle_invalidate(html_content *html)
{
    uint32_t count = 0;
    nserror err;
#ifdef WISP_ENABLE_PARTIAL_UPDATES
    if (html->restyle.partial) {
        for (uint32_t i = 0; i < html->restyle.active.count; i++) {
            uint32_t part;
            err = nscss_invalidate_subtree(html->restyle.active.roots[i], &part);
            count += part;
            if (err != NSERROR_OK)
                return err;
        }
    } else
#endif
    {
        err = nscss_invalidate_subtree((dom_node *)html->document, &count);
        if (err != NSERROR_OK)
            return err;
    }
    HTML_PROFILE(html->restyle_profile.invalidated_nodes += count);
    return NSERROR_OK;
}

nserror html_restyle_capture_inputs(html_content *html)
{
#ifdef WISP_ENABLE_PARTIAL_UPDATES
    /* Detachment can synchronize a previously unseen selector-visible edit. */
    if (html->restyle.partial && html->restyle.dom_generation != html->restyle.styled_generation) {
        html_restyle_use_full(html, "detach_changed");
        nserror err = html_restyle_invalidate(html);
        if (err != NSERROR_OK)
            return err;
    }
    html->restyle.active_generation = html->restyle.dom_generation;
    html->restyle.active_inputs = inputs(html);
#endif
    return NSERROR_OK;
}

nserror html_restyle_reuse_style(html_content *html, dom_node *node, css_select_results **styles)
{
    *styles = NULL;
#ifdef WISP_ENABLE_PARTIAL_UPDATES
    if (html->restyle.partial) {
        struct html_restyle_entry key = {.node = node};
        const struct html_restyle_entry *entry = bsearch(&key, html->restyle.entries, html->restyle.count,
            sizeof(*entry), compare_entries);
        css_select_results *clone;
        if (entry != NULL && css_select_results_clone(entry->styles, &clone) == CSS_OK) {
            HTML_PROFILE(html->restyle_profile.reused_styles++;
                html->restyle_profile.cloned_result_bytes += sizeof(*clone);
                for (unsigned i = 0; i < CSS_PSEUDO_ELEMENT_COUNT; i++)
                    html->restyle_profile.style_references += clone->styles[i] != NULL);
            *styles = clone;
            return NSERROR_OK;
        }
        /* Hidden elements can retain node data without a box-owned result. A
         * fresh selection must replace that node's data, while keeping its
         * ancestors' environments. This also handles clone allocation failure. */
        uint32_t invalidated;
        nserror err = nscss_invalidate_node(node, &invalidated);
        HTML_PROFILE(html->restyle_profile.invalidated_nodes += invalidated);
        return err;
    }
#endif
    return NSERROR_OK;
}

bool html_restyle_needs_restart(html_content *html)
{
#ifdef WISP_ENABLE_PARTIAL_UPDATES
    if (html->restyle.partial) {
        HTML_PROFILE(if (html->restyle.force_restart &&
            (html->restyle.css_only || html->restyle_old_bctx == NULL) &&
            html->restyle_profile.styled_elements + html->restyle_profile.reused_styles != 0) {
            html->restyle.force_restart = false;
            html->restyle.dom_generation++;
        });
        struct html_restyle_inputs current = inputs(html);
        return html->restyle.dom_generation != html->restyle.active_generation ||
            html->restyle.pending.changed_controls != 0 || html->restyle.pending.full_reason != NULL ||
            !same_inputs(&current, &html->restyle.active_inputs);
    }
#endif
    return false;
}

void html_restyle_end(html_content *html, bool success)
{
#ifdef WISP_ENABLE_PARTIAL_UPDATES
    if (success) {
        struct html_restyle_inputs current = inputs(html);
        html->restyle.styled_generation = html->restyle.active_generation;
        html->restyle.styled_inputs = html->restyle.active_inputs;
        if (!same_inputs(&current, &html->restyle.active_inputs))
            html->restyle.styled_inputs.valid = false;
    } else {
        html->restyle.styled_inputs.valid = false;
    }
    discard_active(html);
#endif
}

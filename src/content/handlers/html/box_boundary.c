/* SPDX-License-Identifier: GPL-2.0-only */
#include <limits.h>
#include <stdlib.h>
#include <wisp/content/handlers/html/private.h>
#include "content/handlers/html/box_boundary.h"
#include "content/handlers/html/box_construct.h"
#include "utils/talloc.h"

struct allocation {
    uintptr_t start;
    size_t size;
};

struct arena {
    struct allocation *allocations;
    size_t count;
    bool references;
};

struct boundary {
    const struct box *old_root;
    struct arena old, fresh;
    uintptr_t *styles;
    size_t count, capacity;
    const char *reason;
};

static nserror reject(struct boundary *b, const char *reason)
{
    b->reason = reason;
    return NSERROR_NOT_IMPLEMENTED;
}

static void record_allocation(const void *ptr, int depth, int max_depth, int is_ref, void *pw)
{
    struct arena *arena = pw;
    if (is_ref) {
        arena->references = true;
        return;
    }
    arena->allocations[arena->count++] = (struct allocation){(uintptr_t)ptr, talloc_get_size(ptr)};
}

static int compare_allocation(const void *a, const void *b)
{
    uintptr_t x = ((const struct allocation *)a)->start, y = ((const struct allocation *)b)->start;
    return (x > y) - (x < y);
}

static nserror collect_arena(struct boundary *b, const void *owner, struct arena *arena)
{
    size_t count = talloc_total_blocks(owner);
    if (count > UINT_MAX)
        return NSERROR_NOMEM;
    arena->allocations = talloc_array(b, struct allocation, count);
    if (arena->allocations == NULL)
        return NSERROR_NOMEM;
    talloc_report_depth_cb(owner, 0, -1, record_allocation, arena);
    if (arena->references)
        return reject(b, "arena_reference");
    qsort(arena->allocations, arena->count, sizeof(*arena->allocations), compare_allocation);
    return NSERROR_OK;
}

/* Text fragments point inside allocations; never pass a borrowed/interior/static
 * pointer to talloc_parent(). The temporary address ranges also include hidden
 * construction allocations that are absent from the physical box tree. */
static bool in_arena(const struct arena *arena, const void *ptr)
{
    if (ptr == NULL)
        return false;
    uintptr_t address = (uintptr_t)ptr;
    size_t lo = 0, hi = arena->count;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (arena->allocations[mid].start <= address)
            lo = mid + 1;
        else
            hi = mid;
    }
    if (lo == 0)
        return false;
    const struct allocation *a = &arena->allocations[lo - 1];
    return address == a->start || address - a->start < a->size;
}

static int compare_style(const void *a, const void *b)
{
    uintptr_t x = *(const uintptr_t *)a, y = *(const uintptr_t *)b;
    return (x > y) - (x < y);
}

static nserror keep_style(struct boundary *b, const css_computed_style *style)
{
    if (style == NULL)
        return NSERROR_OK;
    if (b->count == b->capacity) {
        if (b->capacity > UINT_MAX / 2)
            return NSERROR_NOMEM;
        size_t capacity = b->capacity == 0 ? 64 : b->capacity * 2;
        uintptr_t *styles = talloc_realloc(b, b->styles, uintptr_t, capacity);
        if (styles == NULL)
            return NSERROR_NOMEM;
        b->styles = styles;
        b->capacity = capacity;
    }
    b->styles[b->count++] = (uintptr_t)style;
    return NSERROR_OK;
}

static nserror check_box(struct boundary *b, const struct box *box, bool inside, bool fresh, bool styles)
{
    bool retiring = in_arena(&b->old, box);
    if ((!fresh && retiring != inside) || (fresh && (!in_arena(&b->fresh, box) || retiring)))
        return reject(b, "box_ownership");
    if (box->children == NULL ? box->last != NULL
                              : (box->children->parent != box || box->children->prev != NULL || box->last == NULL ||
                                    box->last->next != NULL))
        return reject(b, "tree_links");
    if (box->next != NULL && (box->next->prev != box || box->next->parent != box->parent))
        return reject(b, "tree_links");
    if (box->list_marker != NULL) {
        const struct box *marker = box->list_marker;
        bool outside = box_has_outside_marker(box);
        if (marker->prev != NULL || marker->children != NULL || marker->list_marker != NULL ||
            (outside ? marker->next != NULL :
                (marker->parent != box->children || !box_is_inside_marker(marker) ||
                    marker->parent->children != marker)))
            return reject(b, "marker_links");
    }
    const struct box *links[] = {box->inline_end, box->float_children, box->next_float, box->float_container};
    for (size_t i = 0; i < sizeof(links) / sizeof(links[0]); i++) {
        if (links[i] != NULL && (fresh ? !in_arena(&b->fresh, links[i]) : in_arena(&b->old, links[i]) != inside))
            return reject(b, i == 0 ? "inline_crossing" : "float_crossing");
    }
    /* Outbound absolute/fixed links to a surviving ancestor or viewport are
     * safe to retire. An outside box pointing into the retiring arena is not. */
    if ((fresh || !inside) && in_arena(&b->old, box->abs_containing_block))
        return reject(b, "containing_block_crossing");
    if (!fresh && inside)
        return NSERROR_OK;
    const void *storage[] = {
        box->text, box->title, box->target, box->col, box->usemap, box->background_layers, box->object_params};
    for (size_t i = 0; i < sizeof(storage) / sizeof(storage[0]); i++)
        if (in_arena(&b->old, storage[i]))
            return reject(b, "borrowed_storage");
    if (!styles) {
        if (box->flags & CLONE)
            return NSERROR_OK; /* Layout clones borrow, even if they copied flags. */
        nserror err = NSERROR_OK;
        if (box->flags & STYLE_OWNED)
            err = keep_style(b, box->style);
        for (unsigned i = 0; err == NSERROR_OK && box->styles != NULL && i < CSS_PSEUDO_ELEMENT_COUNT; i++)
            err = keep_style(b, box->styles->styles[i]);
        return err;
    }
    const css_computed_style *borrowed[] = {box->style, box->space_style};
    for (size_t i = 0; i < sizeof(borrowed) / sizeof(borrowed[0]); i++) {
        if (borrowed[i] != NULL) {
            uintptr_t key = (uintptr_t)borrowed[i];
            if (b->count == 0 || bsearch(&key, b->styles, b->count, sizeof(key), compare_style) == NULL)
                return reject(b, "borrowed_style");
        }
    }
    return NSERROR_OK;
}

/* Include detached list markers, which are not in the children/next chain.
 * Engine-produced trees must be acyclic; this is not a corrupt-tree parser. */
static nserror walk_tree(struct boundary *b, const struct box *root, bool fresh, bool styles)
{
    bool inside = false, seen = false;
    for (const struct box *box = root; box != NULL;) {
        if (box == b->old_root)
            inside = seen = true;
        nserror err = check_box(b, box, inside, fresh, styles);
        if (err != NSERROR_OK)
            return err;
        /* Find the next box, clearing membership when the walk leaves the old
         * subtree. No arena lookup or ancestor search is needed for that state. */
        if (box_has_outside_marker(box) || box->children != NULL) {
            box = box_has_outside_marker(box) ? box->list_marker : box->children;
            continue;
        }
        while (box != root) {
            const struct box *parent = box->parent;
            if (box == b->old_root)
                inside = false;
            if (box == parent->list_marker) {
                if (parent->children != NULL) {
                    box = parent->children;
                    break;
                }
            } else if (box->next != NULL) {
                box = box->next;
                break;
            }
            if (parent->last != box && parent->list_marker != box)
                return reject(b, "tree_links");
            box = parent;
        }
        if (box == root)
            box = NULL;
    }
    return fresh || seen ? NSERROR_OK : reject(b, "root_not_live");
}

nserror html_box_check_boundary(const html_content *html, const struct box *old_root,
    const struct box_construct_region *candidate, const char **reason)
{
    if (reason != NULL)
        *reason = "no_owned_region";
    void *owner = box_construct_region_owner(old_root);
    if (html == NULL || html->layout == NULL || owner == NULL || old_root == html->layout || old_root->parent == NULL ||
        (old_root->type != BOX_BLOCK && old_root->type != BOX_FLEX && old_root->type != BOX_GRID) ||
        (old_root->flags & CLONE))
        return NSERROR_NOT_IMPLEMENTED;
    if (candidate != NULL && candidate->root != NULL &&
        (candidate->root->parent != NULL || candidate->root->prev != NULL || candidate->root->next != NULL)) {
        if (reason != NULL)
            *reason = "candidate_attached";
        return NSERROR_NOT_IMPLEMENTED;
    }
    struct boundary *b = talloc_zero(NULL, struct boundary);
    if (b == NULL) {
        if (reason != NULL)
            *reason = "no_memory";
        return NSERROR_NOMEM;
    }
    b->old_root = old_root;
    nserror err = collect_arena(b, owner, &b->old);
    if (err == NSERROR_OK && in_arena(&b->old, candidate))
        err = reject(b, "candidate_ownership");
    if (err == NSERROR_OK && candidate != NULL)
        err = collect_arena(b, candidate, &b->fresh);
    for (unsigned pass = 0; err == NSERROR_OK && pass < 2; pass++) {
        err = walk_tree(b, html->layout, false, pass != 0);
        if (err == NSERROR_OK)
            err = check_box(b, &html->viewport.box, false, false, pass != 0);
        if (err == NSERROR_OK && candidate != NULL)
            err = walk_tree(b, candidate->root, true, pass != 0);
        if (pass == 0 && b->count != 0)
            qsort(b->styles, b->count, sizeof(*b->styles), compare_style);
    }
    if (reason != NULL)
        *reason = err == NSERROR_OK ? "ok" : err == NSERROR_NOMEM ? "no_memory" : b->reason;
    talloc_free(b);
    return err;
}

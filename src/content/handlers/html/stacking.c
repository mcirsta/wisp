/*
 * Copyright 2024 Neosurf contributors
 *
 * This file is part of Neosurf, http://www.netsurf-browser.org/
 *
 * Neosurf is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * Neosurf is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * \file
 * CSS z-index stacking context utilities implementation.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include <wisp/content/handlers/html/box.h>
#include <wisp/content/handlers/html/box_inspect.h>
#include <wisp/utils/utils.h>
#include "content/handlers/html/stacking.h"
#include "desktop/scrollbar.h"

/* See stacking.h for documentation */
bool box_creates_stacking_context(const struct box *box)
{
    return box_get_z_index(box) != Z_INDEX_AUTO;
}

/* See stacking.h for documentation */
int32_t box_get_z_index(const struct box *box)
{
    if (box == NULL || box->style == NULL) {
        return Z_INDEX_AUTO;
    }

    /* Transforms, fixed and sticky boxes establish stacking contexts even with auto
     * z-index. Keep their descendants inside that context during collection. */
    uint32_t count = 0;
    const css_transform_function *functions = NULL;
    bool transformed = box->node &&
        css_computed_transform(box->style, &count, &functions) == CSS_TRANSFORM_FUNCTIONS && count > 0;
    uint8_t pos = css_computed_position(box->style);
    bool is_positioned = (pos == CSS_POSITION_RELATIVE || pos == CSS_POSITION_ABSOLUTE ||
        pos == CSS_POSITION_FIXED || pos == CSS_POSITION_STICKY);

    if (!is_positioned) {
        return transformed ? 0 : Z_INDEX_AUTO;
    }

    int32_t z_index;
    uint8_t z_type = css_computed_z_index(box->style, &z_index);

    return (z_type == CSS_Z_INDEX_SET) ? z_index :
        ((pos == CSS_POSITION_FIXED || pos == CSS_POSITION_STICKY || transformed) ? 0 : Z_INDEX_AUTO);
}

int32_t box_get_paint_z_index(const struct box *box)
{
    int32_t z = box_get_z_index(box);
    if (z == Z_INDEX_AUTO && box && box->style &&
        css_computed_position(box->style) != CSS_POSITION_STATIC)
        return 0;
    return z;
}

/* See stacking.h for documentation */
int stacking_compare_z_index(const void *a, const void *b)
{
    const struct box *box_a = *(const struct box **)a;
    const struct box *box_b = *(const struct box **)b;

    int32_t z_a = box_get_z_index(box_a);
    int32_t z_b = box_get_z_index(box_b);

    /* Treat auto as 0 for sorting purposes */
    if (z_a == Z_INDEX_AUTO)
        z_a = 0;
    if (z_b == Z_INDEX_AUTO)
        z_b = 0;

    if (z_a < z_b)
        return -1;
    if (z_a > z_b)
        return 1;
    return 0;
}

/** Compare zindex_entry by z_index for stable_sort */
static int zindex_entry_compare(const void *a, const void *b)
{
    const struct zindex_entry *ea = (const struct zindex_entry *)a;
    const struct zindex_entry *eb = (const struct zindex_entry *)b;

    if (ea->z_index < eb->z_index)
        return -1;
    if (ea->z_index > eb->z_index)
        return 1;
    return 0;
}

/* See stacking.h for documentation */
void stacking_context_init(struct stacking_context *ctx)
{
    ctx->entries = NULL;
    ctx->count = 0;
    ctx->capacity = 0;
}

/* See stacking.h for documentation */
bool stacking_context_add(struct stacking_context *ctx, struct box *box, int32_t z_index, int x_parent, int y_parent)
{
    /* Grow array if needed */
    if (ctx->count >= ctx->capacity) {
        size_t new_capacity = (ctx->capacity == 0) ? 16 : ctx->capacity * 2;
        struct zindex_entry *new_entries = realloc(ctx->entries, new_capacity * sizeof(*new_entries));
        if (new_entries == NULL) {
            return false;
        }
        ctx->entries = new_entries;
        ctx->capacity = new_capacity;
    }

    /* Add entry */
    ctx->entries[ctx->count].box = box;
    ctx->entries[ctx->count].z_index = z_index;
    ctx->entries[ctx->count].x_parent = x_parent;
    ctx->entries[ctx->count].y_parent = y_parent;
    ctx->count++;

    return true;
}

/* See stacking.h for documentation */
void stacking_context_sort(struct stacking_context *ctx)
{
    if (ctx->count <= 1) {
        return;
    }
    /* Use stable_sort to preserve document order for equal z-index */
    stable_sort(ctx->entries, ctx->count, sizeof(*ctx->entries), zindex_entry_compare);
}

/* See stacking.h for documentation */
void stacking_context_fini(struct stacking_context *ctx)
{
    free(ctx->entries);
    ctx->entries = NULL;
    ctx->count = 0;
    ctx->capacity = 0;
}

/* See stacking.h for documentation */
bool stacking_context_collect_subtree(struct stacking_context *negative_ctx, struct stacking_context *positive_ctx,
    struct box *box, int x_parent, int y_parent, const struct stacking_coordinates *coordinates)
{
    struct box *c;
    int x_offset, y_offset;

    /* Transparent subtrees must not leak positioned descendants into an
     * ancestor's deferred paint list. Layout and hit testing are unaffected. */
    if (box == NULL || (box->flags & IS_OPACITY_ZERO)) {
        return true;
    }

    if (box->abs_containing_block || box->type == BOX_FLOAT_LEFT || box->type == BOX_FLOAT_RIGHT) {
        int x, y;
        box_coords_at_scroll(box, &x, &y, coordinates->scroll_x, coordinates->scroll_y);
        x_parent = x + coordinates->origin_x - box->x;
        y_parent = y + coordinates->origin_y - box->y;
    }
    /* Calculate this box's content position, including element scrolling. */
    x_offset = x_parent + box->x;
    y_offset = y_parent + box->y;
    if (box->scroll_x)
        x_offset -= scrollbar_get_offset(box->scroll_x);
    if (box->scroll_y)
        y_offset -= scrollbar_get_offset(box->scroll_y);

    int32_t z = box_get_paint_z_index(box);
    if (z != Z_INDEX_AUTO) {
        /* Collect into appropriate list based on z-index sign */
        struct stacking_context *ctx = (z < 0) ? negative_ctx : positive_ctx;
        if (!stacking_context_add(ctx, box, z, x_parent, y_parent)) {
            return false; /* Allocation failed */
        }
        /* Auto positioning defers paint without isolating descendants. */
        if (box_creates_stacking_context(box))
            return true;
    }

    /* Collect in structural tree order, including floats. Their separate
     * formatting-context list is a layout/paint index, not document order. */
    for (c = box->children; c; c = c->next) {
        if (!stacking_context_collect_subtree(negative_ctx, positive_ctx, c, x_offset, y_offset, coordinates)) {
            return false;
        }
    }

    return true;
}

enum box_paint_phase stacking_box_phase(const struct box *box)
{
    if (box->type == BOX_FLOAT_LEFT || box->type == BOX_FLOAT_RIGHT)
        return BOX_PAINT_FLOAT;
    if (box->type == BOX_INLINE_CONTAINER || box->type == BOX_INLINE || box->type == BOX_INLINE_END ||
        box->type == BOX_TEXT || box->type == BOX_BR || box->type == BOX_INLINE_BLOCK || box->type == BOX_INLINE_FLEX ||
        box->type == BOX_INLINE_GRID ||
        (box->type == BOX_TABLE && box->parent && box->parent->type == BOX_INLINE_CONTAINER))
        return BOX_PAINT_INLINE;
    /* Flex/grid items paint like inline-blocks (Flexbox §4.3, Grid §6.5). */
    if (box->parent &&
        (box->parent->type == BOX_FLEX || box->parent->type == BOX_INLINE_FLEX || box->parent->type == BOX_GRID ||
            box->parent->type == BOX_INLINE_GRID))
        return BOX_PAINT_INLINE;
    return BOX_PAINT_BLOCK;
}

bool stacking_box_is_atomic(const struct box *box)
{
    enum box_paint_phase phase = stacking_box_phase(box);
    return phase == BOX_PAINT_FLOAT ||
        (phase == BOX_PAINT_INLINE && box->type != BOX_INLINE_CONTAINER && box->type != BOX_INLINE &&
            box->type != BOX_INLINE_END && box->type != BOX_TEXT && box->type != BOX_BR);
}

void stacking_float_parent_coords(struct box *box, int *x, int *y)
{
    if (box->type != BOX_FLOAT_LEFT && box->type != BOX_FLOAT_RIGHT)
        return;
    int bx, by, px, py;
    box_coords_unscrolled(box, &bx, &by);
    box_coords_unscrolled(box->parent, &px, &py);
    *x += bx - px - box->x;
    *y += by - py - box->y;
}

/* Find a hit's phase in the common ancestor's paint group. Atomic inlines
 * and floats contain ordinary descendants, but never trap positioned ones. */
static void stacking_branch_level(const struct box *box, const struct box *ancestor, int32_t *z, int *phase)
{
    bool positioned = false;
    *z = 0;
    *phase = box == ancestor ? BOX_PAINT_BLOCK : stacking_box_phase(box);
    for (const struct box *p = box; p != ancestor; p = p->parent) {
        int32_t level = box_get_z_index(p);
        if (level != Z_INDEX_AUTO) {
            *z = level;
            positioned = true;
        } else if (box_get_paint_z_index(p) != Z_INDEX_AUTO) {
            positioned = true;
        } else if (!positioned && stacking_box_is_atomic(p)) {
            *phase = stacking_box_phase(p);
        }
    }
    if (positioned)
        *phase = BOX_PAINT_ALL; /* Positioned zero/positive paint after inlines. */
}

/* Compare the outermost stacking contexts below the common ancestor, then
 * paint phase and tree order. The hit walker's float order is not paint order. */
bool stacking_box_above(const struct box *a, const struct box *b)
{
    if (a == NULL || b == NULL)
        return false;
    const struct box *ac = a, *bc = b;
    int ad = 0, bd = 0;
    for (const struct box *p = a; p != NULL; p = p->parent)
        ad++;
    for (const struct box *p = b; p != NULL; p = p->parent)
        bd++;
    while (ad > bd) {
        ac = ac->parent;
        ad--;
    }
    while (bd > ad) {
        bc = bc->parent;
        bd--;
    }
    while (ac != bc) {
        ac = ac->parent;
        bc = bc->parent;
    }
    /* A real context's background is below even its negative children. An
     * auto-positioned ancestor does not isolate those children this way. */
    if (ac && (!ac->parent || box_creates_stacking_context(ac))) {
        if (a == ac)
            return false;
        if (b == ac)
            return true;
    }
    int32_t az, bz;
    int ap, bp;
    stacking_branch_level(a, ac, &az, &ap);
    stacking_branch_level(b, ac, &bz, &bp);
    if (az != bz)
        return az > bz;
    if (ap != bp)
        return ap > bp;
    /* All equal phases use structural tree order, including floats. The
     * interaction walker visits the formatting-context float list first. */
    if (ac && a != ac && b != ac) {
        while (a->parent != ac)
            a = a->parent;
        while (b->parent != ac)
            b = b->parent;
        for (const struct box *p = a->next; p; p = p->next)
            if (p == b)
                return false;
        return true;
    }
    return b == ac && a != ac;
}

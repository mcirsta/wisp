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
#include <wisp/utils/utils.h>
#include "content/handlers/html/stacking.h"

/* See stacking.h for documentation */
bool box_creates_stacking_context(const struct box *box)
{
    if (box == NULL || box->style == NULL) {
        return false;
    }

    uint8_t pos = css_computed_position(box->style);
    bool is_positioned = (pos == CSS_POSITION_RELATIVE || pos == CSS_POSITION_ABSOLUTE || pos == CSS_POSITION_FIXED);

    if (!is_positioned) {
        return false;
    }

    int32_t z_index;
    uint8_t z_type = css_computed_z_index(box->style, &z_index);

    /* Stacking context if positioned AND z-index is not auto */
    return z_type == CSS_Z_INDEX_SET;
}

/* See stacking.h for documentation */
int32_t box_get_z_index(const struct box *box)
{
    if (box == NULL || box->style == NULL) {
        return Z_INDEX_AUTO;
    }

    /* Only positioned elements can have z-index */
    uint8_t pos = css_computed_position(box->style);
    bool is_positioned = (pos == CSS_POSITION_RELATIVE || pos == CSS_POSITION_ABSOLUTE || pos == CSS_POSITION_FIXED);

    if (!is_positioned) {
        return Z_INDEX_AUTO;
    }

    int32_t z_index;
    uint8_t z_type = css_computed_z_index(box->style, &z_index);

    return (z_type == CSS_Z_INDEX_SET) ? z_index : Z_INDEX_AUTO;
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
    struct box *box, int x_parent, int y_parent)
{
    struct box *c;
    int x_offset, y_offset;

    if (box == NULL) {
        return true;
    }

    /* Calculate this box's position */
    x_offset = x_parent + box->x;
    y_offset = y_parent + box->y;

    /* Check if this box has explicit z-index */
    int32_t z = box_get_z_index(box);
    if (z != Z_INDEX_AUTO) {
        /* Collect into appropriate list based on z-index sign */
        struct stacking_context *ctx = (z < 0) ? negative_ctx : positive_ctx;
        if (!stacking_context_add(ctx, box, z, x_parent, y_parent)) {
            return false; /* Allocation failed */
        }
        /* Don't recurse into this subtree - it will be rendered
         * separately when we render this deferred element */
        return true;
    }

    /* Recurse into children to find positioned descendants */
    for (c = box->children; c; c = c->next) {
        if (c->type == BOX_FLOAT_LEFT || c->type == BOX_FLOAT_RIGHT) {
            continue; /* Handle floats separately */
        }
        if (!stacking_context_collect_subtree(negative_ctx, positive_ctx, c, x_offset, y_offset)) {
            return false;
        }
    }

    /* Also check float children */
    for (c = box->float_children; c; c = c->next_float) {
        if (!stacking_context_collect_subtree(negative_ctx, positive_ctx, c, x_offset, y_offset)) {
            return false;
        }
    }

    return true;
}

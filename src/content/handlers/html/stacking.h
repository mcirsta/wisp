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
 * CSS z-index stacking context utilities interface.
 */

#ifndef WISP_HTML_STACKING_H
#define WISP_HTML_STACKING_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct box;

/** Value indicating z-index: auto */
#define Z_INDEX_AUTO INT32_MIN

/**
 * Determine if a box creates a stacking context.
 *
 * A box creates a stacking context if it is positioned
 * (absolute, relative, or fixed) AND has an explicit z-index value.
 *
 * \param box  Box to check
 * \return true if box creates a stacking context
 */
bool box_creates_stacking_context(const struct box *box);

/**
 * Get the effective z-index for a box.
 *
 * Returns Z_INDEX_AUTO for z-index: auto or non-positioned elements.
 *
 * \param box  Box to get z-index for
 * \return z-index value, or Z_INDEX_AUTO
 */
int32_t box_get_z_index(const struct box *box);

/**
 * Compare two boxes by z-index for sorting.
 *
 * For use with qsort. Boxes with lower z-index sort first.
 * For equal z-index, order is undefined (stable sort not guaranteed).
 *
 * \param a  Pointer to first box pointer
 * \param b  Pointer to second box pointer
 * \return negative if a < b, 0 if equal, positive if a > b
 */
int stacking_compare_z_index(const void *a, const void *b);

/**
 * Entry for a deferred positioned element in a stacking context.
 */
struct zindex_entry {
    struct box *box;
    int32_t z_index;
    int x_parent;
    int y_parent;
};

/**
 * Dynamic collection of deferred positioned elements for stacking context.
 */
struct stacking_context {
    struct zindex_entry *entries;
    size_t count;
    size_t capacity;
};

/**
 * Initialize a stacking context.
 *
 * \param ctx  Stacking context to initialize
 */
void stacking_context_init(struct stacking_context *ctx);

/**
 * Add a positioned element to the stacking context.
 *
 * \param ctx       Stacking context
 * \param box       Box to add
 * \param z_index   Z-index value
 * \param x_parent  Parent X offset
 * \param y_parent  Parent Y offset
 * \return true on success, false on allocation failure
 */
bool stacking_context_add(struct stacking_context *ctx, struct box *box, int32_t z_index, int x_parent, int y_parent);

/**
 * Sort the stacking context entries by z-index (stable sort).
 *
 * \param ctx  Stacking context to sort
 */
void stacking_context_sort(struct stacking_context *ctx);

/**
 * Free resources used by a stacking context.
 *
 * \param ctx  Stacking context to free
 */
void stacking_context_fini(struct stacking_context *ctx);

/**
 * Recursively collect positioned elements with explicit z-index from subtree.
 *
 * This finds all descendants with z-index != auto and adds them to the
 * appropriate stacking context based on z-index sign (negative or positive).
 * It stops recursion when it finds a positioned element (which will be
 * rendered separately with its own subtree).
 *
 * \param negative_ctx  Stacking context for z-index < 0 elements
 * \param positive_ctx  Stacking context for z-index >= 0 elements
 * \param box           Root of subtree to scan
 * \param x_parent      X offset of parent
 * \param y_parent      Y offset of parent
 * \return true on success, false on allocation failure
 */
bool stacking_context_collect_subtree(struct stacking_context *negative_ctx, struct stacking_context *positive_ctx,
    struct box *box, int x_parent, int y_parent);

#endif /* NEOSURF_HTML_STACKING_H */

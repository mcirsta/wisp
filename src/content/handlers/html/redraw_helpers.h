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
 * HTML redraw helper functions for clip intersection.
 *
 * These functions are extracted from html_redraw_box to allow unit testing.
 */

#ifndef WISP_HTML_REDRAW_HELPERS_H
#define WISP_HTML_REDRAW_HELPERS_H

#include <stdbool.h>

/* Forward declare rect since we don't want to pull in all of types.h */
struct rect;

/**
 * Result of clip intersection operation
 */
typedef enum {
    CLIP_RESULT_OK, /**< Intersection successful, clip is valid */
    CLIP_RESULT_EMPTY, /**< Intersection resulted in empty rect (nothing to draw) */
} clip_result_t;

/**
 * Intersect a box rectangle with a clip rectangle.
 *
 * For transformed boxes, the visual position after transform differs from
 * the pre-transform box coordinates. In this case, intersection should be
 * skipped and the parent clip used directly.
 *
 * \param[in,out] r           Box rectangle to intersect (modified in place)
 * \param[in]     clip        Parent clip rectangle
 * \param[in]     has_transform True if box has CSS transform applied
 * \return CLIP_RESULT_OK if clip is valid, CLIP_RESULT_EMPTY if nothing to draw
 */
clip_result_t html_clip_intersect_box(struct rect *r, const struct rect *clip, bool has_transform);

/**
 * Intersect a clip rectangle with box content bounds for object-fit clipping.
 *
 * For object-fit: cover/none/scale-down, we need to clip to the box bounds
 * to prevent overflow. For transformed boxes, this intersection should be
 * skipped since the visual bounds differ from box coordinates.
 *
 * \param[in,out] object_clip Clip rectangle for object (modified in place)
 * \param[in]     box_left    Left edge of box content area
 * \param[in]     box_top     Top edge of box content area
 * \param[in]     box_right   Right edge of box content area
 * \param[in]     box_bottom  Bottom edge of box content area
 * \param[in]     has_transform True if box has CSS transform applied
 * \return CLIP_RESULT_OK if clip is valid, CLIP_RESULT_EMPTY if nothing to draw
 */
clip_result_t html_clip_intersect_object_fit(
    struct rect *object_clip, int box_left, int box_top, int box_right, int box_bottom, bool has_transform);

#endif /* NETSURF_HTML_REDRAW_HELPERS_H */

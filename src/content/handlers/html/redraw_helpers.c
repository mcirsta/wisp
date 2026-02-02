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
 * These functions are extracted from html_redraw_box to allow unit testing
 * of clip behavior for transformed elements.
 */

#include "content/handlers/html/redraw_helpers.h"
#include <wisp/types.h>

/* See redraw_helpers.h for documentation */
clip_result_t html_clip_intersect_box(struct rect *r, const struct rect *clip, bool has_transform)
{
    if (has_transform) {
        /* For transformed boxes, use the incoming clip as-is.
         * The visual position after transform differs from the
         * pre-transform box coordinates. */
        *r = *clip;
        return CLIP_RESULT_OK;
    }

    /* Find intersection of clip rectangle and box */
    if (r->x0 < clip->x0)
        r->x0 = clip->x0;
    if (r->y0 < clip->y0)
        r->y0 = clip->y0;
    if (clip->x1 < r->x1)
        r->x1 = clip->x1;
    if (clip->y1 < r->y1)
        r->y1 = clip->y1;

    /* Check if intersection resulted in empty rectangle */
    if (r->x0 >= r->x1 || r->y0 >= r->y1) {
        return CLIP_RESULT_EMPTY;
    }

    return CLIP_RESULT_OK;
}

/* See redraw_helpers.h for documentation */
clip_result_t html_clip_intersect_object_fit(
    struct rect *object_clip, int box_left, int box_top, int box_right, int box_bottom, bool has_transform)
{
    if (has_transform) {
        /* For transformed boxes, skip intersection.
         * The visual bounds differ from box coordinates. */
        return CLIP_RESULT_OK;
    }

    /* Intersect with box content bounds */
    if (object_clip->x0 < box_left)
        object_clip->x0 = box_left;
    if (object_clip->y0 < box_top)
        object_clip->y0 = box_top;
    if (object_clip->x1 > box_right)
        object_clip->x1 = box_right;
    if (object_clip->y1 > box_bottom)
        object_clip->y1 = box_bottom;

    /* Check if intersection resulted in empty rectangle */
    if (object_clip->x0 >= object_clip->x1 || object_clip->y0 >= object_clip->y1) {
        return CLIP_RESULT_EMPTY;
    }

    return CLIP_RESULT_OK;
}

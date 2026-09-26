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
#include <assert.h>
#include <math.h>

void html_gradient_positions(const css_gradient_stop *stops, unsigned int count, const css_computed_style *style,
    const css_unit_ctx *unit_ctx, float length, float scale, float *positions)
{
    assert(count >= 2 && length > 0);
    unsigned int previous = 0;
    for (unsigned int i = 0; i < count; i++) {
        if (stops[i].auto_offset) {
            if (i == 0)
                positions[i] = 0;
            else if (i == count - 1)
                positions[i] = 1;
            else
                continue;
        } else if (stops[i].unit == CSS_UNIT_PCT) {
            positions[i] = FIXTOFLT(stops[i].offset) / 100.0f;
        } else {
            positions[i] = FIXTOFLT(css_unit_len2device_px(style, unit_ctx, stops[i].offset, stops[i].unit)) * scale /
                length;
        }
        if (i > 0) {
            positions[i] = fmaxf(positions[i], positions[previous]);
            /* Distribute an omitted run between its surrounding positioned stops. */
            for (unsigned int j = previous + 1; j < i; j++)
                positions[j] = positions[previous] +
                    (positions[i] - positions[previous]) * (j - previous) / (i - previous);
        }
        previous = i;
    }
}

css_color
html_gradient_color_at(const css_gradient_stop *stops, const float *positions, unsigned int count, float position)
{
    if (position < positions[0])
        return stops[0].color;
    unsigned int upper = 1;
    while (upper < count && position >= positions[upper])
        upper++;
    if (upper == count)
        return stops[count - 1].color;
    float t = (position - positions[upper - 1]) / (positions[upper] - positions[upper - 1]);
    css_color a = stops[upper - 1].color, b = stops[upper].color;
    float alpha_a = a >> 24, alpha_b = b >> 24;
    float alpha = alpha_a + (alpha_b - alpha_a) * t;
    if (alpha <= 0)
        return 0;
    css_color color = (css_color)(alpha + 0.5f) << 24;
    for (unsigned int shift = 0; shift < 24; shift += 8) {
        float channel_a = ((a >> shift) & 255) * alpha_a;
        float channel_b = ((b >> shift) & 255) * alpha_b;
        unsigned int channel = (unsigned int)((channel_a + (channel_b - channel_a) * t) / alpha + 0.5f);
        color |= (channel > 255 ? 255 : channel) << shift;
    }
    return color;
}

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

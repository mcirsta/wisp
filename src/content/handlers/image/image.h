/*
 * Copyright 2011 John-Mark Bell <jmb@netsurf-browser.org>
 *
 * This file is part of NetSurf, http://www.netsurf-browser.org/
 *
 * NetSurf is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * NetSurf is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/** \file
 * Initialisation/finalisation of image handlers.
 */

#ifndef WISP_IMAGE_IMAGE_H_
#define WISP_IMAGE_IMAGE_H_

#include <stdbool.h>
#include <wisp/utils/errors.h>

struct bitmap;
struct content;
struct content_redraw_data;

/** Zero-initialized state owned by an image handler or cache entry. */
struct image_decode_error {
    struct content *content;
    nserror error;
    bool pending;
};

/** Report a permanent decode failure after painting/conversion unwinds.
 * Allocation failures remain retryable. Call again if scheduling failed.
 */
void image_decode_error_schedule(struct image_decode_error *state, struct content *content, nserror error);

/** Cancel a queued notification before releasing its owner. */
void image_decode_error_cancel(struct image_decode_error *state);

/** Initialise the content handlers for image types.
 */
nserror image_init(void);

/** Common image content handler bitmap plot call.
 *
 * This plots the specified bitmap controlled by the redraw context
 * and specific content redraw data. It is a helper specifically
 * provided for image content handlers redraw callback.
 */
bool image_bitmap_plot(
    struct bitmap *bitmap, struct content_redraw_data *data, const struct rect *clip, const struct redraw_context *ctx);

#endif

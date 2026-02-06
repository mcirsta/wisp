/*
 * Copyright 2026 Marius Cirsta
 *
 * This file is part of Wisp, http://www.wispbrowser.org/
 *
 * Wisp is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * Wisp is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * \file
 * Implementation of content handling for image/avif
 *
 * This implementation uses the libavif library for decoding.
 * Only static (single-frame) AVIF images are supported.
 * Image cache handling is performed by the generic handler.
 */

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include <avif/avif.h>

#include <wisp/bitmap.h>
#include <wisp/content/content_protected.h>
#include <wisp/content/llcache.h>
#include <wisp/desktop/gui_internal.h>
#include <wisp/utils/log.h>
#include <wisp/utils/messages.h>
#include <wisp/utils/utils.h>
#include "content/content_factory.h"
#include "desktop/bitmap.h"

#include "content/handlers/image/image_cache.h"

#include "avif.h"

/**
 * Content create entry point.
 *
 * Create a content object for the AVIF image.
 */
static nserror avif_create(const content_handler *handler, lwc_string *imime_type, const struct http_parameter *params,
    llcache_handle *llcache, const char *fallback_charset, bool quirks, struct content **c)
{
    struct content *avif_c;
    nserror res;

    avif_c = calloc(1, sizeof(struct content));
    if (avif_c == NULL) {
        return NSERROR_NOMEM;
    }

    res = content__init(avif_c, handler, imime_type, params, llcache, fallback_charset, quirks);
    if (res != NSERROR_OK) {
        free(avif_c);
        return res;
    }

    *c = avif_c;

    return NSERROR_OK;
}

/**
 * Create a bitmap from AVIF content.
 */
static struct bitmap *avif_cache_convert(struct content *c)
{
    const uint8_t *source_data;
    size_t source_size;
    avifDecoder *decoder = NULL;
    avifResult result;
    avifRGBImage rgb;
    struct bitmap *bitmap = NULL;
    unsigned int bmap_flags;
    uint8_t *pixels;
    size_t rowstride;

    source_data = content__get_source_data(c, &source_size);
    if (source_data == NULL || source_size == 0) {
        NSLOG(wisp, ERROR, "AVIF: No source data");
        return NULL;
    }

    /* Create decoder */
    decoder = avifDecoderCreate();
    if (decoder == NULL) {
        NSLOG(wisp, ERROR, "AVIF: Failed to create decoder");
        return NULL;
    }

    /* We only want the first frame (static image) */
    decoder->maxThreads = 1;
    decoder->ignoreExif = AVIF_TRUE;
    decoder->ignoreXMP = AVIF_TRUE;

    /* Set memory source */
    result = avifDecoderSetIOMemory(decoder, source_data, source_size);
    if (result != AVIF_RESULT_OK) {
        NSLOG(wisp, ERROR, "AVIF: SetIOMemory failed: %s", avifResultToString(result));
        avifDecoderDestroy(decoder);
        return NULL;
    }

    /* Parse the image header */
    result = avifDecoderParse(decoder);
    if (result != AVIF_RESULT_OK) {
        NSLOG(wisp, ERROR, "AVIF: Parse failed: %s", avifResultToString(result));
        avifDecoderDestroy(decoder);
        return NULL;
    }

    /* Decode the first (and only for static) image */
    result = avifDecoderNextImage(decoder);
    if (result != AVIF_RESULT_OK) {
        NSLOG(wisp, ERROR, "AVIF: Decode failed: %s", avifResultToString(result));
        avifDecoderDestroy(decoder);
        return NULL;
    }

    /* Determine bitmap flags based on alpha presence */
    if (decoder->image->alphaPlane != NULL) {
        bmap_flags = BITMAP_NONE;
    } else {
        bmap_flags = BITMAP_OPAQUE;
    }

    /* Create bitmap */
    bitmap = guit->bitmap->create(decoder->image->width, decoder->image->height, bmap_flags);
    if (bitmap == NULL) {
        NSLOG(wisp, ERROR, "AVIF: Failed to create bitmap");
        avifDecoderDestroy(decoder);
        return NULL;
    }

    pixels = guit->bitmap->get_buffer(bitmap);
    if (pixels == NULL) {
        NSLOG(wisp, ERROR, "AVIF: Failed to get bitmap buffer");
        guit->bitmap->destroy(bitmap);
        avifDecoderDestroy(decoder);
        return NULL;
    }

    rowstride = guit->bitmap->get_rowstride(bitmap);

    /* Set up RGB conversion */
    memset(&rgb, 0, sizeof(rgb));
    avifRGBImageSetDefaults(&rgb, decoder->image);
    rgb.depth = 8;
    rgb.pixels = pixels;
    rgb.rowBytes = rowstride;

    /* Set pixel format based on platform bitmap layout */
    switch (bitmap_fmt.layout) {
    case BITMAP_LAYOUT_R8G8B8A8:
        rgb.format = AVIF_RGB_FORMAT_RGBA;
        break;
    case BITMAP_LAYOUT_B8G8R8A8:
        rgb.format = AVIF_RGB_FORMAT_BGRA;
        break;
    case BITMAP_LAYOUT_A8R8G8B8:
        rgb.format = AVIF_RGB_FORMAT_ARGB;
        break;
    case BITMAP_LAYOUT_A8B8G8R8:
        rgb.format = AVIF_RGB_FORMAT_ABGR;
        break;
    default:
        rgb.format = AVIF_RGB_FORMAT_RGBA;
        break;
    }

    /* Handle premultiplied alpha if needed */
    if (bitmap_fmt.pma) {
        rgb.alphaPremultiplied = AVIF_TRUE;
    }

    /* Convert YUV to RGB */
    result = avifImageYUVToRGB(decoder->image, &rgb);
    if (result != AVIF_RESULT_OK) {
        NSLOG(wisp, ERROR, "AVIF: YUV to RGB conversion failed: %s", avifResultToString(result));
        guit->bitmap->destroy(bitmap);
        avifDecoderDestroy(decoder);
        return NULL;
    }

    avifDecoderDestroy(decoder);

    guit->bitmap->modified(bitmap);

    return bitmap;
}

/**
 * Convert the AVIF source data content.
 *
 * This ensures there is valid AVIF source data in the content object
 * and then adds it to the image cache ready to be converted on demand.
 *
 * \param c The AVIF content object
 * \return true on successful processing else false
 */
static bool avif_convert(struct content *c)
{
    const uint8_t *data;
    size_t data_size;
    avifDecoder *decoder = NULL;
    avifResult result;

    data = content__get_source_data(c, &data_size);
    if (data == NULL || data_size == 0) {
        NSLOG(wisp, ERROR, "AVIF: No source data for conversion");
        return false;
    }

    /* Create temporary decoder just to get dimensions */
    decoder = avifDecoderCreate();
    if (decoder == NULL) {
        NSLOG(wisp, ERROR, "AVIF: Failed to create decoder");
        return false;
    }

    result = avifDecoderSetIOMemory(decoder, data, data_size);
    if (result != AVIF_RESULT_OK) {
        NSLOG(wisp, ERROR, "AVIF: SetIOMemory failed: %s", avifResultToString(result));
        avifDecoderDestroy(decoder);
        return false;
    }

    result = avifDecoderParse(decoder);
    if (result != AVIF_RESULT_OK) {
        NSLOG(wisp, ERROR, "AVIF: Parse failed: %s", avifResultToString(result));
        avifDecoderDestroy(decoder);
        return false;
    }

    c->width = decoder->image->width;
    c->height = decoder->image->height;
    c->size = c->width * c->height * 4;

    avifDecoderDestroy(decoder);

    image_cache_add(c, NULL, avif_cache_convert);

    content_set_ready(c);
    content_set_done(c);

    return true;
}

/**
 * Clone content.
 */
static nserror avif_clone(const struct content *old, struct content **new_c)
{
    struct content *avif_c;
    nserror res;

    avif_c = calloc(1, sizeof(struct content));
    if (avif_c == NULL) {
        return NSERROR_NOMEM;
    }

    res = content__clone(old, avif_c);
    if (res != NSERROR_OK) {
        content_destroy(avif_c);
        return res;
    }

    /* Re-convert if the content is ready */
    if ((old->status == CONTENT_STATUS_READY) || (old->status == CONTENT_STATUS_DONE)) {
        if (avif_convert(avif_c) == false) {
            content_destroy(avif_c);
            return NSERROR_CLONE_FAILED;
        }
    }

    *new_c = avif_c;

    return NSERROR_OK;
}

static const content_handler avif_content_handler = {
    .create = avif_create,
    .data_complete = avif_convert,
    .destroy = image_cache_destroy,
    .redraw = image_cache_redraw,
    .clone = avif_clone,
    .get_internal = image_cache_get_internal,
    .type = image_cache_content_type,
    .is_opaque = image_cache_is_opaque,
    .no_share = false,
};

static const char *avif_types[] = {"image/avif"};

CONTENT_FACTORY_REGISTER_TYPES(nsavif, avif_types, avif_content_handler);

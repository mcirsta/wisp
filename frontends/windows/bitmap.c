/*
 * Copyright 2008 Vincent Sanders <vince@simtec.co.uk>
 * Copyright 2009 Mark Benjamin <netsurf-browser.org.MarkBenjamin@dfgh.net>
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

/**
 * \file
 * win32 implementation of the bitmap operations.
 */

#include "wisp/utils/config.h"

#include <sys/types.h>
#include <inttypes.h>
#include <string.h>
#include <windows.h>

#include "wisp/bitmap.h"
#include "wisp/content.h"
#include "wisp/plotters.h"
#include "wisp/utils/log.h"

#include "wisp/utils/errors.h"
#include "windows/bitmap.h"
#include "windows/plot.h"

/**
 * Create a bitmap.
 *
 * \param  width   width of image in pixels
 * \param  height  height of image in pixels
 * \param  state   flags   flags for bitmap creation
 * \return an opaque struct bitmap, or NULL on memory exhaustion
 */
static void *win32_bitmap_create(int width, int height, enum gui_bitmap_flags flags)
{
    struct bitmap *bitmap;
    BITMAPV5HEADER *pbmi;
    HBITMAP windib;
    uint8_t *pixdata;

    NSLOG(wisp, INFO, "width %d, height %d, flags %u", width, height, (unsigned)flags);

    pbmi = calloc(1, sizeof(BITMAPV5HEADER));
    if (pbmi == NULL) {
        return NULL;
    }

    pbmi->bV5Size = sizeof(BITMAPV5HEADER);
    pbmi->bV5Width = width;
    pbmi->bV5Height = -height;
    pbmi->bV5Planes = 1;
    pbmi->bV5BitCount = 32;
    /* Use BI_RGB for 32-bit bitmaps - required for AlphaBlend with AC_SRC_ALPHA.
     * With BI_RGB on 32-bit, Windows uses BGRA byte order (B in low byte, A in high byte).
     * The color masks below are informational but not strictly required for BI_RGB. */
    pbmi->bV5Compression = BI_RGB;

    windib = CreateDIBSection(NULL, (BITMAPINFO *)pbmi, DIB_RGB_COLORS, (void **)&pixdata, NULL, 0);

    if (windib == NULL) {
        free(pbmi);
        return NULL;
    }

    bitmap = calloc(1, sizeof(struct bitmap));
    if (bitmap == NULL) {
        DeleteObject(windib);
        free(pbmi);
        return NULL;
    }

    bitmap->width = width;
    bitmap->height = height;
    bitmap->windib = windib;
    bitmap->pbmi = pbmi;
    bitmap->pixdata = pixdata;
    if ((flags & BITMAP_OPAQUE) != 0) {
        bitmap->opaque = true;
    } else {
        bitmap->opaque = false;
    }

    NSLOG(wisp, INFO, "bitmap %p", bitmap);

    return bitmap;
}


/**
 * Return a pointer to the pixel data in a bitmap.
 *
 * The pixel data is packed as BITMAP_FORMAT, possibly with padding at the end
 * of rows. The width of a row in bytes is given by bitmap_get_rowstride().
 *
 * \param  bitmap  a bitmap, as returned by bitmap_create()
 * \return pointer to the pixel buffer
 */
static unsigned char *bitmap_get_buffer(void *bitmap)
{
    struct bitmap *bm = bitmap;
    if (bitmap == NULL) {
        NSLOG(wisp, INFO, "NULL bitmap!");
        return NULL;
    }

    return bm->pixdata;
}


/**
 * Find the width of a pixel row in bytes.
 *
 * \param  bitmap  a bitmap, as returned by bitmap_create()
 * \return width of a pixel row in the bitmap
 */
static size_t bitmap_get_rowstride(void *bitmap)
{
    struct bitmap *bm = bitmap;

    if (bitmap == NULL) {
        NSLOG(wisp, INFO, "NULL bitmap!");
        return 0;
    }

    return (bm->width) * 4;
}


/**
 * Free a bitmap.
 *
 * \param  bitmap  a bitmap, as returned by bitmap_create()
 */
void win32_bitmap_destroy(void *bitmap)
{
    struct bitmap *bm = bitmap;

    if (bitmap == NULL) {
        NSLOG(wisp, INFO, "NULL bitmap!");
        return;
    }

    win32_bitmap_flush_scaled(bm);
    DeleteObject(bm->windib);
    free(bm->pbmi);
    free(bm);
}


/**
 * The bitmap image has changed, so flush any persistant cache.
 *
 * \param  bitmap  a bitmap, as returned by bitmap_create()
 */
static void bitmap_modified(void *bitmap)
{
    if (bitmap == NULL) {
        return;
    }
    win32_bitmap_flush_scaled(bitmap);
}

/**
 * Sets whether a bitmap should be plotted opaque
 *
 * \param  bitmap  a bitmap, as returned by bitmap_create()
 * \param  opaque  whether the bitmap should be plotted opaque
 */
static void bitmap_set_opaque(void *bitmap, bool opaque)
{
    struct bitmap *bm = bitmap;

    if (bitmap == NULL) {
        NSLOG(wisp, INFO, "NULL bitmap!");
        return;
    }

    NSLOG(wisp, INFO, "setting bitmap %p to %s", bm, opaque ? "opaque" : "transparent");
    bm->opaque = opaque;
}


/**
 * Gets whether a bitmap should be plotted opaque
 *
 * \param  bitmap  a bitmap, as returned by bitmap_create()
 */
static bool bitmap_get_opaque(void *bitmap)
{
    struct bitmap *bm = bitmap;

    if (bitmap == NULL) {
        NSLOG(wisp, INFO, "NULL bitmap!");
        return false;
    }

    return bm->opaque;
}

static int bitmap_get_width(void *bitmap)
{
    struct bitmap *bm = bitmap;

    if (bitmap == NULL) {
        NSLOG(wisp, INFO, "NULL bitmap!");
        return 0;
    }

    return (bm->width);
}

static int bitmap_get_height(void *bitmap)
{
    struct bitmap *bm = bitmap;

    if (bitmap == NULL) {
        NSLOG(wisp, INFO, "NULL bitmap!");
        return 0;
    }

    return (bm->height);
}

struct bitmap *bitmap_scale(struct bitmap *prescale, int width, int height)
{
    struct bitmap *ret = malloc(sizeof(struct bitmap));
    int i, ii, v, vv;
    uint32_t *retpixdata, *inpixdata; /* 4 byte types for quicker
                                       * transfer */
    if (ret == NULL)
        return NULL;

    retpixdata = malloc(width * height * 4);
    if (retpixdata == NULL) {
        free(ret);
        return NULL;
    }

    inpixdata = (uint32_t *)prescale->pixdata;
    ret->pixdata = (uint8_t *)retpixdata;
    ret->height = height;
    ret->width = width;
    for (i = 0; i < height; i++) {
        v = i * width;
        vv = (int)((i * prescale->height) / height) * prescale->width;
        for (ii = 0; ii < width; ii++) {
            retpixdata[v + ii] = inpixdata[vv + (int)((ii * prescale->width) / width)];
        }
    }
    return ret;
}


nserror win32_bitmap_ensure_scaled(struct bitmap *bm, int width, int height)
{
    BITMAPV5HEADER *spbmi;
    HBITMAP swindib;
    uint8_t *spix;
    HDC sdc;
    int bltres;

    if (bm == NULL) {
        return NSERROR_INVALID;
    }

    if (bm->scaled_windib != NULL && bm->scaled_pbmi != NULL && bm->scaled_pixdata != NULL &&
        bm->scaled_width == width && bm->scaled_height == height) {
        return NSERROR_OK;
    }

    win32_bitmap_flush_scaled(bm);

    spbmi = calloc(1, sizeof(BITMAPV5HEADER));
    if (spbmi == NULL) {
        return NSERROR_NOMEM;
    }
    spbmi->bV5Size = sizeof(BITMAPV5HEADER);
    spbmi->bV5Width = width;
    spbmi->bV5Height = -height;
    spbmi->bV5Planes = 1;
    spbmi->bV5BitCount = 32;
    /* Use BI_RGB for AlphaBlend compatibility - same as main bitmap */
    spbmi->bV5Compression = BI_RGB;

    swindib = CreateDIBSection(NULL, (BITMAPINFO *)spbmi, DIB_RGB_COLORS, (void **)&spix, NULL, 0);
    if (swindib == NULL) {
        free(spbmi);
        return NSERROR_INVALID;
    }

    sdc = CreateCompatibleDC(NULL);
    if (sdc == NULL) {
        DeleteObject(swindib);
        free(spbmi);
        return NSERROR_INVALID;
    }
    SelectObject(sdc, swindib);
    /* Use HALFTONE for better quality interpolation when scaling.
     * SetBrushOrgEx is required when using HALFTONE mode. */
    SetStretchBltMode(sdc, HALFTONE);
    SetBrushOrgEx(sdc, 0, 0, NULL);
    bltres = StretchDIBits(sdc, 0, 0, width, height, 0, 0, bm->width, bm->height, bm->pixdata, (BITMAPINFO *)bm->pbmi,
        DIB_RGB_COLORS, SRCCOPY);

    DeleteDC(sdc);

    if (bltres == 0) {
        DeleteObject(swindib);
        free(spbmi);
        return NSERROR_INVALID;
    }

    bm->scaled_windib = swindib;
    bm->scaled_pbmi = spbmi;
    bm->scaled_pixdata = spix;
    bm->scaled_width = width;
    bm->scaled_height = height;

    return NSERROR_OK;
}

void win32_bitmap_flush_scaled(struct bitmap *bm)
{
    if (bm == NULL) {
        return;
    }
    if (bm->scaled_windib != NULL) {
        DeleteObject(bm->scaled_windib);
        bm->scaled_windib = NULL;
    }
    if (bm->scaled_pbmi != NULL) {
        free(bm->scaled_pbmi);
        bm->scaled_pbmi = NULL;
    }
    bm->scaled_pixdata = NULL;
    bm->scaled_width = 0;
    bm->scaled_height = 0;
}


static nserror bitmap_render(struct bitmap *bitmap, struct hlcache_handle *content)
{
    int width;
    int height;
    HDC hdc, bufferdc, minidc;
    struct bitmap *fsbitmap;
    struct redraw_context ctx = {.interactive = false, .background_images = true, .plot = &win_plotters};

    width = min(max(content_get_width(content), bitmap->width), 1024);
    height = ((width * bitmap->height) + (bitmap->width / 2)) / bitmap->width;

    NSLOG(wisp, INFO, "bitmap %p for content %p width %d, height %d", bitmap, content, width, height);

    /* create two memory device contexts to put the bitmaps in */
    bufferdc = CreateCompatibleDC(NULL);
    if (bufferdc == NULL) {
        return NSERROR_NOMEM;
    }

    minidc = CreateCompatibleDC(NULL);
    if (minidc == NULL) {
        DeleteDC(bufferdc);
        return NSERROR_NOMEM;
    }

    /* create a full size bitmap and plot into it */
    fsbitmap = win32_bitmap_create(width, height, BITMAP_CLEAR | BITMAP_OPAQUE);

    SelectObject(bufferdc, fsbitmap->windib);

    hdc = plot_hdc;
    plot_hdc = bufferdc;
    /* render the content  */
    content_scaled_redraw(content, width, height, &ctx);
    plot_hdc = hdc;

    /* scale bitmap bufferbm into minibm */
    SelectObject(minidc, bitmap->windib);

    bitmap->opaque = true;

    StretchBlt(minidc, 0, 0, bitmap->width, bitmap->height, bufferdc, 0, 0, width, height, SRCCOPY);

    DeleteDC(bufferdc);
    DeleteDC(minidc);
    win32_bitmap_destroy(fsbitmap);

    return NSERROR_OK;
}

static struct gui_bitmap_table bitmap_table = {
    .create = win32_bitmap_create,
    .destroy = win32_bitmap_destroy,
    .set_opaque = bitmap_set_opaque,
    .get_opaque = bitmap_get_opaque,
    .get_buffer = bitmap_get_buffer,
    .get_rowstride = bitmap_get_rowstride,
    .get_width = bitmap_get_width,
    .get_height = bitmap_get_height,
    .modified = bitmap_modified,
    .render = bitmap_render,
};

struct gui_bitmap_table *win32_bitmap_table = &bitmap_table;

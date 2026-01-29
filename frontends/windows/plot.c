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
 * win32 plotter implementation.
 */

#include <sys/types.h>
#include "neosurf/utils/config.h"
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <string.h>
#include <windows.h>

#include "neosurf/mouse.h"
#include "neosurf/plotters.h"
#include "neosurf/utils/log.h"
#include "neosurf/utils/utf8.h"
#include "neosurf/window.h"

#include "windows/bitmap.h"
#include "windows/font.h"
#include "windows/gui.h"
#include "windows/plot.h"

HDC plot_hdc;

/** currently set clipping rectangle */
static RECT plot_clip;


/**
 * bitmap helper to plot a solid block of colour
 *
 * \param col colour to plot with
 * \param x the x coordinate to plot at
 * \param y the y coordinate to plot at
 * \param width the width of block to plot
 * \param height the height to plot
 * \return NSERROR_OK on sucess else error code.
 */
static nserror plot_block(COLORREF col, int x, int y, int width, int height)
{
    HRGN clipregion;
    HGDIOBJ original = NULL;

    /* Bail early if we can */
    if ((x >= plot_clip.right) || ((x + width) < plot_clip.left) || (y >= plot_clip.bottom) ||
        ((y + height) < plot_clip.top)) {
        /* Image completely outside clip region */
        return NSERROR_OK;
    }

    /* ensure the plot HDC is set */
    if (plot_hdc == NULL) {
        NSLOG(neosurf, INFO, "HDC not set on call to plotters");
        return NSERROR_INVALID;
    }

    clipregion = CreateRectRgnIndirect(&plot_clip);
    if (clipregion == NULL) {
        return NSERROR_INVALID;
    }

    SelectClipRgn(plot_hdc, clipregion);

    /* Saving the original pen object */
    original = SelectObject(plot_hdc, GetStockObject(DC_PEN));

    SelectObject(plot_hdc, GetStockObject(DC_PEN));
    SelectObject(plot_hdc, GetStockObject(DC_BRUSH));
    SetDCPenColor(plot_hdc, col);
    SetDCBrushColor(plot_hdc, col);
    Rectangle(plot_hdc, x, y, width, height);

    SelectObject(plot_hdc, original); /* Restoring the original pen object */

    DeleteObject(clipregion);

    return NSERROR_OK;
}


/**
 * plot an alpha blended bitmap
 *
 * blunt force truma way of achiving alpha blended plotting
 *
 * \param hdc drawing cotext
 * \param bitmap bitmap to render
 * \param x x coordinate to plot at
 * \param y y coordinate to plot at
 * \param width The width to plot the bitmap into
 * \param height The height to plot the bitmap into
 * \return NSERROR_OK on success else appropriate error code.
 */
static nserror plot_alpha_bitmap(HDC hdc, struct bitmap *bitmap, int x, int y, int width, int height, colour bg)
{
    if (bg != NS_TRANSPARENT) {
        RECT target_rect = {.left = x, .top = y, .right = x + width, .bottom = y + height};
        HBRUSH bg_brush = CreateSolidBrush((COLORREF)(bg & 0x00FFFFFF));
        if (bg_brush != NULL) {
            FillRect(hdc, &target_rect, bg_brush);
            DeleteObject(bg_brush);
        }
    }

    BLENDFUNCTION blnd = {AC_SRC_OVER, 0, 0xff, AC_SRC_ALPHA};
    HDC bmihdc;
    bool bltres;
    bmihdc = CreateCompatibleDC(hdc);
    SelectObject(bmihdc, bitmap->windib);
    bltres = AlphaBlend(hdc, x, y, width, height, bmihdc, 0, 0, bitmap->width, bitmap->height, blnd);
    DeleteDC(bmihdc);
    if (!bltres) {
        return NSERROR_INVALID;
    }

    return NSERROR_OK;
}


/**
 * Internal bitmap plotting
 *
 * \param bitmap The bitmap to plot
 * \param x x coordinate to plot at
 * \param y y coordinate to plot at
 * \param width The width to plot the bitmap into
 * \param height The height to plot the bitmap into
 * \return NSERROR_OK on success else appropriate error code.
 */
static nserror plot_bitmap(struct bitmap *bitmap, int x, int y, int width, int height, colour bg)
{
    HRGN clipregion;
    nserror res = NSERROR_OK;

    /* Bail early if we can */
    if ((x >= plot_clip.right) || ((x + width) < plot_clip.left) || (y >= plot_clip.bottom) ||
        ((y + height) < plot_clip.top)) {
        /* Image completely outside clip region */
        return NSERROR_OK;
    }

    /* ensure the plot HDC is set */
    if (plot_hdc == NULL) {
        NSLOG(neosurf, INFO, "HDC not set on call to plotters");
        return NSERROR_INVALID;
    }

    clipregion = CreateRectRgnIndirect(&plot_clip);
    if (clipregion == NULL) {
        return NSERROR_INVALID;
    }

    SelectClipRgn(plot_hdc, clipregion);

    if (bitmap->opaque) {
        int bltres;
        /* opaque bitmap */
        if ((bitmap->width == width) && (bitmap->height == height)) {
            /* unscaled */
            bltres = SetDIBitsToDevice(plot_hdc, x, y, width, height, 0, 0, 0, height, bitmap->pixdata,
                (BITMAPINFO *)bitmap->pbmi, DIB_RGB_COLORS);
        } else {
            if (win32_bitmap_ensure_scaled(bitmap, width, height) != NSERROR_OK) {
                DeleteObject(clipregion);
                return NSERROR_INVALID;
            }
            bltres = SetDIBitsToDevice(plot_hdc, x, y, width, height, 0, 0, 0, height, bitmap->scaled_pixdata,
                (BITMAPINFO *)bitmap->scaled_pbmi, DIB_RGB_COLORS);
        }
        /* check to see if GDI operation failed */
        if (bltres == 0) {
            res = NSERROR_INVALID;
        }
        NSLOG(plot, DEEPDEBUG, "bltres = %d", bltres);
    } else {
        /* Bitmap with alpha.*/
        res = plot_alpha_bitmap(plot_hdc, bitmap, x, y, width, height, bg);
    }

    DeleteObject(clipregion);

    return res;
}


/**
 * \brief Sets a clip rectangle for subsequent plot operations.
 *
 * \param ctx The current redraw context.
 * \param clip The rectangle to limit all subsequent plot
 *              operations within.
 * \return NSERROR_OK on success else error code.
 */
static nserror clip(const struct redraw_context *ctx, const struct rect *clip)
{
    NSLOG(plot, DEEPDEBUG, "clip %d,%d to %d,%d", clip->x0, clip->y0, clip->x1, clip->y1);

    plot_clip.left = clip->x0;
    plot_clip.top = clip->y0;
    plot_clip.right = clip->x1 + 1; /* co-ordinates are exclusive */
    plot_clip.bottom = clip->y1 + 1; /* co-ordinates are exclusive */

    return NSERROR_OK;
}


/**
 * Plots an arc
 *
 * plot an arc segment around (x,y), anticlockwise from angle1
 *  to angle2. Angles are measured anticlockwise from
 *  horizontal, in degrees.
 *
 * \param ctx The current redraw context.
 * \param style Style controlling the arc plot.
 * \param x The x coordinate of the arc.
 * \param y The y coordinate of the arc.
 * \param radius The radius of the arc.
 * \param angle1 The start angle of the arc.
 * \param angle2 The finish angle of the arc.
 * \return NSERROR_OK on success else error code.
 */
static nserror
arc(const struct redraw_context *ctx, const plot_style_t *style, int x, int y, int radius, int angle1, int angle2)
{
    NSLOG(plot, DEEPDEBUG, "arc centre %d,%d radius %d from %d to %d", x, y, radius, angle1, angle2);

    /* ensure the plot HDC is set */
    if (plot_hdc == NULL) {
        NSLOG(neosurf, INFO, "HDC not set on call to plotters");
        return NSERROR_INVALID;
    }

    HRGN clipregion = CreateRectRgnIndirect(&plot_clip);
    if (clipregion == NULL) {
        return NSERROR_INVALID;
    }

    COLORREF col = (DWORD)(style->stroke_colour & 0x00FFFFFF);
    HPEN pen = CreatePen(PS_GEOMETRIC | PS_SOLID, 1, col);
    if (pen == NULL) {
        DeleteObject(clipregion);
        return NSERROR_INVALID;
    }
    HGDIOBJ penbak = SelectObject(plot_hdc, (HGDIOBJ)pen);
    if (penbak == NULL) {
        DeleteObject(clipregion);
        DeleteObject(pen);
        return NSERROR_INVALID;
    }

    int q1, q2;
    double a1 = 1.0, a2 = 1.0, b1 = 1.0, b2 = 1.0;
    q1 = (int)((angle1 + 45) / 90) - 45;
    q2 = (int)((angle2 + 45) / 90) - 45;
    while (q1 > 4)
        q1 -= 4;
    while (q2 > 4)
        q2 -= 4;
    while (q1 <= 0)
        q1 += 4;
    while (q2 <= 0)
        q2 += 4;
    angle1 = ((angle1 + 45) % 90) - 45;
    angle2 = ((angle2 + 45) % 90) - 45;

    switch (q1) {
    case 1:
        a1 = 1.0;
        b1 = -tan((M_PI / 180) * angle1);
        break;
    case 2:
        b1 = -1.0;
        a1 = -tan((M_PI / 180) * angle1);
        break;
    case 3:
        a1 = -1.0;
        b1 = tan((M_PI / 180) * angle1);
        break;
    case 4:
        b1 = 1.0;
        a1 = tan((M_PI / 180) * angle1);
        break;
    }

    switch (q2) {
    case 1:
        a2 = 1.0;
        b2 = -tan((M_PI / 180) * angle2);
        break;
    case 2:
        b2 = -1.0;
        a2 = -tan((M_PI / 180) * angle2);
        break;
    case 3:
        a2 = -1.0;
        b2 = tan((M_PI / 180) * angle2);
        break;
    case 4:
        b2 = 1.0;
        a2 = tan((M_PI / 180) * angle2);
        break;
    }

    SelectClipRgn(plot_hdc, clipregion);

    Arc(plot_hdc, x - radius, y - radius, x + radius, y + radius, x + (int)(a1 * radius), y + (int)(b1 * radius),
        x + (int)(a2 * radius), y + (int)(b2 * radius));

    SelectClipRgn(plot_hdc, NULL);
    pen = SelectObject(plot_hdc, penbak);
    DeleteObject(clipregion);
    DeleteObject(pen);

    return NSERROR_OK;
}


/**
 * Plots a circle
 *
 * Plot a circle centered on (x,y), which is optionally filled.
 *
 * \param ctx The current redraw context.
 * \param style Style controlling the circle plot.
 * \param x x coordinate of circle centre.
 * \param y y coordinate of circle centre.
 * \param radius circle radius.
 * \return NSERROR_OK on success else error code.
 */
static nserror disc(const struct redraw_context *ctx, const plot_style_t *style, int x, int y, int radius)
{
    NSLOG(plot, DEEPDEBUG, "disc at %d,%d radius %d", x, y, radius);

    /* ensure the plot HDC is set */
    if (plot_hdc == NULL) {
        NSLOG(neosurf, INFO, "HDC not set on call to plotters");
        return NSERROR_INVALID;
    }

    HRGN clipregion = CreateRectRgnIndirect(&plot_clip);
    if (clipregion == NULL) {
        return NSERROR_INVALID;
    }

    COLORREF col = (DWORD)((style->fill_colour | style->stroke_colour) & 0x00FFFFFF);
    HPEN pen = CreatePen(PS_GEOMETRIC | PS_SOLID, 1, col);
    if (pen == NULL) {
        DeleteObject(clipregion);
        return NSERROR_INVALID;
    }
    HGDIOBJ penbak = SelectObject(plot_hdc, (HGDIOBJ)pen);
    if (penbak == NULL) {
        DeleteObject(clipregion);
        DeleteObject(pen);
        return NSERROR_INVALID;
    }
    HBRUSH brush = CreateSolidBrush(col);
    if (brush == NULL) {
        DeleteObject(clipregion);
        SelectObject(plot_hdc, penbak);
        DeleteObject(pen);
        return NSERROR_INVALID;
    }
    HGDIOBJ brushbak = SelectObject(plot_hdc, (HGDIOBJ)brush);
    if (brushbak == NULL) {
        DeleteObject(clipregion);
        SelectObject(plot_hdc, penbak);
        DeleteObject(pen);
        DeleteObject(brush);
        return NSERROR_INVALID;
    }

    SelectClipRgn(plot_hdc, clipregion);

    if (style->fill_type == PLOT_OP_TYPE_NONE) {
        Arc(plot_hdc, x - radius, y - radius, x + radius, y + radius, x - radius, y - radius, x - radius, y - radius);
    } else {
        Ellipse(plot_hdc, x - radius, y - radius, x + radius, y + radius);
    }

    SelectClipRgn(plot_hdc, NULL);
    pen = SelectObject(plot_hdc, penbak);
    brush = SelectObject(plot_hdc, brushbak);
    DeleteObject(clipregion);
    DeleteObject(pen);
    DeleteObject(brush);

    return NSERROR_OK;
}


/**
 * Plots a line
 *
 * plot a line from (x0,y0) to (x1,y1). Coordinates are at
 *  centre of line width/thickness.
 *
 * \param ctx The current redraw context.
 * \param style Style controlling the line plot.
 * \param line A rectangle defining the line to be drawn
 * \return NSERROR_OK on success else error code.
 */
static nserror line(const struct redraw_context *ctx, const plot_style_t *style, const struct rect *line)
{
    NSLOG(plot, DEEPDEBUG, "from %d,%d to %d,%d", line->x0, line->y0, line->x1, line->y1);

    /* ensure the plot HDC is set */
    if (plot_hdc == NULL) {
        NSLOG(neosurf, INFO, "HDC not set on call to plotters");
        return NSERROR_INVALID;
    }

    HRGN clipregion = CreateRectRgnIndirect(&plot_clip);
    if (clipregion == NULL) {
        return NSERROR_INVALID;
    }

    COLORREF col = (DWORD)(style->stroke_colour & 0x00FFFFFF);
    /* windows 0x00bbggrr */
    DWORD penstyle = PS_GEOMETRIC |
        ((style->stroke_type == PLOT_OP_TYPE_DOT)           ? PS_DOT
                : (style->stroke_type == PLOT_OP_TYPE_DASH) ? PS_DASH
                                                            : 0);
    LOGBRUSH lb = {BS_SOLID, col, 0};
    HPEN pen = ExtCreatePen(penstyle, plot_style_fixed_to_int(style->stroke_width), &lb, 0, NULL);
    if (pen == NULL) {
        DeleteObject(clipregion);
        return NSERROR_INVALID;
    }
    HGDIOBJ bak = SelectObject(plot_hdc, (HGDIOBJ)pen);
    if (bak == NULL) {
        DeleteObject(pen);
        DeleteObject(clipregion);
        return NSERROR_INVALID;
    }

    SelectClipRgn(plot_hdc, clipregion);

    MoveToEx(plot_hdc, line->x0, line->y0, (LPPOINT)NULL);

    LineTo(plot_hdc, line->x1, line->y1);

    SelectClipRgn(plot_hdc, NULL);
    pen = SelectObject(plot_hdc, bak);

    DeleteObject(pen);
    DeleteObject(clipregion);

    return NSERROR_OK;
}


/**
 * Plots a rectangle.
 *
 * The rectangle can be filled an outline or both controlled
 *  by the plot style The line can be solid, dotted or
 *  dashed. Top left corner at (x0,y0) and rectangle has given
 *  width and height.
 *
 * \param ctx The current redraw context.
 * \param style Style controlling the rectangle plot.
 * \param rect A rectangle defining the line to be drawn
 * \return NSERROR_OK on success else error code.
 */
static nserror rectangle(const struct redraw_context *ctx, const plot_style_t *style, const struct rect *rect)
{
    NSLOG(plot, DEEPDEBUG, "rectangle from %d,%d to %d,%d", rect->x0, rect->y0, rect->x1, rect->y1);

    /* ensure the plot HDC is set */
    if (plot_hdc == NULL) {
        NSLOG(neosurf, INFO, "HDC not set on call to plotters");
        return NSERROR_INVALID;
    }

    HRGN clipregion = CreateRectRgnIndirect(&plot_clip);
    if (clipregion == NULL) {
        return NSERROR_INVALID;
    }

    COLORREF pencol = (DWORD)(style->stroke_colour & 0x00FFFFFF);
    DWORD penstyle = PS_GEOMETRIC |
        (style->stroke_type == PLOT_OP_TYPE_DOT
                ? PS_DOT
                : (style->stroke_type == PLOT_OP_TYPE_DASH ? PS_DASH
                                                           : (style->stroke_type == PLOT_OP_TYPE_NONE ? PS_NULL : 0)));
    LOGBRUSH lb = {BS_SOLID, pencol, 0};
    LOGBRUSH lb1 = {BS_SOLID, style->fill_colour, 0};
    if (style->fill_type == PLOT_OP_TYPE_NONE)
        lb1.lbStyle = BS_HOLLOW;

    HPEN pen = ExtCreatePen(penstyle, plot_style_fixed_to_int(style->stroke_width), &lb, 0, NULL);
    if (pen == NULL) {
        return NSERROR_INVALID;
    }
    HGDIOBJ penbak = SelectObject(plot_hdc, (HGDIOBJ)pen);
    if (penbak == NULL) {
        DeleteObject(pen);
        return NSERROR_INVALID;
    }
    HBRUSH brush = CreateBrushIndirect(&lb1);
    if (brush == NULL) {
        SelectObject(plot_hdc, penbak);
        DeleteObject(pen);
        return NSERROR_INVALID;
    }
    HGDIOBJ brushbak = SelectObject(plot_hdc, (HGDIOBJ)brush);
    if (brushbak == NULL) {
        SelectObject(plot_hdc, penbak);
        DeleteObject(pen);
        DeleteObject(brush);
        return NSERROR_INVALID;
    }

    SelectClipRgn(plot_hdc, clipregion);

    /* windows GDI call coordinates are inclusive */
    Rectangle(plot_hdc, rect->x0, rect->y0, rect->x1 + 1, rect->y1 + 1);

    pen = SelectObject(plot_hdc, penbak);
    brush = SelectObject(plot_hdc, brushbak);
    SelectClipRgn(plot_hdc, NULL);
    DeleteObject(pen);
    DeleteObject(brush);
    DeleteObject(clipregion);

    return NSERROR_OK;
}


/**
 * Plot a polygon
 *
 * Plots a filled polygon with straight lines between
 * points. The lines around the edge of the ploygon are not
 * plotted. The polygon is filled with the non-zero winding
 * rule.
 *
 * \param ctx The current redraw context.
 * \param style Style controlling the polygon plot.
 * \param p verticies of polygon
 * \param n number of verticies.
 * \return NSERROR_OK on success else error code.
 */
static nserror polygon(const struct redraw_context *ctx, const plot_style_t *style, const int *p, unsigned int n)
{
    NSLOG(plot, DEEPDEBUG, "polygon %d points", n);

    /* ensure the plot HDC is set */
    if (plot_hdc == NULL) {
        NSLOG(neosurf, INFO, "HDC not set on call to plotters");
        return NSERROR_INVALID;
    }

    POINT points[n];
    unsigned int i;
    HRGN clipregion = CreateRectRgnIndirect(&plot_clip);
    if (clipregion == NULL) {
        return NSERROR_INVALID;
    }

    COLORREF pencol = (DWORD)(style->fill_colour & 0x00FFFFFF);
    COLORREF brushcol = (DWORD)(style->fill_colour & 0x00FFFFFF);
    HPEN pen = CreatePen(PS_GEOMETRIC | PS_NULL, 1, pencol);
    if (pen == NULL) {
        DeleteObject(clipregion);
        return NSERROR_INVALID;
    }
    HPEN penbak = SelectObject(plot_hdc, pen);
    if (penbak == NULL) {
        DeleteObject(clipregion);
        DeleteObject(pen);
        return NSERROR_INVALID;
    }
    HBRUSH brush = CreateSolidBrush(brushcol);
    if (brush == NULL) {
        DeleteObject(clipregion);
        SelectObject(plot_hdc, penbak);
        DeleteObject(pen);
        return NSERROR_INVALID;
    }
    HBRUSH brushbak = SelectObject(plot_hdc, brush);
    if (brushbak == NULL) {
        DeleteObject(clipregion);
        SelectObject(plot_hdc, penbak);
        DeleteObject(pen);
        DeleteObject(brush);
        return NSERROR_INVALID;
    }
    SetPolyFillMode(plot_hdc, WINDING);
    for (i = 0; i < n; i++) {
        points[i].x = (long)p[2 * i];
        points[i].y = (long)p[2 * i + 1];

        NSLOG(plot, DEEPDEBUG, "%ld,%ld ", points[i].x, points[i].y);
    }

    SelectClipRgn(plot_hdc, clipregion);

    if (n >= 2) {
        Polygon(plot_hdc, points, n);
    }

    SelectClipRgn(plot_hdc, NULL);

    pen = SelectObject(plot_hdc, penbak);
    brush = SelectObject(plot_hdc, brushbak);
    DeleteObject(clipregion);
    DeleteObject(pen);
    DeleteObject(brush);

    return NSERROR_OK;
}


/**
 * Plots a path.
 *
 * Path plot consisting of cubic Bezier curves. Line and fill colour is
 *  controlled by the plot style.
 *
 * \param ctx The current redraw context.
 * \param pstyle Style controlling the path plot.
 * \param p elements of path
 * \param n nunber of elements on path
 * \param transform A transform to apply to the path.
 * \return NSERROR_OK on success else error code.
 */
static nserror path(const struct redraw_context *ctx, const plot_style_t *pstyle, const float *p, unsigned int n,
    const float transform[6])
{
    HRGN clipregion;
    HGDIOBJ penbak = NULL;
    HGDIOBJ brushbak = NULL;
    HPEN pen = NULL;
    HBRUSH brush = NULL;
    DWORD penstyle;
    COLORREF pencol;
    COLORREF brushcol;
    LOGBRUSH lb;
    int i = 0;
    POINT pts[3];
    int x, y;

    if (plot_hdc == NULL) {
        return NSERROR_INVALID;
    }

    clipregion = CreateRectRgnIndirect(&plot_clip);
    if (clipregion == NULL) {
        return NSERROR_INVALID;
    }

    pencol = (DWORD)(pstyle->stroke_colour & 0x00FFFFFF);
    penstyle = PS_GEOMETRIC;
    switch (pstyle->stroke_type) {
    case PLOT_OP_TYPE_NONE:
        penstyle |= PS_NULL;
        break;
    case PLOT_OP_TYPE_DOT:
        penstyle |= PS_DOT;
        break;
    case PLOT_OP_TYPE_DASH:
        penstyle |= PS_DASH;
        break;
    default:
        penstyle |= PS_SOLID;
        break;
    }
    lb.lbStyle = BS_SOLID;
    lb.lbColor = pencol;
    lb.lbHatch = 0;
    pen = ExtCreatePen(penstyle, plot_style_fixed_to_int(pstyle->stroke_width), &lb, 0, NULL);
    if (pen == NULL) {
        DeleteObject(clipregion);
        return NSERROR_INVALID;
    }
    penbak = SelectObject(plot_hdc, (HGDIOBJ)pen);
    if (penbak == NULL) {
        DeleteObject(pen);
        DeleteObject(clipregion);
        return NSERROR_INVALID;
    }

    if (pstyle->fill_type == PLOT_OP_TYPE_NONE) {
        brush = GetStockObject(HOLLOW_BRUSH);
        brushbak = SelectObject(plot_hdc, brush);
    } else {
        brushcol = (DWORD)(pstyle->fill_colour & 0x00FFFFFF);
        brush = CreateSolidBrush(brushcol);
        if (brush == NULL) {
            SelectObject(plot_hdc, penbak);
            DeleteObject(pen);
            DeleteObject(clipregion);
            return NSERROR_INVALID;
        }
        brushbak = SelectObject(plot_hdc, (HGDIOBJ)brush);
        if (brushbak == NULL) {
            DeleteObject(brush);
            SelectObject(plot_hdc, penbak);
            DeleteObject(pen);
            DeleteObject(clipregion);
            return NSERROR_INVALID;
        }
    }

    SetPolyFillMode(plot_hdc, WINDING);
    SelectClipRgn(plot_hdc, clipregion);

    BeginPath(plot_hdc);

    while (i < (int)n) {
        int cmd = (int)p[i++];
        switch (cmd) {
        default:
            break;
        case PLOTTER_PATH_MOVE:
            x = (int)(transform[0] * p[i] + transform[2] * p[i + 1] + transform[4]);
            y = (int)(transform[1] * p[i] + transform[3] * p[i + 1] + transform[5]);
            i += 2;
            MoveToEx(plot_hdc, x, y, (LPPOINT)NULL);
            break;
        case PLOTTER_PATH_LINE:
            x = (int)(transform[0] * p[i] + transform[2] * p[i + 1] + transform[4]);
            y = (int)(transform[1] * p[i] + transform[3] * p[i + 1] + transform[5]);
            i += 2;
            LineTo(plot_hdc, x, y);
            break;
        case PLOTTER_PATH_BEZIER:
            pts[0].x = (LONG)(transform[0] * p[i] + transform[2] * p[i + 1] + transform[4]);
            pts[0].y = (LONG)(transform[1] * p[i] + transform[3] * p[i + 1] + transform[5]);
            pts[1].x = (LONG)(transform[0] * p[i + 2] + transform[2] * p[i + 3] + transform[4]);
            pts[1].y = (LONG)(transform[1] * p[i + 2] + transform[3] * p[i + 3] + transform[5]);
            pts[2].x = (LONG)(transform[0] * p[i + 4] + transform[2] * p[i + 5] + transform[4]);
            pts[2].y = (LONG)(transform[1] * p[i + 4] + transform[3] * p[i + 5] + transform[5]);
            i += 6;
            PolyBezierTo(plot_hdc, pts, 3);
            break;
        case PLOTTER_PATH_CLOSE:
            CloseFigure(plot_hdc);
            break;
        }
    }

    EndPath(plot_hdc);

    if (pstyle->fill_type != PLOT_OP_TYPE_NONE && pstyle->stroke_type != PLOT_OP_TYPE_NONE) {
        StrokeAndFillPath(plot_hdc);
    } else if (pstyle->fill_type != PLOT_OP_TYPE_NONE) {
        FillPath(plot_hdc);
    } else if (pstyle->stroke_type != PLOT_OP_TYPE_NONE) {
        StrokePath(plot_hdc);
    }

    SelectClipRgn(plot_hdc, NULL);
    SelectObject(plot_hdc, brushbak);
    if (brush != NULL && pstyle->fill_type != PLOT_OP_TYPE_NONE) {
        DeleteObject(brush);
    }
    SelectObject(plot_hdc, penbak);
    DeleteObject(pen);
    DeleteObject(clipregion);

    return NSERROR_OK;
}


/**
 * Plot a bitmap
 *
 * Tiled plot of a bitmap image. (x,y) gives the top left
 * coordinate of an explicitly placed tile. From this tile the
 * image can repeat in all four directions -- up, down, left
 * and right -- to the extents given by the current clip
 * rectangle.
 *
 * The bitmap_flags say whether to tile in the x and y
 * directions. If not tiling in x or y directions, the single
 * image is plotted. The width and height give the dimensions
 * the image is to be scaled to.
 *
 * \param ctx The current redraw context.
 * \param bitmap The bitmap to plot
 * \param x The x coordinate to plot the bitmap
 * \param y The y coordiante to plot the bitmap
 * \param width The width of area to plot the bitmap into
 * \param height The height of area to plot the bitmap into
 * \param bg the background colour to alpha blend into
 * \param flags the flags controlling the type of plot operation
 * \return NSERROR_OK on success else error code.
 */
static nserror bitmap(const struct redraw_context *ctx, struct bitmap *bitmap, int x, int y, int width, int height,
    colour bg, bitmap_flags_t flags)
{
    int xf, yf;
    bool repeat_x = (flags & BITMAPF_REPEAT_X);
    bool repeat_y = (flags & BITMAPF_REPEAT_Y);

    /* Bail early if we can */

    NSLOG(plot, DEEPDEBUG, "Plotting %p at %d,%d by %d,%d", bitmap, x, y, width, height);

    if (bitmap == NULL) {
        NSLOG(neosurf, INFO, "Passed null bitmap!");
        return NSERROR_OK;
    }

    /* check if nothing to plot */
    if (width == 0 || height == 0)
        return NSERROR_OK;

    /* x and y define coordinate of top left of of the initial explicitly
     * placed tile. The width and height are the image scaling and the
     * bounding box defines the extent of the repeat (which may go in all
     * four directions from the initial tile).
     */

    if (!(repeat_x || repeat_y)) {
        /* Not repeating at all, so just plot it */
        if ((bitmap->width == 1) && (bitmap->height == 1)) {
            if ((*(bitmap->pixdata + 3) & 0xff) == 0) {
                return NSERROR_OK;
            }
            return plot_block((*(COLORREF *)bitmap->pixdata) & 0xffffff, x, y, x + width, y + height);

        } else {
            return plot_bitmap(bitmap, x, y, width, height, bg);
        }
    }

    /* Optimise tiled plots of 1x1 bitmaps by replacing with a flat fill
     * of the area.  Can only be done when image is fully opaque. */
    if ((bitmap->width == 1) && (bitmap->height == 1)) {
        if ((*(COLORREF *)bitmap->pixdata & 0xff000000) != 0) {
            return plot_block((*(COLORREF *)bitmap->pixdata) & 0xffffff, plot_clip.left, plot_clip.top, plot_clip.right,
                plot_clip.bottom);
        }
    }

    /* Optimise tiled plots of bitmaps scaled to 1x1 by replacing with
     * a flat fill of the area.  Can only be done when image is fully
     * opaque.
     */
    if ((width == 1) && (height == 1)) {
        if (bitmap->opaque) {
            /** TODO: Currently using top left pixel. Maybe centre
             *        pixel or average value would be better. */
            return plot_block((*(COLORREF *)bitmap->pixdata) & 0xffffff, plot_clip.left, plot_clip.top, plot_clip.right,
                plot_clip.bottom);
        }
    }

    NSLOG(plot, DEEPDEBUG, "Tiled plotting %d,%d by %d,%d", x, y, width, height);
    NSLOG(plot, DEEPDEBUG, "clipped %ld,%ld to %ld,%ld", plot_clip.left, plot_clip.top, plot_clip.right,
        plot_clip.bottom);

    /* get left most tile position */
    if (repeat_x) {
        for (; x > plot_clip.left; x -= width)
            ;
    }

    /* get top most tile position */
    if (repeat_y) {
        for (; y > plot_clip.top; y -= height)
            ;
    }

    NSLOG(plot, DEEPDEBUG, "repeat from %d,%d to %ld,%ld", x, y, plot_clip.right, plot_clip.bottom);

    /* tile down and across to extents */
    for (xf = x; xf < plot_clip.right; xf += width) {
        for (yf = y; yf < plot_clip.bottom; yf += height) {

            plot_bitmap(bitmap, xf, yf, width, height, bg);
            if (!repeat_y)
                break;
        }
        if (!repeat_x)
            break;
    }
    return NSERROR_OK;
}


/**
 * Text plotting.
 *
 * \param ctx The current redraw context.
 * \param fstyle plot style for this text
 * \param x x coordinate
 * \param y y coordinate
 * \param text UTF-8 string to plot
 * \param length length of string, in bytes
 * \return NSERROR_OK on success else error code.
 */
static nserror text(const struct redraw_context *ctx, const struct plot_font_style *fstyle, int x, int y,
    const char *text, size_t length)
{
    NSLOG(plot, DEEPDEBUG, "words %s at %d,%d", text, x, y);

    /* ensure the plot HDC is set */
    if (plot_hdc == NULL) {
        NSLOG(neosurf, INFO, "HDC not set on call to plotters");
        return NSERROR_INVALID;
    }

    HRGN clipregion = CreateRectRgnIndirect(&plot_clip);
    if (clipregion == NULL) {
        return NSERROR_INVALID;
    }

    HFONT fontbak, font = get_font(fstyle);
    if (font == NULL) {
        DeleteObject(clipregion);
        return NSERROR_INVALID;
    }
    int wlen;
    SIZE s;
    LPWSTR wstring;
    fontbak = (HFONT)SelectObject(plot_hdc, font);
    GetTextExtentPoint(plot_hdc, text, length, &s);

    SelectClipRgn(plot_hdc, clipregion);

    SetTextAlign(plot_hdc, TA_BASELINE | TA_LEFT);
    if ((fstyle->background & 0xFF000000) != 0x01000000) {
        /* 100% alpha */
        SetBkColor(plot_hdc, (DWORD)(fstyle->background & 0x00FFFFFF));
    }
    SetBkMode(plot_hdc, TRANSPARENT);
    SetTextColor(plot_hdc, (DWORD)(fstyle->foreground & 0x00FFFFFF));

    wlen = MultiByteToWideChar(CP_UTF8, 0, text, length, NULL, 0);
    wstring = malloc(2 * (wlen + 1));
    if (wstring == NULL) {
        return NSERROR_INVALID;
    }
    MultiByteToWideChar(CP_UTF8, 0, text, length, wstring, wlen);
    TextOutW(plot_hdc, x, y, wstring, wlen);

    SelectClipRgn(plot_hdc, NULL);
    free(wstring);
    font = SelectObject(plot_hdc, fontbak);
    DeleteObject(clipregion);
    DeleteObject(font);

    return NSERROR_OK;
}


#ifdef NEOSURF_WINDOWS_NATIVE_LINEAR_GRADIENT
/**
 * Plot a linear gradient filling a path.
 *
 * Uses GDI GradientFill API with triangle mesh for gradient rendering.
 * This supports arbitrary gradient angles, not just axis-aligned.
 *
 * \param ctx The current redraw context.
 * \param path Path data (float array with path commands).
 * \param path_len Number of elements in path array.
 * \param transform 6-element affine transform to apply to path.
 * \param x0, y0 Start point of gradient line.
 * \param x1, y1 End point of gradient line.
 * \param stops Array of color stops.
 * \param stop_count Number of color stops.
 * \return NSERROR_OK on success else error code.
 */
static nserror win_plot_linear_gradient(const struct redraw_context *ctx, const float *path, unsigned int path_len,
    const float transform[6], float x0, float y0, float x1, float y1, const struct gradient_stop *stops,
    unsigned int stop_count)
{
    HRGN clipregion;
    float minx, miny, maxx, maxy;
    int bbox_init = 0;
    unsigned int i;

    if (plot_hdc == NULL) {
        NSLOG(neosurf, INFO, "HDC not set on call to plotters");
        return NSERROR_INVALID;
    }

    if (stop_count < 2) {
        NSLOG(plot, WARNING, "Linear gradient needs at least 2 stops, got %u", stop_count);
        return NSERROR_INVALID;
    }

    /* Use identity transform if none provided (CSS gradients don't provide transform) */
    static const float identity[6] = {1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f};
    if (transform == NULL) {
        transform = identity;
    }

    /* Extract bounding box from path */
    if (path != NULL && path_len >= 3) {
        i = 0;
        while (i < path_len) {
            int cmd = (int)path[i++];
            switch (cmd) {
            case PLOTTER_PATH_MOVE:
            case PLOTTER_PATH_LINE: {
                float px = path[i++];
                float py = path[i++];
                /* Apply transform */
                float tx = transform[0] * px + transform[2] * py + transform[4];
                float ty = transform[1] * px + transform[3] * py + transform[5];
                if (!bbox_init) {
                    minx = maxx = tx;
                    miny = maxy = ty;
                    bbox_init = 1;
                } else {
                    if (tx < minx)
                        minx = tx;
                    if (tx > maxx)
                        maxx = tx;
                    if (ty < miny)
                        miny = ty;
                    if (ty > maxy)
                        maxy = ty;
                }
                break;
            }
            case PLOTTER_PATH_BEZIER: {
                /* Include all bezier control points in bounding box */
                for (int j = 0; j < 3; j++) {
                    float px = path[i++];
                    float py = path[i++];
                    float tx = transform[0] * px + transform[2] * py + transform[4];
                    float ty = transform[1] * px + transform[3] * py + transform[5];
                    if (!bbox_init) {
                        minx = maxx = tx;
                        miny = maxy = ty;
                        bbox_init = 1;
                    } else {
                        if (tx < minx)
                            minx = tx;
                        if (tx > maxx)
                            maxx = tx;
                        if (ty < miny)
                            miny = ty;
                        if (ty > maxy)
                            maxy = ty;
                    }
                }
                break;
            }
            case PLOTTER_PATH_CLOSE:
                break;
            default:
                NSLOG(plot, WARNING, "Unknown path command %d in gradient", cmd);
                break;
            }
        }
    }

    if (!bbox_init) {
        /* No valid path - use the current clip rect as the fill area.
         * This is the correct behavior for CSS gradients where the clip
         * region defines the shape to fill. */
        minx = (float)plot_clip.left;
        miny = (float)plot_clip.top;
        maxx = (float)plot_clip.right;
        maxy = (float)plot_clip.bottom;
    }

    NSLOG(plot, DEBUG, "GDI linear gradient: (%.1f,%.1f) to (%.1f,%.1f), %u stops, bbox=(%.1f,%.1f)-(%.1f,%.1f)", x0,
        y0, x1, y1, stop_count, minx, miny, maxx, maxy);

    /* Set up clipping region - for SVG gradients, clip to the path shape */
    HRGN pathregion = NULL;
    if (path != NULL && path_len >= 3) {
        /* Create a GDI path from the path data */
        BeginPath(plot_hdc);

        i = 0;
        while (i < path_len) {
            int cmd = (int)path[i++];
            switch (cmd) {
            case PLOTTER_PATH_MOVE: {
                float px = path[i++];
                float py = path[i++];
                float tx = transform[0] * px + transform[2] * py + transform[4];
                float ty = transform[1] * px + transform[3] * py + transform[5];
                MoveToEx(plot_hdc, (int)tx, (int)ty, NULL);
                break;
            }
            case PLOTTER_PATH_LINE: {
                float px = path[i++];
                float py = path[i++];
                float tx = transform[0] * px + transform[2] * py + transform[4];
                float ty = transform[1] * px + transform[3] * py + transform[5];
                LineTo(plot_hdc, (int)tx, (int)ty);
                break;
            }
            case PLOTTER_PATH_BEZIER: {
                POINT pts[3];
                for (int j = 0; j < 3; j++) {
                    float px = path[i++];
                    float py = path[i++];
                    pts[j].x = (LONG)(transform[0] * px + transform[2] * py + transform[4]);
                    pts[j].y = (LONG)(transform[1] * px + transform[3] * py + transform[5]);
                }
                PolyBezierTo(plot_hdc, pts, 3);
                break;
            }
            case PLOTTER_PATH_CLOSE:
                CloseFigure(plot_hdc);
                break;
            default:
                break;
            }
        }

        EndPath(plot_hdc);

        /* Convert path to region for clipping */
        pathregion = PathToRegion(plot_hdc);
        if (pathregion != NULL) {
            /* Also intersect with the plot_clip to respect viewport bounds */
            HRGN cliprect = CreateRectRgnIndirect(&plot_clip);
            if (cliprect != NULL) {
                CombineRgn(pathregion, pathregion, cliprect, RGN_AND);
                DeleteObject(cliprect);
            }
            SelectClipRgn(plot_hdc, pathregion);
        }
    }

    /* If no path region was created, use rectangle clip */
    if (pathregion == NULL) {
        clipregion = CreateRectRgnIndirect(&plot_clip);
        if (clipregion == NULL) {
            return NSERROR_INVALID;
        }
        SelectClipRgn(plot_hdc, clipregion);
    } else {
        clipregion = pathregion;
    }

    /* Transform gradient endpoints */
    float gx0 = transform[0] * x0 + transform[2] * y0 + transform[4];
    float gy0 = transform[1] * x0 + transform[3] * y0 + transform[5];
    float gx1 = transform[0] * x1 + transform[2] * y1 + transform[4];
    float gy1 = transform[1] * x1 + transform[3] * y1 + transform[5];

    /* Calculate gradient direction */
    float gdx = gx1 - gx0;
    float gdy = gy1 - gy0;
    float glen = sqrtf(gdx * gdx + gdy * gdy);

    if (glen < 0.01f) {
        /* Degenerate gradient - just fill with first color */
        COLORREF col = RGB(stops[0].color & 0xFF, (stops[0].color >> 8) & 0xFF, (stops[0].color >> 16) & 0xFF);
        HBRUSH brush = CreateSolidBrush(col);
        RECT fillrect = {(LONG)minx, (LONG)miny, (LONG)(maxx + 1), (LONG)(maxy + 1)};
        FillRect(plot_hdc, &fillrect, brush);
        DeleteObject(brush);
        SelectClipRgn(plot_hdc, NULL);
        DeleteObject(clipregion);
        return NSERROR_OK;
    }

    /* Normalize gradient direction */
    float ndx = gdx / glen;
    float ndy = gdy / glen;

    /* Perpendicular direction */
    float pdx = -ndy;
    float pdy = ndx;

    /* Calculate how far the bounding box extends along the gradient direction */
    float corners[4][2] = {{minx, miny}, {maxx, miny}, {maxx, maxy}, {minx, maxy}};
    float min_proj = 1e30f, max_proj = -1e30f;
    float max_perp = 0;

    for (int c = 0; c < 4; c++) {
        float dx = corners[c][0] - gx0;
        float dy = corners[c][1] - gy0;
        float proj = dx * ndx + dy * ndy;
        float perp = fabsf(dx * pdx + dy * pdy);
        if (proj < min_proj)
            min_proj = proj;
        if (proj > max_proj)
            max_proj = proj;
        if (perp > max_perp)
            max_perp = perp;
    }

    /* Use GradientFill with triangle mesh for smooth gradient */
    /* We'll create strips perpendicular to gradient direction */

    /* Number of vertices = 2 * number of stops (left and right edge of each strip) */
    TRIVERTEX *vertices = malloc(sizeof(TRIVERTEX) * stop_count * 2);
    GRADIENT_TRIANGLE *triangles = malloc(sizeof(GRADIENT_TRIANGLE) * (stop_count - 1) * 2);

    if (vertices == NULL || triangles == NULL) {
        free(vertices);
        free(triangles);
        SelectClipRgn(plot_hdc, NULL);
        DeleteObject(clipregion);
        return NSERROR_NOMEM;
    }

    /* Extend perpendicular distance to cover the bounding box */
    float perp_extend = max_perp + 10;

    for (i = 0; i < stop_count; i++) {
        float t = stops[i].offset;
        /* Project position along gradient line */
        float pos = t * glen;

        /* Calculate left and right vertices perpendicular to gradient */
        float cx = gx0 + ndx * pos;
        float cy = gy0 + ndy * pos;

        /* Left vertex (negative perpendicular direction) */
        vertices[i * 2].x = (LONG)(cx - pdx * perp_extend);
        vertices[i * 2].y = (LONG)(cy - pdy * perp_extend);

        /* Right vertex (positive perpendicular direction) */
        vertices[i * 2 + 1].x = (LONG)(cx + pdx * perp_extend);
        vertices[i * 2 + 1].y = (LONG)(cy + pdy * perp_extend);

        /* Set color - GDI uses 16-bit color components */
        colour c = stops[i].color;
        USHORT red = ((c & 0xFF) << 8);
        USHORT green = (((c >> 8) & 0xFF) << 8);
        USHORT blue = (((c >> 16) & 0xFF) << 8);
        USHORT alpha = 0xFF00; /* Full opacity */

        vertices[i * 2].Red = red;
        vertices[i * 2].Green = green;
        vertices[i * 2].Blue = blue;
        vertices[i * 2].Alpha = alpha;

        vertices[i * 2 + 1].Red = red;
        vertices[i * 2 + 1].Green = green;
        vertices[i * 2 + 1].Blue = blue;
        vertices[i * 2 + 1].Alpha = alpha;
    }

    /* Create triangles between adjacent stop lines */
    for (i = 0; i < stop_count - 1; i++) {
        /* First triangle: current-left, current-right, next-left */
        triangles[i * 2].Vertex1 = i * 2;
        triangles[i * 2].Vertex2 = i * 2 + 1;
        triangles[i * 2].Vertex3 = (i + 1) * 2;

        /* Second triangle: current-right, next-right, next-left */
        triangles[i * 2 + 1].Vertex1 = i * 2 + 1;
        triangles[i * 2 + 1].Vertex2 = (i + 1) * 2 + 1;
        triangles[i * 2 + 1].Vertex3 = (i + 1) * 2;
    }

    /* Call GradientFill */
    BOOL result = GradientFill(
        plot_hdc, vertices, stop_count * 2, triangles, (stop_count - 1) * 2, GRADIENT_FILL_TRIANGLE);

    if (!result) {
        NSLOG(plot, WARNING, "GradientFill failed with error %lu", GetLastError());
    }

    free(vertices);
    free(triangles);

    SelectClipRgn(plot_hdc, NULL);
    DeleteObject(clipregion);

    return result ? NSERROR_OK : NSERROR_INVALID;
}
#endif /* NEOSURF_WINDOWS_NATIVE_LINEAR_GRADIENT */


/**
 * win32 API plot operation table
 */
const struct plotter_table win_plotters = {
    .rectangle = rectangle,
    .line = line,
    .polygon = polygon,
    .clip = clip,
    .text = text,
    .disc = disc,
    .arc = arc,
    .bitmap = bitmap,
    .path = path,
#ifdef NEOSURF_WINDOWS_NATIVE_LINEAR_GRADIENT
    .linear_gradient = win_plot_linear_gradient,
#endif
    .option_knockout = true,
};

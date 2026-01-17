/*
 * Copyright 2021 Vincent Sanders <vince@netsurf-browser.org>
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
 * Implementation of plotters for qt.
 */

#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>
#include <stddef.h>

extern "C" {

#include "utils/errors.h"
#include "utils/log.h"
#include "neosurf/mouse.h"
#include "neosurf/plotters.h"
#include "neosurf/types.h"
#include "neosurf/window.h"
}

#include "qt/layout.h"
#include "qt/plotters.h"
#include "qt/window.h"


/**
 * setup qt painter styles according to netsurf plot style
 */
static nserror nsqt_set_style(QPainter *painter, const plot_style_t *style)
{
    /* Extract RGBA components - NS color format is 0xAABBGGRR
     * Note: NS uses inverted alpha (0=opaque, 255=transparent)
     * Qt uses standard alpha (0=transparent, 255=opaque) */
    int fill_r = style->fill_colour & 0xFF;
    int fill_g = (style->fill_colour & 0xFF00) >> 8;
    int fill_b = (style->fill_colour & 0xFF0000) >> 16;
    int fill_a = 255 - ((style->fill_colour >> 24) & 0xFF);

    QColor fillcolour(fill_r, fill_g, fill_b, fill_a);

    Qt::BrushStyle brushstyle = Qt::NoBrush;
    if (style->fill_type != PLOT_OP_TYPE_NONE) {
        brushstyle = Qt::SolidPattern;
    }
    QBrush brush(fillcolour, brushstyle);
    painter->setBrush(brush);

    /* Extract RGBA components for stroke - same alpha inversion */
    int stroke_r = style->stroke_colour & 0xFF;
    int stroke_g = (style->stroke_colour & 0xFF00) >> 8;
    int stroke_b = (style->stroke_colour & 0xFF0000) >> 16;
    int stroke_a = 255 - ((style->stroke_colour >> 24) & 0xFF);
    QColor strokecolour(stroke_r, stroke_g, stroke_b, stroke_a);

    QPen pen(strokecolour);
    Qt::PenStyle penstyle = Qt::NoPen;
    if (style->stroke_type != PLOT_OP_TYPE_NONE) {
        penstyle = Qt::SolidLine;
    }
    pen.setStyle(penstyle);
    pen.setWidthF(plot_style_fixed_to_float(style->stroke_width));

    /* Note: Dash patterns are handled in svg.c by converting dashed lines
     * to filled rectangles for cross-platform compatibility */

    painter->setPen(pen);

    return NSERROR_OK;
}


/**
 * \brief Sets a clip rectangle for subsequent plot operations.
 *
 * \param ctx The current redraw context.
 * \param clip The rectangle to limit all subsequent plot
 *              operations within.
 * \return NSERROR_OK on success else error code.
 */
static nserror nsqt_plot_clip(const struct redraw_context *ctx, const struct rect *clip)
{
    QPainter *painter = (QPainter *)ctx->priv;

    /* Clip rect is in world coordinates, but setClipRect interprets coords
     * in the current (transformed) coordinate space. We need to apply the
     * inverse transform to get correct world-space clipping. */
    QTransform inv = painter->worldTransform().inverted();
    QRectF worldClip(clip->x0, clip->y0, clip->x1 - clip->x0, clip->y1 - clip->y0);
    QRectF transformedClip = inv.mapRect(worldClip);
    painter->setClipRect(transformedClip);
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
static nserror nsqt_plot_arc(
    const struct redraw_context *ctx, const plot_style_t *style, int x, int y, int radius, int angle1, int angle2)
{
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
static nserror nsqt_plot_disc(const struct redraw_context *ctx, const plot_style_t *style, int x, int y, int radius)
{
    QPainter *painter = (QPainter *)ctx->priv;
    nsqt_set_style(painter, style);
    painter->drawEllipse(QPoint(x, y), radius, radius);
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
static nserror nsqt_plot_line(const struct redraw_context *ctx, const plot_style_t *style, const struct rect *line)
{
    QPainter *painter = (QPainter *)ctx->priv;
    nsqt_set_style(painter, style);

    painter->drawLine(line->x0, line->y0, line->x1, line->y1);

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
static nserror nsqt_plot_rectangle(const struct redraw_context *ctx, const plot_style_t *style, const struct rect *rect)
{
    QPainter *painter = (QPainter *)ctx->priv;
    nsqt_set_style(painter, style);

    /* Use fillRect when only fill is needed (no stroke) to avoid ghost outline */
    if (style->stroke_type == PLOT_OP_TYPE_NONE) {
        if (style->fill_type != PLOT_OP_TYPE_NONE) {
            /* Disable anti-aliasing for filled rectangles to ensure pixel-perfect edges */
            bool had_aa = painter->testRenderHint(QPainter::Antialiasing);
            if (had_aa)
                painter->setRenderHint(QPainter::Antialiasing, false);
            painter->fillRect(rect->x0, rect->y0, rect->x1 - rect->x0, rect->y1 - rect->y0, painter->brush());
            if (had_aa)
                painter->setRenderHint(QPainter::Antialiasing, true);
        }
    } else {
        painter->drawRect(rect->x0, rect->y0, rect->x1 - rect->x0, rect->y1 - rect->y0);
    }
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
static nserror
nsqt_plot_polygon(const struct redraw_context *ctx, const plot_style_t *style, const int *p, unsigned int n)
{
    return NSERROR_OK;
}


/**
 * Plots a path.
 *
 * Path plot consisting of cubic Bezier curves. Line and fill colour is
 *  controlled by the plot style.
 *
 * The transform to apply is affine (meaning it omits the three
 * projection factor parameters from the standard 3x3 matrix assumining
 * default values)
 *
 * +--------------+--------------+--------------+
 * | transform[0] | transform[1] |      0.0     |
 * +--------------+--------------+--------------+
 * | transform[2] | transform[3] |      0.0     |
 * +--------------+--------------+--------------+
 * | transform[4] | transform[5] |      1.0     |
 * +--------------+--------------+--------------+

 * \param ctx The current redraw context.
 * \param pstyle Style controlling the path plot.
 * \param p elements of path
 * \param pn nunber of elements on path
 * \param transform A transform to apply to the path.
 * \return NSERROR_OK on success else error code.
 */
static nserror nsqt_plot_path(const struct redraw_context *ctx, const plot_style_t *pstyle, const float *p,
    unsigned int pn, const float transform[6])
{
    unsigned int idx = 0;
    QPainter *painter = (QPainter *)ctx->priv;

    if (pn < 3) {
        /* path does not have enough points for initial move */
        return NSERROR_OK;
    }
    if (p[0] != PLOTTER_PATH_MOVE) {
        NSLOG(netsurf, INFO, "Path does not start with move");
        return NSERROR_INVALID;
    }

    QPainterPath qtpath(QPointF(p[1], p[2]));
    for (idx = 3; idx < pn;) {
        switch ((int)p[idx]) {
        case PLOTTER_PATH_MOVE:
            qtpath.moveTo(p[idx + 1], p[idx + 2]);
            idx += 3;
            break;
        case PLOTTER_PATH_CLOSE:
            qtpath.closeSubpath();
            idx += 1;
            break;
        case PLOTTER_PATH_LINE:
            qtpath.lineTo(p[idx + 1], p[idx + 2]);
            idx += 3;
            break;
        case PLOTTER_PATH_BEZIER:
            qtpath.cubicTo(p[idx + 1], p[idx + 2], p[idx + 3], p[idx + 4], p[idx + 5], p[idx + 6]);
            idx += 7;
            break;

        default:
            NSLOG(netsurf, INFO, "bad path command %f", p[idx]);
            return NSERROR_INVALID;
        }
    }
    qtpath.setFillRule(Qt::WindingFill);

    /* Normal rendering - dashed lines are pre-converted to rectangles in svg.c */
    nsqt_set_style(painter, pstyle);
    const QTransform orig_transform = painter->transform();
    painter->setTransform(
        QTransform(transform[0], transform[1], 0.0, transform[2], transform[3], 0.0, transform[4], transform[5], 1.0),
        true);
    painter->drawPath(qtpath);
    painter->setTransform(orig_transform);
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
static nserror nsqt_plot_bitmap(const struct redraw_context *ctx, struct bitmap *bitmap, int x, int y, int width,
    int height, colour bg, bitmap_flags_t flags)
{
    QImage *img = (QImage *)bitmap;
    QPainter *painter = (QPainter *)ctx->priv;
    QRectF source(0, 0, img->width(), img->height());
    QRectF target(x, y, width, height);

    NSLOG(netsurf, WARNING, "bitmap plot: target=(%d,%d %dx%d) source=(%dx%d)", x, y, width, height, img->width(),
        img->height());

    /* Enable smooth scaling for better image quality when resizing */
    painter->setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter->drawImage(target, *img, source);

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
static nserror nsqt_plot_text(const struct redraw_context *ctx, const struct plot_font_style *fstyle, int x, int y,
    const char *text, size_t length)
{
    return nsqt_layout_plot((QPainter *)ctx->priv, fstyle, x, y, text, length);
}


/**
 * Push a transformation matrix onto the transform stack.
 *
 * Uses QPainter's save/setTransform to apply the transform.
 * The transform is combined with the current transform.
 *
 * \param ctx The current redraw context.
 * \param transform 6-element affine transform matrix.
 * \return NSERROR_OK on success else error code.
 */
static nserror nsqt_push_transform(const struct redraw_context *ctx, const float transform[6])
{
    QPainter *painter = (QPainter *)ctx->priv;

    /* Save current state (includes transform) */
    painter->save();

    /* Create QTransform from the 6-element affine matrix */
    /* Matrix format: [a, b, c, d, tx, ty]
     * QTransform constructor: m11, m12, m21, m22, dx, dy
     * Where:
     *   m11 = a (scale x / cos for rotate)
     *   m12 = b (shear y / sin for rotate)
     *   m21 = c (shear x / -sin for rotate)
     *   m22 = d (scale y / cos for rotate)
     *   dx = tx (translate x)
     *   dy = ty (translate y)
     */
    QTransform matrix(transform[0], transform[1], transform[2], transform[3], transform[4], transform[5]);

    /* Combine with current transform */
    painter->setTransform(matrix, true);

    return NSERROR_OK;
}


/**
 * Pop the most recent transform from the transform stack.
 *
 * Restores the previous transform state using QPainter::restore().
 *
 * \param ctx The current redraw context.
 * \return NSERROR_OK on success else error code.
 */
static nserror nsqt_pop_transform(const struct redraw_context *ctx)
{
    QPainter *painter = (QPainter *)ctx->priv;

    /* Restore previous state (includes transform) */
    painter->restore();

    return NSERROR_OK;
}


/**
 * Convert NetSurf color to QColor.
 * NS format is 0xAABBGGRR with inverted alpha (0=opaque, 255=transparent).
 */
static inline QColor nsqt_qcolor_from_ns(colour c)
{
    int r = c & 0xFF;
    int g = (c >> 8) & 0xFF;
    int b = (c >> 16) & 0xFF;
    int a = 255 - ((c >> 24) & 0xFF); /* Invert alpha */
    return QColor(r, g, b, a);
}


/**
 * Plot a linear gradient filling a path.
 *
 * Uses Qt's native QLinearGradient with QPainterPath for path-clipped fills.
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
static nserror nsqt_plot_linear_gradient(const struct redraw_context *ctx, const float *path, unsigned int path_len,
    const float transform[6], float x0, float y0, float x1, float y1, const struct gradient_stop *stops,
    unsigned int stop_count)
{
    QPainter *painter = (QPainter *)ctx->priv;
    if (painter == nullptr || stop_count < 2) {
        return NSERROR_INVALID;
    }

    /* Create gradient */
    QLinearGradient gradient(x0, y0, x1, y1);
    for (unsigned int i = 0; i < stop_count; i++) {
        QColor color = nsqt_qcolor_from_ns(stops[i].color);
        gradient.setColorAt(stops[i].offset, color);
    }

    /* Path-based fill (SVG) vs clip-based fill (CSS) */
    if (path != nullptr && path_len >= 3) {
        NSLOG(plot, DEBUG, "Native linear gradient: x0=%.1f y0=%.1f x1=%.1f y1=%.1f stops=%u path_len=%u", x0, y0, x1,
            y1, stop_count, path_len);
        NSLOG(plot, DEBUG, "  Gradient QLinearGradient start=(%.1f,%.1f) end=(%.1f,%.1f)", x0, y0, x1, y1);
        NSLOG(plot, DEBUG, "  Path first point: (%.1f, %.1f)", path[1], path[2]);

        QPainterPath qtpath(QPointF(path[1], path[2]));
        for (unsigned int idx = 3; idx < path_len;) {
            switch ((int)path[idx]) {
            case PLOTTER_PATH_MOVE:
                qtpath.moveTo(path[idx + 1], path[idx + 2]);
                idx += 3;
                break;
            case PLOTTER_PATH_CLOSE:
                qtpath.closeSubpath();
                idx += 1;
                break;
            case PLOTTER_PATH_LINE:
                qtpath.lineTo(path[idx + 1], path[idx + 2]);
                idx += 3;
                break;
            case PLOTTER_PATH_BEZIER:
                qtpath.cubicTo(
                    path[idx + 1], path[idx + 2], path[idx + 3], path[idx + 4], path[idx + 5], path[idx + 6]);
                idx += 7;
                break;
            default:
                NSLOG(netsurf, WARNING, "bad path command %f in gradient", path[idx]);
                return NSERROR_INVALID;
            }
        }
        qtpath.setFillRule(Qt::WindingFill);

        /* Log path bounds and clip state for debugging */
        QRectF path_bounds = qtpath.boundingRect();
        QRectF clip_bounds = painter->clipBoundingRect();
        QRegion clip_region = painter->clipRegion();
        NSLOG(plot, DEBUG, "  QPainterPath bounds: (%.1f,%.1f) to (%.1f,%.1f) size=%.1fx%.1f", path_bounds.left(),
            path_bounds.top(), path_bounds.right(), path_bounds.bottom(), path_bounds.width(), path_bounds.height());
        NSLOG(plot, DEBUG, "  QPainter clipBoundingRect: (%.1f,%.1f) to (%.1f,%.1f)", clip_bounds.left(),
            clip_bounds.top(), clip_bounds.right(), clip_bounds.bottom());
        NSLOG(plot, DEBUG, "  QPainter hasClipping=%d clipRegion.isEmpty=%d", painter->hasClipping(),
            clip_region.isEmpty());

        /* Path and gradient are in scaled-only space, apply transform for rendering */
        const QTransform orig_transform = painter->transform();
        NSLOG(plot, DEBUG, "  QPainter orig_transform: [%.2f,%.2f,%.2f,%.2f,%.2f,%.2f]", orig_transform.m11(),
            orig_transform.m12(), orig_transform.m21(), orig_transform.m22(), orig_transform.dx(), orig_transform.dy());

        if (transform != nullptr) {
            NSLOG(plot, DEBUG, "  Applying transform: [%.2f,%.2f,%.2f,%.2f,%.2f,%.2f]", transform[0], transform[1],
                transform[2], transform[3], transform[4], transform[5]);
            painter->setTransform(QTransform(transform[0], transform[1], 0.0, transform[2], transform[3], 0.0,
                                      transform[4], transform[5], 1.0),
                true);
        }
        painter->fillPath(qtpath, gradient);
        painter->setTransform(orig_transform);
    } else {
        /* CSS gradients: fill the clip rect */
        NSLOG(plot, DEBUG, "Native linear gradient (CSS): (%.1f,%.1f) to (%.1f,%.1f) with %u stops", x0, y0, x1, y1,
            stop_count);
        QRectF clip_rect = painter->clipBoundingRect();
        if (clip_rect.isEmpty()) {
            /* If no clip set, use the gradient bounds */
            float minX = (x0 < x1) ? x0 : x1;
            float minY = (y0 < y1) ? y0 : y1;
            float maxX = (x0 > x1) ? x0 : x1;
            float maxY = (y0 > y1) ? y0 : y1;
            if (minX == maxX) {
                minX -= 1000;
                maxX += 1000;
            }
            if (minY == maxY) {
                minY -= 1000;
                maxY += 1000;
            }
            clip_rect = QRectF(minX, minY, maxX - minX, maxY - minY);
        }
        painter->fillRect(clip_rect, gradient);
    }

    return NSERROR_OK;
}
/**
 * Plot a radial gradient filling a path.
 *
 * Uses Qt's native QRadialGradient with QPainterPath for path-clipped fills.
 * For elliptical gradients, uses a transform to stretch the circle.
 *
 * \param ctx The current redraw context.
 * \param path Path data (float array with path commands).
 * \param path_len Number of elements in path array.
 * \param transform 6-element affine transform to apply to path.
 * \param cx, cy Center point of gradient.
 * \param rx, ry X and Y radii (set both equal for circle).
 * \param stops Array of color stops.
 * \param stop_count Number of color stops.
 * \return NSERROR_OK on success else error code.
 */
static nserror nsqt_plot_radial_gradient(const struct redraw_context *ctx, const float *path, unsigned int path_len,
    const float transform[6], float cx, float cy, float rx, float ry, const struct gradient_stop *stops,
    unsigned int stop_count)
{
    QPainter *painter = (QPainter *)ctx->priv;
    if (painter == nullptr || stop_count < 2) {
        return NSERROR_INVALID;
    }

    /* QRadialGradient uses a single radius, so we'll use the larger one
     * and apply a transform for ellipses */
    float radius = (rx > ry) ? rx : ry;
    bool is_ellipse = (rx != ry);

    QRadialGradient gradient(cx, cy, radius);
    for (unsigned int i = 0; i < stop_count; i++) {
        QColor color = nsqt_qcolor_from_ns(stops[i].color);
        gradient.setColorAt(stops[i].offset, color);
    }

    /* Path-based fill (SVG) vs clip-based fill (CSS) */
    if (path != nullptr && path_len >= 3) {
        /* Convert path to QPainterPath */
        QPainterPath qtpath(QPointF(path[1], path[2]));
        for (unsigned int idx = 3; idx < path_len;) {
            switch ((int)path[idx]) {
            case PLOTTER_PATH_MOVE:
                qtpath.moveTo(path[idx + 1], path[idx + 2]);
                idx += 3;
                break;
            case PLOTTER_PATH_CLOSE:
                qtpath.closeSubpath();
                idx += 1;
                break;
            case PLOTTER_PATH_LINE:
                qtpath.lineTo(path[idx + 1], path[idx + 2]);
                idx += 3;
                break;
            case PLOTTER_PATH_BEZIER:
                qtpath.cubicTo(
                    path[idx + 1], path[idx + 2], path[idx + 3], path[idx + 4], path[idx + 5], path[idx + 6]);
                idx += 7;
                break;
            default:
                NSLOG(netsurf, WARNING, "bad path command %f in gradient", path[idx]);
                return NSERROR_INVALID;
            }
        }
        qtpath.setFillRule(Qt::WindingFill);

        /* Path and gradient are in scaled-only space, apply transform for rendering */
        const QTransform orig_transform = painter->transform();
        if (transform != nullptr) {
            QTransform path_transform(
                transform[0], transform[1], 0.0, transform[2], transform[3], 0.0, transform[4], transform[5], 1.0);
            if (is_ellipse) {
                path_transform.translate(cx, cy);
                path_transform.scale(rx / radius, ry / radius);
                path_transform.translate(-cx, -cy);
            }
            painter->setTransform(path_transform, true);
        } else if (is_ellipse) {
            painter->translate(cx, cy);
            painter->scale(rx / radius, ry / radius);
            painter->translate(-cx, -cy);
        }
        painter->fillPath(qtpath, gradient);
        painter->setTransform(orig_transform);
    } else {
        /* CSS gradients: fill the clip rect */
        QRectF clip_rect = painter->clipBoundingRect();

        if (is_ellipse) {
            /* Apply ellipse transform to the gradient brush, not the painter */
            QTransform ellipse_transform;
            ellipse_transform.translate(cx, cy);
            ellipse_transform.scale(rx / radius, ry / radius);
            ellipse_transform.translate(-cx, -cy);

            QBrush brush(gradient);
            brush.setTransform(ellipse_transform);
            painter->fillRect(clip_rect, brush);
        } else {
            painter->fillRect(clip_rect, gradient);
        }
    }

    return NSERROR_OK;
}


/**
 * QT plotter table
 */
const struct plotter_table nsqt_plotters = {.clip = nsqt_plot_clip,
    .arc = nsqt_plot_arc,
    .disc = nsqt_plot_disc,
    .line = nsqt_plot_line,
    .rectangle = nsqt_plot_rectangle,
    .polygon = nsqt_plot_polygon,
    .path = nsqt_plot_path,
    .bitmap = nsqt_plot_bitmap,
    .text = nsqt_plot_text,
    .group_start = NULL,
    .group_end = NULL,
    .flush = NULL,
    .push_transform = nsqt_push_transform,
    .pop_transform = nsqt_pop_transform,
    .linear_gradient = nsqt_plot_linear_gradient,
    .radial_gradient = nsqt_plot_radial_gradient,
    .option_knockout = true};

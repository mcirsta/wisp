/*
 * Copyright 2007-2008 James Bursa <bursa@users.sourceforge.net>
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
 * implementation of content for image/svg using libsvgtiny.
 */

#include <assert.h>
#include <limits.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

#include <svgtiny.h>

#include <neosurf/utils/messages.h>
#include <neosurf/utils/utils.h>
#include <neosurf/utils/nsurl.h>
#include <neosurf/utils/log.h>
#include <neosurf/plotters.h>
#include <neosurf/content.h>
#include <neosurf/content/content_protected.h>
#include "content/content_factory.h"

#include "content/handlers/image/svg.h"

/* Maximum number of floats sent per plotter path call to avoid oversized buffers */
#define SVG_COMBO_FLUSH_LIMIT 960

/* Split a path buffer into safe chunks at MOVE boundaries and plot each chunk */
static nserror svg_plot_path_chunked(const struct redraw_context *ctx,
                                     const plot_style_t *style,
                                     const float *p,
                                     unsigned int n,
                                     const float transform[6])
{
    unsigned int pos = 0;
    unsigned int grp_start = 0;
    unsigned int grp_len = 0;
    unsigned int grp_moves = 0;
    float gb_minx = 0.0f, gb_miny = 0.0f, gb_maxx = 0.0f, gb_maxy = 0.0f;
    int gb_init = 0;
    nserror r = NSERROR_OK;

    while (pos < n) {
        while (pos < n && (int)p[pos] != PLOTTER_PATH_MOVE) pos++;
        if (pos >= n) break;

        unsigned int sp_start = pos;
        float sb_minx = 0.0f, sb_miny = 0.0f, sb_maxx = 0.0f, sb_maxy = 0.0f;
        int sb_init = 0;

        while (pos < n) {
            int cmd = (int)p[pos++];
            if (cmd == PLOTTER_PATH_MOVE || cmd == PLOTTER_PATH_LINE) {
                float xx = p[pos++];
                float yy = p[pos++];
                if (!sb_init) { sb_minx = sb_maxx = xx; sb_miny = sb_maxy = yy; sb_init = 1; }
                if (xx < sb_minx) sb_minx = xx; if (xx > sb_maxx) sb_maxx = xx;
                if (yy < sb_miny) sb_miny = yy; if (yy > sb_maxy) sb_maxy = yy;
            } else if (cmd == PLOTTER_PATH_BEZIER) {
                float x1 = p[pos++]; float y1 = p[pos++];
                float x2 = p[pos++]; float y2 = p[pos++];
                float x3 = p[pos++]; float y3 = p[pos++];
                if (!sb_init) { sb_minx = sb_maxx = x1; sb_miny = sb_maxy = y1; sb_init = 1; }
                if (x1 < sb_minx) sb_minx = x1; if (x1 > sb_maxx) sb_maxx = x1;
                if (y1 < sb_miny) sb_miny = y1; if (y1 > sb_maxy) sb_maxy = y1;
                if (x2 < sb_minx) sb_minx = x2; if (x2 > sb_maxx) sb_maxx = x2;
                if (y2 < sb_miny) sb_miny = y2; if (y2 > sb_maxy) sb_maxy = y2;
                if (x3 < sb_minx) sb_minx = x3; if (x3 > sb_maxx) sb_maxx = x3;
                if (y3 < sb_miny) sb_miny = y3; if (y3 > sb_maxy) sb_maxy = y3;
            } else if (cmd == PLOTTER_PATH_CLOSE) {
                /* no coords */
            }
            if (pos < n && (int)p[pos] == PLOTTER_PATH_MOVE) break;
        }
        unsigned int sp_end = pos;
        unsigned int sp_len = sp_end - sp_start;
        NSLOG(neosurf, INFO, "SVG subpath parsed: sp_len=%u sbbox=%.2f,%.2f..%.2f,%.2f", sp_len, sb_minx, sb_miny, sb_maxx, sb_maxy);

        if (grp_len == 0) {
            grp_start = sp_start;
            grp_len = sp_len;
            grp_moves = 1;
            gb_minx = sb_minx; gb_miny = sb_miny; gb_maxx = sb_maxx; gb_maxy = sb_maxy; gb_init = sb_init;
            continue;
        }

        int overlap = (sb_maxx >= gb_minx && sb_minx <= gb_maxx && sb_maxy >= gb_miny && sb_miny <= gb_maxy);
        NSLOG(neosurf, INFO, "SVG group decision: grp_len=%u grp_moves=%u sb_len=%u gbbox=%.2f,%.2f..%.2f,%.2f sbbox=%.2f,%.2f..%.2f,%.2f overlap=%d next_total=%u limit=%u",
              grp_len, grp_moves, sp_len,
              gb_minx, gb_miny, gb_maxx, gb_maxy,
              sb_minx, sb_miny, sb_maxx, sb_maxy,
              overlap, grp_len + sp_len, SVG_COMBO_FLUSH_LIMIT);
        if (!overlap || grp_len + sp_len > SVG_COMBO_FLUSH_LIMIT) {
            NSLOG(neosurf, INFO, "SVG chunk flush: len=%u moves=%u reason=%s", grp_len, grp_moves, (!overlap ? "disjoint" : "limit"));
            nserror rr = ctx->plot->path(ctx, style, p + grp_start, grp_len, transform);
            if (rr != NSERROR_OK) {
                NSLOG(neosurf, ERROR, "SVG chunk flush failed: len=%u err=%d; splitting fallback", grp_len, rr);
                unsigned int pos2 = grp_start;
                while (pos2 < grp_start + grp_len) {
                    while (pos2 < grp_start + grp_len && (int)p[pos2] != PLOTTER_PATH_MOVE) pos2++;
                    if (pos2 >= grp_start + grp_len) break;
                    unsigned int sp = pos2;
                    unsigned int ep = sp + 1;
                    while (ep < grp_start + grp_len) {
                        int c = (int)p[ep++];
                        if (c == PLOTTER_PATH_MOVE || c == PLOTTER_PATH_LINE) { ep += 2; }
                        else if (c == PLOTTER_PATH_BEZIER) { ep += 6; }
                        else if (c == PLOTTER_PATH_CLOSE) { }
                        if (ep < grp_start + grp_len && (int)p[ep] == PLOTTER_PATH_MOVE) break;
                    }
                    unsigned int slen = ep - sp;
                    NSLOG(neosurf, INFO, "SVG chunk fallback split: subpath_len=%u", slen);
                    nserror rr2 = ctx->plot->path(ctx, style, p + sp, slen, transform);
                    if (rr2 != NSERROR_OK) { NSLOG(neosurf, ERROR, "SVG fallback subpath failed: len=%u err=%d", slen, rr2); r = rr2; }
                    pos2 = ep;
                }
            }
            grp_start = sp_start;
            grp_len = sp_len;
            grp_moves = 1;
            gb_minx = sb_minx; gb_miny = sb_miny; gb_maxx = sb_maxx; gb_maxy = sb_maxy; gb_init = sb_init;
        } else {
            grp_len += sp_len;
            grp_moves += 1;
            if (!gb_init) { gb_minx = sb_minx; gb_miny = sb_miny; gb_maxx = sb_maxx; gb_maxy = sb_maxy; gb_init = sb_init; }
            if (sb_minx < gb_minx) gb_minx = sb_minx; if (sb_maxx > gb_maxx) gb_maxx = sb_maxx;
            if (sb_miny < gb_miny) gb_miny = sb_miny; if (sb_maxy > gb_maxy) gb_maxy = sb_maxy;
        }
    }

    if (grp_len > 0) {
        NSLOG(neosurf, INFO, "SVG chunk final flush: len=%u moves=%u", grp_len, grp_moves);
        nserror rr = ctx->plot->path(ctx, style, p + grp_start, grp_len, transform);
        if (rr != NSERROR_OK) {
            NSLOG(neosurf, ERROR, "SVG chunk final flush failed: len=%u err=%d; splitting fallback", grp_len, rr);
            unsigned int pos2 = grp_start;
            while (pos2 < grp_start + grp_len) {
                while (pos2 < grp_start + grp_len && (int)p[pos2] != PLOTTER_PATH_MOVE) pos2++;
                if (pos2 >= grp_start + grp_len) break;
                unsigned int sp = pos2;
                unsigned int ep = sp + 1;
                while (ep < grp_start + grp_len) {
                    int c = (int)p[ep++];
                    if (c == PLOTTER_PATH_MOVE || c == PLOTTER_PATH_LINE) { ep += 2; }
                    else if (c == PLOTTER_PATH_BEZIER) { ep += 6; }
                    else if (c == PLOTTER_PATH_CLOSE) { }
                    if (ep < grp_start + grp_len && (int)p[ep] == PLOTTER_PATH_MOVE) break;
                }
                unsigned int slen = ep - sp;
                NSLOG(neosurf, INFO, "SVG chunk fallback split: subpath_len=%u", slen);
                nserror rr2 = ctx->plot->path(ctx, style, p + sp, slen, transform);
                if (rr2 != NSERROR_OK) { NSLOG(neosurf, ERROR, "SVG fallback subpath failed: len=%u err=%d", slen, rr2); r = rr2; }
                pos2 = ep;
            }
        }
    }
    return r;
}

typedef struct svg_content {
    struct content base;

    struct svgtiny_diagram *diagram;

    int current_width;
    int current_height;
} svg_content;



static nserror svg_create_svg_data(svg_content *c)
{
	c->diagram = svgtiny_create();
	if (c->diagram == NULL)
		goto no_memory;

	c->current_width = INT_MAX;
	c->current_height = INT_MAX;

	return NSERROR_OK;

no_memory:
	content_broadcast_error(&c->base, NSERROR_NOMEM, NULL);
	return NSERROR_NOMEM;
}


/**
 * Create a CONTENT_SVG.
 */

static nserror svg_create(const content_handler *handler,
		lwc_string *imime_type, const struct http_parameter *params,
		struct llcache_handle *llcache, const char *fallback_charset,
		bool quirks, struct content **c)
{
	svg_content *svg;
	nserror error;

	svg = calloc(1, sizeof(svg_content));
	if (svg == NULL)
		return NSERROR_NOMEM;

	error = content__init(&svg->base, handler, imime_type, params,
			llcache, fallback_charset, quirks);
	if (error != NSERROR_OK) {
		free(svg);
		return error;
	}

	error = svg_create_svg_data(svg);
	if (error != NSERROR_OK) {
		free(svg);
		return error;
	}

	*c = (struct content *) svg;

	return NSERROR_OK;
}



/**
 * Convert a CONTENT_SVG for display.
 */

static bool svg_convert(struct content *c)
{
	/*c->title = malloc(100);
	if (c->title)
		snprintf(c->title, 100, messages_get("svgTitle"),
				width, height, c->source_size);*/
	//c->size += ?;
	content_set_ready(c);
    content_set_done(c);
	/* Done: update status bar */
	content_set_status(c, "");

    return true;
}

/**
 * Reformat a CONTENT_SVG.
 */

static void svg_reformat(struct content *c, int width, int height)
{
    svg_content *svg = (svg_content *) c;
    const uint8_t *source_data;
    size_t source_size;

	assert(svg->diagram);

	/* Avoid reformats to same width/height as we already reformatted to */
	if (width != svg->current_width || height != svg->current_height) {
		NSLOG(neosurf, INFO, "PROFILER: START SVG parsing %p", c);
		source_data = content__get_source_data(c, &source_size);

    svgtiny_parse(svg->diagram,
                  (const char *)source_data,
                  source_size,
                  nsurl_access(content_get_url(c)),
                  width,
                  height);
    NSLOG(neosurf, INFO, "PROFILER: STOP SVG parsing %p", c);

    svg->current_width = width;
    svg->current_height = height;
    }

    c->width = svg->diagram->width;
    c->height = svg->diagram->height;
}


/**
 * Redraw a CONTENT_SVG.
 */

static bool
svg_redraw_internal(svg_content *svg,
		    int x,
		    int y,
		    int width,
		    int height,
		    const struct rect *clip,
		    const struct redraw_context *ctx,
		    float scale,
		    colour background_colour)
{
    float transform[6];
    struct svgtiny_diagram *diagram = svg->diagram;
    int px, py;
    unsigned int i;
    plot_font_style_t fstyle = *plot_style_font;
    plot_style_t pstyle;
    nserror res;
    bool ok = true;
    const char *url_str = nsurl_access(content_get_url(&svg->base));

	assert(diagram);

    float sx = (float) width / (float) svg->base.width;
    float sy = (float) height / (float) svg->base.height;
    transform[0] = 1.0f;
    transform[1] = 0.0f;
    transform[2] = 0.0f;
    transform[3] = 1.0f;
    transform[4] = x;
    transform[5] = y;

    NSLOG(neosurf, INFO, "PROFILER: START SVG rendering %p", svg);
    NSLOG(neosurf, INFO, "SVG redraw start: url=%s scale=%.3f clip=%d,%d..%d,%d limit=%u",
          url_str, scale, clip->x0, clip->y0, clip->x1, clip->y1, SVG_COMBO_FLUSH_LIMIT);

#define BGR(c) (((svgtiny_RED((c))) |					\
		 (svgtiny_GREEN((c)) << 8) |				\
		 (svgtiny_BLUE((c)) << 16)))

    unsigned int max_path_len = 0;
    for (i = 0; i != diagram->shape_count; i++) {
        if (diagram->shape[i].path && diagram->shape[i].path_length > max_path_len) {
            max_path_len = diagram->shape[i].path_length;
        }
    }
    float *scaled = NULL;
    if (max_path_len > 0) {
        scaled = malloc(sizeof(float) * max_path_len);
        if (scaled == NULL) {
            return false;
        }
    }
    float *combo = NULL;
    unsigned int combo_len = 0;
    unsigned int combo_cap = 0;
    plot_style_t combo_style;
    int combo_active = 0;

    for (i = 0; i != diagram->shape_count; i++) {
        if (diagram->shape[i].path) {
            /* stroke style */
            if (diagram->shape[i].stroke == svgtiny_TRANSPARENT) {
                pstyle.stroke_type = PLOT_OP_TYPE_NONE;
                pstyle.stroke_colour = NS_TRANSPARENT;
            } else {
                pstyle.stroke_type = PLOT_OP_TYPE_SOLID;
                pstyle.stroke_colour = BGR(diagram->shape[i].stroke);
            }
            pstyle.stroke_width = plot_style_int_to_fixed(
                    diagram->shape[i].stroke_width);

            /* fill style */
            if (diagram->shape[i].fill == svgtiny_TRANSPARENT) {
                pstyle.fill_type = PLOT_OP_TYPE_NONE;
                pstyle.fill_colour = NS_TRANSPARENT;
            } else {
                pstyle.fill_type = PLOT_OP_TYPE_SOLID;
                pstyle.fill_colour = BGR(diagram->shape[i].fill);
            }
            if (scaled != NULL) {
                unsigned int j = 0;
                unsigned int k = 0;
                float minx = 0.0f, miny = 0.0f, maxx = 0.0f, maxy = 0.0f;
                int initbb = 0;
                while (j < diagram->shape[i].path_length) {
                    int cmd = (int)diagram->shape[i].path[j++];
                    scaled[k++] = (float)cmd;
                    switch (cmd) {
                    case PLOTTER_PATH_MOVE:
                    case PLOTTER_PATH_LINE: {
                        float xx = diagram->shape[i].path[j++] * sx;
                        float yy = diagram->shape[i].path[j++] * sy;
                        scaled[k++] = xx;
                        scaled[k++] = yy;
                        if (!initbb) { minx = maxx = xx; miny = maxy = yy; initbb = 1; }
                        if (xx < minx) minx = xx; if (xx > maxx) maxx = xx;
                        if (yy < miny) miny = yy; if (yy > maxy) maxy = yy;
                        break;
                    }
                    case PLOTTER_PATH_BEZIER: {
                        float x1 = diagram->shape[i].path[j++] * sx;
                        float y1 = diagram->shape[i].path[j++] * sy;
                        float x2 = diagram->shape[i].path[j++] * sx;
                        float y2 = diagram->shape[i].path[j++] * sy;
                        float x3 = diagram->shape[i].path[j++] * sx;
                        float y3 = diagram->shape[i].path[j++] * sy;
                        scaled[k++] = x1; scaled[k++] = y1;
                        scaled[k++] = x2; scaled[k++] = y2;
                        scaled[k++] = x3; scaled[k++] = y3;
                        if (!initbb) { minx = maxx = x1; miny = maxy = y1; initbb = 1; }
                        if (x1 < minx) minx = x1; if (x1 > maxx) maxx = x1;
                        if (y1 < miny) miny = y1; if (y1 > maxy) maxy = y1;
                        if (x2 < minx) minx = x2; if (x2 > maxx) maxx = x2;
                        if (y2 < miny) miny = y2; if (y2 > maxy) maxy = y2;
                        if (x3 < minx) minx = x3; if (x3 > maxx) maxx = x3;
                        if (y3 < miny) miny = y3; if (y3 > maxy) maxy = y3;
                        break;
                    }
                    case PLOTTER_PATH_CLOSE:
                    default:
                        break;
                    }
                }
                int lx = (int)floorf(minx) + x;
                int rx = (int)ceilf(maxx) + x;
                int ty = (int)floorf(miny) + y;
                int by = (int)ceilf(maxy) + y;
                if (!(rx < clip->x0 || lx >= clip->x1 || by < clip->y0 || ty >= clip->y1)) {
                    NSLOG(neosurf, INFO,
                          "SVG path begin: url=%s index=%u orig_len=%u scaled_len=%u bbox=%d,%d..%d,%d",
                          url_str, i, diagram->shape[i].path_length, k, lx, ty, rx, by);
#ifdef NEOSURF_SVG_COMBO_DISABLE
                    res = ctx->plot->path(ctx, &pstyle, scaled, k, transform);
                    if (res != NSERROR_OK) {
                        ok = false;
                        int stroke_rgb = (svgtiny_RED(diagram->shape[i].stroke) << 16) |
                                         (svgtiny_GREEN(diagram->shape[i].stroke) << 8) |
                                         (svgtiny_BLUE(diagram->shape[i].stroke));
                        int fill_rgb = (svgtiny_RED(diagram->shape[i].fill) << 16) |
                                       (svgtiny_GREEN(diagram->shape[i].fill) << 8) |
                                       (svgtiny_BLUE(diagram->shape[i].fill));
                        NSLOG(neosurf, ERROR,
                              "SVG render failed: url=%s element=path index=%u path_len=%u err=%d stroke=#%06x fill=#%06x stroke_w=%d",
                              url_str,
                              i,
                              diagram->shape[i].path_length,
                              res,
                              stroke_rgb,
                              fill_rgb,
                              diagram->shape[i].stroke_width);
                    }
                    continue;
#endif
                    int same = combo_active &&
                        combo_style.stroke_type == pstyle.stroke_type &&
                        combo_style.fill_type == pstyle.fill_type &&
                        combo_style.stroke_colour == pstyle.stroke_colour &&
                        combo_style.fill_colour == pstyle.fill_colour &&
                        combo_style.stroke_width == pstyle.stroke_width;
                    if (!same) {
                        /* Flush previous combo group in chunks when style changes */
                        if (combo_active && combo_len > 0) {
                            NSLOG(neosurf, INFO, "SVG combo style change flush: len=%u", combo_len);
                            res = svg_plot_path_chunked(ctx, &combo_style, combo, combo_len, transform);
                            if (res != NSERROR_OK) { ok = false; NSLOG(neosurf, ERROR, "SVG render failed: url=%s element=path combo_flush len=%u", url_str, combo_len); }
                            combo_len = 0;
                        }
                        combo_style = pstyle;
                        combo_active = 1;
                    }
                    /* Flush combo if adding current path would exceed chunk limit */
                    if (combo_active && combo_len > 0 && combo_len + k > SVG_COMBO_FLUSH_LIMIT) {
                        NSLOG(neosurf, INFO, "SVG combo limit flush: combo_len=%u next_len=%u", combo_len, k);
                        res = svg_plot_path_chunked(ctx, &combo_style, combo, combo_len, transform);
                        if (res != NSERROR_OK) { ok = false; NSLOG(neosurf, ERROR, "SVG render failed: url=%s element=path combo_flush len=%u", url_str, combo_len); }
                        combo_len = 0;
                    }
                    if (k > SVG_COMBO_FLUSH_LIMIT) {
                        NSLOG(neosurf, INFO, "SVG direct chunk plot: scaled_len=%u limit=%u", k, SVG_COMBO_FLUSH_LIMIT);
                        res = svg_plot_path_chunked(ctx, &pstyle, scaled, k, transform);
                        if (res != NSERROR_OK) {
                            ok = false;
                            int stroke_rgb = (svgtiny_RED(diagram->shape[i].stroke) << 16) |
                                             (svgtiny_GREEN(diagram->shape[i].stroke) << 8) |
                                             (svgtiny_BLUE(diagram->shape[i].stroke));
                            int fill_rgb = (svgtiny_RED(diagram->shape[i].fill) << 16) |
                                           (svgtiny_GREEN(diagram->shape[i].fill) << 8) |
                                           (svgtiny_BLUE(diagram->shape[i].fill));
                            NSLOG(neosurf, ERROR,
                                  "SVG render failed: url=%s element=path index=%u path_len=%u scaled_len=%u err=%d stroke=#%06x fill=#%06x stroke_w=%d",
                                  url_str,
                                  i,
                                  diagram->shape[i].path_length,
                                  k,
                                  res,
                                  stroke_rgb,
                                  fill_rgb,
                                  diagram->shape[i].stroke_width);
                        }
                        continue;
                    }
                    if (combo_len + k > combo_cap) {
                        unsigned int ncap = combo_cap ? combo_cap * 2 : k;
                        while (ncap < combo_len + k) ncap *= 2;
                        float *nbuf = realloc(combo, sizeof(float) * ncap);
                    if (nbuf == NULL) { if (scaled) free(scaled); if (combo) free(combo); return false; }
                    combo = nbuf;
                    combo_cap = ncap;
                }
                memcpy(combo + combo_len, scaled, sizeof(float) * k);
                combo_len += k;
                /* Periodic chunked flush to keep combo buffer bounded */
                if (combo_len >= SVG_COMBO_FLUSH_LIMIT) {
                    NSLOG(neosurf, INFO, "SVG periodic combo flush: len=%u", combo_len);
                    res = svg_plot_path_chunked(ctx, &combo_style, combo, combo_len, transform);
                    if (res != NSERROR_OK) { ok = false; NSLOG(neosurf, ERROR, "SVG render failed: url=%s element=path combo_flush len=%u", url_str, combo_len); }
                    combo_len = 0;
                }
            }
        } else {
                res = ctx->plot->path(ctx,
                        &pstyle,
                        diagram->shape[i].path,
                        diagram->shape[i].path_length,
                        transform);
                if (res != NSERROR_OK) {
                    ok = false;
                    int stroke_rgb = (svgtiny_RED(diagram->shape[i].stroke) << 16) |
                                     (svgtiny_GREEN(diagram->shape[i].stroke) << 8) |
                                     (svgtiny_BLUE(diagram->shape[i].stroke));
                    int fill_rgb = (svgtiny_RED(diagram->shape[i].fill) << 16) |
                                   (svgtiny_GREEN(diagram->shape[i].fill) << 8) |
                                   (svgtiny_BLUE(diagram->shape[i].fill));
                    NSLOG(neosurf, ERROR,
                          "SVG render failed: url=%s element=path index=%u path_len=%u err=%d stroke=#%06x fill=#%06x stroke_w=%d",
                          url_str,
                          i,
                          diagram->shape[i].path_length,
                          res,
                          stroke_rgb,
                          fill_rgb,
                          diagram->shape[i].stroke_width);
                }
        }

    } else if (diagram->shape[i].text) {
        /* Ensure combo is flushed safely before plotting text */
        if (combo_active && combo_len > 0) {
                res = svg_plot_path_chunked(ctx, &combo_style, combo, combo_len, transform);
                if (res != NSERROR_OK) { ok = false; NSLOG(neosurf, ERROR, "SVG render failed: url=%s element=text combo_flush", url_str); }
                combo_len = 0;
                combo_active = 0;
            }
            px = (int)(diagram->shape[i].text_x * sx) + transform[4];
            py = (int)(diagram->shape[i].text_y * sy) + transform[5];

            fstyle.background = 0xffffff;
            fstyle.foreground = 0x000000;
            fstyle.size = (8 * PLOT_STYLE_SCALE) * scale;

            res = ctx->plot->text(ctx,
                      &fstyle,
                      px, py,
                      diagram->shape[i].text,
                      strlen(diagram->shape[i].text));
            if (res != NSERROR_OK) { ok = false; NSLOG(neosurf, ERROR, "SVG render failed: url=%s element=text index=%u pos=%d,%d text='%s'", url_str, i, px, py, diagram->shape[i].text); }
        }
    }

#undef BGR
    /* Final chunked flush of any remaining combined paths */
        if (combo_active && combo_len > 0) {
        NSLOG(neosurf, INFO, "SVG final combo flush: len=%u", combo_len);
            res = svg_plot_path_chunked(ctx, &combo_style, combo, combo_len, transform);
            if (res != NSERROR_OK) { ok = false; NSLOG(neosurf, ERROR, "SVG render failed: url=%s element=path final_flush len=%u", url_str, combo_len); }
        }
    if (scaled) free(scaled);
    if (combo) free(combo);
    NSLOG(neosurf, INFO, "PROFILER: STOP SVG rendering %p", svg);
    return ok;
}


bool svg_redraw_diagram(struct svgtiny_diagram *diagram,
                        int x,
                        int y,
                        int width,
                        int height,
                        const struct rect *clip,
                        const struct redraw_context *ctx,
                        float scale,
                        colour background_colour)
{
    svg_content tmp;
    memset(&tmp, 0, sizeof(tmp));
    tmp.diagram = diagram;
    tmp.base.width = width;
    tmp.base.height = height;
    return svg_redraw_internal(&tmp, x, y, width, height, clip, ctx, scale, background_colour);
}


static bool
svg_redraw_tiled_internal(svg_content *svg,
			  struct content_redraw_data *data,
			  const struct rect *clip,
			  const struct redraw_context *ctx)
{
	/* Tiled redraw required.  SVG repeats to extents of clip
	 * rectangle, in x, y or both directions */
	int x, y, x0, y0, x1, y1;

	x = x0 = data->x;
	y = y0 = data->y;

	/* Find the redraw boundaries to loop within */
	if (data->repeat_x) {
		for (; x0 > clip->x0; x0 -= data->width);
		x1 = clip->x1;
	} else {
		x1 = x + 1;
	}
	if (data->repeat_y) {
		for (; y0 > clip->y0; y0 -= data->height);
		y1 = clip->y1;
	} else {
		y1 = y + 1;
	}

	/* Repeatedly plot the SVG across the area */
	for (y = y0; y < y1; y += data->height) {
		for (x = x0; x < x1; x += data->width) {
			if (!svg_redraw_internal(svg, x, y,
						 data->width, data->height,
						 clip, ctx, data->scale,
						 data->background_colour)) {
				return false;
			}
		}
	}

	return true;
}


/**
 * Redraw a CONTENT_SVG.
 */
static bool
svg_redraw(struct content *c,
           struct content_redraw_data *data,
           const struct rect *clip,
           const struct redraw_context *ctx)
{
    svg_content *svg = (svg_content *)c;

	if ((data->width <= 0) && (data->height <= 0)) {
		/* No point trying to plot SVG if it does not occupy a valid
		 * area */
		return true;
	}

    if ((data->repeat_x == false) && (data->repeat_y == false)) {
        return svg_redraw_internal(svg, data->x, data->y,
                data->width, data->height,
                clip, ctx, data->scale,
                data->background_colour);
    }

	return svg_redraw_tiled_internal(svg, data, clip, ctx);
}


/**
 * Destroy a CONTENT_SVG and free all resources it owns.
 */

static void svg_destroy(struct content *c)
{
    svg_content *svg = (svg_content *) c;

    if (svg->diagram != NULL)
        svgtiny_free(svg->diagram);
}


static nserror svg_clone(const struct content *old, struct content **newc)
{
	svg_content *svg;
	nserror error;

	svg = calloc(1, sizeof(svg_content));
	if (svg == NULL)
		return NSERROR_NOMEM;

	error = content__clone(old, &svg->base);
	if (error != NSERROR_OK) {
		content_destroy(&svg->base);
		return error;
	}

	/* Simply replay create/convert */
	error = svg_create_svg_data(svg);
	if (error != NSERROR_OK) {
		content_destroy(&svg->base);
		return error;
	}

	if (old->status == CONTENT_STATUS_READY ||
			old->status == CONTENT_STATUS_DONE) {
		if (svg_convert(&svg->base) == false) {
			content_destroy(&svg->base);
			return NSERROR_CLONE_FAILED;
		}
	}

	*newc = (struct content *) svg;

	return NSERROR_OK;
}

static content_type svg_content_type(void)
{
	return CONTENT_IMAGE;
}

static const content_handler svg_content_handler = {
	.create = svg_create,
	.data_complete = svg_convert,
	.reformat = svg_reformat,
	.destroy = svg_destroy,
	.redraw = svg_redraw,
	.clone = svg_clone,
	.type = svg_content_type,
	.no_share = true
};

static const char *svg_types[] = {
	"image/svg",
	"image/svg+xml"
};


CONTENT_FACTORY_REGISTER_TYPES(svg, svg_types, svg_content_handler);

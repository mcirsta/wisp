/* SPDX-License-Identifier: GPL-2.0-or-later
 * Shared CSS transforms and CSSOM border-fragment geometry. */
#include "box_geometry.h"
#include <math.h>
#include <wisp/browser_window.h>
#include <wisp/content/handlers/html/private.h>
#include <wisp/content/handlers/html/box_inspect.h>

bool box_has_transform(const struct box *box)
{
    uint32_t count;
    const css_transform_function *functions;
    /* Non-replaced inline boxes are not transformable (CSS Transforms 1). */
    return box->node && box->style && (box->type != BOX_INLINE || (box->flags & IS_REPLACED)) &&
        css_computed_transform(box->style, &count, &functions) == CSS_TRANSFORM_FUNCTIONS && count > 0;
}

static double translation(
    const struct box *box, const css_unit_ctx *units, css_fixed value, css_unit unit, double reference, double scale)
{
    if (unit == CSS_UNIT_PCT)
        return FIXTOFLT(value) * reference / 100;
    return FIXTOFLT(css_unit_len2device_px(box->style, units, value, unit)) * scale;
}

bool box_transform_matrix(
    const struct box *box, const css_unit_ctx *units, double x, double y, double scale, double m[6])
{
    m[0] = m[3] = 1;
    m[1] = m[2] = m[4] = m[5] = 0;
    if (!box_has_transform(box))
        return false;
    uint32_t count;
    const css_transform_function *functions;
    css_computed_transform(box->style, &count, &functions);
    double width = ((double)box->width + box->padding[LEFT] + box->padding[RIGHT] + box->border[LEFT].width +
                       box->border[RIGHT].width) *
        scale;
    double height = ((double)box->height + box->padding[TOP] + box->padding[BOTTOM] + box->border[TOP].width +
                        box->border[BOTTOM].width) *
        scale;
    double cx = x - box->border[LEFT].width * scale + width / 2;
    double cy = y - box->border[TOP].width * scale + height / 2;
    for (uint32_t i = 0; i < count; i++) {
        const css_transform_function *f = &functions[i];
        double a = m[0], b = m[1], c = m[2], d = m[3];
        switch (f->type) {
        case CSS_TRANSFORM_FUNC_TRANSLATE:
        case CSS_TRANSFORM_FUNC_TRANSLATEX:
        case CSS_TRANSFORM_FUNC_TRANSLATEY: {
            double tx = f->type == CSS_TRANSFORM_FUNC_TRANSLATEY
                ? 0
                : translation(box, units, f->value1, f->unit1, width, scale);
            double ty = f->type == CSS_TRANSFORM_FUNC_TRANSLATEX
                ? 0
                : translation(box, units, f->type == CSS_TRANSFORM_FUNC_TRANSLATEY ? f->value1 : f->value2,
                      f->type == CSS_TRANSFORM_FUNC_TRANSLATEY ? f->unit1 : f->unit2, height, scale);
            m[4] += a * tx + c * ty;
            m[5] += b * tx + d * ty;
            break;
        }
        case CSS_TRANSFORM_FUNC_SCALE:
        case CSS_TRANSFORM_FUNC_SCALEX:
        case CSS_TRANSFORM_FUNC_SCALEY: {
            double sx = f->type == CSS_TRANSFORM_FUNC_SCALEY ? 1 : FIXTOFLT(f->value1);
            double sy = f->type == CSS_TRANSFORM_FUNC_SCALEX
                ? 1
                : FIXTOFLT(f->type == CSS_TRANSFORM_FUNC_SCALEY ? f->value1 : f->value2);
            m[0] *= sx;
            m[1] *= sx;
            m[2] *= sy;
            m[3] *= sy;
            break;
        }
        case CSS_TRANSFORM_FUNC_ROTATE: {
            double angle = FIXTOFLT(f->value1) * M_PI / 180;
            double sine = sin(angle), cosine = cos(angle);
            m[0] = a * cosine + c * sine;
            m[1] = b * cosine + d * sine;
            m[2] = c * cosine - a * sine;
            m[3] = d * cosine - b * sine;
            break;
        }
        }
    }
    m[4] += cx * (1 - m[0]) - cy * m[2];
    m[5] += cy * (1 - m[3]) - cx * m[1];
    return m[0] != 1 || m[1] != 0 || m[2] != 0 || m[3] != 1 || m[4] != 0 || m[5] != 0;
}

struct bounds_accumulator {
    struct html_content *html;
    struct box_rect first, bounds;
    bool visual, any, nonempty, area;
    int scroll_x, scroll_y;
};

static void add_fragment(struct bounds_accumulator *acc, struct box *owner, struct box_rect r)
{
    if (acc->visual) {
        double x[] = {r.x0, r.x1, r.x1, r.x0}, y[] = {r.y0, r.y0, r.y1, r.y1};
        for (struct box *b = owner; b != NULL; b = b->parent) {
            if (!box_has_transform(b))
                continue;
            int bx, by;
            box_coords_at_scroll(b, &bx, &by, acc->scroll_x, acc->scroll_y);
            double m[6];
            box_transform_matrix(b, &acc->html->unit_len_ctx, bx, by, 1, m);
            for (int i = 0; i < 4; i++) {
                double ox = x[i], oy = y[i];
                x[i] = m[0] * ox + m[2] * oy + m[4];
                y[i] = m[1] * ox + m[3] * oy + m[5];
            }
        }
        r = (struct box_rect){x[0], y[0], x[0], y[0]};
        for (int i = 1; i < 4; i++) {
            r.x0 = fmin(r.x0, x[i]);
            r.x1 = fmax(r.x1, x[i]);
            r.y0 = fmin(r.y0, y[i]);
            r.y1 = fmax(r.y1, y[i]);
        }
        r.x0 -= acc->scroll_x;
        r.x1 -= acc->scroll_x;
        r.y0 -= acc->scroll_y;
        r.y1 -= acc->scroll_y;
    }
    if (!acc->any)
        acc->first = r;
    acc->any = true;
    acc->area |= r.x0 != r.x1 && r.y0 != r.y1;
    /* CSSOM View: when an area-bearing fragment exists, include fragments
     * having a nonzero width OR height. If none has area, return the first. */
    if (acc->visual && r.x0 == r.x1 && r.y0 == r.y1)
        return;
    if (!acc->nonempty)
        acc->bounds = r;
    else {
        acc->bounds.x0 = fmin(acc->bounds.x0, r.x0);
        acc->bounds.x1 = fmax(acc->bounds.x1, r.x1);
        acc->bounds.y0 = fmin(acc->bounds.y0, r.y0);
        acc->bounds.y1 = fmax(acc->bounds.y1, r.y1);
    }
    acc->nonempty = true;
}

static struct box_rect border_rect(struct bounds_accumulator *acc, struct box *box)
{
    int x, y;
    if (acc->visual)
        box_coords_at_scroll(box, &x, &y, acc->scroll_x, acc->scroll_y);
    else
        box_coords_unscrolled(box, &x, &y);
    return (struct box_rect){(double)x - box->border[LEFT].width, (double)y - box->border[TOP].width,
        (double)x + box->padding[LEFT] + box->width + box->padding[RIGHT] + box->border[RIGHT].width,
        (double)y + box->padding[TOP] + box->height + box->padding[BOTTOM] + box->border[BOTTOM].width};
}

/* Inlines are flattened into sibling runs. Their matching end may be in a
 * later inline container when a block descendant splits the principal box. */
static void inline_bounds(struct bounds_accumulator *acc, struct box *box)
{
    struct box *container = box->parent, *item = box;
    struct box_rect line = border_rect(acc, box);
    bool have_line = true;
    while (item != NULL) {
        bool out_of_flow = item->type == BOX_FLOAT_LEFT || item->type == BOX_FLOAT_RIGHT ||
            (item->style &&
                (css_computed_position(item->style) == CSS_POSITION_ABSOLUTE ||
                    css_computed_position(item->style) == CSS_POSITION_FIXED));
        if (!out_of_flow) {
            struct box_rect r = border_rect(acc, item);
            if (!have_line || ((item->flags & NEW_LINE) && item != box)) {
                if (have_line)
                    add_fragment(acc, box, line);
                line.x0 = r.x0;
                line.y0 = r.y0 - box->border[TOP].width - box->padding[TOP];
                line.y1 = line.y0 + box->border[TOP].width + box->padding[TOP] + box->height + box->padding[BOTTOM] +
                    box->border[BOTTOM].width;
                have_line = true;
            }
            line.x1 = r.x1;
        }
        if (item == box->inline_end)
            break;
        item = item->next;
        if (item == NULL && container != box->inline_end->parent) {
            if (have_line)
                add_fragment(acc, box, line);
            have_line = false;
            while ((container = container->next) != NULL && container->type != BOX_INLINE_CONTAINER) {
                if (!acc->visual) {
                    struct box_rect r = border_rect(acc, container);
                    if (r.x1 > r.x0 && r.y1 > r.y0)
                        add_fragment(acc, container, r);
                }
            }
            item = container ? container->children : NULL;
        }
    }
    if (have_line)
        add_fragment(acc, box, line);
}

void box_geometry_bounds(struct html_content *html, struct box *box, bool visual, struct box_rect *bounds)
{
    struct bounds_accumulator acc = {.html = html, .visual = visual};
    browser_window_get_scroll(html->bw, &acc.scroll_x, &acc.scroll_y);
    if (box != NULL) {
        if (box->type == BOX_INLINE && !(box->flags & IS_REPLACED) && box->inline_end)
            inline_bounds(&acc, box);
        else
            add_fragment(&acc, box, border_rect(&acc, box));
    }
    *bounds = acc.nonempty && (!visual || acc.area) ? acc.bounds : acc.first;
}

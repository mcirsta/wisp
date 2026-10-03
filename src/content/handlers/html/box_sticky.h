/* CSS sticky-position constraints, independent of painting and scroll events. */
#ifndef WISP_HTML_BOX_STICKY_H
#define WISP_HTML_BOX_STICKY_H

#include <stdbool.h>
#include <stdint.h>
#include <limits.h>

struct box;
struct html_content;
struct rect;

/**
 * One physical axis in a common, unscaled coordinate space. Start is the low
 * coordinate (left/top), even in RTL. All lengths are resolved layout pixels;
 * auto insets have separate flags and their numeric values are ignored.
 *
 * The containing interval is the formatting-context containing block, not
 * necessarily the scrollport. reverse means logical end is the low edge (RTL
 * horizontal axis); it determines which inset yields for an oversized box.
 */
struct box_sticky_axis {
    int border_start, border_size;
    int containing_start, containing_size;
    int margin_start, margin_end;
    int scrollport_start, scrollport_size;
    int inset_start, inset_end;
    bool start_auto, end_auto;
    bool reverse;
};

/**
 * Return the visual displacement, without changing layout geometry. Wide
 * intermediates/result avoid overflow when subtracting far-apart coordinates.
 * This does not resolve styles, identify ancestors, or apply ancestor offsets.
 */
int64_t box_sticky_axis_offset(const struct box_sticky_axis *axis);

/** Sparse box-owned record. Borrowed boxes are ancestors (or the viewport). */
struct box_sticky {
    struct html_content *owner;
    struct box *containing_block;
    struct box *scrolling_ancestor;
    struct box_sticky_axis x, y;
    bool valid;
};

/** NULL for supported geometry; otherwise a stable developer-facing reason. */
const char *box_sticky_unsupported(const struct box *box);

/** Prepare one box after all descendant bounds have settled. Returns false on
 * allocation failure. Does not change layout coordinates or create scrollbars. */
bool box_sticky_prepare(struct html_content *html, struct box *box);

/** Never return partially prepared or invalidated constraints. */
const struct box_sticky *box_sticky_get(const struct box *box);

/** Cumulative visual displacement. Explicit document scroll, or NULL for the
 * live window. No allocation or mutation of layout/constraints. */
void box_sticky_total_offset(const struct box *box, const int scroll[2], int64_t *x, int64_t *y);

/** Correction to inherited coordinate-parent painting/walk coordinates. Ordinary
 * children inherit their parent's shift; floats may skip structural ancestors. */
void box_sticky_local_offset(const struct box *box, const int scroll[2], int64_t *x, int64_t *y);

/** Intersect a document-space clip with real ancestor overflow/CSS clips. Used
 * when stacking collection or hit traversal bypasses ordinary ancestry. */
bool box_sticky_clip(const struct box *box, const int scroll[2], struct rect *clip);

/** Intersect document-space bounds with ancestor overflow/CSS clips. Used by
 * deferred painting, which can bypass those ancestors' recursive redraw. */
bool box_clip_to_ancestors(const struct html_content *html, const struct box *box,
    const int scroll[2], struct rect *clip);

/** Narrow only at the existing integer coordinate boundary. */
static inline int box_sticky_coordinate(int64_t value)
{
    return value < INT_MIN + 1 ? INT_MIN + 1 : value > INT_MAX - 1 ? INT_MAX - 1 : (int)value;
}

#endif

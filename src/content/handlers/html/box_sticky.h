/* CSS sticky-position constraints, independent of painting and scroll events. */
#ifndef WISP_HTML_BOX_STICKY_H
#define WISP_HTML_BOX_STICKY_H

#include <stdbool.h>
#include <stdint.h>

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

#endif

/* CSS Positioned Layout, section 3.4: sticky positioning. */
#include <assert.h>
#include <stddef.h>
#include <stdint.h>

#include "content/handlers/html/box_sticky.h"

static int64_t lesser(int64_t a, int64_t b)
{
    return a < b ? a : b;
}

static int64_t greater(int64_t a, int64_t b)
{
    return a > b ? a : b;
}

int64_t box_sticky_axis_offset(const struct box_sticky_axis *axis)
{
    assert(axis != NULL);
    assert(axis->border_size >= 0 && axis->containing_size >= 0 && axis->scrollport_size >= 0);
    if (axis->start_auto && axis->end_auto)
        return 0;

    int64_t border_start = axis->border_start;
    int64_t border_end = border_start + axis->border_size;
    int64_t view_start = (int64_t)axis->scrollport_start + (axis->start_auto ? 0 : axis->inset_start);
    int64_t view_end = (int64_t)axis->scrollport_start + axis->scrollport_size - (axis->end_auto ? 0 : axis->inset_end);

    /* Only competing specified insets need logical-end reduction. An auto
     * opposite edge is unconstrained, so preserve a sole specified inset even
     * for an oversized box. This follows Chromium/Firefox; see the compatibility
     * decision in docs/sticky-positioning.md. */
    if (!axis->start_auto && !axis->end_auto) {
        int64_t deficit = axis->border_size - (view_end - view_start);
        if (deficit > 0) {
            if (axis->reverse)
                view_start -= deficit;
            else
                view_end += deficit;
        }
    }

    int64_t offset = 0;
    if (!axis->start_auto)
        offset = greater(offset, view_start - border_start);
    if (!axis->end_auto)
        offset = lesser(offset, view_end - border_end);

    /* Margins constrain the available travel. A box already against/outside
     * its containing edge has no travel in that direction: sticky must not
     * pull it back just to repair its normal-flow overflow. Negative margins
     * extend the travel; do not discard their sign. */
    int64_t minimum = lesser(0, (int64_t)axis->containing_start + axis->margin_start - border_start);
    int64_t maximum = greater(
        0, (int64_t)axis->containing_start + axis->containing_size - axis->margin_end - border_end);
    return lesser(greater(offset, minimum), maximum);
}

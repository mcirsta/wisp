/* Shared numeric/keyword <position> grammar. Licensed under the MIT License. */
#ifndef css_parse_position_h_
#define css_parse_position_h_

#include "utils/css_utils.h"

enum css_position_part {
    CSS_POSITION_INVALID, CSS_POSITION_CENTER, CSS_POSITION_LEFT, CSS_POSITION_RIGHT,
    CSS_POSITION_TOP, CSS_POSITION_BOTTOM, CSS_POSITION_OFFSET
};

static inline enum css_position_part css__position_keyword(lwc_string *text)
{
    static const char *const names[] = {"center", "left", "right", "top", "bottom"};
    for (size_t i = 0; i < N_ELEMENTS(names); i++) {
        if (css__keyword_equal(text, names[i]))
            return CSS_POSITION_CENTER + i;
    }
    return CSS_POSITION_INVALID;
}

/* CSS Values 4 section 8.3: two keywords may exchange axes; once an offset
 * occurs, the order must be horizontal then vertical. Index 2 denotes the
 * implicit center for a one-component value. Three/four-value edge offsets
 * and math remain outside the native parser's supported grammar. */
static inline bool css__position_axes(const enum css_position_part parts[2], size_t count, size_t axes[2])
{
    if (count == 0 || count > 2 || parts[0] == CSS_POSITION_INVALID ||
        (count == 2 && parts[1] == CSS_POSITION_INVALID))
        return false;
    bool first_vertical = parts[0] == CSS_POSITION_TOP || parts[0] == CSS_POSITION_BOTTOM;
    axes[0] = first_vertical ? 2 : 0;
    axes[1] = first_vertical ? 0 : 2;
    if (count == 1)
        return true;
    bool second_horizontal = parts[1] == CSS_POSITION_LEFT || parts[1] == CSS_POSITION_RIGHT;
    bool offset = parts[0] == CSS_POSITION_OFFSET || parts[1] == CSS_POSITION_OFFSET;
    bool swap = !offset && (first_vertical || second_horizontal);
    axes[0] = swap ? 1 : 0;
    axes[1] = swap ? 0 : 1;
    return parts[axes[0]] != CSS_POSITION_TOP && parts[axes[0]] != CSS_POSITION_BOTTOM &&
        parts[axes[1]] != CSS_POSITION_LEFT && parts[axes[1]] != CSS_POSITION_RIGHT;
}

#endif

/* Supported <repeat-style> values. Licensed under the MIT License. */
#ifndef css_parse_repeat_h_
#define css_parse_repeat_h_

#include "bytecode/opcodes.h"
#include "utils/css_utils.h"

static inline int css__repeat_keyword(lwc_string *text)
{
    if (css__keyword_equal(text, "repeat"))
        return BACKGROUND_REPEAT_REPEAT;
    if (css__keyword_equal(text, "no-repeat"))
        return BACKGROUND_REPEAT_NO_REPEAT;
    if (css__keyword_equal(text, "repeat-x"))
        return BACKGROUND_REPEAT_REPEAT_X;
    if (css__keyword_equal(text, "repeat-y"))
        return BACKGROUND_REPEAT_REPEAT_Y;
    return -1; /* space/round need different painting and remain unsupported. */
}

static inline bool css__repeat_axis(int value)
{
    return value == BACKGROUND_REPEAT_REPEAT || value == BACKGROUND_REPEAT_NO_REPEAT;
}

static inline int css__repeat_pair(int horizontal, int vertical)
{
    /* The supported repeat-style keywords encode independent repeat flags. */
    return (horizontal == BACKGROUND_REPEAT_REPEAT ? BACKGROUND_REPEAT_REPEAT_X : 0) |
        (vertical == BACKGROUND_REPEAT_REPEAT ? BACKGROUND_REPEAT_REPEAT_Y : 0);
}

#endif

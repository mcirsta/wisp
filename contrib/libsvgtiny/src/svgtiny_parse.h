/*
 * This file is part of Libsvgtiny
 * Licensed under the MIT License,
 *                http://opensource.org/licenses/mit-license.php
 * Copyright 2024 Vincent Sanders <vince@netsurf-browser.org>
 */

#ifndef SVGTINY_PARSE_H
#define SVGTINY_PARSE_H

static inline bool svg_is_whitespace(char c)
{
    return (c == 0x20 || c == 0x09 || c == 0x0A || c == 0x0D);
}

static inline void advance_whitespace(const char **cursor, const char *textend)
{
    while((*cursor) < textend) {
        if (!svg_is_whitespace(**cursor)) {
            break;
        }
        (*cursor)++;
    }
}


/**
 * advance cursor across SVG spec defined comma and whitespace
 *
 * \param cursor current cursor
 * \param textend end of buffer
 */
static inline void advance_comma_whitespace(const char **cursor, const char *textend)
{
	advance_whitespace(cursor, textend);
	if (((*cursor) < textend) && (**cursor == 0x2C /* , */)) {
		(*cursor)++;
		advance_whitespace(cursor, textend);
	}
}

#endif

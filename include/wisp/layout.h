/*
 * Copyright 2016 Vincent Sanders <vince@netsurf-browser.org>
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
 *
 * Interface to platform-specific layout operation table.
 *
 * This table is part of the layout used to measure glyphs before
 * rendering, previously referred to as font functions.
 *
 * \note This is an old interface within the browser, it has been
 * broken out purely to make the API obvious not as an indication this
 * is the correct approach.
 */

#ifndef _WISP_LAYOUT_H_
#define _WISP_LAYOUT_H_

#include <stddef.h>
#include <stdint.h>

#include <wisp/utils/errors.h>

struct plot_font_style;

/** A document-owned CSS face, with descriptors from @font-face */
struct font_variant_id {
    uint64_t font_set; /**< Owning document identity; zero is reserved for system fonts. */
    uint32_t order; /**< Face order within that document, independent of download completion. */
    const char *family_name; /**< CSS font-family name */
    uint8_t weight; /**< css_font_weight_e */
    uint8_t style; /**< css_font_style_e */
};

struct gui_layout_table {
    /**
     * Measure the width of a string.
     *
     * \param[in] fstyle plot style for this text
     * \param[in] string UTF-8 string to measure
     * \param[in] length length of string, in bytes
     * \param[out] width updated to width of string[0..length)
     * \return NSERROR_OK and width updated or appropriate error
     *          code on faliure
     */
    nserror (*width)(const struct plot_font_style *fstyle, const char *string, size_t length, int *width);


    /**
     * Find the position in a string where an x coordinate falls.
     *
     * \param[in] fstyle style for this text
     * \param[in] string UTF-8 string to measure
     * \param[in] length length of string, in bytes
     * \param[in] x coordinate to search for
     * \param[out] char_offset updated to offset in string of actual_x,
     * [0..length]
     * \param[out] actual_x updated to x coordinate of character closest to
     * x
     * \return NSERROR_OK and char_offset and actual_x updated or
     * appropriate error code on faliure
     */
    nserror (*position)(const struct plot_font_style *fstyle, const char *string, size_t length, int x,
        size_t *char_offset, int *actual_x);


    /**
     * Find where to split a string to make it fit a width.
     *
     * \param[in] fstyle       style for this text
     * \param[in] string       UTF-8 string to measure
     * \param[in] length       length of string, in bytes
     * \param[in] x            width available
     * \param[out] char_offset updated to offset in string of actual_x,
     * [1..length]
     * \param[out] actual_x updated to x coordinate of character closest to
     * x
     * \return NSERROR_OK or appropriate error code on faliure
     *
     * On exit, char_offset indicates first character after split point.
     * For a break at a space, leave char_offset on the space and exclude it
     * from actual_x; the caller consumes and accounts for that space separately.
     *
     * \note char_offset of 0 must never be returned.
     *
     *   Returns:
     *     char_offset giving split point closest to x, where actual_x <= x
     *   else
     *     char_offset giving split point closest to x, where actual_x > x
     *
     * Returning char_offset == length means no split possible
     */
    nserror (*split)(const struct plot_font_style *fstyle, const char *string, size_t length, int x,
        size_t *char_offset, int *actual_x);

    /**
     * Load font data into the system font database.
     *
     * This is called when a web font (@font-face) has been downloaded.
     * The frontend binds it only inside id->font_set; font selection must use
     * plot_font_style.font_set, and release_font_set releases that ownership.
     *
     * \param[in] id   Document, rule order and CSS family/weight/style descriptors
     * \param[in] data Raw font file data (TrueType/OpenType/WOFF)
     * \param[in] size Size of font data in bytes
     * \return NSERROR_OK on success, or appropriate error code
     */
    nserror (*load_font_data)(const struct font_variant_id *id, const uint8_t *data, size_t size);

    /** Release native faces and cached selections belonging to a destroyed document. */
    void (*release_font_set)(uint64_t font_set);

    /**
     * Optional font ascent and descent, in unscaled layout pixels.
     * Both values are nonnegative distances from the baseline. The core
     * supplies a fallback when this callback is absent or fails.
     */
    nserror (*font_metrics)(const struct plot_font_style *fstyle, int *ascent, int *descent);
};

#endif

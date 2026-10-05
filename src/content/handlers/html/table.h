/*
 * Copyright 2005 James Bursa <bursa@users.sourceforge.net>
 * Copyright 2005 Richard Wilson <info@tinct.net>
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
 * Interface to HTML table processing and layout.
 */

#ifndef WISP_HTML_TABLE_H
#define WISP_HTML_TABLE_H

#include <stdbool.h>
#include <wisp/content/handlers/html/box.h>

struct box;

/* One resolved border per grid segment, shared by layout and painting. Source
 * information implements CSS 2.2 17.6.2.1 ties and visibility inheritance. */
struct table_border_edge {
    struct box_border border;
    css_fixed priority_width; /* Preserve width ordering before device-pixel rounding. */
    const struct box *owner;
    unsigned int row, column;
};

struct table_border_grid {
    unsigned int rows, columns;
    struct table_border_edge *horizontal; /* (rows + 1) * columns */
    struct table_border_edge *vertical; /* rows * (columns + 1) */
    int *x, *y; /* Grid line coordinates relative to the table padding origin. */
};

struct table_data {
    struct column *columns;
    struct table_border_grid *borders;
};

/* True only for a table or one of its cells using a prepared collapsed grid. */
static inline bool table_has_collapsed_borders(const struct box *box)
{
    if (box->type == BOX_TABLE_CELL && box->parent && box->parent->parent)
        box = box->parent->parent->parent;
    return box && box->type == BOX_TABLE && box->table && box->table->borders;
}


/**
 * Determine the column width types for a table.
 *
 * \param unit_len_ctx Length conversion context
 * \param table box of type BOX_TABLE
 * \return true on success, false on memory exhaustion
 *
 * The table->table->columns array is allocated and filled in for each
 * column.
 */
bool table_calculate_column_types(const css_unit_ctx *unit_len_ctx, struct box *table);


/**
 * Resolve shared edges before intrinsic sizing, and populate cell used borders.
 *
 * \param unit_len_ctx Length conversion context
 * \param table Normalized table with column data
 *
 * \return false on allocation failure
 */
bool table_prepare_borders(const css_unit_ctx *unit_len_ctx, struct box *table);

/* Save grid positions after row layout. No allocations or style selection. */
void table_position_borders(struct box *table, const int *columns);

#endif

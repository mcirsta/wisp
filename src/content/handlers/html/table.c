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
 * implementation of HTML table processing and layout.
 */

#include <dom/dom.h>
#include <assert.h>

#include <wisp/content/handlers/css/utils.h>
#include <wisp/utils/log.h>
#include "utils/talloc.h"

#include <wisp/content/handlers/html/box.h>
#include "content/handlers/html/table.h"

#include <stdint.h>
#include <limits.h>
#include <string.h>

static inline bool table__length_to_px_int(const css_computed_style *style, const css_unit_ctx *unit_len_ctx,
    css_fixed_or_calc length, css_unit unit, int *px_out)
{
    if (unit == CSS_UNIT_CALC) {
        return css_computed_length_to_px(style, unit_len_ctx, -1, length, unit, px_out) == CSS_OK;
    }
    *px_out = FIXTOINT(css_unit_len2device_px(style, unit_len_ctx, length.value, unit));
    return true;
}

/* exported interface documented in html/table.h */
bool table_calculate_column_types(const css_unit_ctx *unit_len_ctx, struct box *table)
{
    unsigned int i, j;
    struct column *col;
    struct box *row_group, *row, *cell;

    if (table->table)
        /* Column sizing is cached until layout_reset_box(). */
        return true;

    table->table = talloc_zero(table, struct table_data);
    if (!table->table)
        return false;
    table->table->columns = col = talloc_array(table->table, struct column, table->columns);
    if (!col) {
        talloc_free(table->table);
        table->table = NULL;
        return false;
    }

    for (i = 0; i != table->columns; i++) {
        col[i].type = COLUMN_WIDTH_UNKNOWN;
        col[i].width = 0;
        col[i].positioned = true;
    }

    /* 1st pass: cells with colspan 1 only */
    for (row_group = table->children; row_group; row_group = row_group->next)
        for (row = row_group->children; row; row = row->next)
            for (cell = row->children; cell; cell = cell->next) {
                enum css_width_e type;
                css_fixed_or_calc value = (css_fixed_or_calc)0;
                css_unit unit = CSS_UNIT_PX;

                assert(cell->type == BOX_TABLE_CELL);
                assert(cell->style);

                if (cell->columns != 1)
                    continue;
                i = cell->start_column;

                if (css_computed_position(cell->style) != CSS_POSITION_ABSOLUTE &&
                    css_computed_position(cell->style) != CSS_POSITION_FIXED) {
                    col[i].positioned = false;
                }

                type = css_computed_width(cell->style, &value, &unit);

                /* fixed width takes priority over any other
                 * width type */
                if (col[i].type != COLUMN_WIDTH_FIXED && type == CSS_WIDTH_SET && unit != CSS_UNIT_PCT) {
                    int px = 0;
                    bool ok = true;
                    if (unit == CSS_UNIT_CALC) {
                        ok = table__length_to_px_int(cell->style, unit_len_ctx, value, unit, &px);
                    } else {
                        px = FIXTOINT(css_unit_len2device_px(cell->style, unit_len_ctx, value.value, unit));
                    }
                    if (ok) {
                        col[i].type = COLUMN_WIDTH_FIXED;
                        col[i].width = px;
                        if (col[i].width < 0)
                            col[i].width = 0;
                        continue;
                    }
                }

                if (col[i].type != COLUMN_WIDTH_UNKNOWN)
                    continue;

                if (type == CSS_WIDTH_SET && unit == CSS_UNIT_PCT) {
                    col[i].type = COLUMN_WIDTH_PERCENT;
                    col[i].width = FIXTOINT(value.value);
                    if (col[i].width < 0)
                        col[i].width = 0;
                } else if (type == CSS_WIDTH_AUTO) {
                    col[i].type = COLUMN_WIDTH_AUTO;
                }
            }

    /* 2nd pass: cells which span multiple columns */
    for (row_group = table->children; row_group; row_group = row_group->next)
        for (row = row_group->children; row; row = row->next)
            for (cell = row->children; cell; cell = cell->next) {
                unsigned int fixed_columns = 0, percent_columns = 0, auto_columns = 0, unknown_columns = 0;
                int fixed_width = 0, percent_width = 0;
                enum css_width_e type;
                css_fixed_or_calc value = (css_fixed_or_calc)0;
                css_unit unit = CSS_UNIT_PX;

                if (cell->columns == 1)
                    continue;
                i = cell->start_column;

                for (j = i; j < i + cell->columns; j++) {
                    col[j].positioned = false;
                }

                /* count column types in spanned cells */
                for (j = 0; j != cell->columns; j++) {
                    if (col[i + j].type == COLUMN_WIDTH_FIXED) {
                        fixed_width += col[i + j].width;
                        fixed_columns++;
                    } else if (col[i + j].type == COLUMN_WIDTH_PERCENT) {
                        percent_width += col[i + j].width;
                        percent_columns++;
                    } else if (col[i + j].type == COLUMN_WIDTH_AUTO) {
                        auto_columns++;
                    } else {
                        unknown_columns++;
                    }
                }

                if (!unknown_columns)
                    continue;

                type = css_computed_width(cell->style, &value, &unit);

                /* if cell is fixed width, and all spanned
                 * columns are fixed or unknown width, split
                 * extra width among unknown columns */
                if (type == CSS_WIDTH_SET && unit != CSS_UNIT_PCT && fixed_columns + unknown_columns == cell->columns) {
                    int total_width = 0;
                    bool ok = true;
                    if (unit == CSS_UNIT_CALC) {
                        ok = table__length_to_px_int(cell->style, unit_len_ctx, value, unit, &total_width);
                    } else {
                        total_width = FIXTOINT(css_unit_len2device_px(cell->style, unit_len_ctx, value.value, unit));
                    }
                    if (ok) {
                        int width = (total_width - fixed_width) / unknown_columns;
                        if (width < 0)
                            width = 0;
                        for (j = 0; j != cell->columns; j++) {
                            if (col[i + j].type == COLUMN_WIDTH_UNKNOWN) {
                                col[i + j].type = COLUMN_WIDTH_FIXED;
                                col[i + j].width = width;
                            }
                        }
                    }
                }

                /* as above for percentage width */
                if (type == CSS_WIDTH_SET && unit == CSS_UNIT_PCT &&
                    percent_columns + unknown_columns == cell->columns) {
                    int width = (FIXTOFLT(value.value) - percent_width) / unknown_columns;
                    if (width < 0)
                        width = 0;
                    for (j = 0; j != cell->columns; j++) {
                        if (col[i + j].type == COLUMN_WIDTH_UNKNOWN) {
                            col[i + j].type = COLUMN_WIDTH_PERCENT;
                            col[i + j].width = width;
                        }
                    }
                }
            }

    /* use AUTO if no width type was specified */
    for (i = 0; i != table->columns; i++) {
        if (col[i].type == COLUMN_WIDTH_UNKNOWN)
            col[i].type = COLUMN_WIDTH_AUTO;
    }


    return true;
}


/* Read a border once per originating box, not once per adjacent cell. */
static void table_read_borders(
    const css_unit_ctx *ctx, const struct box *box, unsigned int row, struct table_border_edge borders[4])
{
    static uint8_t (*const styles[])(const css_computed_style *) = {css_computed_border_top_style,
        css_computed_border_right_style, css_computed_border_bottom_style, css_computed_border_left_style};
    static uint8_t (*const colors[])(const css_computed_style *, css_color *) = {css_computed_border_top_color,
        css_computed_border_right_color, css_computed_border_bottom_color, css_computed_border_left_color};
    static uint8_t (*const widths[])(const css_computed_style *, css_fixed_or_calc *, css_unit *) = {
        css_computed_border_top_width, css_computed_border_right_width, css_computed_border_bottom_width,
        css_computed_border_left_width};
    for (unsigned int side = 0; side < 4; side++) {
        borders[side] = (struct table_border_edge){.border.style = CSS_BORDER_STYLE_NONE, .owner = box, .row = row};
        struct box_border *border = &borders[side].border;
        if (!box->style)
            continue;
        border->style = styles[side](box->style);
        colors[side](box->style, &border->c);
        if (border->style > CSS_BORDER_STYLE_HIDDEN) {
            css_fixed_or_calc width = (css_fixed_or_calc)0;
            css_unit unit = CSS_UNIT_PX;
            widths[side](box->style, &width, &unit);
            if (unit == CSS_UNIT_CALC) {
                table__length_to_px_int(box->style, ctx, width, unit, &border->width);
                borders[side].priority_width = INTTOFIX(border->width);
            } else {
                borders[side].priority_width = css_unit_len2device_px(box->style, ctx, width.value, unit);
                border->width = FIXTOINT(borders[side].priority_width);
            }
        }
    }
}

static int table_border_style_rank(enum css_border_style_e style)
{
    switch (style) {
    case CSS_BORDER_STYLE_DOUBLE:
        return 8;
    case CSS_BORDER_STYLE_SOLID:
        return 7;
    case CSS_BORDER_STYLE_DASHED:
        return 6;
    case CSS_BORDER_STYLE_DOTTED:
        return 5;
    case CSS_BORDER_STYLE_RIDGE:
        return 4;
    case CSS_BORDER_STYLE_OUTSET:
        return 3;
    case CSS_BORDER_STYLE_GROOVE:
        return 2;
    case CSS_BORDER_STYLE_INSET:
        return 1;
    default:
        return 0;
    }
}

static int table_border_source_rank(const struct box *box)
{
    if (!box)
        return 0;
    switch (box->type) {
    case BOX_TABLE_CELL:
        return 4;
    case BOX_TABLE_ROW:
        return 3;
    case BOX_TABLE_ROW_GROUP:
        return 2;
    default:
        return 1;
    }
}

static void
table_offer_border(struct table_border_edge *edge, const struct table_border_edge *candidate, bool horizontal, bool rtl)
{
    const struct box_border *old = &edge->border, *border = &candidate->border;
    /* Hidden also marks grid lines inside a spanning cell: no border may cross it. */
    if (old->style == CSS_BORDER_STYLE_HIDDEN || border->style == CSS_BORDER_STYLE_NONE)
        return;
    if (border->style != CSS_BORDER_STYLE_HIDDEN && old->style != CSS_BORDER_STYLE_NONE) {
        if (candidate->priority_width != edge->priority_width) {
            if (candidate->priority_width < edge->priority_width)
                return;
        } else if (border->style != old->style) {
            if (table_border_style_rank(border->style) < table_border_style_rank(old->style))
                return;
        } else if (table_border_source_rank(candidate->owner) != table_border_source_rank(edge->owner)) {
            if (table_border_source_rank(candidate->owner) < table_border_source_rank(edge->owner))
                return;
        } else {
            /* Prefer the cell above a horizontal edge, or the cell on the
             * direction's leading side of a vertical edge. Source-row order
             * alone is insufficient when a neighbour spans several rows. */
            bool before = candidate->column != edge->column
                ? (rtl ? candidate->column > edge->column : candidate->column < edge->column)
                : candidate->row < edge->row;
            if (horizontal && candidate->row != edge->row)
                before = candidate->row < edge->row;
            if (!before)
                return;
        }
    }
    *edge = *candidate;
}

static void table_offer_perimeter(const css_unit_ctx *ctx, struct table_border_grid *grid, const struct box *box,
    unsigned int top, unsigned int bottom, unsigned int left, unsigned int right, bool rtl)
{
    struct table_border_edge borders[4];
    table_read_borders(ctx, box, top, borders);
    for (unsigned int side = 0; side < 4; side++)
        borders[side].column = left;
    for (unsigned int x = left; x < right; x++) {
        table_offer_border(&grid->horizontal[(size_t)top * grid->columns + x], &borders[TOP], true, rtl);
        table_offer_border(&grid->horizontal[(size_t)bottom * grid->columns + x], &borders[BOTTOM], true, rtl);
    }
    for (unsigned int y = top; y < bottom; y++) {
        table_offer_border(&grid->vertical[(size_t)y * (grid->columns + 1) + left], &borders[LEFT], false, rtl);
        table_offer_border(&grid->vertical[(size_t)y * (grid->columns + 1) + right], &borders[RIGHT], false, rtl);
    }
}

static void table_max_border(struct box_border *used, const struct table_border_edge *edge)
{
    if (edge->border.width > used->width)
        *used = edge->border;
}

static unsigned int table_group_rows(const struct box *group)
{
    unsigned int rows = 0;
    for (const struct box *row = group->children; row; row = row->next)
        rows++;
    return rows;
}

static void table_assign_cell_borders(struct box *table, const struct table_border_grid *grid)
{
    /* CSS 2.2 17.6.2: center borders on grid lines. For odd device widths,
     * consistently assign the extra pixel to the right/bottom half. Each cell
     * reserves its inward half, using the widest segment on a spanned side. */
    unsigned int y = 0;
    for (struct box *group = table->children; group; group = group->next) {
        unsigned int group_end = y + table_group_rows(group);
        for (struct box *row = group->children; row; row = row->next, y++) {
            for (struct box *cell = row->children; cell; cell = cell->next) {
                unsigned int left = table_cell_start_column(table, cell), right = left + cell->columns;
                unsigned int bottom = cell->rows < group_end - y ? y + cell->rows : group_end;
                for (unsigned int side = 0; side < 4; side++)
                    cell->border[side] = (struct box_border){.style = CSS_BORDER_STYLE_NONE};
                for (unsigned int c = left; c < right; c++) {
                    table_max_border(&cell->border[TOP], &grid->horizontal[(size_t)y * grid->columns + c]);
                    table_max_border(&cell->border[BOTTOM], &grid->horizontal[(size_t)bottom * grid->columns + c]);
                }
                for (unsigned int r = y; r < bottom; r++) {
                    table_max_border(&cell->border[LEFT], &grid->vertical[(size_t)r * (grid->columns + 1) + left]);
                    table_max_border(&cell->border[RIGHT], &grid->vertical[(size_t)r * (grid->columns + 1) + right]);
                }
                for (unsigned int side = 0; side < 4; side++)
                    cell->border[side].width = (cell->border[side].width + (side == LEFT || side == TOP)) / 2;
            }
        }
    }
}

bool table_prepare_borders(const css_unit_ctx *ctx, struct box *table)
{
    if (table->table->borders)
        return true;
    if (css_computed_border_collapse(table->style) != CSS_BORDER_COLLAPSE_COLLAPSE) {
        for (struct box *group = table->children; group; group = group->next)
            for (struct box *row = group->children; row; row = row->next)
                for (struct box *cell = row->children; cell; cell = cell->next) {
                    struct table_border_edge borders[4];
                    table_read_borders(ctx, cell, 0, borders);
                    for (unsigned int side = 0; side < 4; side++)
                        cell->border[side] = borders[side].border;
                }
        return true;
    }

    struct table_border_grid *grid = talloc_zero(table->table, struct table_border_grid);
    if (!grid)
        return false;
    grid->columns = table->columns;
    for (struct box *group = table->children; group; group = group->next)
        grid->rows += table_group_rows(group);
    /* The bundled talloc takes unsigned array counts, even on 64-bit hosts. */
    uint64_t horizontal_count = ((uint64_t)grid->rows + 1) * grid->columns;
    uint64_t vertical_count = (uint64_t)grid->rows * ((uint64_t)grid->columns + 1);
    if (grid->rows == UINT_MAX || grid->columns == UINT_MAX || horizontal_count > UINT_MAX ||
        vertical_count > UINT_MAX) {
        talloc_free(grid);
        return false;
    }
    grid->horizontal = talloc_zero_array(grid, struct table_border_edge, horizontal_count);
    grid->vertical = talloc_zero_array(grid, struct table_border_edge, vertical_count);
    grid->x = talloc_array(grid, int, (size_t)grid->columns + 1);
    grid->y = talloc_array(grid, int, (size_t)grid->rows + 1);
    if (!grid->horizontal || !grid->vertical || !grid->x || !grid->y) {
        talloc_free(grid);
        return false;
    }
    for (size_t i = 0; i < horizontal_count; i++)
        grid->horizontal[i].border.style = CSS_BORDER_STYLE_NONE;
    for (size_t i = 0; i < vertical_count; i++)
        grid->vertical[i].border.style = CSS_BORDER_STYLE_NONE;

    unsigned int y = 0;
    for (struct box *group = table->children; group; group = group->next) {
        unsigned int group_end = y + table_group_rows(group);
        for (struct box *row = group->children; row; row = row->next, y++) {
            for (struct box *cell = row->children; cell; cell = cell->next) {
                unsigned int left = table_cell_start_column(table, cell), right = left + cell->columns;
                unsigned int bottom = cell->rows < group_end - y ? y + cell->rows : group_end;
                for (unsigned int r = y; r < bottom; r++)
                    for (unsigned int c = left + 1; c < right; c++)
                        grid->vertical[(size_t)r * (grid->columns + 1) + c].border.style = CSS_BORDER_STYLE_HIDDEN;
                for (unsigned int r = y + 1; r < bottom; r++)
                    for (unsigned int c = left; c < right; c++)
                        grid->horizontal[(size_t)r * grid->columns + c].border.style = CSS_BORDER_STYLE_HIDDEN;
            }
        }
    }

    bool rtl = css_computed_direction(table->style) == CSS_DIRECTION_RTL;
    table_offer_perimeter(ctx, grid, table, 0, grid->rows, 0, grid->columns, rtl);
    y = 0;
    for (struct box *group = table->children; group; group = group->next) {
        unsigned int group_end = y + table_group_rows(group);
        table_offer_perimeter(ctx, grid, group, y, group_end, 0, grid->columns, rtl);
        for (struct box *row = group->children; row; row = row->next, y++) {
            table_offer_perimeter(ctx, grid, row, y, y + 1, 0, grid->columns, rtl);
            for (struct box *cell = row->children; cell; cell = cell->next) {
                unsigned int bottom = cell->rows < group_end - y ? y + cell->rows : group_end;
                unsigned int left = table_cell_start_column(table, cell);
                table_offer_perimeter(ctx, grid, cell, y, bottom, left, left + cell->columns, rtl);
            }
        }
    }

    table_assign_cell_borders(table, grid);

    for (unsigned int side = 0; side < 4; side++)
        table->border[side] = (struct box_border){.style = CSS_BORDER_STYLE_NONE};
    if (grid->rows && grid->columns) {
        table->border[LEFT] = grid->vertical[0].border;
        table->border[RIGHT] = grid->vertical[grid->columns].border;
        for (unsigned int c = 0; c < grid->columns; c++) {
            table_max_border(&table->border[TOP], &grid->horizontal[c]);
            table_max_border(&table->border[BOTTOM], &grid->horizontal[(size_t)grid->rows * grid->columns + c]);
        }
    }
    for (unsigned int side = 0; side < 4; side++)
        table->border[side].width = (table->border[side].width + (side == RIGHT || side == BOTTOM)) / 2;
    table->table->borders = grid;
    return true;
}

void table_position_borders(struct box *table, const int *columns)
{
    struct table_border_grid *grid = table->table->borders;
    if (!grid)
        return;
    memcpy(grid->x, columns, ((size_t)grid->columns + 1) * sizeof(*columns));
    unsigned int y = 0;
    grid->y[0] = 0;
    for (const struct box *group = table->children; group; group = group->next)
        for (const struct box *row = group->children; row; row = row->next) {
            grid->y[y++] = group->y + row->y;
            grid->y[y] = group->y + row->y + row->height;
        }
}

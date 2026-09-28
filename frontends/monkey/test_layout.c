/* SPDX-License-Identifier: GPL-2.0-only */
#include <stdio.h>
#include <stdlib.h>
#include <wisp/layout.h>
#include <wisp/plot_style.h>
#include "monkey/layout.h"

#define REQUIRE(expr) do { if (!(expr)) { \
    fprintf(stderr, "line %d: %s\n", __LINE__, #expr); return EXIT_FAILURE; \
} } while (0)

int main(void)
{
    const plot_font_style_t style = {.size = 0};
    const char text[] = "zero size";
    const size_t length = sizeof(text) - 1;
    size_t offset;
    int width;
    REQUIRE(monkey_layout_table->width(&style, text, length, &width) == NSERROR_OK);
    REQUIRE(width == 0);
    REQUIRE(monkey_layout_table->position(&style, text, length, 1, &offset, &width) == NSERROR_OK);
    REQUIRE(offset == length && width == 0);
    REQUIRE(monkey_layout_table->position(&style, text, length, 0, &offset, &width) == NSERROR_OK);
    REQUIRE(offset == 0 && width == 0);
    REQUIRE(monkey_layout_table->split(&style, text, length, 0, &offset, &width) == NSERROR_OK);
    REQUIRE(offset == length && width == 0);
    return EXIT_SUCCESS;
}

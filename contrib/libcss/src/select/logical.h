/* Horizontal-tb inline edge alternatives for partial computed styles.
 * Licensed under the MIT License. */
#ifndef css_select_logical_h_
#define css_select_logical_h_

#include <libcss/computed.h>
#include "bytecode/bytecode.h"

enum { CSS_INLINE_EDGE_COUNT = 6 };

struct css_inline_edge_value {
    css_fixed_or_calc length;
    css_unit unit;
    uint8_t type;
};

/* Main computed slots hold the LTR cascade. Only affected partial styles own
 * these RTL alternatives. Fully composed styles use ordinary physical slots. */
struct css_inline_edges {
    struct css_inline_edge_value rtl[CSS_INLINE_EDGE_COUNT];
    uint8_t inherit_logical[2]; /* Winning inherit came from a logical name. */
};

int css__inline_edge_index(opcode_t op);
opcode_t css__inline_edge_opcode(unsigned index);
css_error css__inline_edges_create(css_computed_style *style);
void css__inline_edges_destroy(css_computed_style *style);
css_error css__inline_edges_clone(const css_computed_style *from, css_computed_style *to);
void css__inline_edge_swap(css_computed_style *style, unsigned index);
void css__inline_edge_copy(const css_computed_style *from, css_computed_style *to, unsigned index, bool rtl);
void css__inline_edges_compose(
    const css_computed_style *parent, const css_computed_style *child, css_computed_style *result);
bool css__inline_edges_equal(const css_computed_style *a, const css_computed_style *b);
uint32_t css__inline_edges_hash(const css_computed_style *style);

#endif

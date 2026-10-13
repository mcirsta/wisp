/* CSS Logical 1 inline mapping without repeating selector matching. Partial
 * styles remain composable with either parent direction. MIT License. */
#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "select/logical.h"
#include "select/computed.h"
#include "select/propget.h"
#include "select/propset.h"

static const struct {
    opcode_t op;
    uint8_t (*get)(const css_computed_style *, css_fixed_or_calc *, css_unit *);
    css_error (*set)(css_computed_style *, uint8_t, css_fixed_or_calc, css_unit);
} edges[CSS_INLINE_EDGE_COUNT] = {
    {CSS_PROP_MARGIN_LEFT, get_margin_left, set_margin_left},
    {CSS_PROP_MARGIN_RIGHT, get_margin_right, set_margin_right},
    {CSS_PROP_PADDING_LEFT, get_padding_left, set_padding_left},
    {CSS_PROP_PADDING_RIGHT, get_padding_right, set_padding_right},
    {CSS_PROP_LEFT, get_left, set_left},
    {CSS_PROP_RIGHT, get_right, set_right},
};

int css__inline_edge_index(opcode_t op)
{
    switch (op) {
    case CSS_PROP_MARGIN_LEFT:
        return 0;
    case CSS_PROP_MARGIN_RIGHT:
        return 1;
    case CSS_PROP_PADDING_LEFT:
        return 2;
    case CSS_PROP_PADDING_RIGHT:
        return 3;
    case CSS_PROP_LEFT:
        return 4;
    case CSS_PROP_RIGHT:
        return 5;
    default:
        return -1;
    }
}

opcode_t css__inline_edge_opcode(unsigned index)
{
    assert(index < CSS_INLINE_EDGE_COUNT);
    return edges[index].op;
}

/* Values returned here are borrowed; retained alternatives own calc strings. */
static struct css_inline_edge_value read_edge(const css_computed_style *style, unsigned index)
{
    struct css_inline_edge_value value = {.unit = CSS_UNIT_PX};
    value.type = edges[index].get(style, &value.length, &value.unit);
    return value;
}

static void ref_edge(struct css_inline_edge_value *value)
{
    if (value->unit == CSS_UNIT_CALC)
        lwc_string_ref(value->length.calc);
}

static void unref_edge(struct css_inline_edge_value *value)
{
    if (value->unit == CSS_UNIT_CALC)
        lwc_string_unref(value->length.calc);
}

static void write_edge(css_computed_style *style, unsigned index, struct css_inline_edge_value value)
{
    /* The scalar setters only assign packed fields and retain calc strings. */
    ref_edge(&value); /* A root may compose into its own scalar slots. */
    edges[index].set(style, value.type, value.length, value.unit);
    unref_edge(&value);
}

css_error css__inline_edges_create(css_computed_style *style)
{
    assert(style->inline_edges == NULL);
    style->inline_edges = calloc(1, sizeof(*style->inline_edges));
    if (style->inline_edges == NULL)
        return CSS_NOMEM;
    for (unsigned i = 0; i < CSS_INLINE_EDGE_COUNT; i++) {
        style->inline_edges->rtl[i] = read_edge(style, i);
        ref_edge(&style->inline_edges->rtl[i]);
    }
    return CSS_OK;
}

void css__inline_edges_destroy(css_computed_style *style)
{
    if (style->inline_edges == NULL)
        return;
    for (unsigned i = 0; i < CSS_INLINE_EDGE_COUNT; i++)
        unref_edge(&style->inline_edges->rtl[i]);
    free(style->inline_edges);
    style->inline_edges = NULL;
}

css_error css__inline_edges_clone(const css_computed_style *from, css_computed_style *to)
{
    if (from->inline_edges == NULL)
        return CSS_OK;
    assert(to->inline_edges == NULL);
    to->inline_edges = malloc(sizeof(*to->inline_edges));
    if (to->inline_edges == NULL)
        return CSS_NOMEM;
    *to->inline_edges = *from->inline_edges;
    for (unsigned i = 0; i < CSS_INLINE_EDGE_COUNT; i++)
        ref_edge(&to->inline_edges->rtl[i]);
    return CSS_OK;
}

/* Temporarily put one alternative in the ordinary slot so both cascades use
 * the same native parser, priority comparison, defaulting and value ownership. */
void css__inline_edge_swap(css_computed_style *style, unsigned index)
{
    struct css_inline_edge_value main = read_edge(style, index);
    ref_edge(&main);
    write_edge(style, index, style->inline_edges->rtl[index]);
    unref_edge(&style->inline_edges->rtl[index]);
    style->inline_edges->rtl[index] = main;
}

void css__inline_edge_copy(const css_computed_style *from, css_computed_style *to, unsigned index, bool rtl)
{
    write_edge(to, index, rtl && from->inline_edges != NULL ? from->inline_edges->rtl[index] : read_edge(from, index));
}

void css__inline_edges_compose(
    const css_computed_style *parent, const css_computed_style *child, css_computed_style *result)
{
    if (child->inline_edges == NULL)
        return;
    bool rtl = get_direction(result) == CSS_DIRECTION_RTL;
    bool parent_rtl = parent != NULL && get_direction(parent) == CSS_DIRECTION_RTL;
    unsigned logical = child->inline_edges->inherit_logical[rtl];
    for (unsigned i = 0; i < CSS_INLINE_EDGE_COUNT; i++) {
        struct css_inline_edge_value value = rtl ? child->inline_edges->rtl[i] : read_edge(child, i);
        /* All six properties use zero for INHERIT. A logical inherit follows
         * the namesake on the parent, including opposite parent/child directions. */
        if (value.type == 0 && parent != NULL)
            value = read_edge(parent, (logical & (1u << i)) && rtl != parent_rtl ? i ^ 1 : i);
        write_edge(result, i, value);
    }
}

bool css__inline_edges_equal(const css_computed_style *a, const css_computed_style *b)
{
    const struct css_inline_edges *x = a->inline_edges, *y = b->inline_edges;
    if (x == NULL || y == NULL)
        return x == y;
    if (memcmp(x->inherit_logical, y->inherit_logical, sizeof(x->inherit_logical)) != 0)
        return false;
    for (unsigned i = 0; i < CSS_INLINE_EDGE_COUNT; i++) {
        const struct css_inline_edge_value *v = &x->rtl[i], *w = &y->rtl[i];
        if (v->type != w->type || v->unit != w->unit ||
            (v->unit == CSS_UNIT_CALC ? v->length.calc != w->length.calc : v->length.value != w->length.value))
            return false;
    }
    return true;
}

uint32_t css__inline_edges_hash(const css_computed_style *style)
{
    const struct css_inline_edges *edges = style->inline_edges;
    if (edges == NULL)
        return 0;
    uint32_t hash = 2166136261u;
    for (unsigned i = 0; i < CSS_INLINE_EDGE_COUNT; i++) {
        const struct css_inline_edge_value *v = &edges->rtl[i];
        uint32_t payload = v->unit == CSS_UNIT_CALC ? lwc_string_hash_value(v->length.calc) : (uint32_t)v->length.value;
        hash = (hash ^ payload) * 16777619u;
        hash = (hash ^ v->type ^ ((uint32_t)v->unit << 8)) * 16777619u;
    }
    return hash ^ edges->inherit_logical[0] ^ ((uint32_t)edges->inherit_logical[1] << 8);
}

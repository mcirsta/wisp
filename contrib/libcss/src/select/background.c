/* Background list ownership and cascade. Licensed under the MIT License. */
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "bytecode/bytecode.h"
#include "select/background.h"
#include "select/propget.h"
#include "select/propset.h"
#include "select/properties/helpers.h"
#include "select/arena_hash.h"

static struct css_background_list *get_list(const css_computed_style *s, enum css_background_property p)
{
    return s->background_layers == NULL ? NULL : s->background_layers->list[p];
}

static void value_destroy(css_background_value *v, enum css_background_property p)
{
    if (p == CSS_BG_IMAGE) {
        if (v->image.type == CSS_BACKGROUND_IMAGE_IMAGE && v->image.data.url != NULL) {
            lwc_string_unref(v->image.data.url);
        } else if (v->image.type == CSS_BACKGROUND_IMAGE_LINEAR_GRADIENT)
            free(v->image.data.linear);
        else if (v->image.type == CSS_BACKGROUND_IMAGE_RADIAL_GRADIENT)
            free(v->image.data.radial);
    } else if (p == CSS_BG_POSITION) {
        if (v->position.xunit == CSS_UNIT_CALC)
            lwc_string_unref(v->position.x.calc);
        if (v->position.yunit == CSS_UNIT_CALC)
            lwc_string_unref(v->position.y.calc);
    }
}

static void list_destroy(struct css_background_list *list, enum css_background_property p)
{
    if (list == NULL)
        return;
    for (uint32_t i = 0; i < list->count; i++)
        value_destroy(&list->values[i], p);
    free(list);
}

void css__background_clear(css_computed_style *s, enum css_background_property p)
{
    if (s->background_layers == NULL)
        return;
    list_destroy(s->background_layers->list[p], p);
    s->background_layers->list[p] = NULL;
    for (unsigned i = 0; i < CSS_BG_COUNT; i++) {
        if (s->background_layers->list[i] != NULL)
            return;
    }
    free(s->background_layers);
    s->background_layers = NULL;
}

void css__background_destroy(css_computed_style *s)
{
    for (unsigned p = 0; p < CSS_BG_COUNT; p++)
        css__background_clear(s, p);
}

static css_error replace_list(css_computed_style *s, enum css_background_property p, struct css_background_list *list)
{
    if (list == NULL) {
        css__background_clear(s, p);
        return CSS_OK;
    }
    if (s->background_layers == NULL) {
        s->background_layers = calloc(1, sizeof(*s->background_layers));
        if (s->background_layers == NULL)
            return CSS_NOMEM;
    }
    list_destroy(s->background_layers->list[p], p);
    s->background_layers->list[p] = list;
    return CSS_OK;
}

static struct css_background_list *list_alloc(uint32_t count)
{
    if (count > (SIZE_MAX - sizeof(struct css_background_list)) / sizeof(css_background_value))
        return NULL;
    struct css_background_list *list = calloc(1, sizeof(*list) + count * sizeof(list->values[0]));
    if (list != NULL)
        list->count = count;
    return list;
}

css_error css__background_copy(const css_computed_style *from, css_computed_style *to, enum css_background_property p)
{
    if (from == to)
        return CSS_OK;
    struct css_background_list *src = get_list(from, p), *dst = NULL;
    if (src != NULL) {
        dst = list_alloc(src->count);
        if (dst == NULL)
            return CSS_NOMEM;
        for (uint32_t i = 0; i < src->count; i++) {
            css_background_value *v = &dst->values[i];
            *v = src->values[i];
            if (p == CSS_BG_IMAGE) {
                if (v->image.type == CSS_BACKGROUND_IMAGE_IMAGE && v->image.data.url != NULL)
                    lwc_string_ref(v->image.data.url);
                else if (v->image.type == CSS_BACKGROUND_IMAGE_LINEAR_GRADIENT ||
                    v->image.type == CSS_BACKGROUND_IMAGE_RADIAL_GRADIENT) {
                    size_t size = v->image.type == CSS_BACKGROUND_IMAGE_LINEAR_GRADIENT ? sizeof(css_linear_gradient)
                                                                                        : sizeof(css_radial_gradient);
                    void *copy = malloc(size);
                    if (copy == NULL) {
                        memset(v, 0, sizeof(*v));
                        list_destroy(dst, p);
                        return CSS_NOMEM;
                    }
                    if (v->image.type == CSS_BACKGROUND_IMAGE_LINEAR_GRADIENT) {
                        memcpy(copy, v->image.data.linear, size);
                        v->image.data.linear = copy;
                    } else {
                        memcpy(copy, v->image.data.radial, size);
                        v->image.data.radial = copy;
                    }
                }
            } else if (p == CSS_BG_POSITION) {
                if (v->position.xunit == CSS_UNIT_CALC)
                    lwc_string_ref(v->position.x.calc);
                if (v->position.yunit == CSS_UNIT_CALC)
                    lwc_string_ref(v->position.y.calc);
            }
        }
    }
    css_error error = replace_list(to, p, dst);
    if (error != CSS_OK)
        list_destroy(dst, p);
    return error;
}

static uint32_t read_word(css_style *style)
{
    uint32_t word = *style->bytecode;
    advance_bytecode(style, sizeof(css_code_t));
    return word;
}

static css_error read_value(uint32_t opv, css_style *style, enum css_background_property p, css_background_value *v)
{
    uint16_t value = getValue(opv);
    if (p == CSS_BG_IMAGE) {
        css_background_image *image = &v->image;
        if (value == BACKGROUND_IMAGE_NONE) {
            image->type = CSS_BACKGROUND_IMAGE_NONE;
        } else if (value == BACKGROUND_IMAGE_URI) {
            image->type = CSS_BACKGROUND_IMAGE_IMAGE;
            css__stylesheet_string_get(style->sheet, read_word(style), &image->data.url);
            lwc_string_ref(image->data.url);
        } else {
            uint32_t direction = read_word(style), count = read_word(style);
            css_gradient_stop *stops;
            if (value == BACKGROUND_IMAGE_LINEAR_GRADIENT) {
                assert(count <= CSS_LINEAR_GRADIENT_MAX_STOPS);
                image->type = CSS_BACKGROUND_IMAGE_LINEAR_GRADIENT;
                image->data.linear = calloc(1, sizeof(*image->data.linear));
                if (image->data.linear == NULL)
                    return CSS_NOMEM;
                image->data.linear->direction = direction;
                image->data.linear->stop_count = count;
                stops = image->data.linear->stops;
            } else {
                assert(value == BACKGROUND_IMAGE_RADIAL_GRADIENT && count <= CSS_RADIAL_GRADIENT_MAX_STOPS);
                image->type = CSS_BACKGROUND_IMAGE_RADIAL_GRADIENT;
                image->data.radial = calloc(1, sizeof(*image->data.radial));
                if (image->data.radial == NULL)
                    return CSS_NOMEM;
                image->data.radial->shape = direction;
                image->data.radial->stop_count = count;
                stops = image->data.radial->stops;
            }
            for (uint32_t i = 0; i < count; i++) {
                stops[i].color = read_word(style);
                stops[i].offset = (css_fixed)read_word(style);
                uint32_t unit = read_word(style);
                stops[i].auto_offset = unit == 0;
                stops[i].unit = unit == 0 ? CSS_UNIT_PCT : css__to_css_unit(unit);
            }
        }
    } else if (p == CSS_BG_POSITION) {
        css_background_position *pos = &v->position;
        pos->xunit = pos->yunit = CSS_UNIT_PCT;
        switch (value & 0xf0) {
        case BACKGROUND_POSITION_HORZ_SET:
            pos->x.value = (css_fixed)read_word(style);
            pos->xunit = css__to_css_unit(read_word(style));
            break;
        case BACKGROUND_POSITION_HORZ_CENTER:
            pos->x.value = INTTOFIX(50);
            break;
        case BACKGROUND_POSITION_HORZ_RIGHT:
            pos->x.value = INTTOFIX(100);
            break;
        default:
            break;
        }
        switch (value & 0x0f) {
        case BACKGROUND_POSITION_VERT_SET:
            pos->y.value = (css_fixed)read_word(style);
            pos->yunit = css__to_css_unit(read_word(style));
            break;
        case BACKGROUND_POSITION_VERT_CENTER:
            pos->y.value = INTTOFIX(50);
            break;
        case BACKGROUND_POSITION_VERT_BOTTOM:
            pos->y.value = INTTOFIX(100);
            break;
        default:
            break;
        }
    } else if (p == CSS_BG_REPEAT_X || p == CSS_BG_REPEAT_Y) {
        assert(value == BACKGROUND_REPEAT_REPEAT || value == BACKGROUND_REPEAT_NO_REPEAT);
        v->keyword = value == BACKGROUND_REPEAT_REPEAT ? CSS_BACKGROUND_REPEAT_AXIS_REPEAT
                                                      : CSS_BACKGROUND_REPEAT_AXIS_NO_REPEAT;
    } else {
        v->keyword = value == BACKGROUND_ATTACHMENT_FIXED ? CSS_BACKGROUND_ATTACHMENT_FIXED
                                                          : CSS_BACKGROUND_ATTACHMENT_SCROLL;
    }
    return CSS_OK;
}

css_error
css__background_cascade(uint32_t opv, css_style *style, css_select_state *state, enum css_background_property p)
{
    uint32_t count = 1;
    bool list = css__is_background_list(opv);
    if (list) {
        uint32_t words = read_word(style);
        /* Losing declarations need no allocations or item decoding. */
        if (!css__outranks_existing(getOpcode(opv), isImportant(opv), state, getFlagValue(opv))) {
            advance_bytecode(style, words * sizeof(css_code_t));
            return CSS_OK;
        }
        count = read_word(style);
        assert(count > 1);
    }
    struct css_background_list *tail = count == 1 ? NULL : list_alloc(count - 1);
    if (count > 1 && tail == NULL)
        return CSS_NOMEM;
    css_background_value first = {0};
    css_error error = CSS_OK;
    for (uint32_t i = 0; i < count && !hasFlagValue(opv); i++) {
        error = read_value(list ? read_word(style) : opv, style, p, i == 0 ? &first : &tail->values[i - 1]);
        if (error != CSS_OK)
            goto cleanup;
    }
    if (!list && !css__outranks_existing(getOpcode(opv), isImportant(opv), state, getFlagValue(opv)))
        goto cleanup;
    error = replace_list(state->computed, p, tail);
    if (error != CSS_OK)
        goto cleanup;
    tail = NULL;
    switch (p) {
    case CSS_BG_IMAGE:
        error = set_background_image(state->computed, first.image.type,
            first.image.type == CSS_BACKGROUND_IMAGE_IMAGE ? first.image.data.url : NULL);
        free(state->computed->background_gradient);
        free(state->computed->background_radial_gradient);
        state->computed->background_gradient = first.image.type == CSS_BACKGROUND_IMAGE_LINEAR_GRADIENT
            ? first.image.data.linear
            : NULL;
        state->computed->background_radial_gradient = first.image.type == CSS_BACKGROUND_IMAGE_RADIAL_GRADIENT
            ? first.image.data.radial
            : NULL;
        if (first.image.type != CSS_BACKGROUND_IMAGE_IMAGE)
            memset(&first, 0, sizeof(first)); /* gradient ownership transferred */
        break;
    case CSS_BG_POSITION:
        error = set_background_position(state->computed,
            hasFlagValue(opv) ? CSS_BACKGROUND_POSITION_INHERIT : CSS_BACKGROUND_POSITION_SET, first.position.x,
            first.position.xunit, first.position.y, first.position.yunit);
        break;
    case CSS_BG_REPEAT_X:
        error = set_background_repeat_x(state->computed, first.keyword);
        break;
    case CSS_BG_REPEAT_Y:
        error = set_background_repeat_y(state->computed, first.keyword);
        break;
    case CSS_BG_ATTACHMENT:
        error = set_background_attachment(state->computed, first.keyword);
        break;
    default:
        break;
    }
cleanup:
    value_destroy(&first, p);
    list_destroy(tail, p);
    return error;
}

uint32_t css_computed_background_layer_count(const css_computed_style *style)
{
    struct css_background_list *list = get_list(style, CSS_BG_IMAGE);
    return list == NULL ? 1 : 1 + list->count;
}

uint8_t css_computed_background_image_layer(const css_computed_style *style, uint32_t layer, lwc_string **url)
{
    *url = NULL;
    if (layer == 0)
        return get_background_image(style, url);
    struct css_background_list *list = get_list(style, CSS_BG_IMAGE);
    if (list == NULL || layer > list->count)
        return CSS_BACKGROUND_IMAGE_NONE;
    const css_background_image *image = &list->values[layer - 1].image;
    if (image->type == CSS_BACKGROUND_IMAGE_IMAGE)
        *url = image->data.url;
    return image->type;
}

const css_linear_gradient *css_computed_background_gradient_layer(const css_computed_style *s, uint32_t layer)
{
    if (layer == 0)
        return s->background_gradient;
    struct css_background_list *list = get_list(s, CSS_BG_IMAGE);
    return list != NULL && layer <= list->count &&
            list->values[layer - 1].image.type == CSS_BACKGROUND_IMAGE_LINEAR_GRADIENT
        ? list->values[layer - 1].image.data.linear
        : NULL;
}

const css_radial_gradient *css_computed_background_radial_gradient_layer(const css_computed_style *s, uint32_t layer)
{
    if (layer == 0)
        return s->background_radial_gradient;
    struct css_background_list *list = get_list(s, CSS_BG_IMAGE);
    return list != NULL && layer <= list->count &&
            list->values[layer - 1].image.type == CSS_BACKGROUND_IMAGE_RADIAL_GRADIENT
        ? list->values[layer - 1].image.data.radial
        : NULL;
}

uint8_t css_computed_background_position_layer(
    const css_computed_style *s, uint32_t layer, css_fixed_or_calc *x, css_unit *xu, css_fixed_or_calc *y, css_unit *yu)
{
    struct css_background_list *list = get_list(s, CSS_BG_POSITION);
    if (list != NULL)
        layer %= list->count + 1;
    if (list == NULL || layer == 0)
        return get_background_position(s, x, xu, y, yu);
    const css_background_position *pos = &list->values[layer - 1].position;
    *x = pos->x;
    *xu = pos->xunit;
    *y = pos->y;
    *yu = pos->yunit;
    return CSS_BACKGROUND_POSITION_SET;
}

static uint8_t repeat_axis_layer(const css_computed_style *s, enum css_background_property axis, uint32_t layer)
{
    struct css_background_list *list = get_list(s, axis);
    if (list != NULL)
        layer %= list->count + 1;
    if (list != NULL && layer != 0)
        return list->values[layer - 1].keyword;
    return axis == CSS_BG_REPEAT_X ? get_background_repeat_x(s) : get_background_repeat_y(s);
}

uint8_t css_computed_background_repeat_layer(const css_computed_style *s, uint32_t layer)
{
    /* CSS Backgrounds 4: repeat each axis's own list to match the image layers. */
    uint8_t x = repeat_axis_layer(s, CSS_BG_REPEAT_X, layer);
    uint8_t y = repeat_axis_layer(s, CSS_BG_REPEAT_Y, layer);
    if (x == CSS_BACKGROUND_REPEAT_AXIS_INHERIT || y == CSS_BACKGROUND_REPEAT_AXIS_INHERIT)
        return CSS_BACKGROUND_REPEAT_INHERIT;
    if (x == CSS_BACKGROUND_REPEAT_AXIS_REPEAT)
        return y == CSS_BACKGROUND_REPEAT_AXIS_REPEAT ? CSS_BACKGROUND_REPEAT_REPEAT : CSS_BACKGROUND_REPEAT_REPEAT_X;
    return y == CSS_BACKGROUND_REPEAT_AXIS_REPEAT ? CSS_BACKGROUND_REPEAT_REPEAT_Y : CSS_BACKGROUND_REPEAT_NO_REPEAT;
}

uint8_t css_computed_background_repeat(const css_computed_style *s)
{
    return css_computed_background_repeat_layer(s, 0);
}

uint8_t css_computed_background_attachment_layer(const css_computed_style *s, uint32_t layer)
{
    struct css_background_list *list = get_list(s, CSS_BG_ATTACHMENT);
    if (list != NULL)
        layer %= list->count + 1;
    return list == NULL || layer == 0 ? get_background_attachment(s) : list->values[layer - 1].keyword;
}

/* All payloads are zero-initialised, but compare actual fields: padding and
 * separately allocated gradient addresses must never affect style sharing. */
static bool stops_equal(const css_gradient_stop *a, const css_gradient_stop *b, uint8_t count)
{
    for (uint8_t i = 0; i < count; i++) {
        if (a[i].color != b[i].color || a[i].offset != b[i].offset || a[i].unit != b[i].unit ||
            a[i].auto_offset != b[i].auto_offset)
            return false;
    }
    return true;
}

static bool value_equal(const css_background_value *a, const css_background_value *b, enum css_background_property p)
{
    if (p == CSS_BG_IMAGE) {
        if (a->image.type != b->image.type)
            return false;
        switch (a->image.type) {
        case CSS_BACKGROUND_IMAGE_IMAGE:
            return a->image.data.url == b->image.data.url;
        case CSS_BACKGROUND_IMAGE_LINEAR_GRADIENT: {
            const css_linear_gradient *x = a->image.data.linear, *y = b->image.data.linear;
            return x->direction == y->direction && x->stop_count == y->stop_count &&
                stops_equal(x->stops, y->stops, x->stop_count);
        }
        case CSS_BACKGROUND_IMAGE_RADIAL_GRADIENT: {
            const css_radial_gradient *x = a->image.data.radial, *y = b->image.data.radial;
            return x->shape == y->shape && x->stop_count == y->stop_count &&
                stops_equal(x->stops, y->stops, x->stop_count);
        }
        default:
            return true;
        }
    }
    if (p == CSS_BG_POSITION) {
        const css_background_position *x = &a->position, *y = &b->position;
        return x->xunit == y->xunit && x->yunit == y->yunit &&
            (x->xunit == CSS_UNIT_CALC ? x->x.calc == y->x.calc : x->x.value == y->x.value) &&
            (x->yunit == CSS_UNIT_CALC ? x->y.calc == y->y.calc : x->y.value == y->y.value);
    }
    return a->keyword == b->keyword;
}

bool css__background_equal(const css_computed_style *a, const css_computed_style *b)
{
    if (a->background_layers == b->background_layers)
        return true;
    for (unsigned p = 0; p < CSS_BG_COUNT; p++) {
        struct css_background_list *x = get_list(a, p), *y = get_list(b, p);
        if (x == NULL || y == NULL) {
            if (x != y)
                return false;
            continue;
        }
        if (x->count != y->count)
            return false;
        for (uint32_t i = 0; i < x->count; i++) {
            if (!value_equal(&x->values[i], &y->values[i], p))
                return false;
        }
    }
    return true;
}

static uint32_t hash_stops(uint32_t hash, const css_gradient_stop *stops, uint8_t count)
{
    hash = hash * 33 + count;
    for (uint8_t i = 0; i < count; i++) {
        hash = hash * 33 + stops[i].color;
        hash = hash * 33 + (uint32_t)stops[i].offset;
        hash = hash * 33 + stops[i].unit;
        hash = hash * 33 + stops[i].auto_offset;
    }
    return hash;
}

uint32_t css__background_hash(const css_computed_style *s)
{
    uint32_t hash = 0;
    if (s->background_layers == NULL)
        return hash;
    for (unsigned p = 0; p < CSS_BG_COUNT; p++) {
        struct css_background_list *list = get_list(s, p);
        if (list == NULL)
            continue;
        hash = hash * 33 + list->count;
        for (uint32_t i = 0; i < list->count; i++) {
            const css_background_value *v = &list->values[i];
            if (p == CSS_BG_IMAGE) {
                hash = hash * 33 + v->image.type;
                if (v->image.type == CSS_BACKGROUND_IMAGE_LINEAR_GRADIENT) {
                    const css_linear_gradient *g = v->image.data.linear;
                    hash = hash_stops(hash * 33 + g->direction, g->stops, g->stop_count);
                } else if (v->image.type == CSS_BACKGROUND_IMAGE_RADIAL_GRADIENT) {
                    const css_radial_gradient *g = v->image.data.radial;
                    hash = hash_stops(hash * 33 + g->shape, g->stops, g->stop_count);
                } else if (v->image.type == CSS_BACKGROUND_IMAGE_IMAGE && v->image.data.url != NULL)
                    hash ^= css__arena_hash(
                        (const uint8_t *)lwc_string_data(v->image.data.url), lwc_string_length(v->image.data.url));
            } else if (p == CSS_BG_POSITION) {
                hash = hash * 33 + v->position.xunit;
                hash = hash * 33 + v->position.yunit;
                if (v->position.xunit != CSS_UNIT_CALC)
                    hash = hash * 33 + (uint32_t)v->position.x.value;
                if (v->position.yunit != CSS_UNIT_CALC)
                    hash = hash * 33 + (uint32_t)v->position.y.value;
            } else {
                hash = hash * 33 + v->keyword;
            }
        }
    }
    return hash;
}

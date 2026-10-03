/*
 * Copyright 2005 James Bursa <bursa@users.sourceforge.net>
 * Copyright 2003 Phil Mellor <monkeyson@users.sourceforge.net>
 * Copyright 2005 John M Bell <jmb202@ecs.soton.ac.uk>
 * Copyright 2006 Richard Wilson <info@tinct.net>
 * Copyright 2008 Michael Drake <tlsa@netsurf-browser.org>
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
 * Implementation of conversion from DOM tree to box tree.
 */

#include <dom/dom.h>
#include <string.h>

#include <wisp/desktop/gui_internal.h>
#include <wisp/misc.h>
#include <wisp/utils/ascii.h>
#include <wisp/utils/corestrings.h>
#include <wisp/utils/errors.h>
#include <wisp/utils/log.h>
#include <wisp/utils/nsoption.h>
#include <wisp/utils/nsurl.h>
#include <wisp/utils/string.h>
#include <wisp/utils/utf8.h>
#include <nsutils/time.h>
#include "utils/talloc.h"
#include "utils/utils.h"
#include "content/handlers/css/select.h"
#include <wctype.h>

#include <wisp/content/fetch.h>
#include <wisp/content/handlers/html/box.h>
#include <wisp/content/handlers/html/form_internal.h>
#include <wisp/content/handlers/html/private.h>
#include "content/handlers/html/box_construct.h"
#include "content/handlers/html/box_manipulate.h"
#include "content/handlers/html/box_normalise.h"
#include "content/handlers/html/box_special.h"
#include "content/handlers/html/object.h"
#include "content/handlers/html/restyle_profile.h"
#include "content/handlers/html/restyle.h"
#include "content/handlers/html/content_visibility.h"

/* Only the active DOM ancestry is needed during construction. Inline boxes
 * are flattened in the output tree, so retain their formatting container too. */
struct box_construct_ancestor {
    struct box *box;
    struct box *containing_block;
};

/**
 * Context for box tree construction
 */
struct box_construct_ctx {
    html_content *content; /**< Content we're constructing for */

    dom_node *n; /**< Current node to process */
    dom_node *root_node; /**< Explicit traversal boundary; owns a DOM reference */
    struct box_construct_ancestor *ancestors;
    size_t depth, capacity;

    struct box *root_box; /**< Root box in the tree */

    box_construct_complete_cb cb; /**< Callback to invoke on completion */

    void *bctx; /**< talloc context */
    const struct box *document_root; /**< Root style, distinct from subtree boundary */
    struct box_construct_region *region; /**< Owned until detached success */
    struct box *seed; /**< Private parent for detached construction */
    box_construct_region_cb region_cb;
    void *region_pw;
    nserror error; /**< Also carries errors from generated content */
    dom_node *owner_node; /**< Borrowed boundary of an arena in full construction */
};

/**
 * Transient properties for construction of current node
 */
struct box_construct_props {
    /** Style from which to inherit, or NULL if none */
    const css_computed_style *parent_style;
    /** Paint state from the DOM parent, before inline boxes are flattened. */
    box_flags inherited_flags;
    /** Current link target, or NULL if none */
    struct nsurl *href;
    /** Current frame target, or NULL if none */
    const char *target;
    /** Current title attribute, or NULL if none */
    const char *title;
    /** Identity of the current block-level container */
    struct box *containing_block;
    /** Current container for inlines, or NULL if none
     * \note If non-NULL, will be the last child of containing_block */
    struct box *inline_container;
    /** Whether the current node is the root of the DOM tree */
    bool node_is_root;
};

static const content_type image_types = CONTENT_IMAGE;

/* Mapping from CSS display to box type.
 * Uses designated initializers so order doesn't matter. */
static const box_type box_map[CSS_DISPLAY_FLOW_ROOT + 1] = {
    [CSS_DISPLAY_INHERIT] = BOX_BLOCK,
    [CSS_DISPLAY_INLINE] = BOX_INLINE,
    [CSS_DISPLAY_BLOCK] = BOX_BLOCK,
    [CSS_DISPLAY_LIST_ITEM] = BOX_BLOCK,
    [CSS_DISPLAY_RUN_IN] = BOX_INLINE,
    [CSS_DISPLAY_INLINE_BLOCK] = BOX_INLINE_BLOCK,
    [CSS_DISPLAY_TABLE] = BOX_TABLE,
    [CSS_DISPLAY_INLINE_TABLE] = BOX_TABLE,
    [CSS_DISPLAY_TABLE_ROW_GROUP] = BOX_TABLE_ROW_GROUP,
    [CSS_DISPLAY_TABLE_HEADER_GROUP] = BOX_TABLE_ROW_GROUP,
    [CSS_DISPLAY_TABLE_FOOTER_GROUP] = BOX_TABLE_ROW_GROUP,
    [CSS_DISPLAY_TABLE_ROW] = BOX_TABLE_ROW,
    [CSS_DISPLAY_TABLE_COLUMN_GROUP] = BOX_NONE,
    [CSS_DISPLAY_TABLE_COLUMN] = BOX_NONE,
    [CSS_DISPLAY_TABLE_CELL] = BOX_TABLE_CELL,
    [CSS_DISPLAY_TABLE_CAPTION] = BOX_INLINE,
    [CSS_DISPLAY_NONE] = BOX_NONE,
    [CSS_DISPLAY_FLEX] = BOX_FLEX,
    [CSS_DISPLAY_INLINE_FLEX] = BOX_INLINE_FLEX,
    [CSS_DISPLAY_GRID] = BOX_GRID,
    [CSS_DISPLAY_INLINE_GRID] = BOX_INLINE_GRID,
    [CSS_DISPLAY_FLOW_ROOT] = BOX_BLOCK,
};
_Static_assert(CSS_DISPLAY_FLOW_ROOT == 0x15, "css_display_e has new values — update box_map");


/* All exits, including cancellation and restyle restart, retire the ancestry
 * stack. The current traversal node is released by the caller. */
static void box_construct_ctx_free(struct box_construct_ctx *ctx)
{
    dom_node_unref(ctx->root_node);
    box_construct_region_destroy(ctx->region);
    free(ctx->ancestors);
    free(ctx);
}

/** Extract inherited properties from the construction ancestry, not live DOM
 * mappings. Called before entering an element or after leaving it. */
static void box_extract_properties(const struct box_construct_ctx *ctx, struct box_construct_props *props)
{
    memset(props, 0, sizeof(*props));
    props->node_is_root = ctx->region == NULL && ctx->depth == 0;
    if (ctx->depth != 0) {
        const struct box_construct_ancestor *parent = &ctx->ancestors[ctx->depth - 1];
        props->parent_style = parent->box->style;
        props->inherited_flags = parent->box->flags & IS_OPACITY_ZERO;
        props->href = parent->box->href;
        props->target = parent->box->target;
        props->title = parent->box->title;
        props->containing_block = parent->containing_block;
    }
    if (props->containing_block != NULL && props->containing_block->last != NULL &&
        props->containing_block->last->type == BOX_INLINE_CONTAINER)
        props->inline_container = props->containing_block->last;
}

static bool box_construct_push(struct box_construct_ctx *ctx, struct box *box, struct box *containing_block)
{
    if (ctx->depth == ctx->capacity) {
        size_t capacity = ctx->capacity == 0 ? 16 : ctx->capacity * 2;
        if (capacity < ctx->capacity || capacity > SIZE_MAX / sizeof(*ctx->ancestors))
            return false;
        struct box_construct_ancestor *ancestors = realloc(ctx->ancestors, capacity * sizeof(*ancestors));
        if (ancestors == NULL)
            return false;
        ctx->ancestors = ancestors;
        ctx->capacity = capacity;
    }
    ctx->ancestors[ctx->depth++] = (struct box_construct_ancestor){
        .box = box, .containing_block = box->type == BOX_INLINE || box->type == BOX_BR ? containing_block : box};
    return true;
}


/**
 * Get the style for an element.
 *
 * \param  c               content of type CONTENT_HTML that is being processed
 * \param  parent_style    style at this point in xml tree, or NULL for root
 * \param  root_style      root node's style, or NULL for root
 * \param  n               node in xml tree
 * \return  the new style, or NULL on memory exhaustion
 */
css_select_results *box_get_style(
    html_content *c, const css_computed_style *parent_style, const css_computed_style *root_style, dom_node *n)
{
    dom_string *s = NULL;
    css_stylesheet *inline_style = NULL;
    css_select_results *styles;
    nscss_select_ctx ctx;

    if (html_restyle_reuse_style(c, n, &styles) != NSERROR_OK)
        return NULL;
    if (styles != NULL)
        return styles;
    HTML_PROFILE(if (c->restyling) c->restyle_profile.styled_elements++);

    /* Firstly, construct inline stylesheet, if any */
    if (nsoption_bool(author_level_css)) {
        dom_exception err;
        err = dom_element_get_attribute(n, corestring_dom_style, &s);
        if (err != DOM_NO_ERR) {
            return NULL;
        }
    }

    if (s != NULL) {
        inline_style = nscss_create_inline_style((const uint8_t *)dom_string_data(s), dom_string_byte_length(s),
            c->encoding, nsurl_access(c->base_url), c->quirks != DOM_DOCUMENT_QUIRKS_MODE_NONE);

        dom_string_unref(s);

        if (inline_style == NULL)
            return NULL;
    }

    /* Populate selection context */
    ctx.ctx = c->select_ctx;
    ctx.quirks = (c->quirks == DOM_DOCUMENT_QUIRKS_MODE_FULL);
    ctx.base_url = c->base_url;
    ctx.universal = c->universal;
    ctx.root_style = root_style;
    ctx.parent_style = parent_style;

    /* Select style for element */
    styles = nscss_get_style(&ctx, n, &c->media, &c->unit_len_ctx, inline_style);

    /* No longer need inline style */
    if (inline_style != NULL)
        css_stylesheet_destroy(inline_style);

    return styles;
}


static int box_construct_object_destroy(struct box_construct_object *object)
{
    nsurl_unref(object->url);
    return 0;
}

/* exported function documented in html/box_construct.h */
bool box_construct_request_object(html_content *content, struct box_construct_region *region, nsurl *url,
    struct box *box, content_type permitted_types, bool background, uint32_t layer)
{
    if (region == NULL) {
        return background ? html_fetch_background(content, url, box, permitted_types, layer)
                          : html_fetch_object(content, url, box, permitted_types, false);
    }
    struct box_construct_object *object = talloc_zero(region, struct box_construct_object);
    if (object == NULL)
        return false;
    object->url = nsurl_ref(url);
    object->box = box;
    object->permitted_types = permitted_types;
    object->background = background;
    object->background_layer = layer;
    object->next = region->objects;
    talloc_set_destructor(object, box_construct_object_destroy);
    region->objects = object;
    return true;
}

/* exported function documented in html/box_construct.h */
bool box_construct_request_buffer(html_content *content, struct box_construct_region *region, const uint8_t *buffer,
    size_t length, const char *mime_type, struct box *box, content_type permitted_types)
{
    if (region == NULL)
        return html_fetch_object_buffer(content, buffer, length, mime_type, box, permitted_types);

    struct box_construct_object *object = talloc_zero(region, struct box_construct_object);
    if (object == NULL)
        return false;
    object->buffer = talloc_memdup(object, buffer, length);
    object->mime_type = talloc_strdup(object, mime_type);
    if (object->buffer == NULL || object->mime_type == NULL) {
        talloc_free(object);
        return false;
    }
    object->buffer_length = length;
    object->box = box;
    object->permitted_types = permitted_types;
    object->next = region->objects;
    region->objects = object;
    return true;
}

/**
 * Create a box from a CSS content item.
 *
 * This handles all content types defined in CSS 2.1 and CSS Generated Content:
 * - STRING: text content
 * - URI: images or font icons
 * - COUNTER/COUNTERS: counter values
 * - ATTR: attribute values
 * - quotes: open/close quote characters
 *
 * \param item      Content item to create box from
 * \param style     Computed style for the pseudo-element
 * \param ctx       Construction context with resource and allocation owners
 * \param node      DOM node for ATTR lookups (may be NULL)
 * \return          Box, or NULL on failure or unsupported type
 */
static struct box *create_content_box(const css_computed_content_item *item, const css_computed_style *style,
    struct box_construct_ctx *ctx, dom_node *node)
{
    html_content *content = ctx->content;
    struct box *box = NULL;

    switch (item->type) {
    case CSS_COMPUTED_CONTENT_STRING: {
        /* Text content - most common case */
        const char *text_data = lwc_string_data(item->data.string);
        size_t text_len = lwc_string_length(item->data.string);

        if (text_len == 0)
            return NULL;

        box = box_create(NULL, (css_computed_style *)style, false, NULL, NULL, NULL, NULL, ctx->bctx);
        if (box == NULL) {
            ctx->error = NSERROR_NOMEM;
            return NULL;
        }

        box->type = BOX_TEXT;
        box->text = talloc_strndup(ctx->bctx, text_data, text_len);
        if (box->text == NULL) {
            ctx->error = NSERROR_NOMEM;
            /* Can't free box here - relies on talloc cleanup */
            return NULL;
        }
        box->length = text_len;

        NSLOG(wisp, DEEPDEBUG, "create_content_box: STRING '%.*s'", (int)(text_len > 50 ? 50 : text_len), text_data);
        break;
    }

    case CSS_COMPUTED_CONTENT_URI: {
        /* URI content - fetch image and create object box.
         * Similar pattern to list-style-image handling. */
        nsurl *url;
        nserror error;

        error = nsurl_create(lwc_string_data(item->data.uri), &url);
        if (error != NSERROR_OK) {
            if (error == NSERROR_NOMEM)
                ctx->error = error;
            NSLOG(wisp, WARNING, "create_content_box: URI nsurl_create failed");
            break;
        }

        /* Create box to hold the image object */
        box = box_create(NULL, (css_computed_style *)style, false, NULL, NULL, NULL, NULL, ctx->bctx);
        if (box == NULL) {
            ctx->error = NSERROR_NOMEM;
            nsurl_unref(url);
            break;
        }

        /* Mark as replaced (image) and set type for inline context */
        box->type = BOX_INLINE;
        box->flags |= IS_REPLACED;

        /* Request the image now, or defer it with the detached candidate. */
        if (!box_construct_request_object(content, ctx->region, url, box, CONTENT_IMAGE, false, 0)) {
            ctx->error = NSERROR_NOMEM;
            NSLOG(wisp, WARNING, "create_content_box: URI request failed");
            nsurl_unref(url);
            /* Box allocation will be cleaned up by talloc */
            box = NULL;
            break;
        }

        nsurl_unref(url);
        NSLOG(wisp, DEEPDEBUG, "create_content_box: URI requested %s", lwc_string_data(item->data.uri));
        break;
    }

    case CSS_COMPUTED_CONTENT_COUNTER: {
        /* Counter - would need counter state tracking.
         * TODO: Implement counter support */
        NSLOG(wisp, DEEPDEBUG, "create_content_box: COUNTER (not implemented)");
        box = NULL;
        break;
    }

    case CSS_COMPUTED_CONTENT_COUNTERS: {
        /* Nested counters with separator
         * TODO: Implement counters support */
        NSLOG(wisp, DEEPDEBUG, "create_content_box: COUNTERS (not implemented)");
        box = NULL;
        break;
    }

    case CSS_COMPUTED_CONTENT_ATTR: {
        /* Attribute value - get from DOM node */
        if (node != NULL && item->data.attr != NULL) {
            dom_string *attr_value = NULL;
            dom_string *attr_name = NULL;
            dom_exception err;

            err = dom_string_create_interned(
                (const uint8_t *)lwc_string_data(item->data.attr), lwc_string_length(item->data.attr), &attr_name);

            if (err == DOM_NO_ERR && attr_name != NULL) {
                err = dom_element_get_attribute(node, attr_name, &attr_value);
                dom_string_unref(attr_name);

                if (err == DOM_NO_ERR && attr_value != NULL) {
                    const char *text_data = dom_string_data(attr_value);
                    size_t text_len = dom_string_length(attr_value);

                    if (text_len > 0) {
                        box = box_create(NULL, (css_computed_style *)style, false, NULL, NULL, NULL, NULL, ctx->bctx);
                        if (box == NULL)
                            ctx->error = NSERROR_NOMEM;
                        if (box != NULL) {
                            box->type = BOX_TEXT;
                            box->text = talloc_strndup(ctx->bctx, text_data, text_len);
                            box->length = text_len;
                            NSLOG(wisp, DEEPDEBUG, "create_content_box: ATTR '%.*s'",
                                (int)(text_len > 50 ? 50 : text_len), text_data);
                        }
                    }
                    dom_string_unref(attr_value);
                }
            }
        }
        break;
    }

    case CSS_COMPUTED_CONTENT_OPEN_QUOTE:
    case CSS_COMPUTED_CONTENT_CLOSE_QUOTE: {
        /* Quote characters - would need to check 'quotes' property.
         * Default quotes are typically " and '
         * TODO: Implement proper quote handling with nesting level */
        const char *quote;
        if (item->type == CSS_COMPUTED_CONTENT_OPEN_QUOTE) {
            quote = "\""; /* Default open quote */
        } else {
            quote = "\""; /* Default close quote */
        }

        box = box_create(NULL, (css_computed_style *)style, false, NULL, NULL, NULL, NULL, ctx->bctx);
        if (box == NULL)
            ctx->error = NSERROR_NOMEM;
        if (box != NULL) {
            box->type = BOX_TEXT;
            box->text = talloc_strdup(ctx->bctx, quote);
            box->length = strlen(quote);
            NSLOG(wisp, DEEPDEBUG, "create_content_box: %s_QUOTE",
                item->type == CSS_COMPUTED_CONTENT_OPEN_QUOTE ? "OPEN" : "CLOSE");
        }
        break;
    }

    case CSS_COMPUTED_CONTENT_NO_OPEN_QUOTE:
    case CSS_COMPUTED_CONTENT_NO_CLOSE_QUOTE:
        /* These affect quote nesting but produce no content */
        box = NULL;
        break;

    default:
        NSLOG(wisp, WARNING, "create_content_box: unknown type %d", item->type);
        box = NULL;
        break;
    }

    if (box != NULL && box->type == BOX_TEXT && box->text == NULL) {
        ctx->error = NSERROR_NOMEM;
        return NULL;
    }
    return box;
}


/**
 * Ensure an inline container exists for inline-level content.
 *
 * This helper creates or reuses an inline container. It checks if the
 * containing block already has an inline container as its last child
 * (e.g., from a ::before pseudo-element) and reuses it if so.
 *
 * \param containing_block  Parent block to contain the inline container
 * \param inline_container_ptr  Pointer to inline container (may be updated)
 * \param bctx              Box context for memory allocation
 * \return true on success, false on memory allocation failure
 */
static bool box_ensure_inline_container(struct box *containing_block, struct box **inline_container_ptr, void *bctx)
{
    if (*inline_container_ptr != NULL) {
        return true; /* Already have one */
    }

    /* Check if containing block's last child is an inline container */
    if (containing_block->last != NULL && containing_block->last->type == BOX_INLINE_CONTAINER) {
        *inline_container_ptr = containing_block->last;
        return true;
    }

    /* Create new inline container */
    struct box *ic = box_create(NULL, NULL, false, NULL, NULL, NULL, NULL, bctx);
    if (ic == NULL) {
        return false;
    }
    ic->type = BOX_INLINE_CONTAINER;
    box_add_child(containing_block, ic);
    *inline_container_ptr = ic;
    return true;
}


/**
 * Request background images for a box, staging them for detached construction.
 *
 * \param box      Box to fetch background for (must have style)
 * \param ctx      Construction and resource-binding policy
 * \return true on success, false on fetch failure
 */
static bool box_fetch_background(struct box *box, struct box_construct_ctx *ctx)
{
    if (box->style == NULL || !nsoption_bool(background_images))
        return true;
    uint32_t count = css_computed_background_layer_count(box->style);
    for (uint32_t i = 0; i < count; i++) {
        lwc_string *uri;
        if (css_computed_background_image_layer(box->style, i, &uri) != CSS_BACKGROUND_IMAGE_IMAGE || uri == NULL)
            continue;
        if (i > 0 && box->background_layers == NULL) {
            box->background_layers = talloc_zero_array(box, struct hlcache_handle *, count - 1);
            if (box->background_layers == NULL)
                return false;
        }
        nsurl *url;
        nserror error = nsurl_create(lwc_string_data(uri), &url);
        if (error == NSERROR_NOMEM)
            return false;
        if (error == NSERROR_OK) {
            bool ok = box_construct_request_object(ctx->content, ctx->region, url, box, image_types, true, i);
            nsurl_unref(url);
            if (!ok)
                return false;
        }
    }
    return true;
}

/* Flex/grid children are blockified and do not float. */
static bool box_parent_is_flex_or_grid(const struct box *parent)
{
    return parent != NULL &&
        (parent->type == BOX_FLEX || parent->type == BOX_INLINE_FLEX || parent->type == BOX_GRID ||
            parent->type == BOX_INLINE_GRID);
}

/* Choose a formatting type and a representation suitable for placement.
 * The computed CSS display remains unchanged. */
static box_type box_construct_type(const css_computed_style *style, const struct box *parent, bool root)
{
    if (root)
        return BOX_BLOCK;

    box_type type = box_map[ns_computed_display(style, false)];
    bool item = box_parent_is_flex_or_grid(parent);
    uint8_t position = css_computed_position(style);

    if (!item && (position == CSS_POSITION_ABSOLUTE || position == CSS_POSITION_FIXED)) {
        /* Keep out-of-flow boxes in the inline stream so they cannot split
         * a text line. The inline variants preserve their inner formatter;
         * layout uses the original display for the static-position anchor. */
        switch (type) {
        case BOX_BLOCK:
            return BOX_INLINE_BLOCK;
        case BOX_FLEX:
            return BOX_INLINE_FLEX;
        case BOX_GRID:
            return BOX_INLINE_GRID;
        default:
            break;
        }
    }

    if (item) {
        switch (type) {
        case BOX_INLINE_FLEX:
            return BOX_FLEX;
        case BOX_INLINE_GRID:
            return BOX_GRID;
        case BOX_INLINE:
        case BOX_INLINE_BLOCK:
        case BOX_TABLE_CELL:
        case BOX_TABLE_ROW:
        case BOX_TABLE_ROW_GROUP:
            return BOX_BLOCK;
        default:
            break;
        }
    }
    return type;
}

/* Insert ordinary and generated boxes by the same rules. In-flow blocks end
 * the current inline run; subsequent inline content starts a new container. */
static bool box_construct_insert(struct box *box, struct box *parent, struct box **inline_container, void *bctx)
{
    if (parent == NULL)
        return true; /* Document root. */

    uint8_t position = css_computed_position(box->style);
    bool positioned = position == CSS_POSITION_ABSOLUTE || position == CSS_POSITION_FIXED;
    bool item = box_parent_is_flex_or_grid(parent);
    uint8_t float_value = css_computed_float(box->style);
    bool floated = !item && !positioned && (float_value == CSS_FLOAT_LEFT || float_value == CSS_FLOAT_RIGHT);
    /* There is no inline table box type. Positioned tables can still use an
     * inline container for their anchor, while retaining table formatting. */
    bool inline_table = !item && positioned && box->type == BOX_TABLE;
    bool inline_level = box->type == BOX_INLINE || box->type == BOX_BR || box->type == BOX_INLINE_BLOCK ||
        box->type == BOX_INLINE_FLEX || box->type == BOX_INLINE_GRID || inline_table;

    if (inline_level || floated) {
        if (!box_ensure_inline_container(parent, inline_container, bctx))
            return false;
        if (floated) {
            struct box *flt = box_create(NULL, NULL, false, box->href, box->target, box->title, NULL, bctx);
            if (flt == NULL)
                return false;
            flt->type = float_value == CSS_FLOAT_LEFT ? BOX_FLOAT_LEFT : BOX_FLOAT_RIGHT;
            box_add_child(*inline_container, flt);
            box_add_child(flt, box);
        } else {
            box_add_child(*inline_container, box);
        }
    } else {
        box_add_child(parent, box);
        *inline_container = NULL;
    }
    return true;
}

static void box_construct_inline_end(struct box *box, struct box *container, struct box_construct_ctx *ctx)
{
    lwc_string *id = box->id == NULL ? NULL : lwc_string_ref(box->id);
    struct box *end = box_create(NULL, box->style, false, box->href, box->target, box->title, id, ctx->bctx);
    if (end == NULL) {
        if (id != NULL)
            lwc_string_unref(id);
        ctx->error = NSERROR_NOMEM;
        return;
    }
    end->type = BOX_INLINE_END;
    end->flags |= box->flags & IS_OPACITY_ZERO;
    box_add_child(container, end);
    box->inline_end = end;
    end->inline_end = box;
}

/* Generate content at the same traversal point as an equivalent DOM child:
 * before children for ::before, after children for ::after. Inline owners use
 * their containing block, just as their real children do. */
static void box_construct_generate(dom_node *n, struct box_construct_ctx *ctx, struct box *owner,
    struct box_construct_props *props, const css_computed_style *style)
{
    if (style == NULL || (owner->flags & (IS_REPLACED | CONTENTS_SKIPPED)))
        return;
    switch (owner->type) {
    case BOX_INLINE:
    case BOX_BLOCK:
    case BOX_INLINE_BLOCK:
    case BOX_FLEX:
    case BOX_INLINE_FLEX:
    case BOX_GRID:
    case BOX_INLINE_GRID:
        break;
    default:
        return;
    }

    const css_computed_content_item *item;
    uint8_t content_type = css_computed_content(style, &item);
    if (content_type == CSS_CONTENT_NORMAL || content_type == CSS_CONTENT_NONE)
        return;

    struct box *parent = owner->type == BOX_INLINE ? props->containing_block : owner;
    struct box *container = parent->last != NULL && parent->last->type == BOX_INLINE_CONTAINER ? parent->last : NULL;
    box_type type = box_construct_type(style, parent, false);
    if (type == BOX_NONE)
        return;

    struct box *gen = box_create(
        NULL, (css_computed_style *)style, false, owner->href, owner->target, owner->title, NULL, ctx->bctx);
    if (gen == NULL) {
        ctx->error = NSERROR_NOMEM;
        return;
    }
    gen->type = type;
    gen->flags |= owner->flags & IS_OPACITY_ZERO;
    if (!box_fetch_background(gen, ctx) || !box_construct_insert(gen, parent, &container, ctx->bctx)) {
        if (ctx->error == NSERROR_OK)
            ctx->error = NSERROR_NOMEM;
        return;
    }
    if (owner->type == BOX_INLINE)
        props->inline_container = container;
    if (css_computed_content_visibility(style) == CSS_CONTENT_VISIBILITY_HIDDEN &&
        html_content_visibility_applies(style, false)) {
        gen->flags |= CONTENTS_SKIPPED;
        return;
    }

    /* An empty string still generates the styled box (e.g. an empty icon).
     * Text and image items use one content path for all owner/display types. */
    struct box *text_container = type == BOX_INLINE ? container : NULL;
    for (; item != NULL && item->type != CSS_COMPUTED_CONTENT_NONE; item++) {
        struct box *child = create_content_box(item, style, ctx, n);
        if (ctx->error != NSERROR_OK)
            return;
        if (child == NULL)
            continue;
        if (!box_ensure_inline_container(gen, &text_container, ctx->bctx)) {
            ctx->error = NSERROR_NOMEM;
            return;
        }
        child->flags |= gen->flags & IS_OPACITY_ZERO;
        box_add_child(text_container, child);
    }
    if (type == BOX_INLINE)
        box_construct_inline_end(gen, container, ctx);
}


/**
 * Construct a list marker box
 *
 * \param box      Box to attach marker to
 * \param title    Current title attribute
 * \param ctx      Box construction context
 * \param parent   Current block-level container
 * \return true on success, false on memory exhaustion
 */
static bool box_construct_marker(struct box *box, const char *title, struct box_construct_ctx *ctx, struct box *parent)
{
    lwc_string *image_uri;
    struct box *marker;
    enum css_list_style_type_e list_style_type;

    marker = box_create(NULL, box->style, false, NULL, NULL, title, NULL, ctx->bctx);
    if (marker == false)
        return false;

    marker->type = BOX_BLOCK;

    list_style_type = css_computed_list_style_type(box->style);

    /** \todo marker content (list-style-type) */
    switch (list_style_type) {
    case CSS_LIST_STYLE_TYPE_DISC:
        /* 2022 BULLET */
        marker->text = (char *)"\342\200\242";
        marker->length = 3;
        break;

    case CSS_LIST_STYLE_TYPE_CIRCLE:
        /* 25CB WHITE CIRCLE */
        marker->text = (char *)"\342\227\213";
        marker->length = 3;
        break;

    case CSS_LIST_STYLE_TYPE_SQUARE:
        /* 25AA BLACK SMALL SQUARE */
        marker->text = (char *)"\342\226\252";
        marker->length = 3;
        break;

    default:
        /* Numerical list counters get handled in layout. */
        /* Fall through. */
    case CSS_LIST_STYLE_TYPE_NONE:
        marker->text = NULL;
        marker->length = 0;
        break;
    }

    if (css_computed_list_style_image(box->style, &image_uri) == CSS_LIST_STYLE_IMAGE_URI && (image_uri != NULL) &&
        (nsoption_bool(foreground_images) == true)) {
        nsurl *url;
        nserror error;


        /* TODO: we get a url out of libcss as a lwc string, but
         *       earlier we already had it as a nsurl after we
         *       nsurl_joined it.  Can this be improved?
         *       For now, just making another nsurl. */
        error = nsurl_create(lwc_string_data(image_uri), &url);
        if (error != NSERROR_OK)
            return false;

        if (!box_construct_request_object(ctx->content, ctx->region, url, marker, image_types, false, 0)) {
            nsurl_unref(url);
            return false;
        }
        nsurl_unref(url);
    }

    box->list_marker = marker;
    marker->parent = box;
    marker->flags |= box->flags & IS_OPACITY_ZERO;

    return true;
}

/* Initial detached boundary policy. Other types may need anonymous wrappers or
 * inline continuations outside the requested subtree. Even an eligible type
 * still needs closure validation before an eventual publication. */
static bool box_construct_region_style(box_type type, const css_computed_style *style)
{
    return style != NULL && (type == BOX_BLOCK || type == BOX_FLEX || type == BOX_GRID) &&
        css_computed_position(style) != CSS_POSITION_ABSOLUTE &&
        css_computed_position(style) != CSS_POSITION_FIXED && css_computed_float(style) == CSS_FLOAT_NONE;
}

static bool box_construct_region_root(const struct box *box)
{
    return box != NULL && box_construct_region_style(box->type, box->style);
}

/* Establish ownership during a normal full reconstruction, only for scopes
 * already selected by checked-state invalidation. Nested scopes share the outer
 * arena. This does not enable replacement or prove boundary closure. */
static void box_construct_begin_owner(
    struct box_construct_ctx *ctx, const struct box_construct_props *props, const css_computed_style *style)
{
    if (ctx->region != NULL || ctx->owner_node != NULL || !ctx->content->restyling ||
        !ctx->content->restyle.partial || ctx->depth == 0)
        return;
    const struct box *parent = ctx->ancestors[ctx->depth - 1].box;
    if (parent != props->containing_block || parent->node == NULL ||
        (parent->type != BOX_BLOCK && parent->type != BOX_FLEX && parent->type != BOX_GRID))
        return;
    for (uint32_t i = 0; i < ctx->content->restyle.active.count; i++) {
        if (ctx->content->restyle.active.roots[i] != ctx->n)
            continue;
        if (!box_construct_region_style(box_construct_type(style, parent, false), style))
            return;
        void *owner = talloc_zero(ctx->bctx, int);
        if (owner != NULL) {
            ctx->bctx = owner;
            ctx->owner_node = ctx->n;
        }
        /* Ownership is an optimization: allocation failure simply leaves this
         * scope under the document owner, requiring full rebuild next time. */
        return;
    }
}

void *box_construct_region_owner(const struct box *box)
{
    return box != NULL && (box->flags & OWNS_REGION) ? talloc_parent(box) : NULL;
}

/**
 * Construct the box tree for an XML element.
 *
 * \param ctx               Tree construction context
 * \param convert_children  Whether to convert children
 * \return  true on success, false on memory exhaustion
 */
static bool box_construct_element(struct box_construct_ctx *ctx, bool *convert_children)
{
    dom_string *title0, *s;
    lwc_string *id = NULL;
    struct box *box = NULL, *old_box;
    css_select_results *styles = NULL;
    lwc_string *bgimage_uri;
    dom_exception err;
    struct box_construct_props props;
    const css_computed_style *root_style = NULL;

    assert(ctx->n != NULL);

    box_extract_properties(ctx, &props);

    if (props.containing_block != NULL) {
        /* In case the containing block is a pre block, we clear
         * the PRE_STRIP flag since it is not used if we follow
         * the pre with a tag */
        props.containing_block->flags &= ~PRE_STRIP;
    }

    if (props.node_is_root == false) {
        root_style = ctx->document_root->style;
    }

    HTML_PROFILE_START(style_start);
    styles = box_get_style(ctx->content, props.parent_style, root_style, ctx->n);
    HTML_PROFILE(
        if (ctx->content->restyling) { ctx->content->restyle_profile.css_ns += html_profile_now() - style_start; });
    if (styles == NULL)
        goto error;

    box_construct_begin_owner(ctx, &props, styles->styles[CSS_PSEUDO_ELEMENT_NONE]);

    /* Extract title attribute, if present */
    err = dom_element_get_attribute(ctx->n, corestring_dom_title, &title0);
    if (err != DOM_NO_ERR)
        goto error;

    if (title0 != NULL) {
        char *t = squash_whitespace(dom_string_data(title0));

        dom_string_unref(title0);

        if (t == NULL)
            goto error;

        props.title = talloc_strdup(ctx->bctx, t);

        free(t);

        if (props.title == NULL)
            goto error;
    }

    /* Extract id attribute, if present */
    err = dom_element_get_attribute(ctx->n, corestring_dom_id, &s);
    if (err != DOM_NO_ERR)
        goto error;

    if (s != NULL) {
        err = dom_string_intern(s, &id);
        if (err != DOM_NO_ERR)
            id = NULL;

        dom_string_unref(s);
    }

    box = box_create(
        styles, styles->styles[CSS_PSEUDO_ELEMENT_NONE], false, props.href, props.target, props.title, id, ctx->bctx);
    if (box == NULL)
        goto error;

    /* Resource requests made by special elements also need DOM identity. */
    box->node = dom_node_ref(ctx->n);

    /* Opacity is not inherited CSS, but a transparent ancestor suppresses
     * all descendants, including those attached as inline siblings. */
    box->flags |= props.inherited_flags;

    /* If this is the root box, add it to the context */
    if (ctx->n == ctx->root_node)
        ctx->root_box = box;
    if (props.node_is_root)
        ctx->document_root = box;

    /* Deal with colspan/rowspan */
    err = dom_element_get_attribute(ctx->n, corestring_dom_colspan, &s);
    if (err != DOM_NO_ERR) {
        NSLOG(wisp, WARNING, "Failed to get colspan attribute");
        goto error;
    }

    if (s != NULL) {
        const char *val = dom_string_data(s);

        /* Convert to a number, clamping to [1,1000] according to 4.9.11
         */
        if ('0' <= val[0] && val[0] <= '9')
            box->columns = clamp(strtol(val, NULL, 10), 1, 1000);

        dom_string_unref(s);
    }

    err = dom_element_get_attribute(ctx->n, corestring_dom_rowspan, &s);
    if (err != DOM_NO_ERR) {
        NSLOG(wisp, WARNING, "Failed to get rowspan attribute");
        goto error;
    }

    if (s != NULL) {
        const char *val = dom_string_data(s);

        /* Convert to a number, clamping to [0,65534] according
         * to 4.9.11 */
        if ('0' <= val[0] && val[0] <= '9')
            box->rows = clamp(strtol(val, NULL, 10), 0, 65534);

        dom_string_unref(s);
    }

    box->type = box_construct_type(box->style, props.containing_block, props.node_is_root);

    ctx->error = convert_special_elements(ctx->n, ctx->content, box, convert_children, ctx->bctx, ctx->region);
    if (ctx->error != NSERROR_OK) {
        NSLOG(wisp, WARNING, "Failed to convert special elements");
        goto error;
    }

    if (box->type == BOX_NONE ||
        (ns_computed_display(box->style, props.node_is_root) == CSS_DISPLAY_NONE && props.node_is_root == false)) {
        css_select_results_destroy(styles);
        box->styles = NULL;
        box->style = NULL;

        /* Free associated gadget, if any. This handles both formless controls
         * and controls in a form's list. form_free_control sets box->gadget
         * to NULL via control->box->gadget = NULL. */
        if (box->gadget != NULL) {
            form_free_control(box->gadget);
            box->gadget = NULL;
        }

        /* Can't do this, because the lifetimes of boxes and gadgets
         * are inextricably linked. Fortunately, talloc will save us
         * (for now) */
        /* box_free_box(box); */

        *convert_children = false;

        return true;
    }

    if (ctx->region != NULL && ctx->n == ctx->root_node && !box_construct_region_root(box)) {
        ctx->error = NSERROR_NOT_IMPLEMENTED;
        goto error;
    }

    if (html_until_found(ctx->n))
        ctx->content->has_until_found = true;
    if (css_computed_content_visibility(box->style) == CSS_CONTENT_VISIBILITY_HIDDEN &&
        html_content_visibility_applies(box->style, (box->flags & IS_REPLACED) != 0)) {
        box->flags |= CONTENTS_SKIPPED;
        *convert_children = false;
        /* Special controls may already have constructed their internal boxes. */
        while (box->children != NULL) {
            struct box *child = box->children;
            box->children = child->next;
            box_free(child);
        }
        box->last = NULL;
    }

    /* Special handlers may change the provisional CSS box type. Such a scope
     * can keep its allocations grouped, but must not advertise a usable owner. */
    if (ctx->owner_node == ctx->n && box_construct_region_root(box))
        box->flags |= OWNS_REGION;

    if (!box_construct_push(ctx, box, props.containing_block))
        goto error;

    /* Detached candidates must leave the displayed tree's mappings intact. */
    if (ctx->region == NULL) {
        err = dom_node_set_user_data(ctx->n, corestring_dom___ns_key_box_node_data, box, NULL, (void *)&old_box);
        if (err != DOM_NO_ERR) {
            ctx->error = NSERROR_DOM;
            return false;
        }
    }

    /* Kick off fetch for any background image */
    if (!box_fetch_background(box, ctx)) {
        goto error;
    }

    if (*convert_children)
        box->flags |= CONVERT_CHILDREN;

    if (!(box->flags & CONTENTS_SKIPPED) &&
        ns_computed_display(box->style, props.node_is_root) == CSS_DISPLAY_LIST_ITEM) {
        if (!box_construct_marker(box, props.title, ctx, props.containing_block))
            goto error;
    }
    if (!box_construct_insert(box, props.containing_block, &props.inline_container, ctx->bctx))
        goto error;

    box_construct_generate(ctx->n, ctx, box, &props, box->styles->styles[CSS_PSEUDO_ELEMENT_BEFORE]);

    return true;

error:
    if (box != NULL) {
        if (ctx->root_box == box)
            ctx->root_box = NULL;
        box_free(box);
    } else {
        if (styles != NULL)
            css_select_results_destroy(styles);
        if (id != NULL)
            lwc_string_unref(id);
    }
    return false;
}


/**
 * Complete construction of the box tree for an element.
 *
 * \param n        DOM node to construct for
 * \param ctx      Construction context
 *
 * This will be called after all children of an element have been processed
 */
static void box_construct_element_after(dom_node *n, struct box_construct_ctx *ctx, struct box *box)
{
    struct box_construct_props props;

    assert(box != NULL);

    box_extract_properties(ctx, &props);

    box_construct_generate(n, ctx, box, &props, box->styles->styles[CSS_PSEUDO_ELEMENT_AFTER]);

    if (box->type == BOX_INLINE || box->type == BOX_BR) {
        bool has_children;
        if (dom_node_has_child_nodes(n, &has_children) != DOM_NO_ERR) {
            ctx->error = NSERROR_DOM;
            return;
        }

        /* Generated siblings also need to be enclosed by the owner's inline
         * end, even if the DOM element itself is empty. */
        has_children |= box->next != NULL || props.containing_block->last != box->parent;
        if (!has_children || (box->flags & CONVERT_CHILDREN) == 0)
            return;
        if (!box_ensure_inline_container(props.containing_block, &props.inline_container, ctx->bctx)) {
            ctx->error = NSERROR_NOMEM;
            return;
        }
        box_construct_inline_end(box, props.inline_container, ctx);
    }
}


/**
 * Find the next node in the DOM tree, completing element construction
 * where appropriate.
 *
 * \param ctx               Construction context and current node
 * \param convert_children  Whether to consider children of \a n
 * \return NSERROR_OK on success; ctx->n is NULL when complete.
 *         On failure, ctx->n still owns the current node reference.
 */
static nserror next_node(struct box_construct_ctx *ctx, bool convert_children)
{
    dom_node *n = ctx->n, *next = NULL;
    dom_exception err;

    if (convert_children) {
        err = dom_node_get_first_child(n, &next);
        if (err != DOM_NO_ERR)
            return NSERROR_DOM;
        if (next != NULL) {
            dom_node_unref(n);
            ctx->n = next;
            return NSERROR_OK;
        }
    }

    while (true) {
        /* Hidden/skipped elements and text nodes did not push an ancestor.
         * Pop before generating ::after: it needs the owner's parent context. */
        if (ctx->depth != 0 && ctx->ancestors[ctx->depth - 1].box->node == n) {
            struct box *box = ctx->ancestors[--ctx->depth].box;
            box_construct_element_after(n, ctx, box);
            if (ctx->error != NSERROR_OK)
                return ctx->error;
        }
        /* Include the boundary's ::after in its arena, then return siblings and
         * following text to the document owner. Also handles skipped roots. */
        if (n == ctx->owner_node) {
            ctx->bctx = ctx->content->bctx;
            ctx->owner_node = NULL;
        }
        if (n == ctx->root_node) {
            dom_node_unref(n);
            ctx->n = NULL;
            assert(ctx->depth == (ctx->region != NULL ? 1 : 0));
            return NSERROR_OK;
        }
        err = dom_node_get_next_sibling(n, &next);
        if (err != DOM_NO_ERR)
            return NSERROR_DOM;
        if (next == NULL) {
            err = dom_node_get_parent_node(n, &next);
            if (err != DOM_NO_ERR || next == NULL)
                return NSERROR_DOM;
            dom_node_unref(n);
            ctx->n = n = next;
        } else {
            dom_node_unref(n);
            ctx->n = next;
            return NSERROR_OK;
        }
    }
}


/**
 * Apply the CSS text-transform property to given text (Unicode-aware).
 *
 * \param  s	string to transform (UTF-8, will be modified in-place)
 * \param  len  length of s in bytes
 * \param  tt	transform type
 *
 * Note: This function handles multi-byte UTF-8 characters correctly.
 * For case transformations where the result has the same byte length
 * (which covers most Latin characters including Romanian diacritics),
 * the transformation is done in-place.
 */
void box_text_transform(char *s, unsigned int len, enum css_text_transform_e tt)
{
    size_t off = 0;
    bool prev_was_space = true; /* For capitalize: treat start as after space */

    if (len == 0)
        return;

    while (off < len) {
        size_t next_off = utf8_next(s, len, off);
        size_t char_len = next_off - off;
        uint32_t c = utf8_to_ucs4(s + off, char_len);
        uint32_t transformed = c;

        switch (tt) {
        case CSS_TEXT_TRANSFORM_UPPERCASE:
            transformed = towupper(c);
            break;
        case CSS_TEXT_TRANSFORM_LOWERCASE:
            transformed = towlower(c);
            break;
        case CSS_TEXT_TRANSFORM_CAPITALIZE:
            if (prev_was_space) {
                transformed = towupper(c);
            }
            /* Track if current char is whitespace for next iteration */
            prev_was_space = (c == ' ' || c == '\t' || c == '\n' || c == '\r');
            break;
        default:
            break;
        }

        /* Only modify if transformation changed the character */
        if (transformed != c) {
            char new_char[6];
            size_t new_len = utf8_from_ucs4(transformed, new_char);

            /* In-place replacement only works if byte length matches.
             * For most European languages (including Romanian), upper/lower
             * case variants have the same UTF-8 byte length. */
            if (new_len == char_len) {
                memcpy(s + off, new_char, new_len);
            }
            /* If lengths differ, skip this character (rare case) */
        }

        off = next_off;
    }
}


/**
 * Construct the box tree for an XML text node.
 *
 * \param  ctx  Tree construction context
 * \return  true on success, false on memory exhaustion
 */
static bool box_construct_text(struct box_construct_ctx *ctx)
{
    struct box_construct_props props;
    struct box *box = NULL;
    dom_string *content;
    dom_exception err;

    assert(ctx->n != NULL);

    box_extract_properties(ctx, &props);

    assert(props.containing_block != NULL);

    err = dom_characterdata_get_data(ctx->n, &content);
    if (err != DOM_NO_ERR || content == NULL)
        return false;

    if (css_computed_white_space(props.parent_style) == CSS_WHITE_SPACE_NORMAL ||
        css_computed_white_space(props.parent_style) == CSS_WHITE_SPACE_NOWRAP) {
        char *text;

        text = squash_whitespace(dom_string_data(content));

        dom_string_unref(content);

        if (text == NULL)
            return false;

        /* if the text is just a space, combine it with the preceding
         * text node, if any */
        if (text[0] == ' ' && text[1] == 0) {
            if (props.inline_container != NULL) {
                assert(props.inline_container->last != NULL);

                props.inline_container->last->space = UNKNOWN_WIDTH;
            }

            free(text);

            return true;
        }

        if (props.inline_container == NULL) {
            /* Child of a block without a current container
             * (i.e. this box is the first child of its parent, or
             * was preceded by block-level siblings) */

            /* DEBUG: Log when containing block doesn't have inline container */
            if (props.containing_block != NULL) {
                const char *tag = "";
                const char *cls = "";
                dom_string *name = NULL;
                dom_string *class_attr = NULL;
                if (props.containing_block->node != NULL) {
                    if (dom_node_get_node_name(props.containing_block->node, &name) == DOM_NO_ERR && name != NULL) {
                        tag = dom_string_data(name);
                    }
                    if (dom_element_get_attribute(props.containing_block->node, corestring_dom_class, &class_attr) ==
                            DOM_NO_ERR &&
                        class_attr != NULL) {
                        cls = dom_string_data(class_attr);
                    }
                }
                NSLOG(wisp, INFO, "TEXT_BOX: creating inline_container for text, parent: tag=%s class='%s' type=%d",
                    tag, cls, props.containing_block->type);
                if (name)
                    dom_string_unref(name);
                if (class_attr)
                    dom_string_unref(class_attr);
            }

            props.inline_container = box_create(NULL, NULL, false, NULL, NULL, NULL, NULL, ctx->bctx);
            if (props.inline_container == NULL) {
                free(text);
                return false;
            }

            props.inline_container->type = BOX_INLINE_CONTAINER;

            box_add_child(props.containing_block, props.inline_container);
        }

        /** \todo Dropping const here is not clever */
        box = box_create(NULL, (css_computed_style *)props.parent_style, false, props.href, props.target, props.title,
            NULL, ctx->bctx);
        if (box == NULL) {
            free(text);
            return false;
        }

        box->type = BOX_TEXT;
        /* Preserve source identity through line splitting and reveal restyles.
         * Hit testing resolves the enclosing element from this text node. */
        box->text_node = dom_node_ref(ctx->n);

        box->flags |= props.inherited_flags;
        box->text = talloc_strdup(ctx->bctx, text);
        free(text);
        if (box->text == NULL)
            return false;

        box->length = strlen(box->text);

        /* strip ending space char off */
        if (box->length > 1 && box->text[box->length - 1] == ' ') {
            box->space = UNKNOWN_WIDTH;
            box->length--;
        }

        if (css_computed_text_transform(props.parent_style) != CSS_TEXT_TRANSFORM_NONE)
            box_text_transform(box->text, box->length, css_computed_text_transform(props.parent_style));

        box_add_child(props.inline_container, box);

        if (box->text[0] == ' ') {
            box->length--;

            memmove(box->text, &box->text[1], box->length);

            if (box->prev != NULL)
                box->prev->space = UNKNOWN_WIDTH;
        }
    } else {
        /* white-space: pre */
        char *text;
        size_t text_len = dom_string_byte_length(content);
        size_t i;
        char *current;
        enum css_white_space_e white_space = css_computed_white_space(props.parent_style);

        /* note: pre-wrap/pre-line are unimplemented */
        assert(white_space == CSS_WHITE_SPACE_PRE || white_space == CSS_WHITE_SPACE_PRE_LINE ||
            white_space == CSS_WHITE_SPACE_PRE_WRAP);

        text = malloc(text_len + 1);
        dom_string_unref(content);

        if (text == NULL)
            return false;

        memcpy(text, dom_string_data(content), text_len);
        text[text_len] = '\0';

        /* TODO: Handle tabs properly */
        for (i = 0; i < text_len; i++)
            if (text[i] == '\t')
                text[i] = ' ';

        if (css_computed_text_transform(props.parent_style) != CSS_TEXT_TRANSFORM_NONE)
            box_text_transform(text, strlen(text), css_computed_text_transform(props.parent_style));

        current = text;

        /* swallow a single leading new line */
        if (props.containing_block->flags & PRE_STRIP) {
            switch (*current) {
            case '\n':
                current++;
                break;
            case '\r':
                current++;
                if (*current == '\n')
                    current++;
                break;
            }
            props.containing_block->flags &= ~PRE_STRIP;
        }

        do {
            size_t len = strcspn(current, "\r\n");

            char old = current[len];

            current[len] = 0;

            if (props.inline_container == NULL) {
                /* Child of a block without a current container
                 * (i.e. this box is the first child of its
                 * parent, or was preceded by block-level
                 * siblings) */
                props.inline_container = box_create(NULL, NULL, false, NULL, NULL, NULL, NULL, ctx->bctx);
                if (props.inline_container == NULL) {
                    free(text);
                    return false;
                }

                props.inline_container->type = BOX_INLINE_CONTAINER;

                box_add_child(props.containing_block, props.inline_container);
            }

            /** \todo Dropping const isn't clever */
            box = box_create(NULL, (css_computed_style *)props.parent_style, false, props.href, props.target,
                props.title, NULL, ctx->bctx);
            if (box == NULL) {
                free(text);
                return false;
            }

            box->type = BOX_TEXT;
            box->text_node = dom_node_ref(ctx->n);

            box->flags |= props.inherited_flags;
            box->text = talloc_strdup(ctx->bctx, current);
            if (box->text == NULL) {
                free(text);
                return false;
            }

            box->length = strlen(box->text);

            box_add_child(props.inline_container, box);

            current[len] = old;

            current += len;

            if (current[0] != '\0') {
                /* Linebreak: create new inline container */
                props.inline_container = box_create(NULL, NULL, false, NULL, NULL, NULL, NULL, ctx->bctx);
                if (props.inline_container == NULL) {
                    free(text);
                    return false;
                }

                props.inline_container->type = BOX_INLINE_CONTAINER;

                box_add_child(props.containing_block, props.inline_container);

                if (current[0] == '\r' && current[1] == '\n')
                    current += 2;
                else
                    current++;
            }
        } while (*current);

        free(text);
    }

    return true;
}


/* Convert one element plus intervening non-element nodes. Both traversal and
 * inherited state belong to ctx, independently of published DOM mappings. */
static bool box_construct_step(struct box_construct_ctx *ctx)
{
    bool convert_children = true;
    if (!box_construct_element(ctx, &convert_children) || ctx->error != NSERROR_OK)
        return false;
    if ((ctx->error = next_node(ctx, convert_children)) != NSERROR_OK)
        return false;
    while (ctx->n != NULL) {
        dom_node_type type;
        if (dom_node_get_node_type(ctx->n, &type) != DOM_NO_ERR) {
            ctx->error = NSERROR_DOM;
            return false;
        }
        if (type == DOM_ELEMENT_NODE)
            break;
        if (type == DOM_TEXT_NODE && !box_construct_text(ctx))
            return false;
        if ((ctx->error = next_node(ctx, true)) != NSERROR_OK)
            return false;
    }
    return true;
}


/* Detached candidates have no live resource bindings to detach. Retire all
 * temporary storage before notifying the owner, which may start another build. */
static void box_construct_region_complete(struct box_construct_ctx *ctx, nserror error)
{
    html_content *content = ctx->content;
    box_construct_region_cb cb = ctx->region_cb;
    void *pw = ctx->region_pw;
    struct box_construct_region *result = NULL;
    if (error == NSERROR_OK) {
        result = ctx->region;
        ctx->region = NULL;
    }
    dom_node_unref(ctx->n);
    box_construct_ctx_free(ctx);
    cb(content, error, result, pw);
}

static void box_construct_failed(struct box_construct_ctx *ctx)
{
    if (ctx->region != NULL) {
        box_construct_region_complete(ctx, ctx->error == NSERROR_OK ? NSERROR_NOMEM : ctx->error);
        return;
    }
    /* The callback detaches resource bindings before the partial tree retires. */
    ctx->cb(ctx->content, false);
    dom_node_unref(ctx->n);
    if (ctx->root_box != NULL)
        box_free(ctx->root_box);
    box_construct_ctx_free(ctx);
}

/* The current callback is already off the scheduler's queue. Release its
 * traversal state before starting a replacement; do not cancel a running slice. */
static void restart_restyle(struct box_construct_ctx *ctx)
{
    if (ctx->region != NULL) {
        box_construct_region_complete(ctx, NSERROR_INVALID);
        return;
    }
    html_content *content = ctx->content;
    dom_node_unref(ctx->n);
    box_construct_ctx_free(ctx);
    content->box_conversion_context = NULL;
    html_restyle_restart(content);
}

static void box_construct_region_normalise(struct box_construct_ctx *ctx)
{
    HTML_PROFILE_START(normalise_start);
    /* Placement used the real parent's type. This private wrapper only provides
     * the normalization entry point; it is never part of the result. */
    ctx->seed->type = BOX_BLOCK;
    if (!box_normalise_block(ctx->seed, ctx->document_root, ctx->content, ctx->bctx)) {
        box_construct_region_complete(ctx, NSERROR_NOMEM);
        return;
    }
    struct box *root = ctx->seed->children;
    if (root != NULL && (root != ctx->root_box || root->next != NULL)) {
        box_construct_region_complete(ctx, NSERROR_NOT_IMPLEMENTED);
        return;
    }
    ctx->region->root = root;
    if (root != NULL)
        root->parent = NULL;
    talloc_free(ctx->seed);
    HTML_PROFILE(if (ctx->content->restyling) {
        uint64_t elapsed = html_profile_now() - normalise_start;
        ctx->content->restyle_profile.normalise_ns += elapsed;
        ctx->content->restyle_profile.construction_ns += elapsed;
    });
    box_construct_region_complete(ctx, NSERROR_OK);
}

/**
 * Convert an ELEMENT node to a box tree fragment,
 * then schedule conversion of the next ELEMENT node
 */
static void convert_xml_to_box(void *p)
{
    HTML_PROFILE_START(slice_start);
    struct box_construct_ctx *ctx = p;
    if (html_restyle_needs_restart(ctx->content)) {
        restart_restyle(ctx);
        return;
    }
    HTML_PROFILE(if (ctx->content->restyling) ctx->content->restyle_profile.construction_slices++);
    uint32_t num_processed = 0;
    uint64_t start_time, now_time;

    nsu_getmonotonic_ms(&start_time);
    NSLOG(wisp, DEBUG, "PROFILER: START Box construction slice %p", ctx);

    do {
        assert(ctx->n != NULL);
        if (!box_construct_step(ctx)) {
            HTML_PROFILE(if (ctx->content->restyling)
                ctx->content->restyle_profile.construction_ns += html_profile_now() - slice_start);
            NSLOG(wisp, WARNING, "Box construction step failed");
            box_construct_failed(ctx);
            NSLOG(wisp, DEBUG, "PROFILER: STOP Box construction slice %p", ctx);
            return;
        }

        if (ctx->n == NULL) {
            if (html_restyle_needs_restart(ctx->content)) {
                HTML_PROFILE(ctx->content->restyle_profile.construction_ns += html_profile_now() - slice_start);
                restart_restyle(ctx);
                return;
            }
            if (ctx->region != NULL) {
                HTML_PROFILE(if (ctx->content->restyling)
                    ctx->content->restyle_profile.construction_ns += html_profile_now() - slice_start);
                box_construct_region_normalise(ctx);
                return;
            }
            /* Conversion complete */
            struct box root;

            memset(&root, 0, sizeof(root));

            root.type = BOX_BLOCK;
            root.children = root.last = ctx->root_box;
            root.children->parent = &root;

            /** \todo Remove box_normalise_block */
            HTML_PROFILE_START(normalise_start);
            bool normalised = box_normalise_block(&root, ctx->root_box, ctx->content, ctx->bctx);
            HTML_PROFILE(if (ctx->content->restyling) {
                uint64_t end = html_profile_now();
                ctx->content->restyle_profile.normalise_ns += end - normalise_start;
                ctx->content->restyle_profile.construction_ns += end - slice_start;
            });
            if (normalised == false) {
                NSLOG(wisp, WARNING, "box_normalise_block failed");
                ctx->cb(ctx->content, false);
                if (ctx->root_box != NULL)
                    box_free(ctx->root_box);
            } else {
                ctx->content->layout = root.children;
                ctx->content->layout->parent = NULL;

                ctx->cb(ctx->content, true);
            }

            assert(ctx->n == NULL);

            box_construct_ctx_free(ctx);
            NSLOG(wisp, DEBUG, "PROFILER: STOP Box construction slice %p", ctx);
            return;
        }

        /* Check for yield every 64 nodes */
        if ((++num_processed & 0x3F) == 0) {
#ifdef WISP_PROFILE_RESTYLE
            if (ctx->content->restyling && ctx->content->restyle.slow_restyle)
                break;
#endif
            nsu_getmonotonic_ms(&now_time);
            /* Yield if we've been running for more than 50ms */
            if (now_time - start_time > 50) {
                break;
            }
        }
    } while (true);

    NSLOG(wisp, DEBUG, "PROFILER: STOP Box construction slice %p", ctx);
    HTML_PROFILE(
        if (ctx->content->restyling) ctx->content->restyle_profile.construction_ns += html_profile_now() - slice_start);
    /* More work to do: schedule a continuation */
    int delay = 0;
#ifdef WISP_PROFILE_RESTYLE
    if (ctx->content->restyling && ctx->content->restyle.slow_restyle)
        delay = 50;
#endif
    if (guit->misc->schedule(delay, (void *)convert_xml_to_box, ctx) != NSERROR_OK)
        box_construct_failed(ctx);
}


/* exported function documented in html/box_construct.h */
nserror dom_to_box(dom_node *n, html_content *c, box_construct_complete_cb cb, void **box_conversion_context)
{
    struct box_construct_ctx *ctx;

    assert(box_conversion_context != NULL);

    if (c->bctx == NULL) {
        /* create a context allocation for this box tree */
        c->bctx = talloc_zero(0, int);
        if (c->bctx == NULL) {
            return NSERROR_NOMEM;
        }
    }

    ctx = calloc(1, sizeof(*ctx));
    if (ctx == NULL) {
        return NSERROR_NOMEM;
    }

    ctx->content = c;
    ctx->n = dom_node_ref(n);
    ctx->root_node = dom_node_ref(n);
    ctx->root_box = NULL;
    ctx->cb = cb;
    ctx->bctx = c->bctx;

    *box_conversion_context = ctx;

    nserror err = guit->misc->schedule(0, (void *)convert_xml_to_box, ctx);
    if (err != NSERROR_OK) {
        dom_node_unref(ctx->n);
        box_construct_ctx_free(ctx);
        *box_conversion_context = NULL;
    }
    return err;
}


/* exported function documented in html/box_construct.h */
void box_construct_region_destroy(struct box_construct_region *region)
{
    talloc_free(region);
}

nserror dom_to_box_region(dom_node *node, html_content *content, const struct box *parent, void *owner,
    box_construct_region_cb cb, void *pw, void **box_conversion_context)
{
    if (node == NULL || content == NULL || parent == NULL || cb == NULL || box_conversion_context == NULL)
        return NSERROR_BAD_PARAMETER;
    if (*box_conversion_context != NULL)
        return NSERROR_BAD_PARAMETER;
    dom_node_type type;
    if (dom_node_get_node_type(node, &type) != DOM_NO_ERR)
        return NSERROR_DOM;
    if (type != DOM_ELEMENT_NODE || content->layout == NULL || content->enable_scripting ||
        content->box_conversion_context != NULL || parent->node == NULL || parent->style == NULL ||
        (parent->type != BOX_BLOCK && parent->type != BOX_FLEX && parent->type != BOX_GRID))
        return NSERROR_NOT_IMPLEMENTED;
    dom_node *dom_parent = NULL;
    if (dom_node_get_parent_node(node, &dom_parent) != DOM_NO_ERR)
        return NSERROR_DOM;
    bool mapped_parent = dom_parent != NULL && dom_parent == parent->node && box_for_node(dom_parent) == parent;
    dom_node_unref(dom_parent);
    struct box *old = box_for_node(node);
    if (!mapped_parent || (old != NULL && (!box_construct_region_root(old) || old->parent != parent)))
        return NSERROR_NOT_IMPLEMENTED;

    struct box_construct_ctx *ctx = calloc(1, sizeof(*ctx));
    if (ctx == NULL)
        return NSERROR_NOMEM;
    ctx->content = content;
    ctx->n = dom_node_ref(node);
    ctx->root_node = dom_node_ref(node);
    ctx->document_root = content->layout;
    ctx->region_cb = cb;
    ctx->region_pw = pw;
    ctx->region = talloc_zero(owner, struct box_construct_region);
    if (ctx->region == NULL)
        goto no_memory;
    ctx->bctx = ctx->region;
    ctx->seed = box_create(NULL, parent->style, false, parent->href, parent->target, parent->title, NULL, ctx->bctx);
    if (ctx->seed == NULL)
        goto no_memory;
    ctx->seed->type = parent->type;
    ctx->seed->flags |= parent->flags & IS_OPACITY_ZERO;
    if (!box_construct_push(ctx, ctx->seed, NULL))
        goto no_memory;

    nserror error = guit->misc->schedule(0, (void *)convert_xml_to_box, ctx);
    if (error == NSERROR_OK) {
        *box_conversion_context = ctx;
        return NSERROR_OK;
    }
    dom_node_unref(ctx->n);
    box_construct_ctx_free(ctx);
    return error;

no_memory:
    dom_node_unref(ctx->n);
    box_construct_ctx_free(ctx);
    return NSERROR_NOMEM;
}

/* exported function documented in html/box_construct.h */
nserror cancel_dom_to_box(void *box_conversion_context)
{
    struct box_construct_ctx *ctx = box_conversion_context;
    nserror err;

    err = guit->misc->schedule(-1, (void *)convert_xml_to_box, ctx);
    if (err != NSERROR_OK) {
        return err;
    }

    dom_node_unref(ctx->n);
    box_construct_ctx_free(ctx);

    return NSERROR_OK;
}


/* exported function documented in html/box_construct.h */
struct box *box_for_node(dom_node *n)
{
    struct box *box = NULL;
    dom_exception err;

    err = dom_node_get_user_data(n, corestring_dom___ns_key_box_node_data, (void *)&box);
    if (err != DOM_NO_ERR)
        return NULL;

    return box;
}

/* exported function documented in html/box_construct.h */
bool box_extract_link(const html_content *content, const dom_string *dsrel, nsurl *base, nsurl **result)
{
    char *s, *s1, *apos0 = 0, *apos1 = 0, *quot0 = 0, *quot1 = 0;
    unsigned int i, j, end;
    nserror error;
    const char *rel;

    rel = dom_string_data(dsrel);

    s1 = s = malloc(3 * strlen(rel) + 1);
    if (!s)
        return false;

    /* copy to s, removing white space and control characters */
    for (i = 0; rel[i] && ascii_is_space(rel[i]); i++)
        ;
    for (end = strlen(rel); (end != i) && ascii_is_space(rel[end - 1]); end--)
        ;
    for (j = 0; i != end; i++) {
        if ((unsigned char)rel[i] < 0x20) {
            ; /* skip control characters */
        } else if (rel[i] == ' ') {
            s[j++] = '%';
            s[j++] = '2';
            s[j++] = '0';
        } else {
            s[j++] = rel[i];
        }
    }
    s[j] = 0;

    if (content->enable_scripting == false) {
        /* extract first quoted string out of "javascript:" link */
        if (strncmp(s, "javascript:", 11) == 0) {
            apos0 = strchr(s, '\'');
            if (apos0)
                apos1 = strchr(apos0 + 1, '\'');
            quot0 = strchr(s, '"');
            if (quot0)
                quot1 = strchr(quot0 + 1, '"');
            if (apos0 && apos1 && (!quot0 || !quot1 || apos0 < quot0)) {
                *apos1 = 0;
                s1 = apos0 + 1;
            } else if (quot0 && quot1) {
                *quot1 = 0;
                s1 = quot0 + 1;
            }
        }
    }

    /* construct absolute URL */
    error = nsurl_join(base, s1, result);
    free(s);
    if (error != NSERROR_OK) {
        *result = NULL;
        return false;
    }

    return true;
}

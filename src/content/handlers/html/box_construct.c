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

/**
 * Context for box tree construction
 */
struct box_construct_ctx {
    html_content *content; /**< Content we're constructing for */

    dom_node *n; /**< Current node to process */

    struct box *root_box; /**< Root box in the tree */

    box_construct_complete_cb cb; /**< Callback to invoke on completion */

    int *bctx; /**< talloc context */
};

/**
 * Transient properties for construction of current node
 */
struct box_construct_props {
    /** Style from which to inherit, or NULL if none */
    const css_computed_style *parent_style;
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
static const box_type box_map[CSS_DISPLAY_INLINE_GRID + 1] = {
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
};
_Static_assert(CSS_DISPLAY_INLINE_GRID == 0x14, "css_display_e has new values — update box_map");


/**
 * determine if a box is the root node
 *
 * \param n node to check
 * \return true if node is root else false.
 */
static inline bool box_is_root(dom_node *n)
{
    dom_node *parent;
    dom_node_type type;
    dom_exception err;

    err = dom_node_get_parent_node(n, &parent);
    if (err != DOM_NO_ERR)
        return false;

    if (parent != NULL) {
        err = dom_node_get_node_type(parent, &type);

        dom_node_unref(parent);

        if (err != DOM_NO_ERR)
            return false;

        if (type != DOM_DOCUMENT_NODE)
            return false;
    }

    return true;
}

/**
 * Extract transient construction properties
 *
 * \param n      Current DOM node to convert
 * \param props  Property object to populate
 */
static void box_extract_properties(dom_node *n, struct box_construct_props *props)
{
    memset(props, 0, sizeof(*props));

    props->node_is_root = box_is_root(n);

    /* Extract properties from containing DOM node */
    if (props->node_is_root == false) {
        dom_node *current_node = n;
        dom_node *parent_node = NULL;
        struct box *parent_box;
        dom_exception err;

        /* Find ancestor node containing parent box */
        while (true) {
            err = dom_node_get_parent_node(current_node, &parent_node);
            if (err != DOM_NO_ERR || parent_node == NULL)
                break;

            parent_box = box_for_node(parent_node);

            if (parent_box != NULL) {
                props->parent_style = parent_box->style;
                props->href = parent_box->href;
                props->target = parent_box->target;
                props->title = parent_box->title;

                dom_node_unref(parent_node);
                break;
            } else {
                if (current_node != n)
                    dom_node_unref(current_node);
                current_node = parent_node;
                parent_node = NULL;
            }
        }

        /* Find containing block (may be parent) */
        while (true) {
            struct box *b;

            err = dom_node_get_parent_node(current_node, &parent_node);
            if (err != DOM_NO_ERR || parent_node == NULL) {
                if (current_node != n)
                    dom_node_unref(current_node);
                break;
            }

            if (current_node != n)
                dom_node_unref(current_node);

            b = box_for_node(parent_node);

            /* Children of nodes that created an inline box
             * will generate boxes which are attached as
             * _siblings_ of the box generated for their
             * parent node. Note, however, that we'll still
             * use the parent node's styling as the parent
             * style, above. */
            if (b != NULL && b->type != BOX_INLINE && b->type != BOX_BR) {
                props->containing_block = b;

                dom_node_unref(parent_node);
                break;
            } else {
                current_node = parent_node;
                parent_node = NULL;
            }
        }
    }

    /* Compute current inline container, if any */
    if (props->containing_block != NULL && props->containing_block->last != NULL &&
        props->containing_block->last->type == BOX_INLINE_CONTAINER)
        props->inline_container = props->containing_block->last;
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
static css_select_results *box_get_style(
    html_content *c, const css_computed_style *parent_style, const css_computed_style *root_style, dom_node *n)
{
    dom_string *s = NULL;
    css_stylesheet *inline_style = NULL;
    css_select_results *styles;
    nscss_select_ctx ctx;

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
 * \param content   HTML content for memory allocation
 * \param node      DOM node for ATTR lookups (may be NULL)
 * \return          Box, or NULL on failure or unsupported type
 */
static struct box *create_content_box(
    const css_computed_content_item *item, const css_computed_style *style, html_content *content, dom_node *node)
{
    struct box *box = NULL;

    switch (item->type) {
    case CSS_COMPUTED_CONTENT_STRING: {
        /* Text content - most common case */
        const char *text_data = lwc_string_data(item->data.string);
        size_t text_len = lwc_string_length(item->data.string);

        if (text_len == 0)
            return NULL;

        box = box_create(NULL, (css_computed_style *)style, false, NULL, NULL, NULL, NULL, content->bctx);
        if (box == NULL)
            return NULL;

        box->type = BOX_TEXT;
        box->text = talloc_strndup(content->bctx, text_data, text_len);
        if (box->text == NULL) {
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
            NSLOG(wisp, WARNING, "create_content_box: URI nsurl_create failed");
            break;
        }

        /* Create box to hold the image object */
        box = box_create(NULL, (css_computed_style *)style, false, NULL, NULL, NULL, NULL, content->bctx);
        if (box == NULL) {
            nsurl_unref(url);
            break;
        }

        /* Mark as replaced (image) and set type for inline context */
        box->type = BOX_INLINE;
        box->flags |= IS_REPLACED;

        /* Start async fetch - box->object will be set when done */
        if (html_fetch_object(content, url, box, CONTENT_IMAGE, false) == false) {
            NSLOG(wisp, WARNING, "create_content_box: URI html_fetch_object failed");
            nsurl_unref(url);
            /* Box allocation will be cleaned up by talloc */
            box = NULL;
            break;
        }

        nsurl_unref(url);
        NSLOG(wisp, DEEPDEBUG, "create_content_box: URI started fetch for %s", lwc_string_data(item->data.uri));
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
                        box = box_create(
                            NULL, (css_computed_style *)style, false, NULL, NULL, NULL, NULL, content->bctx);
                        if (box != NULL) {
                            box->type = BOX_TEXT;
                            box->text = talloc_strndup(content->bctx, text_data, text_len);
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

        box = box_create(NULL, (css_computed_style *)style, false, NULL, NULL, NULL, NULL, content->bctx);
        if (box != NULL) {
            box->type = BOX_TEXT;
            box->text = talloc_strdup(content->bctx, quote);
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
static bool box_ensure_inline_container(struct box *containing_block, struct box **inline_container_ptr, int *bctx)
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
 * Add box to parent with optional float wrapping.
 *
 * If the box has float:left or float:right (and is not a flex child),
 * wraps it in a BOX_FLOAT_LEFT/RIGHT box before adding to the inline container.
 * Otherwise, adds directly to the parent.
 *
 * \param box              Box to add
 * \param parent           Parent to add to (inline_container or containing_block)
 * \param bctx             Box context for memory allocation
 * \param is_flex_child    True if parent is flex/grid (floats don't apply)
 * \return true on success, false on memory allocation failure
 */
static bool box_add_with_float_wrap(struct box *box, struct box *parent, int *bctx, bool is_flex_child)
{
    if (box->style == NULL) {
        box_add_child(parent, box);
        return true;
    }

    uint8_t float_val = css_computed_float(box->style);
    bool is_floated = !is_flex_child && (float_val == CSS_FLOAT_LEFT || float_val == CSS_FLOAT_RIGHT);

    if (is_floated) {
        struct box *flt = box_create(NULL, NULL, false, NULL, NULL, NULL, NULL, bctx);
        if (flt == NULL) {
            return false;
        }
        flt->type = (float_val == CSS_FLOAT_LEFT) ? BOX_FLOAT_LEFT : BOX_FLOAT_RIGHT;
        box_add_child(parent, flt);
        box_add_child(flt, box);
    } else {
        box_add_child(parent, box);
    }

    return true;
}


/**
 * Fetch background image for a box if specified in its style.
 *
 * \param box      Box to fetch background for (must have style)
 * \param content  HTML content for resource fetching
 * \return true on success, false on fetch failure
 */
static bool box_fetch_background(struct box *box, html_content *content)
{
    lwc_string *bgimage_uri;

    if (box->style == NULL) {
        return true;
    }

    if (css_computed_background_image(box->style, &bgimage_uri) == CSS_BACKGROUND_IMAGE_IMAGE && bgimage_uri != NULL &&
        nsoption_bool(background_images) == true) {
        nsurl *url;
        nserror error;

        error = nsurl_create(lwc_string_data(bgimage_uri), &url);
        if (error == NSERROR_OK) {
            if (html_fetch_object(content, url, box, image_types, true) == false) {
                NSLOG(wisp, WARNING, "box_fetch_background: Failed to fetch background image");
                nsurl_unref(url);
                return false;
            }
            nsurl_unref(url);
        }
    }

    return true;
}


/**
 * Check if a box type needs an inline container.
 *
 * \param box_type      The box type to check
 * \param is_floated    Whether the box has float:left or float:right
 * \return true if box needs inline container, false otherwise
 */
static inline bool box_needs_inline_container(box_type type, bool is_floated)
{
    return type == BOX_INLINE || type == BOX_BR || type == BOX_INLINE_BLOCK || type == BOX_INLINE_FLEX ||
        type == BOX_INLINE_GRID || is_floated;
}


/**
 * Construct the box required for a generated element.
 *
 * \param n        XML node of type XML_ELEMENT_NODE
 * \param content  Content of type CONTENT_HTML that is being processed
 * \param box      Box which may have generated content
 * \param style    Complete computed style for pseudo element, or NULL
 *
 * This function handles ::before and ::after pseudo-elements by:
 * 1. Creating a box for the pseudo-element itself
 * 2. Processing the 'content' property to create child text boxes
 */
static void box_construct_generate(dom_node *n, html_content *content, struct box *box, const css_computed_style *style)
{
    struct box *gen = NULL;
    struct box *inline_container = NULL;
    enum css_display_e computed_display;
    const css_computed_content_item *c_item;
    uint8_t content_type;

    /* Generated content can be added to container box types that can have children.
     * Block-level and inline-level containers that establish formatting contexts
     * can have ::before/::after pseudo-elements per CSS spec.
     *
     * Note: BOX_INLINE is NOT supported here because inline boxes in this
     * codebase have a different structure (text stored directly on box, not
     * as children). Inline elements are handled separately in box_construct_element. */
    switch (box->type) {
    case BOX_BLOCK:
    case BOX_INLINE_BLOCK:
    case BOX_FLEX:
    case BOX_INLINE_FLEX:
    case BOX_GRID:
    case BOX_INLINE_GRID:
        /* These can have generated content children */
        break;
    default:
        /* Other box types (BOX_INLINE, TABLE_*, FLOAT_*, etc.) cannot directly
         * have generated content in the current implementation */
        return;
    }

    /* To determine if an element has a pseudo element, we select
     * for it and test to see if the returned style's content
     * property is set to normal. */
    if (style == NULL)
        return;

    content_type = css_computed_content(style, &c_item);
    if (content_type == CSS_CONTENT_NORMAL || content_type == CSS_CONTENT_NONE)
        return;

    /* create box for this element */
    computed_display = ns_computed_display(style, box_is_root(n));

    /** \todo Not wise to drop const from the computed style */
    gen = box_create(NULL, (css_computed_style *)style, false, NULL, NULL, NULL, NULL, content->bctx);
    if (gen == NULL) {
        return;
    }

    /* set box type from computed display */
    gen->type = box_map[computed_display];

    /* Skip BOX_NONE - display:none pseudo-elements should not be added */
    if (gen->type == BOX_NONE) {
        return;
    }

    /* Fetch background image for pseudo-element */
    if (!box_fetch_background(gen, content)) {
        return;
    }

    /* Check if we need an inline container */
    uint8_t float_val = css_computed_float(style);
    bool is_floated = (float_val == CSS_FLOAT_LEFT || float_val == CSS_FLOAT_RIGHT);

    if (box_needs_inline_container(gen->type, is_floated)) {
        /* Ensure inline container exists */
        if (!box_ensure_inline_container(box, &inline_container, content->bctx)) {
            return;
        }
        /* Add with float wrapping if needed */
        if (!box_add_with_float_wrap(gen, inline_container, content->bctx, false)) {
            return;
        }
    } else {
        /* Block-level: add directly to parent */
        box_add_child(box, gen);
    }

    /* Now process the content property items */
    if (c_item != NULL) {
        while (c_item->type != CSS_COMPUTED_CONTENT_NONE) {
            if (c_item->type == CSS_COMPUTED_CONTENT_STRING) {
                /* Create a text box for the string content */
                const char *text_data = lwc_string_data(c_item->data.string);
                size_t text_len = lwc_string_length(c_item->data.string);

                if (text_len > 0) {
                    if (gen->type == BOX_INLINE) {
                        /* For inline boxes, text goes
                         * directly on the box */
                        char *text_copy = talloc_strndup(content->bctx, text_data, text_len);
                        if (text_copy == NULL) {
                            break;
                        }
                        gen->text = text_copy;
                        gen->length = text_len;
                    } else {
                        /* For block boxes, create
                         * inline container + text box
                         */
                        struct box *text_container;
                        struct box *text_box;
                        char *text_copy;

                        /* Create inline container if
                         * needed */
                        if (gen->last != NULL && gen->last->type == BOX_INLINE_CONTAINER) {
                            text_container = gen->last;
                        } else {
                            text_container = box_create(NULL, NULL, false, NULL, NULL, NULL, NULL, content->bctx);
                            if (text_container == NULL) {
                                break;
                            }
                            text_container->type = BOX_INLINE_CONTAINER;
                            box_add_child(gen, text_container);
                        }

                        /* Create text box */
                        text_box = box_create(
                            NULL, (css_computed_style *)style, false, NULL, NULL, NULL, NULL, content->bctx);
                        if (text_box == NULL) {
                            break;
                        }

                        text_box->type = BOX_TEXT;

                        text_copy = talloc_strndup(content->bctx, text_data, text_len);
                        if (text_copy == NULL) {
                            break;
                        }

                        text_box->text = text_copy;
                        text_box->length = text_len;

                        box_add_child(text_container, text_box);
                    }
                }
            }
            /* TODO: Handle CSS_COMPUTED_CONTENT_URI for images */
            c_item++;
        }
    }
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

        if (html_fetch_object(ctx->content, url, marker, image_types, false) == false) {
            nsurl_unref(url);
            return false;
        }
        nsurl_unref(url);
    }

    box->list_marker = marker;
    marker->parent = box;

    return true;
}

static inline bool box__style_is_float(const struct box *box)
{
    return css_computed_float(box->style) == CSS_FLOAT_LEFT || css_computed_float(box->style) == CSS_FLOAT_RIGHT;
}

static inline bool box__is_flex(const struct box *box)
{
    return box->type == BOX_FLEX || box->type == BOX_INLINE_FLEX;
}

static inline bool box__containing_block_is_flex(const struct box_construct_props *props)
{
    return props->containing_block != NULL && box__is_flex(props->containing_block);
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
    enum css_display_e css_display;
    struct box *box = NULL, *old_box;
    css_select_results *styles = NULL;
    lwc_string *bgimage_uri;
    dom_exception err;
    struct box_construct_props props;
    const css_computed_style *root_style = NULL;

    assert(ctx->n != NULL);

    box_extract_properties(ctx->n, &props);

    if (props.containing_block != NULL) {
        /* In case the containing block is a pre block, we clear
         * the PRE_STRIP flag since it is not used if we follow
         * the pre with a tag */
        props.containing_block->flags &= ~PRE_STRIP;
    }

    if (props.node_is_root == false) {
        root_style = ctx->root_box->style;
    }

    styles = box_get_style(ctx->content, props.parent_style, root_style, ctx->n);
    if (styles == NULL)
        return false;

    /* Extract title attribute, if present */
    err = dom_element_get_attribute(ctx->n, corestring_dom_title, &title0);
    if (err != DOM_NO_ERR)
        return false;

    if (title0 != NULL) {
        char *t = squash_whitespace(dom_string_data(title0));

        dom_string_unref(title0);

        if (t == NULL)
            return false;

        props.title = talloc_strdup(ctx->bctx, t);

        free(t);

        if (props.title == NULL)
            return false;
    }

    /* Extract id attribute, if present */
    err = dom_element_get_attribute(ctx->n, corestring_dom_id, &s);
    if (err != DOM_NO_ERR)
        return false;

    if (s != NULL) {
        err = dom_string_intern(s, &id);
        if (err != DOM_NO_ERR)
            id = NULL;

        dom_string_unref(s);
    }

    box = box_create(
        styles, styles->styles[CSS_PSEUDO_ELEMENT_NONE], false, props.href, props.target, props.title, id, ctx->bctx);
    if (box == NULL)
        return false;

    /* If this is the root box, add it to the context */
    if (props.node_is_root)
        ctx->root_box = box;

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

    css_display = ns_computed_display_static(box->style);

    /* Set box type from computed display */
    if ((css_computed_position(box->style) == CSS_POSITION_ABSOLUTE ||
            css_computed_position(box->style) == CSS_POSITION_FIXED) &&
        (css_display == CSS_DISPLAY_INLINE || css_display == CSS_DISPLAY_INLINE_BLOCK ||
            css_display == CSS_DISPLAY_INLINE_TABLE || css_display == CSS_DISPLAY_INLINE_FLEX)) {
        /* Special case for absolute positioning: make absolute inlines
         * into inline block so that the boxes are constructed in an
         * inline container as if they were not absolutely positioned.
         * Layout expects and handles this. */
        box->type = box_map[CSS_DISPLAY_INLINE_BLOCK];
    } else if (props.node_is_root) {
        /* Special case for root element: force it to BLOCK, or the
         * rest of the layout will break. */
        box->type = BOX_BLOCK;
    } else {
        /* Normal mapping */
        box->type = box_map[ns_computed_display(box->style, props.node_is_root)];

        NSLOG(wisp, INFO, "box_construct: display %d map_type %d mapped from %d",
            ns_computed_display(box->style, props.node_is_root), box->type,
            ns_computed_display(box->style, props.node_is_root));

        if (props.containing_block->type == BOX_FLEX || props.containing_block->type == BOX_INLINE_FLEX ||
            props.containing_block->type == BOX_GRID || props.containing_block->type == BOX_INLINE_GRID) {
            /* Blockification per CSS Flexbox spec §4, CSS Grid spec, and CSS Display 3 §2.7:
             * In-flow children of flex/grid containers are blockified.
             * This means display:inline becomes display:block, etc.
             * Layout-internal boxes (table-cell, table-row, etc.) also become block.
             * This must happen BEFORE anonymous box creation. */
            switch (box->type) {
            case BOX_INLINE_FLEX:
                box->type = BOX_FLEX;
                break;
            case BOX_INLINE_GRID:
                box->type = BOX_GRID;
                break;
            case BOX_INLINE_BLOCK:
            case BOX_INLINE:
            case BOX_TABLE_CELL:
            case BOX_TABLE_ROW:
            case BOX_TABLE_ROW_GROUP:
                /* Layout-internal boxes blockified to block per CSS Display 3 §2.7 */
                box->type = BOX_BLOCK;
                break;
            default:
                break;
            }
        }
    }

    if (convert_special_elements(ctx->n, ctx->content, box, convert_children) == false) {
        NSLOG(wisp, WARNING, "Failed to convert special elements");
        goto error;
    }

    /* Handle the :before pseudo element */
    if (!(box->flags & IS_REPLACED)) {
        box_construct_generate(ctx->n, ctx->content, box, box->styles->styles[CSS_PSEUDO_ELEMENT_BEFORE]);
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

    /* Attach DOM node to box */
    err = dom_node_set_user_data(ctx->n, corestring_dom___ns_key_box_node_data, box, NULL, (void *)&old_box);
    if (err != DOM_NO_ERR)
        return false;

    /* Attach box to DOM node */
    box->node = dom_node_ref(ctx->n);

    if (props.inline_container == NULL &&
        (box->type == BOX_INLINE || box->type == BOX_BR || box->type == BOX_INLINE_BLOCK ||
            box->type == BOX_INLINE_FLEX || box->type == BOX_INLINE_GRID ||
            (box__style_is_float(box) && !box__containing_block_is_flex(&props))) &&
        props.node_is_root == false) {
        /* Found an inline child of a block without a current container
         * (i.e. this box is the first child of its parent, or was
         * preceded by block-level siblings) */
        assert(props.containing_block != NULL && "Box must have containing block.");

        /* Use helper to ensure inline container exists (may reuse from ::before) */
        if (!box_ensure_inline_container(props.containing_block, &props.inline_container, ctx->bctx)) {
            NSLOG(wisp, WARNING, "Failed to create inline container box");
            goto error;
        }
    }

    /* Kick off fetch for any background image */
    if (!box_fetch_background(box, ctx->content)) {
        goto error;
    }

    if (*convert_children)
        box->flags |= CONVERT_CHILDREN;

    if (box->type == BOX_INLINE || box->type == BOX_BR || box->type == BOX_INLINE_FLEX ||
        box->type == BOX_INLINE_BLOCK || box->type == BOX_INLINE_GRID) {
        /* Inline container must exist, as we'll have
         * created it above if it didn't */
        assert(props.inline_container != NULL);

        box_add_child(props.inline_container, box);
    } else {
        if (ns_computed_display(box->style, props.node_is_root) == CSS_DISPLAY_LIST_ITEM) {
            /* List item: compute marker */
            if (box_construct_marker(box, props.title, ctx, props.containing_block) == false) {
                NSLOG(wisp, WARNING, "Failed to construct list marker");
                goto error;
            }
        }

        if (props.node_is_root == false && box__containing_block_is_flex(&props) == false &&
            (css_computed_float(box->style) == CSS_FLOAT_LEFT || css_computed_float(box->style) == CSS_FLOAT_RIGHT)) {
            /* Float: insert a float between the parent and box. */
            struct box *flt = box_create(NULL, NULL, false, props.href, props.target, props.title, NULL, ctx->bctx);
            if (flt == NULL) {
                NSLOG(wisp, WARNING, "Failed to create float box");
                goto error;
            }

            if (css_computed_float(box->style) == CSS_FLOAT_LEFT)
                flt->type = BOX_FLOAT_LEFT;
            else
                flt->type = BOX_FLOAT_RIGHT;

            box_add_child(props.inline_container, flt);
            box_add_child(flt, box);
        } else {
            /* Non-floated block-level box: add to containing block
             * if there is one. If we're the root box, then there
             * won't be. */
            if (props.containing_block != NULL)
                box_add_child(props.containing_block, box);
        }
    }

    return true;

error:
    if (box != NULL) {
        if (ctx->root_box == box)
            ctx->root_box = NULL;
        box_free(box);
    }
    return false;
}


/**
 * Complete construction of the box tree for an element.
 *
 * \param n        DOM node to construct for
 * \param content  Containing document
 *
 * This will be called after all children of an element have been processed
 */
static void box_construct_element_after(dom_node *n, html_content *content)
{
    struct box_construct_props props;
    struct box *box = box_for_node(n);

    assert(box != NULL);

    box_extract_properties(n, &props);

    /* TODO: Handle ::before pseudo-element for inline boxes.
     * This is disabled for now because:
     * 1. It only handles STRING content, not URI/COUNTER/ATTR/etc.
     * 2. The layout code needs more work to properly handle styled BOX_TEXT.
     *
     * Proper implementation requires:
     * - Handle all content types (STRING, URI, COUNTER, ATTR, quotes)
     * - Use BOX_INLINE wrapper for margins to work correctly
     * - Normalization flattens children to siblings in correct order
     */
    if (box->type == BOX_INLINE && !(box->flags & IS_REPLACED) && box->styles != NULL &&
        box->styles->styles[CSS_PSEUDO_ELEMENT_BEFORE] != NULL) {
        const css_computed_style *before_style = box->styles->styles[CSS_PSEUDO_ELEMENT_BEFORE];
        const css_computed_content_item *c_item;
        uint8_t content_type = css_computed_content(before_style, &c_item);

        if (content_type != CSS_CONTENT_NORMAL && content_type != CSS_CONTENT_NONE && c_item != NULL) {
            /* Create BOX_INLINE wrapper - this gets margins/padding from the style */
            struct box *pseudo_box = box_create(
                NULL, (css_computed_style *)before_style, false, NULL, NULL, NULL, NULL, content->bctx);

            if (pseudo_box != NULL) {
                pseudo_box->type = BOX_INLINE;
                bool has_content = false;

                /* Create content boxes as children of the pseudo-element */
                while (c_item->type != CSS_COMPUTED_CONTENT_NONE) {
                    struct box *content_box = create_content_box(c_item, before_style, content, n);
                    if (content_box != NULL) {
                        box_add_child(pseudo_box, content_box);
                        has_content = true;
                    }
                    c_item++;
                }

                /* Only insert if we created content */
                if (has_content) {
                    /* Insert as FIRST child of parent inline box.
                     * After flattening in normalization:
                     *   INLINE_CONTAINER
                     *     ├─ INLINE(parent)
                     *     ├─ INLINE(::before)  <- pseudo_box
                     *     ├─ content children  <- flattened
                     *     ├─ original content
                     *     └─ INLINE_END(parent)
                     */
                    pseudo_box->parent = box;
                    pseudo_box->next = box->children;
                    pseudo_box->prev = NULL;
                    if (box->children != NULL) {
                        box->children->prev = pseudo_box;
                    }
                    box->children = pseudo_box;
                    if (box->last == NULL) {
                        box->last = pseudo_box;
                    }

                    NSLOG(wisp, DEEPDEBUG, "inline_before: created BOX_INLINE %p for ::before with %d children",
                        (void *)pseudo_box, pseudo_box->children ? 1 : 0);
                }
            }
        }
    }

    if (box->type == BOX_INLINE || box->type == BOX_BR) {
        /* Insert INLINE_END into containing block */
        struct box *inline_end;
        bool has_children;
        dom_exception err;

        err = dom_node_has_child_nodes(n, &has_children);
        if (err != DOM_NO_ERR)
            return;

        if (has_children == false || (box->flags & CONVERT_CHILDREN) == 0) {
            /* No children, or didn't want children converted */
            return;
        }

        if (props.inline_container == NULL) {
            /* Create inline container if we don't have one */
            props.inline_container = box_create(NULL, NULL, false, NULL, NULL, NULL, NULL, content->bctx);
            if (props.inline_container == NULL)
                return;

            props.inline_container->type = BOX_INLINE_CONTAINER;

            box_add_child(props.containing_block, props.inline_container);
        }

        inline_end = box_create(NULL, box->style, false, box->href, box->target, box->title,
            box->id == NULL ? NULL : lwc_string_ref(box->id), content->bctx);
        if (inline_end != NULL) {
            inline_end->type = BOX_INLINE_END;

            assert(props.inline_container != NULL);

            box_add_child(props.inline_container, inline_end);

            box->inline_end = inline_end;
            inline_end->inline_end = box;
        }
    } else if (!(box->flags & IS_REPLACED)) {
        /* Handle the :after pseudo element */
        box_construct_generate(n, content, box, box->styles->styles[CSS_PSEUDO_ELEMENT_AFTER]);
    }
}


/**
 * Find the next node in the DOM tree, completing element construction
 * where appropriate.
 *
 * \param n                 Current node
 * \param content           Containing content
 * \param convert_children  Whether to consider children of \a n
 * \return Next node to process, or NULL if complete
 *
 * \note \a n will be unreferenced
 */
static dom_node *next_node(dom_node *n, html_content *content, bool convert_children)
{
    dom_node *next = NULL;
    bool has_children;
    dom_exception err;

    err = dom_node_has_child_nodes(n, &has_children);
    if (err != DOM_NO_ERR) {
        dom_node_unref(n);
        return NULL;
    }

    if (convert_children && has_children) {
        err = dom_node_get_first_child(n, &next);
        if (err != DOM_NO_ERR) {
            dom_node_unref(n);
            return NULL;
        }
        dom_node_unref(n);
    } else {
        err = dom_node_get_next_sibling(n, &next);
        if (err != DOM_NO_ERR) {
            dom_node_unref(n);
            return NULL;
        }

        if (next != NULL) {
            if (box_for_node(n) != NULL)
                box_construct_element_after(n, content);
            dom_node_unref(n);
        } else {
            if (box_for_node(n) != NULL)
                box_construct_element_after(n, content);

            while (box_is_root(n) == false) {
                dom_node *parent = NULL;
                dom_node *parent_next = NULL;

                err = dom_node_get_parent_node(n, &parent);
                if (err != DOM_NO_ERR) {
                    dom_node_unref(n);
                    return NULL;
                }

                assert(parent != NULL);

                err = dom_node_get_next_sibling(parent, &parent_next);
                if (err != DOM_NO_ERR) {
                    dom_node_unref(parent);
                    dom_node_unref(n);
                    return NULL;
                }

                if (parent_next != NULL) {
                    dom_node_unref(parent_next);
                    dom_node_unref(parent);
                    break;
                }

                dom_node_unref(n);
                n = parent;
                parent = NULL;

                if (box_for_node(n) != NULL) {
                    box_construct_element_after(n, content);
                }
            }

            if (box_is_root(n) == false) {
                dom_node *parent = NULL;

                err = dom_node_get_parent_node(n, &parent);
                if (err != DOM_NO_ERR) {
                    dom_node_unref(n);
                    return NULL;
                }

                assert(parent != NULL);

                err = dom_node_get_next_sibling(parent, &next);
                if (err != DOM_NO_ERR) {
                    dom_node_unref(parent);
                    dom_node_unref(n);
                    return NULL;
                }

                if (box_for_node(parent) != NULL) {
                    box_construct_element_after(parent, content);
                }

                dom_node_unref(parent);
            }

            dom_node_unref(n);
        }
    }

    return next;
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
static void box_text_transform(char *s, unsigned int len, enum css_text_transform_e tt)
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

    box_extract_properties(ctx->n, &props);

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


/**
 * Convert an ELEMENT node to a box tree fragment,
 * then schedule conversion of the next ELEMENT node
 */
static void convert_xml_to_box(void *p)
{
    struct box_construct_ctx *ctx = p;
    dom_node *next;
    bool convert_children;
    uint32_t num_processed = 0;
    uint64_t start_time, now_time;

    nsu_getmonotonic_ms(&start_time);
    NSLOG(wisp, DEBUG, "PROFILER: START Box construction slice %p", ctx);

    do {
        convert_children = true;

        assert(ctx->n != NULL);

        if (box_construct_element(ctx, &convert_children) == false) {
            NSLOG(wisp, WARNING, "box_construct_element failed");
            ctx->cb(ctx->content, false);
            dom_node_unref(ctx->n);
            if (ctx->root_box != NULL)
                box_free(ctx->root_box);
            free(ctx);
            NSLOG(wisp, DEBUG, "PROFILER: STOP Box construction slice %p", ctx);
            return;
        }

        /* Find next element to process, converting text nodes as we go
         */
        next = next_node(ctx->n, ctx->content, convert_children);
        while (next != NULL) {
            dom_node_type type;
            dom_exception err;

            err = dom_node_get_node_type(next, &type);
            if (err != DOM_NO_ERR) {
                NSLOG(wisp, WARNING, "dom_node_get_node_type failed");
                ctx->cb(ctx->content, false);
                dom_node_unref(next);
                if (ctx->root_box != NULL)
                    box_free(ctx->root_box);
                free(ctx);
                NSLOG(wisp, DEBUG, "PROFILER: STOP Box construction slice %p", ctx);
                return;
            }

            if (type == DOM_ELEMENT_NODE)
                break;

            if (type == DOM_TEXT_NODE) {
                ctx->n = next;
                if (box_construct_text(ctx) == false) {
                    NSLOG(wisp, WARNING, "box_construct_text failed");
                    ctx->cb(ctx->content, false);
                    dom_node_unref(ctx->n);
                    if (ctx->root_box != NULL)
                        box_free(ctx->root_box);
                    free(ctx);
                    NSLOG(wisp, DEBUG, "PROFILER: STOP Box construction slice %p", ctx);
                    return;
                }
            }

            next = next_node(next, ctx->content, true);
        }

        // dom_node_unref(ctx->n);
        ctx->n = next;

        if (next == NULL) {
            /* Conversion complete */
            struct box root;

            memset(&root, 0, sizeof(root));

            root.type = BOX_BLOCK;
            root.children = root.last = ctx->root_box;
            root.children->parent = &root;

            /** \todo Remove box_normalise_block */
            if (box_normalise_block(&root, ctx->root_box, (struct html_content *)ctx->content) == false) {
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

            free(ctx);
            NSLOG(wisp, DEBUG, "PROFILER: STOP Box construction slice %p", ctx);
            return;
        }

        /* Check for yield every 64 nodes */
        if ((++num_processed & 0x3F) == 0) {
            nsu_getmonotonic_ms(&now_time);
            /* Yield if we've been running for more than 50ms */
            if (now_time - start_time > 50) {
                break;
            }
        }
    } while (true);

    NSLOG(wisp, DEBUG, "PROFILER: STOP Box construction slice %p", ctx);
    /* More work to do: schedule a continuation */
    guit->misc->schedule(0, (void *)convert_xml_to_box, ctx);
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

    ctx = malloc(sizeof(*ctx));
    if (ctx == NULL) {
        return NSERROR_NOMEM;
    }

    ctx->content = c;
    ctx->n = dom_node_ref(n);
    ctx->root_box = NULL;
    ctx->cb = cb;
    ctx->bctx = c->bctx;

    *box_conversion_context = ctx;

    return guit->misc->schedule(0, (void *)convert_xml_to_box, ctx);
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
    free(ctx);

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

/*
 * Copyright 2004 James Bursa <bursa@users.sourceforge.net>
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
 * Private data for text/html content.
 */

#ifndef WISP_HTML_PRIVATE_H
#define WISP_HTML_PRIVATE_H

#include <dom/dom.h>
#include <libdom/bindings/hubbub/errors.h>
#include <libdom/bindings/hubbub/parser.h>

#include <wisp/content/content_protected.h>
#include <wisp/content/handlers/css/utils.h>
#include <wisp/content/handlers/html/box.h>
#include "wisp/types.h"


struct gui_layout_table;
struct scrollbar_msg_data;
struct content_redraw_data;
struct selection;
struct svgtiny_diagram;

/* A persistent containing block for viewport-fixed boxes, outside the DOM tree.
 * Its first member permits box-coordinate code to recover the owning document. */
struct html_viewport {
    struct box box;
    struct html_content *owner;
};

#ifdef WISP_PROFILE_RESTYLE
/* Wall-clock durations except layout_cpu_ns (zero when unavailable).
 * CSS and normalization are subsets of construction; reset is part of layout.
 * Painting continues to accumulate until the next refresh starts. */
struct html_restyle_profile {
    uint64_t start_ns, construction_start_ns;
    uint64_t invalidate_ns, detach_ns, css_ns, construction_ns, normalise_ns;
    uint64_t construction_wall_ns, publication_ns, layout_ns, cleanup_ns, total_ns, paint_ns, last_paint_ns;
    uint64_t layout_cpu_ns, layout_reset_ns;
    uint32_t styled_elements, construction_slices, paint_calls;
    uint32_t intrinsic_inline_calculations, intrinsic_inline_reuses;
    uint32_t inline_layout_calculations, inline_layout_reuses;
    uint32_t inline_layout_width_misses, inline_layout_float_misses, inline_layout_intrinsic_misses;
    uint32_t inline_layout_repeat_same_width, inline_layout_repeat_changed_width;
    uint32_t inline_layout_repeat_reuses;
    uint32_t inline_layout_float_exclusions, inline_layout_other_exclusions;
    const char *update_mode, *fallback_reason;
    uint32_t changed_controls, dirty_roots, invalidated_nodes, reused_styles;
    size_t temporary_bytes;
    size_t cloned_result_bytes;
    uint32_t style_references, restarts;
};

/* Document-lifetime counters, including initial layout and scroll-only paints.
 * Kept separate from per-update statistics, which reset on every restyle. */
struct html_render_profile {
    uint64_t layout_ns, paint_ns;
    uint64_t layout_cpu_ns, paint_cpu_ns;
    uint64_t layout_calls, paint_calls, sticky_queries, sticky_ancestor_steps;
    uint64_t scroll_damage_queries, scroll_damage_boxes, scroll_damage_ns;
};
#endif

#define HTML_RESTYLE_MAX_ROOTS 8
/* Pending roots own DOM references; active construction consumes a separate batch. */
struct html_restyle_changes {
    dom_node *roots[HTML_RESTYLE_MAX_ROOTS];
    uint32_t count, changed_controls;
    const char *full_reason;
};

struct html_restyle_inputs {
    css_select_ctx *ctx;
    unsigned char media[sizeof(css_media)];
    css_fixed units[7];
    uint64_t css_revision, font_generation, visited_generation;
    char font_families[512]; /* Exact packed generic names; oversized settings disable reuse. */
    struct nsurl *base_url;
    int width, height, font_default, font_size, font_min_size;
    bool author_css, valid;
};

struct html_restyle_entry;

struct html_restyle_state {
    struct html_restyle_changes pending, active;
    struct html_restyle_inputs styled_inputs, active_inputs;
    struct html_restyle_entry *entries;
    size_t count, capacity;
    uint64_t dom_generation, styled_generation, active_generation;
    bool partial;
#ifdef WISP_PROFILE_RESTYLE
    bool force_restart; /* Capture-only invalidation after construction has begun. */
    bool slow_restyle; /* Capture lifecycle tests yield between small slices. */
    bool fail_retained_layout; /* Capture-only failure after irreversible publication. */
#endif
    bool force_full; /* Developer capture override, not a browser preference. */
    bool css_only; /* Capture override: reuse CSS but reconstruct all boxes. */
    bool no_intrinsic_reuse; /* Capture override: remeasure all retained inline containers. */
    bool no_inline_layout_reuse; /* Capture override: reflow all retained text lines. */
};

/**
 * Pre-serialized inline SVG, stored in linked list during parsing.
 * The XML string is serialized during the parser callback to avoid
 * redundant DOM traversal during box construction.
 */
struct html_inline_svg {
    struct dom_node *node; /**< SVG DOM element (key for lookup) */
    char *svg_xml; /**< Pre-serialized XML string */
    size_t svg_xml_len; /**< Length of XML string */
    struct html_inline_svg *next; /**< Next in list */
};

typedef enum {
    HTML_DRAG_NONE, /** No drag */
    HTML_DRAG_SELECTION, /** Own; Text selection */
    HTML_DRAG_SCROLLBAR, /** Not own; drag in scrollbar widget */
    HTML_DRAG_TEXTAREA_SELECTION, /** Not own; drag in textarea widget */
    HTML_DRAG_TEXTAREA_SCROLLBAR, /** Not own; drag in textarea widget */
    HTML_DRAG_CONTENT_SELECTION, /** Not own; drag in child content */
    HTML_DRAG_CONTENT_SCROLL /** Not own; drag in child content */
} html_drag_type;

/**
 * For drags we don't own
 */
union html_drag_owner {
    bool no_owner;
    struct box *content;
    struct scrollbar *scrollbar;
    struct box *textarea;
};

typedef enum {
    HTML_SELECTION_NONE, /** No selection */
    HTML_SELECTION_TEXTAREA, /** Selection in one of our textareas */
    HTML_SELECTION_SELF, /** Selection in this html content */
    HTML_SELECTION_CONTENT /** Selection in child content */
} html_selection_type;

/**
 * For getting at selections in this content or things in this content
 */
union html_selection_owner {
    bool none;
    struct box *textarea;
    struct box *content;
};

typedef enum {
    HTML_FOCUS_SELF, /**< Focus is our own */
    HTML_FOCUS_CONTENT, /**< Focus belongs to child content */
    HTML_FOCUS_TEXTAREA /**< Focus belongs to textarea */
} html_focus_type;

/**
 * For directing input
 */
union html_focus_owner {
    bool self;
    struct box *textarea;
    struct box *content;
};


/**
 * Data specific to CONTENT_HTML.
 */
typedef struct html_content {
    struct content base;

    dom_hubbub_parser *parser; /**< Parser object handle */
    bool parse_completed; /**< Whether the parse has been completed */
    bool conversion_begun; /**< Whether or not the conversion has begun */
    bool conversion_restart_pending; /**< Whether a restart is pending */
    unsigned int scripts_active; /**< Number of script fetches currently active */
    unsigned int parser_blocking_script; /**< Script array index + 1, or zero. */
    unsigned int async_script_head, async_script_tail; /**< Ready queue, indices + 1. */
    bool scripts_running; /**< Prevent reentrant script processing. */
    bool data_complete; /**< Whether HTML data download is complete */

    /** Document tree */
    dom_document *document;
    /** Quirkyness of document */
    dom_document_quirks_mode quirks;

    /** Encoding of source, NULL if unknown. */
    char *encoding;
    /** Source of encoding information. */
    dom_hubbub_encoding_source encoding_source;

    /** Base URL (may be a copy of content->url). */
    struct nsurl *base_url;
    /** Base target */
    char *base_target;

    /** Content has been aborted in the LOADING state */
    bool aborted;

    /** Whether a meta refresh has been handled */
    bool refresh;

    /** Whether a layout (reflow) is in progress */
    bool reflowing;

    /** Whether a reformat is pending (scheduled) */
    bool pending_reformat;
    /** Deferred state-only restyle; no parsing or page-load lifecycle. */
    bool restyle_pending;
    bool restyling;
    /** Enables DOM-backed searching after until-found or details has appeared. */
    bool has_revealable_content;
    struct html_restyle_state restyle;
#ifdef WISP_PROFILE_RESTYLE
    struct html_restyle_profile restyle_profile;
    struct html_render_profile render_profile;
#endif
    void *restyle_old_bctx;
    struct content_html_iframe *restyle_old_iframe;
    struct content_html_frames *restyle_old_frameset;

    /** Whether an initial layout has been done */
    bool had_initial_layout;

    /** Whether scripts are enabled for this content */
    bool enable_scripting;

    /* Title element node */
    dom_node *title;

    /** A talloc context purely for the render box tree */
    int *bctx;
    /** A context pointer for the box conversion, NULL if no conversion
     * is in progress.
     */
    void *box_conversion_context;
    /** Box tree, or NULL. */
    struct box *layout;
    struct html_viewport viewport;
    /** Sticky constraints are usable only after a complete successful layout. */
    bool sticky_layout_valid;
    /** Settled page can reuse scrolled pixels after repainting sticky/frame damage. */
    bool scroll_reuse_safe;
    /** Partial paint passes do not trim transformed drawing geometry. */
    bool redraw_region_safe;
    /** Document background colour. */
    colour background_colour;

    /** Timestamp when we first delayed box conversion for fonts (ms), 0 if not waiting */
    uint64_t font_wait_start_ms;
    uint64_t font_set_id; /**< Stable identity, never reused by another document. */
    struct html_font_face_set *font_faces;

    /** Font callback table */
    const struct gui_layout_table *font_func;

    /** Number of entries in scripts */
    unsigned int scripts_count;
    /** Scripts */
    struct html_script *scripts;
    /** javascript thread in use */
    struct jsthread *jsthread;

    /** Number of entries in stylesheet_content. */
    unsigned int stylesheet_count;
    /** Stylesheets. Each may be NULL. */
    struct html_stylesheet *stylesheets;
    /**< Style selection context */
    css_select_ctx *select_ctx;
    /**< Style selection media specification */
    css_media media;
    /** CSS length conversion context for document. */
    css_unit_ctx unit_len_ctx;
    /**< Universal selector */
    lwc_string *universal;

    /** Number of entries in object_list. */
    unsigned int num_objects;
    /** List of objects. */
    struct content_html_object *object_list;
    /** Forms, in reverse order to document. */
    struct form *forms;
    /** Hash table of imagemaps. */
    struct imagemap **imagemaps;

    /** Browser window containing this document, or NULL if not open. */
    struct browser_window *bw;

    /** Invalidates input already in progress, even if reopened in the same window. */
    uint64_t input_generation;

    /** Frameset information */
    struct content_html_frames *frameset;

    /** Inline frame information */
    struct content_html_iframe *iframe;

    /** Content of type CONTENT_HTML containing this, or NULL if not an
     * object within a page. */
    struct html_content *page;

    /** Current drag type */
    html_drag_type drag_type;
    /** Widget capturing all mouse events */
    union html_drag_owner drag_owner;

    /** Current selection state */
    html_selection_type selection_type;
    /** Current selection owner */
    union html_selection_owner selection_owner;

    /** Current input focus target type */
    html_focus_type focus_type;
    /** Current input focus target */
    union html_focus_owner focus_owner;

    /** HTML content's own text selection object */
    struct selection *sel;

    /**
     * Open core-handled form SELECT menu, or NULL if none
     *  currently open.
     */
    struct form_control *visible_select_menu;

    /** SVG symbol registry for inline SVG <use> resolution, or NULL */
    struct svg_symbol_registry *svg_symbols;

    /** Linked list of pre-parsed inline SVG diagrams, or NULL */
    struct html_inline_svg *inline_svgs;

} html_content;

/**
 * Set our drag status, and inform whatever owns the content
 *
 * \param html		HTML content
 * \param drag_type	Type of drag
 * \param drag_owner	What owns the drag
 * \param rect		Pointer movement bounds
 */
void html_set_drag_type(
    html_content *html, html_drag_type drag_type, union html_drag_owner drag_owner, const struct rect *rect);

/**
 * Set our selection status, and inform whatever owns the content
 *
 * \param html			HTML content
 * \param selection_type	Type of selection
 * \param selection_owner	What owns the selection
 * \param read_only		True iff selection is read only
 */
void html_set_selection(
    html_content *html, html_selection_type selection_type, union html_selection_owner selection_owner, bool read_only);

/**
 * Set our input focus, and inform whatever owns the content
 *
 * \param html			HTML content
 * \param focus_type		Type of input focus
 * \param focus_owner		What owns the focus
 * \param hide_caret		True iff caret to be hidden
 * \param x			Carret x-coord rel to owner
 * \param y			Carret y-coord rel to owner
 * \param height		Carret height
 * \param clip			Carret clip rect
 */
void html_set_focus(html_content *html, html_focus_type focus_type, union html_focus_owner focus_owner, bool hide_caret,
    int x, int y, int height, const struct rect *clip);

/**
 * Render padding and margin box outlines in html_redraw().
 */
extern bool html_redraw_debug;


/* in html/html.c */
void html_request_restyle(html_content *html);
void html_request_checked_restyle(html_content *html, dom_node *node);

/**
 * redraw a box
 *
 * \param htmlc HTML content
 * \param box The box to redraw.
 */
void html__redraw_a_box(html_content *htmlc, struct box *box);


/**
 * Complete conversion of an HTML document
 *
 * \param htmlc Content to convert
 */
void html_finish_conversion(html_content *htmlc);


/**
 * Test if an HTML content conversion can begin
 *
 * \param htmlc		html content to test
 * \return true iff the html content conversion can begin
 */
bool html_can_begin_conversion(html_content *htmlc);


/**
 * Begin conversion of an HTML document
 *
 * \param htmlc Content to convert
 */
bool html_begin_conversion(html_content *htmlc);

/**
 * Resume conversion of an html content
 *
 * \param p html content
 */
void html_resume_conversion_cb(void *p);


/**
 * execute some text as a script element
 */
bool html_exec(struct content *c, const char *src, size_t srclen);


/**
 * Run ready classic scripts and resume a blocked parser.
 *
 * execute scripts using algorithm found in:
 * https://html.spec.whatwg.org/multipage/scripting.html#prepare-the-script-element
 *
 * \param htmlc html content.
 * \param allow_defer Also run deferred scripts if parsing is complete.
 * \return NSERROR_OK error code.
 */
nserror html_script_exec(html_content *htmlc, bool allow_defer);

/** Resume ready scripts outside parser/resource callbacks, then conversion. */
void html_script_schedule(html_content *htmlc);


/**
 * Free all script resources and references for a html content.
 *
 * \param htmlc html content.
 * \return NSERROR_OK or error code.
 */
nserror html_script_free(html_content *htmlc);


/**
 * Check if any of the scripts loaded were insecure
 */
bool html_saw_insecure_scripts(html_content *htmlc);


/**
 * Complete the HTML content state machine *iff* all scripts are finished
 */
nserror html_proceed_to_done(html_content *html);


/* in html/redraw.c */
bool html_redraw(
    struct content *c, struct content_redraw_data *data, const struct rect *clip, const struct redraw_context *ctx);


/* in html/redraw_border.c */
bool html_redraw_table_borders(
    const struct box *table, int x, int y, const struct rect *clip, float scale, const struct redraw_context *ctx);

bool html_redraw_borders(struct box *box, int x_parent, int y_parent, int p_width, int p_height,
    const struct rect *clip, float scale, const struct redraw_context *ctx);


bool html_redraw_inline_borders(struct box *box, struct rect b, const struct rect *clip, float scale, bool first,
    bool last, const struct redraw_context *ctx);


/* in html/script.c */
dom_hubbub_error html_process_script(void *ctx, dom_node *node);

/* in html/html_svg.c */
dom_hubbub_error html_process_svg(void *ctx, dom_node *node);


/* in html/forms.c */
struct form *html_forms_get_forms(dom_html_document *doc);
struct form_control *html_forms_get_control_for_node(struct form *forms, dom_node *node);

/* Fresh, unlinked native control for unpublished box construction. The caller
 * owns it; publication must associate its DOM form before it becomes live. */
nserror html_forms_create_detached_control(dom_node *node, struct form_control **result);


/* in html/css_fetcher.c */
/**
 * Register the fetcher for the pseudo x-ns-css scheme.
 *
 * \return NSERROR_OK on successful registration or error code on failure.
 */
nserror html_css_fetcher_register(void);
nserror html_css_fetcher_add_item(dom_string *data, struct nsurl *base_url, uint32_t *key);


/* Events */
/**
 * Construct an event and fire it at the DOM
 *
 */
bool fire_generic_dom_event(dom_string *type, dom_node *target, bool bubbles, bool cancelable);

/**
 * Construct a keyboard event and fire it at the DOM
 */
bool fire_dom_keyboard_event(dom_string *type, dom_node *target, bool bubbles, bool cancelable, uint32_t key);

/* Useful dom_string pointers */
struct dom_string;

extern struct dom_string *html_dom_string_map;
extern struct dom_string *html_dom_string_id;
extern struct dom_string *html_dom_string_name;
extern struct dom_string *html_dom_string_area;
extern struct dom_string *html_dom_string_a;
extern struct dom_string *html_dom_string_nohref;
extern struct dom_string *html_dom_string_href;
extern struct dom_string *html_dom_string_target;
extern struct dom_string *html_dom_string_shape;
extern struct dom_string *html_dom_string_default;
extern struct dom_string *html_dom_string_rect;
extern struct dom_string *html_dom_string_rectangle;
extern struct dom_string *html_dom_string_coords;
extern struct dom_string *html_dom_string_circle;
extern struct dom_string *html_dom_string_poly;
extern struct dom_string *html_dom_string_polygon;
extern struct dom_string *html_dom_string_text_javascript;
extern struct dom_string *html_dom_string_type;
extern struct dom_string *html_dom_string_src;

#endif

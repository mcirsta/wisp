/*
 * This file is part of LibCSS
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 * Copyright 2009 John-Mark Bell <jmb@netsurf-browser.org>
 */

#ifndef css_select_select_h_
#define css_select_select_h_

#include <stdbool.h>
#include <stdint.h>

#include <libcss/select.h>

#include "stylesheet.h"
#include "select/variables.h"
#include "select/hash.h"
#include "select/logical.h"

/**
 * Item in the reject cache (only class and id types are valid)
 */
typedef struct reject_item {
    lwc_string *value;
    css_selector_type type;
} reject_item;

typedef struct prop_state {
    uint32_t specificity; /* Specificity of property in result */
    unsigned int set : 1, /* Whether property is set in result */
        origin : 2, /* Origin of property in result */
        important : 1, /* Importance of property in result */
        logical : 1; /* Winning declaration names an inline logical edge. */
    enum flag_value explicit_default : 3; /* Property is set to inherit */
} prop_state;


typedef enum css_node_flags {
    CSS_NODE_FLAGS_NONE = 0,
    CSS_NODE_FLAGS_HAS_HINTS = (1 << 0),
    CSS_NODE_FLAGS_HAS_INLINE_STYLE = (1 << 1),
    CSS_NODE_FLAGS_PSEUDO_CLASS_ACTIVE = (1 << 2),
    CSS_NODE_FLAGS_PSEUDO_CLASS_FOCUS = (1 << 3),
    CSS_NODE_FLAGS_PSEUDO_CLASS_HOVER = (1 << 4),
    CSS_NODE_FLAGS_PSEUDO_CLASS_LINK = (1 << 5),
    CSS_NODE_FLAGS_PSEUDO_CLASS_VISITED = (1 << 6),
    CSS_NODE_FLAGS_TAINT_PSEUDO_CLASS = (1 << 7),
    CSS_NODE_FLAGS_TAINT_ATTRIBUTE = (1 << 8),
    CSS_NODE_FLAGS_TAINT_SIBLING = (1 << 9),
    CSS_NODE_FLAGS__PSEUDO_CLASSES_MASK = (CSS_NODE_FLAGS_PSEUDO_CLASS_ACTIVE | CSS_NODE_FLAGS_PSEUDO_CLASS_FOCUS |
        CSS_NODE_FLAGS_PSEUDO_CLASS_HOVER | CSS_NODE_FLAGS_PSEUDO_CLASS_LINK | CSS_NODE_FLAGS_PSEUDO_CLASS_VISITED),
} css_node_flags;

struct css_node_data {
    css_select_results partial;
    css_bloom *bloom;
    css_node_flags flags;
    css_var_context *var_ctx;  /* Completed element variable environment */
    css_var_context *var_parent; /* Inherited snapshot used for style sharing */
};

struct revert_data {
    prop_state props[CSS_N_PROPERTIES][CSS_PSEUDO_ELEMENT_COUNT];
    prop_state inline_props[CSS_INLINE_EDGE_COUNT][CSS_PSEUDO_ELEMENT_COUNT];
    css_computed_style *style[CSS_PSEUDO_ELEMENT_COUNT];
};

/* Borrowed matched declarations, retained only for this selection. Pseudo
 * variables are evaluated when an ordinary property first consumes var(). */
typedef struct css_pseudo_var_rule {
    const css_style *style;
    css_origin origin;
    uint32_t specificity;
    css_pseudo_element pseudo;
} css_pseudo_var_rule;

/**
 * Selection state
 */
typedef struct css_select_state {
    void *node; /* Node we're selecting for */
    const css_media *media; /* Currently active media spec */
    const css_unit_ctx *unit_ctx; /* Unit conversion context. */
    css_select_results *results; /* Result set to populate */

    /** UA and user styles for handling revert property value. */
    struct revert_data *revert; /* Length: CSS_ORIGIN_AUTHOR */

    css_pseudo_element current_pseudo; /* Current pseudo element */
    css_computed_style *computed; /* Computed style to populate */

    css_select_handler *handler; /* Handler functions */
    void *pw; /* Client data for handlers */

    const css_stylesheet *sheet; /* Current sheet being processed */

    css_origin current_origin; /* Origin of current sheet */
    uint32_t current_specificity; /* Specificity of current rule */

    css_qname element; /* Element we're selecting for */
    lwc_string *id; /* Node id, if any */
    lwc_string **classes; /* Node classes, if any */
    uint32_t n_classes; /* Number of classes */
    css_selector_filter filter; /* Current node's selector lookup keys */

    reject_item reject_cache[128]; /* Reject cache (filled from end) */
    reject_item *next_reject; /* Next free slot in reject cache */

    struct css_node_data *node_data; /* Data we'll store on node */
    const css_bloom *parent_bloom; /* Borrowed ancestor filter; never owned by node_data */

    css_var_context *var_ctx[CSS_PSEUDO_ELEMENT_COUNT];
    css_var_context *var_parent; /* Inherited snapshot before empty-overlay reuse */
    bool current_logical; /* Mapping identity of the declaration being cascaded */
    bool rtl_inline; /* Temporarily cascading/defaulting an RTL edge alternative */
    bool collecting_vars; /* First pass: collect final custom properties only */
    css_pseudo_var_rule *pseudo_var_rules;
    uint32_t n_pseudo_var_rules;
    uint32_t pseudo_var_rules_capacity;

    prop_state props[CSS_N_PROPERTIES][CSS_PSEUDO_ELEMENT_COUNT];
    prop_state inline_props[CSS_INLINE_EDGE_COUNT][CSS_PSEUDO_ELEMENT_COUNT];
} css_select_state;

css_error css__cascade_property(css_code_t opv, css_style *style, css_select_state *state);

bool css__outranks_existing(uint16_t op, bool important, css_select_state *state, enum flag_value explicit_default);

#endif

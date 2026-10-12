/*
 * This file is part of LibCSS.
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 */

#ifndef css_parse_declarations_h_
#define css_parse_declarations_h_

#include <libcss/stylesheet.h>
#include <parserutils/utils/vector.h>

#include "lex/lex.h"

typedef struct css_declaration_list css_declaration_list;
struct css_prop_entry;

/* Parsed tokens, not source text: escapes are decoded, comments discarded and
 * whitespace represented by CSS_TOKEN_S. These must not be used to reconstruct
 * the original spelling of custom property values for CSSOM serialization. */
typedef struct css_declaration_token {
    css_token_type type;
    lwc_string *text; /**< NULL for punctuation/whitespace encoded by type alone */
} css_declaration_token;

typedef struct css_declaration {
    lwc_string *name; /**< Canonical ordinary name (case/aliases); case-sensitive custom name */
    const struct css_prop_entry *property; /**< Static registration; NULL for custom properties */
    /* Original value between ':' and the priority/delimiter, including outer
     * whitespace and comments. This is source text, not CSSOM serialization. */
    lwc_string *source_value;
    /* Bounds after trimming only outer whitespace, without cutting escapes,
     * strings or comments. Relative to source_value; no extra string copy. */
    size_t value_start, value_end;
    bool deferred; /**< Accepted through the renderer's variable-deferred path */
    bool important;
    bool quirks_allowed; /**< Preserve grammar context for ambiguous hashless colours. */
    const css_declaration_token *tokens; /**< Value without surrounding whitespace or priority */
    size_t token_count;
    uint32_t bytecode_offset; /**< Span in the list's inline rule */
    uint32_t bytecode_length;
} css_declaration;

/* Own an independent copy of the specified value, without retaining its parsed
 * stylesheet or bytecode. The destination must be uninitialized; on failure it
 * is zeroed. Used only by native declaration editing, not ordinary styling. */
css_error css__declaration_copy_value(const css_declaration *source, css_declaration *out);
void css__declaration_fini(css_declaration *entry);

/* End of one scalar/function component in validated tokens, or zero for an
 * unbalanced function. Does not split whitespace inside function arguments. */
size_t css__declaration_component_end(const css_declaration *source, size_t start);

/* Internal parser output for a future specified-style layer. Entries preserve
 * duplicates, shorthands and source order. They are NOT a CSSOM declaration
 * block: longhand expansion, cascade/ordering and value serialization belong to
 * that layer. All borrowed entries and the compiled sheet live until destroy.
 *
 * Parsing uses the normal property handlers (including generated handlers).
 * Invalid declarations are skipped exactly as for an inline stylesheet. Fatal
 * errors make the list unusable; destroy it and start a new parse. Results are
 * available only after successful completion, and are immutable afterwards.
 * The caller supplies normal stylesheet callbacks and URL context. The input
 * is a decoded Unicode string supplied as UTF-8 (no BOM/charset sniffing).
 * inline_style/charset in params are overridden accordingly. */
css_error css__declaration_list_create(const css_stylesheet_params *params, css_declaration_list **out);
/* Whole single-value input for a supported, already-decoded property name.
 * Append/complete propagate CSS_INVALID for invalid values; no list recovery,
 * declaration separators or top-level !important. No priority is supplied as
 * text: callers apply their independently validated flag when assigning. */
css_error css__declaration_list_create_for_value(
    const css_stylesheet_params *params, lwc_string *property, css_declaration_list **out);
css_error css__declaration_list_append(css_declaration_list *list, const uint8_t *data, size_t len);
css_error css__declaration_list_complete(css_declaration_list *list);
void css__declaration_list_destroy(css_declaration_list *list);
size_t css__declaration_list_count(const css_declaration_list *list);
const css_declaration *css__declaration_list_get(const css_declaration_list *list, size_t index);
const css_stylesheet *css__declaration_list_sheet(const css_declaration_list *list);

/* Language parser hook, called only after a declaration has been accepted. */
css_error css__declaration_list_record(css_declaration_list *list, const css_token *name,
    const parserutils_vector *tokens, int32_t value_start, uint32_t bytecode_offset, uint32_t bytecode_length,
    bool deferred);

#endif

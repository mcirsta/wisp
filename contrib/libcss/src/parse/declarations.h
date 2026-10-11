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
    lwc_string *name; /**< Lowercase ordinary name; case-sensitive custom name */
    const struct css_prop_entry *property; /**< Static registration; NULL for custom properties */
    /* Original value between ':' and the priority/delimiter, including outer
     * whitespace and comments. This is source text, not CSSOM serialization. */
    lwc_string *source_value;
    bool important;
    const css_declaration_token *tokens; /**< Value without surrounding whitespace or priority */
    size_t token_count;
    uint32_t bytecode_offset; /**< Span in the list's inline rule */
    uint32_t bytecode_length;
} css_declaration;

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
css_error css__declaration_list_append(css_declaration_list *list, const uint8_t *data, size_t len);
css_error css__declaration_list_complete(css_declaration_list *list);
void css__declaration_list_destroy(css_declaration_list *list);
size_t css__declaration_list_count(const css_declaration_list *list);
const css_declaration *css__declaration_list_get(const css_declaration_list *list, size_t index);
const css_stylesheet *css__declaration_list_sheet(const css_declaration_list *list);

/* Language parser hook, called only after a declaration has been accepted. */
css_error css__declaration_list_record(css_declaration_list *list, const css_token *name,
    const parserutils_vector *tokens, int32_t value_start, uint32_t bytecode_offset, uint32_t bytecode_length);

#endif

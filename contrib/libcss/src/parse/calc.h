/* Shared calc grammar. Licensed under the MIT License. */
#ifndef css_parse_calc_h_
#define css_parse_calc_h_

#include "parse/declarations.h"
#include "bytecode/bytecode.h"

/* Locale-independent numeric prefix of a lexer token. Keep its precision for
 * calculation operands and specified-value serialization alike. */
double css__calc_number(lwc_string *text, size_t *consumed);

/* Walk an already accepted calc() component in postfix order. Operands carry
 * their original numeric spelling and parsed unit; operators have text == NULL.
 * This shares the native grammar without using rounded rendering operands.
 * Property type checking has already happened; no stylesheet is needed. */
typedef css_error (*css_calc_emit)(void *pw, css_code_t op, lwc_string *text, uint32_t unit);
css_error css__calc_walk(const css_declaration_token *tokens, size_t count, css_calc_emit emit, void *pw);

/* Specified-value simplification of the supported scalar calc grammar.
 * Expects accepted tokens; malformed or unsupported forms return CSS_INVALID.
 * On success the caller owns *out; on failure *out is NULL. */
css_error css__declaration_calc_serialize(const css_declaration_token *tokens, size_t count,
    lwc_string **out);

#endif

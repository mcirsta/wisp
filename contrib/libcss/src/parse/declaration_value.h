/*
 * This file is part of LibCSS.
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 */

#ifndef css_parse_declaration_value_h_
#define css_parse_declaration_value_h_

#include "parse/declaration_order.h"

/* Serialize one winning component's specified value, not its computed value.
 * Supports custom properties, CSS-wide keywords, pending variable substitution
 * and the value families registered in properties.toml. CSS_INVALID means
 * that a property/value serializer is not implemented yet (e.g. math functions),
 * NOT that the already-accepted declaration is invalid. Callers must propagate
 * this limitation, never substitute raw text or an empty value as a fallback.
 * On success the caller owns *out; on every failure *out is NULL. */
css_error css__declaration_value_serialize(const css_declaration_contribution *value, lwc_string **out);

/* Members must be in the requested shorthand's registered component order.
 * NULL members, mixed priorities, mixed CSS-wide keywords or incompatible
 * pending-substitution values give the CSSOM empty string. Reconstruction of
 * ordinary values covers the registered pair/quad length, keyword and colour
 * families and the structured shorthand families registered in properties.toml.
 * This does not decide whether a shorthand may be used in cssText: that also
 * needs the declaration block's ordering/logical-property intermixing checks. */
css_error css__declaration_shorthand_serialize(const struct css_prop_entry *property,
    const css_declaration_contribution *const *members, size_t count, lwc_string **out);

/* Join a nonempty list of serialized components with a literal separator.
 * Input strings are borrowed; the returned string has its own reference. */
css_error css__declaration_values_join(lwc_string *const *values, size_t count, const char *separator, lwc_string **out);

#endif

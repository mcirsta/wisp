/* Specified background values. Licensed under the MIT License. */
#ifndef css_parse_declaration_background_h_
#define css_parse_declaration_background_h_

#include "parse/declaration_value.h"
#include "parse/property.h"

/* Internal helpers for already validated, non-deferred, non-CSS-wide values.
 * Member order is the background registration's image/position-x/position-y/repeat-x/repeat-y/attachment/color. */
css_error css__declaration_background_image(const css_declaration *source, lwc_string **out);
css_error css__declaration_background_component(const css_declaration_contribution *value, lwc_string **out);
css_error css__declaration_background_shorthand(const css_declaration_contribution *const *members, lwc_string **out);

css_error css__declaration_object_position(const css_declaration *source, lwc_string **out);
css_error css__declaration_background_pair_shorthand(enum css_declaration_value_kind kind,
    const css_declaration_contribution *const *members, lwc_string **out);

#endif

/* Specified background values. Licensed under the MIT License. */
#ifndef css_parse_declaration_background_h_
#define css_parse_declaration_background_h_

#include "parse/declaration_value.h"

/* Internal helpers for already validated, non-deferred, non-CSS-wide values.
 * Member order is the background registration's image/position/repeat-x/repeat-y/attachment/color. */
css_error css__declaration_background_image(const css_declaration *source, lwc_string **out);
css_error css__declaration_background_component(const css_declaration_contribution *value, lwc_string **out);
css_error css__declaration_background_shorthand(const css_declaration_contribution *const *members, lwc_string **out);

css_error css__declaration_background_repeat_shorthand(
    const css_declaration_contribution *const *members, lwc_string **out);

#endif

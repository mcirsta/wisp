/* Specified font shorthand values. Licensed under the MIT License. */
#ifndef css_parse_declaration_font_h_
#define css_parse_declaration_font_h_

#include "parse/declaration_value.h"

/* Already validated, non-deferred, non-CSS-wide values. Member order is the
 * supported style/variant/weight/size/line-height/family registration. */
css_error css__declaration_font_component(const css_declaration_contribution *value, lwc_string **out);
css_error css__declaration_font_shorthand(const css_declaration_contribution *const *members, lwc_string **out);

#endif

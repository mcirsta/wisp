/* Background parsing helpers. Licensed under the MIT License. */
#ifndef CSS_PARSE_BACKGROUND_H
#define CSS_PARSE_BACKGROUND_H
#include "parse/properties/properties.h"
css_error css__parse_background_image_item(css_language *, const parserutils_vector *, int32_t *, css_style *);
css_error css__parse_background_position_item(css_language *, const parserutils_vector *, int32_t *, css_style *);
css_error css__parse_background_repeat_item(css_language *, const parserutils_vector *, int32_t *, css_style *);
css_error css__parse_background_attachment_item(css_language *, const parserutils_vector *, int32_t *, css_style *);
css_error css__parse_background_list(
    css_language *, const parserutils_vector *, int32_t *, css_style *, opcode_t, css_prop_handler);
css_error css__background_list_append(css_style *, css_style *, opcode_t, uint32_t);
#endif

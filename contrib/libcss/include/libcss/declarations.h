/* Owned editable specified declarations. Licensed under the MIT License. */
#ifndef libcss_declarations_h_
#define libcss_declarations_h_
#include <libcss/stylesheet.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct css_declaration_store css_declarations;

/* Decoded UTF-8 declaration list. Invalid CSS is discarded by the parser;
 * fatal errors leave *out NULL. The object owns its values independently. */
css_error
css_declarations_parse(const css_stylesheet_params *params, const uint8_t *text, size_t length, css_declarations **out);
/* Independent mutable copy sharing immutable value sources. */
css_error css_declarations_clone(const css_declarations *source, css_declarations **out);
void css_declarations_destroy(css_declarations *declarations);
size_t css_declarations_count(const css_declarations *declarations);
/* Borrowed decoded property name, or NULL past the end. */
const char *css_declarations_item(const css_declarations *declarations, size_t index);
css_error
css_declarations_value(const css_declarations *declarations, const char *name, size_t length, lwc_string **out);
css_error
css_declarations_priority(const css_declarations *declarations, const char *name, size_t length, bool *important);
css_error css_declarations_serialize(const css_declarations *declarations, lwc_string **out);
/* CSSOM setProperty: decoded name/value/priority, no CSS unescaping of name.
 * Invalid assignments are successful no-ops. Fatal errors preserve old state. */
css_error css_declarations_set(css_declarations *declarations, const css_stylesheet_params *params, const char *name,
    size_t name_length, const char *value, size_t value_length, const char *priority, size_t priority_length,
    bool *changed);
css_error css_declarations_remove(css_declarations *declarations, const char *name, size_t length, bool *changed);
/* Independent inline stylesheet, including retained pending shorthand members. */
css_error css_declarations_compile(
    const css_declarations *declarations, const css_stylesheet_params *params, css_stylesheet **out);

/* Static supported CSS names, including legacy aliases, from the parser's
 * generator. These are feature identities, not a claim of complete grammars. */
const char *const *css_declarations_supported_properties(size_t *count);

#ifdef __cplusplus
}
#endif
#endif

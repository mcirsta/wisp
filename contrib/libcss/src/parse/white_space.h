/* Supported CSS Text 4 white-space decomposition. Licensed under the MIT License. */
#ifndef css_parse_white_space_h_
#define css_parse_white_space_h_

#include "bytecode/opcodes.h"
#include "utils/css_utils.h"

/* Shared by native parsing and specified readback; no computed style lookup.
 * Trim, break-spaces and preserve-spaces need additional rendering support. */
static inline bool css__white_space_values(lwc_string *const *parts, size_t count, uint16_t values[2])
{
    static const struct { const char *name; int collapse, wrap; } words[] = {
        {"normal", WHITE_SPACE_COLLAPSE_COLLAPSE, TEXT_WRAP_MODE_WRAP},
        {"pre", WHITE_SPACE_COLLAPSE_PRESERVE, TEXT_WRAP_MODE_NOWRAP},
        {"pre-wrap", WHITE_SPACE_COLLAPSE_PRESERVE, TEXT_WRAP_MODE_WRAP},
        {"pre-line", WHITE_SPACE_COLLAPSE_PRESERVE_BREAKS, TEXT_WRAP_MODE_WRAP},
        {"collapse", WHITE_SPACE_COLLAPSE_COLLAPSE, -1},
        {"preserve", WHITE_SPACE_COLLAPSE_PRESERVE, -1},
        {"preserve-breaks", WHITE_SPACE_COLLAPSE_PRESERVE_BREAKS, -1},
        {"wrap", -1, TEXT_WRAP_MODE_WRAP},
        {"nowrap", -1, TEXT_WRAP_MODE_NOWRAP},
    };
    bool seen[2] = {false};
    values[0] = WHITE_SPACE_COLLAPSE_COLLAPSE;
    values[1] = TEXT_WRAP_MODE_WRAP;
    if (count == 0 || count > 2)
        return false;
    for (size_t i = 0; i < count; i++) {
        size_t word;
        for (word = 0; word < N_ELEMENTS(words); word++) {
            if (css__keyword_equal(parts[i], words[word].name))
                break;
        }
        if (word == N_ELEMENTS(words) || (word < 4 && count != 1))
            return false;
        const int pair[] = {words[word].collapse, words[word].wrap};
        for (size_t part = 0; part < 2; part++) {
            if (pair[part] < 0)
                continue;
            if (seen[part])
                return false;
            seen[part] = true;
            values[part] = pair[part];
        }
    }
    return true;
}

static inline const char *css__white_space_component(size_t component, uint16_t value)
{
    if (component == 1)
        return value == TEXT_WRAP_MODE_NOWRAP ? "nowrap" : "wrap";
    return value == WHITE_SPACE_COLLAPSE_PRESERVE ? "preserve" :
        value == WHITE_SPACE_COLLAPSE_PRESERVE_BREAKS ? "preserve-breaks" : "collapse";
}

#endif

/* Keep generated libcss setters separate from Wisp's box enums. */
#include "select/propset.h"

css_computed_style *stacking_test_style(uint8_t position, bool automatic, int32_t z)
{
    css_computed_style *style = NULL;
    css_calculator *calc;
    if (css_calculator_create(&calc) != CSS_OK)
        return NULL;
    css_error error = css__computed_style_create(&style, calc);
    css_calculator_unref(calc);
    if (error != CSS_OK)
        return NULL;
    set_position(style, position);
    set_z_index(style, automatic ? CSS_Z_INDEX_AUTO : CSS_Z_INDEX_SET, z);
    return style;
}

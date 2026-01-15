# This file is part of LibCSS.
# Licensed under the MIT License,
# http://www.opensource.org/licenses/mit-license.php
# Copyright 2017 Lucas Neves <lcneves@gmail.com>

COPYRIGHT = '''\
/*
 * This file is part of LibCSS
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 * Copyright 2017 The NetSurf Project
 */

'''

# Include statements for generated headers
INCLUDE_LIBCSS_COMPUTED = '#include <libcss/computed.h>\n'
INCLUDE_LIBCSS_GRADIENT = '#include <libcss/gradient.h>\n'
INCLUDE_PROPGET = '#include "select/propget.h"\n'
INCLUDE_CALC = '#include "select/calc.h"\n'
INCLUDE_COMPUTED = '#include "select/computed.h"\n'

# Union type definition with include guard to prevent redefinition
CSS_FIXED_OR_CALC_UNION = '''\

#ifndef css_fixed_or_calc_typedef
#define css_fixed_or_calc_typedef
typedef union {
	css_fixed value;
	lwc_string *calc;
} css_fixed_or_calc;
#endif
'''


def ifndef(name):
    """Generate include guard for a header file."""
    guard = f"CSS_COMPUTED_{name.upper()}_H_"
    return f"#ifndef {guard}\n#define {guard}\n"


# Asset definitions for generated headers
assets = {}

assets['computed.h'] = {
    'header': (COPYRIGHT + ifndef("computed") +
               INCLUDE_LIBCSS_COMPUTED +
               INCLUDE_LIBCSS_GRADIENT +
               INCLUDE_CALC +
               CSS_FIXED_OR_CALC_UNION),
    'footer': '\n#endif\n'
}

assets['propset.h'] = {
    'header': (COPYRIGHT + ifndef("propset") +
               '\n' + INCLUDE_PROPGET),
    'footer': '\n#endif\n'
}

assets['propget.h'] = {
    'header': (COPYRIGHT + ifndef("propget") +
               INCLUDE_LIBCSS_COMPUTED +
               INCLUDE_COMPUTED +
               CSS_FIXED_OR_CALC_UNION),
    'footer': '\n#endif\n'
}

assets['destroy.inc'] = {
    'header': COPYRIGHT,
    'footer': ''
}

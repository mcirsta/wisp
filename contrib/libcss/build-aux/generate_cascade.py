#!/usr/bin/env python3
"""Generate cascade .c files for CSS properties from properties.toml.

Usage: python3 generate_cascade.py properties.toml output_dir [--diff source_dir]
"""
import sys, os, re

try:
    import tomllib
except ModuleNotFoundError:
    import tomli as tomllib

# Map cascade value → helper function name
HELPER_MAP = {
    'bg_border_color': 'css__cascade_bg_border_color',
    'border_style': 'css__cascade_border_style',
    'border_width': 'css__cascade_border_width',
    'border_width_calc': 'css__cascade_border_width_calc',
    'length': 'css__cascade_length',
    'length_calc': 'css__cascade_length_calc',
    'length_auto': 'css__cascade_length_auto',
    'length_auto_calc': 'css__cascade_length_auto_calc',
    'length_normal': 'css__cascade_length_normal',
    'length_normal_calc': 'css__cascade_length_normal_calc',
    'length_none': 'css__cascade_length_none',
    'length_none_calc': 'css__cascade_length_none_calc',
    'number': 'css__cascade_number',
    'uri_none': 'css__cascade_uri_none',
    'page_break': 'css__cascade_page_break_after_before_inside',
    'break': 'css__cascade_break_after_before_inside',
    'counter': 'css__cascade_counter_increment_reset',
    'integer': 'css__cascade_number',
}

# Map cascade type → value type for set_from_hint data access, getter params, etc.
TYPE_INFO = {
    # type: (hint_data, getter_vars, setter_args_from_get, getter_args)
    'bg_border_color': {
        'hint_data': 'hint->data.color',
        'get_vars': [('css_color', 'color')],
        'set_extra': ', color',
        'get_extra': ', &color',
    },
    'border_style': {
        'hint_data': None,  # status only
        'get_vars': [],
        'set_extra': '',
        'get_extra': '',
    },
    'border_width': {
        'hint_data': 'hint->data.length.value, hint->data.length.unit',
        'get_vars': [('css_fixed', 'length', '0'), ('css_unit', 'unit', 'CSS_UNIT_PX')],
        'set_extra': ', length, unit',
        'get_extra': ', &length, &unit',
    },
    'border_width_calc': {
        'hint_data': '(css_fixed_or_calc)(hint->data.length.value), hint->data.length.unit',
        'get_vars': [('css_fixed_or_calc', 'length', '(css_fixed_or_calc)0'), ('css_unit', 'unit', 'CSS_UNIT_PX')],
        'set_extra': ', length, unit',
        'get_extra': ', &length, &unit',
    },
    'length': {
        'hint_data': 'hint->data.length.value, hint->data.length.unit',
        'get_vars': [('css_fixed', 'length', '0'), ('css_unit', 'unit', 'CSS_UNIT_PX')],
        'set_extra': ', length, unit',
        'get_extra': ', &length, &unit',
    },
    'length_calc': {
        'hint_data': '(css_fixed_or_calc)(hint->data.length.value), hint->data.length.unit',
        'get_vars': [('css_fixed_or_calc', 'length', '(css_fixed_or_calc)0'), ('css_unit', 'unit', 'CSS_UNIT_PX')],
        'set_extra': ', length, unit',
        'get_extra': ', &length, &unit',
    },
    'length_auto': {
        'hint_data': 'hint->data.length.value, hint->data.length.unit',
        'get_vars': [('css_fixed', 'length', '0'), ('css_unit', 'unit', 'CSS_UNIT_PX')],
        'set_extra': ', length, unit',
        'get_extra': ', &length, &unit',
    },
    'length_auto_calc': {
        'hint_data': '(css_fixed_or_calc)(hint->data.length.value), hint->data.length.unit',
        'get_vars': [('css_fixed_or_calc', 'length', '(css_fixed_or_calc)0'), ('css_unit', 'unit', 'CSS_UNIT_PX')],
        'set_extra': ', length, unit',
        'get_extra': ', &length, &unit',
    },
    'length_normal': {
        'hint_data': 'hint->data.length.value, hint->data.length.unit',
        'get_vars': [('css_fixed', 'length', '0'), ('css_unit', 'unit', 'CSS_UNIT_PX')],
        'set_extra': ', length, unit',
        'get_extra': ', &length, &unit',
    },
    'length_normal_calc': {
        'hint_data': '(css_fixed_or_calc)(hint->data.length.value), hint->data.length.unit',
        'get_vars': [('css_fixed_or_calc', 'length', '(css_fixed_or_calc)0'), ('css_unit', 'unit', 'CSS_UNIT_PX')],
        'set_extra': ', length, unit',
        'get_extra': ', &length, &unit',
    },
    'length_none': {
        'hint_data': 'hint->data.length.value, hint->data.length.unit',
        'get_vars': [('css_fixed', 'length', '0'), ('css_unit', 'unit', 'CSS_UNIT_PX')],
        'set_extra': ', length, unit',
        'get_extra': ', &length, &unit',
    },
    'length_none_calc': {
        'hint_data': '(css_fixed_or_calc)(hint->data.length.value), hint->data.length.unit',
        'get_vars': [('css_fixed_or_calc', 'length', '(css_fixed_or_calc)0'), ('css_unit', 'unit', 'CSS_UNIT_PX')],
        'set_extra': ', length, unit',
        'get_extra': ', &length, &unit',
    },
    'number': {
        'hint_data': 'hint->data.fixed',
        'get_vars': [('css_fixed', 'fixed', '0')],
        'set_extra': ', fixed',
        'get_extra': ', &fixed',
    },
    'integer': {
        'hint_data': 'hint->data.integer',
        'get_vars': [('int32_t', 'count', '0')],
        'set_extra': ', count',
        'get_extra': ', &count',
    },
    'page_break': {
        'hint_data': None,
        'get_vars': [],
        'set_extra': '',
        'get_extra': '',
    },
    'break': {
        'hint_data': None,
        'get_vars': [],
        'set_extra': '',
        'get_extra': '',
    },
    'uri_none': {
        'hint_data': 'hint->data.string',
        'get_vars': [('lwc_string *', 'url', None)],
        'set_extra': ', url',
        'get_extra': ', &url',
        'hint_cleanup': True,
    },
    'counter': {
        'hint_data': 'hint->data.counter',
        'get_vars': [('const css_computed_counter *', '{name}', 'NULL')],
        'set_extra': ', {name}',
        'get_extra': ', &{name}',
        'counter': True,
    },
    'ident': {
        'hint_data': None,
        'get_vars': [],
        'set_extra': '',
        'get_extra': '',
    },
    'paint_color': {
        'hint_data': 'hint->data.color',
        'get_vars': [('css_color', 'color')],
        'set_extra': ', color',
        'get_extra': ', &color',
    },
}


def upper_name(name):
    return name.upper()


def parse_opcodes_from_spec(parse_spec, prop_upper):
    """Extract opcode -> CSS_enum mapping from parse spec string."""
    opcodes = []
    if not parse_spec:
        return opcodes
    
    tokens = parse_spec.split()
    skip_idents = {'INHERIT:', 'INITIAL:', 'REVERT:', 'UNSET:', 'NONE:', 'IDENT:(', 'IDENT:)'}
    
    for token in tokens:
        if ':' not in token:
            continue
        if token in skip_idents:
            continue
        # Parse KEYWORD:flags,OPCODE_NAME
        parts = token.split(':')
        if len(parts) != 2:
            continue
        value_part = parts[1]
        if ',' in value_part:
            _, opcode_name = value_part.split(',', 1)
            opcodes.append(opcode_name)
    
    return opcodes


def generate_cascade_file(name, prop_data):
    """Generate a complete cascade .c file for a property."""
    cascade_type = prop_data.get('cascade')
    if not cascade_type or cascade_type in ('none', 'manual'):
        return None
    
    NAME = upper_name(name)
    initial = prop_data.get('initial', 'AUTO')
    initial_data = prop_data.get('initial_data', [])
    condition = prop_data.get('condition')
    # Use enum_prefix from TOML if available, else derive from property name
    enum_prefix = prop_data.get('enum_prefix', f'CSS_{NAME}')
    
    # Safety check (Option B): detect when initial_data would cause arena
    # memcmp mismatches. The setter unconditionally stores all values,
    # but the getter only reads them when type matches `condition`.
    # If initial_data differs from TYPE_INFO defaults, the copy/compose
    # var initializers won't match the initial function's stored values.
    info = TYPE_INFO[cascade_type]
    if condition and info.get('get_vars') and initial_data:
        defaults = [v[2] for v in info['get_vars'] if len(v) > 2 and v[2] is not None]
        if defaults and list(initial_data) != defaults:
            print(f'SKIP: {name} — initial_data {initial_data} differs from '
                  f'TYPE_INFO defaults {defaults}. '
                  f'Conditional getter would cause arena memcmp mismatch. '
                  f'Leave as manual cascade file.')
            return None
    
    lines = []
    lines.append('/*')
    lines.append(' * This file is auto-generated from properties.toml — do not edit.')
    lines.append(' */')
    lines.append('')
    lines.append('#include "utils/css_utils.h"')
    lines.append('#include "bytecode/bytecode.h"')
    lines.append('#include "bytecode/opcodes.h"')
    lines.append('#include "select/propget.h"')
    lines.append('#include "select/propset.h"')
    lines.append('')
    lines.append('#include "select/properties/helpers.h"')
    lines.append('#include "select/properties/properties.h"')
    lines.append('')
    
    # 1. cascade function
    if cascade_type == 'ident':
        lines.extend(generate_cascade_ident(name, NAME, prop_data, enum_prefix, initial))
    elif cascade_type == 'paint_color':
        lines.extend(generate_cascade_paint_color(name, NAME, prop_data, enum_prefix))
    else:
        helper = HELPER_MAP[cascade_type]
        lines.append(f'css_error css__cascade_{name}(uint32_t opv, css_style *style, css_select_state *state)')
        lines.append('{')
        lines.append(f'    return {helper}(opv, style, state, set_{name});')
        lines.append('}')
    
    lines.append('')
    
    # 2. set_from_hint function
    info = TYPE_INFO[cascade_type]
    lines.append(f'css_error css__set_{name}_from_hint(const css_hint *hint, css_computed_style *style)')
    lines.append('{')
    
    if info.get('counter'):
        # Counter special handling
        cname = name  # use property name as variable
        set_enum = f'{enum_prefix}_NAMED'
        lines.append(f'    css_computed_counter *item;')
        lines.append(f'    css_error error;')
        lines.append(f'')
        lines.append(f'    error = set_{name}(style, hint->status, hint->data.counter);')
        lines.append(f'')
        lines.append(f'    if (hint->status == {set_enum} && hint->data.counter != NULL) {{')
        lines.append(f'        for (item = hint->data.counter; item->name != NULL; item++) {{')
        lines.append(f'            lwc_string_unref(item->name);')
        lines.append(f'        }}')
        lines.append(f'    }}')
        lines.append(f'')
        lines.append(f'    if (error != CSS_OK && hint->data.counter != NULL)')
        lines.append(f'        free(hint->data.counter);')
        lines.append(f'')
        lines.append(f'    return error;')
    elif info.get('hint_cleanup'):
        # URI type — needs lwc_string_unref
        lines.append(f'    css_error error;')
        lines.append(f'')
        lines.append(f'    error = set_{name}(style, hint->status, hint->data.string);')
        lines.append(f'')
        lines.append(f'    lwc_string_unref(hint->data.string);')
        lines.append(f'')
        lines.append(f'    return error;')
    elif info['hint_data'] is not None:
        lines.append(f'    return set_{name}(style, hint->status, {info["hint_data"]});')
    else:
        lines.append(f'    return set_{name}(style, hint->status);')
    
    lines.append('}')
    lines.append('')
    
    # 3. initial function
    lines.append(f'css_error css__initial_{name}(css_select_state *state)')
    lines.append('{')
    
    initial_val = f'{enum_prefix}_{initial}'
    if initial_data:
        extra_args = ', '.join(initial_data)
        lines.append(f'    return set_{name}(state->computed, {initial_val}, {extra_args});')
    else:
        lines.append(f'    return set_{name}(state->computed, {initial_val});')
    
    lines.append('}')
    lines.append('')
    
    # 4. copy function
    lines.append(f'css_error css__copy_{name}(const css_computed_style *from, css_computed_style *to)')
    lines.append('{')
    
    if info.get('counter'):
        cname = name
        lines.append(f'    css_error error;')
        lines.append(f'    css_computed_counter *copy = NULL;')
        lines.append(f'    const css_computed_counter *{cname} = NULL;')
        lines.append(f'    uint8_t type = get_{name}(from, &{cname});')
        lines.append(f'')
        lines.append(f'    if (from == to) {{')
        lines.append(f'        return CSS_OK;')
        lines.append(f'    }}')
        lines.append(f'')
        lines.append(f'    error = css__copy_computed_counter_array(false, {cname}, &copy);')
        lines.append(f'    if (error != CSS_OK) {{')
        lines.append(f'        return CSS_NOMEM;')
        lines.append(f'    }}')
        lines.append(f'')
        lines.append(f'    error = set_{name}(to, type, copy);')
        lines.append(f'    if (error != CSS_OK) {{')
        lines.append(f'        free(copy);')
        lines.append(f'    }}')
        lines.append(f'')
        lines.append(f'    return error;')
    elif info['get_vars']:
        for idx, var in enumerate(info['get_vars']):
            vtype = var[0]
            vname = var[1].replace('{name}', name) if '{name}' in var[1] else var[1]
            vinit = var[2] if len(var) > 2 and var[2] is not None else None
            sep = '' if vtype.endswith('*') or vtype.endswith(' ') else ' '
            if vinit:
                lines.append(f'    {vtype}{sep}{vname} = {vinit};')
            else:
                lines.append(f'    {vtype}{sep}{vname};')
        
        get_extra = info['get_extra'].replace('{name}', name)
        set_extra = info['set_extra'].replace('{name}', name)
        
        lines.append(f'    uint8_t type = get_{name}(from{get_extra});')
        lines.append(f'')
        lines.append(f'    if (from == to) {{')
        lines.append(f'        return CSS_OK;')
        lines.append(f'    }}')
        lines.append(f'')
        lines.append(f'    return set_{name}(to, type{set_extra});')
    else:
        lines.append(f'    if (from == to) {{')
        lines.append(f'        return CSS_OK;')
        lines.append(f'    }}')
        lines.append(f'')
        lines.append(f'    return set_{name}(to, get_{name}(from));')
    
    lines.append('}')
    lines.append('')
    
    # 5. compose function — use wrapped format to match originals
    lines.append(f'css_error css__compose_{name}(')
    lines.append(f'    const css_computed_style *parent, const css_computed_style *child, css_computed_style *result)')
    lines.append('{')
    
    if info.get('counter'):
        cname = name
        get_extra = info['get_extra'].replace('{name}', name)
        lines.append(f'    const css_computed_counter *{cname} = NULL;')
        lines.append(f'    uint8_t type = get_{name}(child{get_extra});')
    elif info['get_vars']:
        for idx, var in enumerate(info['get_vars']):
            vtype = var[0]
            vname = var[1].replace('{name}', name) if '{name}' in var[1] else var[1]
            vinit = var[2] if len(var) > 2 and var[2] is not None else None
            sep = '' if vtype.endswith('*') or vtype.endswith(' ') else ' '
            if vinit:
                lines.append(f'    {vtype}{sep}{vname} = {vinit};')
            else:
                lines.append(f'    {vtype}{sep}{vname};')
        
        get_extra = info['get_extra'].replace('{name}', name)
        lines.append(f'    uint8_t type = get_{name}(child{get_extra});')
    else:
        lines.append(f'    uint8_t type = get_{name}(child);')
    
    lines.append(f'')
    lines.append(f'    return css__copy_{name}(type == {enum_prefix}_INHERIT ? parent : child, result);')
    lines.append('}')
    lines.append('')
    
    return '\n'.join(lines)


def generate_cascade_ident(name, NAME, prop_data, enum_prefix, initial):
    """Generate the cascade function for IDENT-switch properties."""
    # When hasFlagValue is true (inherit/initial/unset/revert), the switch
    # is skipped, so this default is stored into the computed style bits.
    # The compose function later checks `get_xxx(child) == CSS_XXX_INHERIT`
    # to decide whether to copy from parent — so we MUST default to INHERIT
    # (0x0), not the initial value.  For FLAG_VALUE_INITIAL, the finalize
    # step in select.c calls css__initial_xxx() which overwrites correctly.
    lines = []
    lines.append(f'css_error css__cascade_{name}(uint32_t opv, css_style *style, css_select_state *state)')
    lines.append('{')
    lines.append(f'    uint16_t value = {enum_prefix}_INHERIT;')
    lines.append(f'')
    lines.append(f'    UNUSED(style);')
    lines.append(f'')
    lines.append(f'    if (hasFlagValue(opv) == false) {{')
    lines.append(f'        switch (getValue(opv)) {{')
    
    # Extract opcodes from parse spec
    parse_spec = prop_data.get('parse', '')
    opcodes = parse_opcodes_from_spec(parse_spec, NAME)

    if not opcodes:
        print(f"FATAL: cascade = 'ident' for '{name}' but no opcodes found in parse spec.",
              file=sys.stderr)
        if not parse_spec:
            print(f"  Property has no 'parse' field. Either add a parse spec with keyword",
                  f"opcodes, or use cascade = 'manual'.", file=sys.stderr)
        else:
            print(f"  Parse spec '{parse_spec}' yielded no keyword opcodes.", file=sys.stderr)
        sys.exit(1)

    for opcode in opcodes:
        lines.append(f'        case {opcode}:')
        lines.append(f'            value = CSS_{opcode};')
        lines.append(f'            break;')

    
    lines.append(f'        }}')
    lines.append(f'    }}')
    lines.append(f'')
    lines.append(f'    if (css__outranks_existing(getOpcode(opv), isImportant(opv), state, getFlagValue(opv))) {{')
    lines.append(f'        return set_{name}(state->computed, value);')
    lines.append(f'    }}')
    lines.append(f'')
    lines.append(f'    return CSS_OK;')
    lines.append('}')
    
    return lines


def generate_cascade_paint_color(name, NAME, prop_data, enum_prefix):
    """Generate the cascade function for SVG paint-color properties.

    Unlike bg_border_color, this properly distinguishes NONE from COLOR.
    Opcodes: {NAME}_NONE, {NAME}_CURRENT_COLOR, {NAME}_SET
    CSS enums: CSS_{NAME}_NONE, CSS_{NAME}_COLOR, CSS_{NAME}_CURRENT_COLOR
    """
    lines = []
    lines.append(f'css_error css__cascade_{name}(uint32_t opv, css_style *style, css_select_state *state)')
    lines.append('{')
    lines.append(f'    uint16_t value = {enum_prefix}_INHERIT;')
    lines.append(f'    css_color color = 0;')
    lines.append(f'')
    lines.append(f'    if (hasFlagValue(opv) == false) {{')
    lines.append(f'        switch (getValue(opv)) {{')
    lines.append(f'        case {NAME}_NONE:')
    lines.append(f'            value = {enum_prefix}_NONE;')
    lines.append(f'            break;')
    lines.append(f'        case {NAME}_CURRENT_COLOR:')
    lines.append(f'            value = {enum_prefix}_CURRENT_COLOR;')
    lines.append(f'            break;')
    lines.append(f'        case {NAME}_SET:')
    lines.append(f'            value = {enum_prefix}_COLOR;')
    lines.append(f'            color = *((css_color *)style->bytecode);')
    lines.append(f'            advance_bytecode(style, sizeof(color));')
    lines.append(f'            break;')
    lines.append(f'        }}')
    lines.append(f'    }}')
    lines.append(f'')
    lines.append(f'    if (css__outranks_existing(getOpcode(opv), isImportant(opv), state, getFlagValue(opv))) {{')
    lines.append(f'        return set_{name}(state->computed, value, color);')
    lines.append(f'    }}')
    lines.append(f'')
    lines.append(f'    return CSS_OK;')
    lines.append('}')
    return lines


def main():
    if len(sys.argv) < 3:
        print(f"Usage: {sys.argv[0]} properties.toml output_dir [--diff source_dir]")
        sys.exit(1)
    
    toml_path = sys.argv[1]
    output_dir = sys.argv[2]
    diff_dir = None
    if len(sys.argv) >= 5 and sys.argv[3] == '--diff':
        diff_dir = sys.argv[4]
    
    with open(toml_path, 'rb') as f:
        data = tomllib.load(f)
    
    os.makedirs(output_dir, exist_ok=True)
    
    generated = 0
    diffs = 0
    
    for name, props in data.items():
        if not isinstance(props, dict):
            continue
        if 'cascade' not in props:
            continue
        
        content = generate_cascade_file(name, props)
        if content is None:
            continue
        
        out_path = os.path.join(output_dir, f'{name}.c')
        with open(out_path, 'w', encoding='utf-8') as f:
            f.write(content)
        generated += 1
        
        if diff_dir:
            src_path = os.path.join(diff_dir, f'{name}.c')
            if os.path.exists(src_path):
                import subprocess
                result = subprocess.run(['diff', '-u', src_path, out_path], 
                                       capture_output=True, text=True)
                if result.returncode != 0:
                    print(f"DIFF: {name}.c")
                    print(result.stdout[:500])
                    diffs += 1
            else:
                print(f"MISSING: {src_path}")
                diffs += 1
    
    print(f"\nGenerated {generated} cascade files to {output_dir}")
    if diff_dir:
        print(f"Files with differences: {diffs}")


if __name__ == '__main__':
    main()

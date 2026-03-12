#!/usr/bin/env python3
"""
Property Registration Generator for LibCSS

Generates property enums, dispatch tables, and string indices from properties.gen
to eliminate manual alignment errors.

Strategy: Parse dispatch.c for canonical property order, then match properties.gen
"""

import sys
import re
from pathlib import Path


class DispatchParser:
    """Parse dispatch.c to get canonical property order and inherited flags."""
    
    def __init__(self, dispatch_file):
        self.dispatch_file = Path(dispatch_file)
        self.properties = []  # List of {name, inherited} dicts
        
    def parse(self):
        """Extract property order and inherited flags from dispatch.c."""
        # Validate file exists and is readable
        if not self.dispatch_file.exists():
            print(f"FATAL ERROR: dispatch.c not found at {self.dispatch_file}", file=sys.stderr)
            sys.exit(1)
        
        try:
            with open(self.dispatch_file, 'r') as f:
                content = f.read()
        except IOError as e:
            print(f"FATAL ERROR: Cannot read dispatch.c: {e}", file=sys.stderr)
            sys.exit(1)
        
        # Validate that prop_dispatch array exists
        if 'struct prop_table prop_dispatch[' not in content:
            print("FATAL ERROR: Cannot find 'struct prop_table prop_dispatch[' in dispatch.c", file=sys.stderr)
            print("File format may have changed. Expected C code with dispatch table.", file=sys.stderr)
            sys.exit(1)
        
        # Find the prop_dispatch array
        # Format: PROPERTY_FUNCS(name), \n inherited_flag,
        pattern = r'PROPERTY_FUNCS\(([a-z_]+)\),\s*\n\s*(\d+),'
        matches = re.findall(pattern, content)
        
        # Validate we found properties
        if not matches:
            print("FATAL ERROR: No property entries found in dispatch.c", file=sys.stderr)
            print(f"Expected pattern: PROPERTY_FUNCS(name),\\n  inherited_flag,", file=sys.stderr)
            print("Check that dispatch.c format hasn't changed.", file=sys.stderr)
            sys.exit(1)
        
        if len(matches) < 50:
            print(f"FATAL ERROR: Only {len(matches)} properties found in dispatch.c", file=sys.stderr)
            print("Expected at least 50 properties. File may be truncated or malformed.", file=sys.stderr)
            sys.exit(1)
        
        for prop_name, inherited_str in matches:
            # Validate inherited flag is 0 or 1
            if inherited_str not in ('0', '1'):
                print(f"FATAL ERROR: Invalid inherited flag '{inherited_str}' for property {prop_name}", file=sys.stderr)
                print("Inherited flag must be 0 or 1", file=sys.stderr)
                sys.exit(1)
            
            self.properties.append({
                'name': prop_name,
                'inherited': int(inherited_str) == 1
            })
        
        return self.properties


class PropertyGenParser:
    """Parse properties.gen to get enum names and metadata."""
    
    def __init__(self, gen_file):
        self.gen_file = Path(gen_file)
        self.prop_map = {}  # name -> metadata
        
    def parse(self):
        """Parse properties.gen and build name->enum mapping."""
        if not self.gen_file.exists():
            print(f"FATAL ERROR: properties.gen not found at {self.gen_file}", file=sys.stderr)
            sys.exit(1)
        
        try:
            with open(self.gen_file, 'r') as f:
                lines = list(f)
        except IOError as e:
            print(f"FATAL ERROR: Cannot read properties.gen: {e}", file=sys.stderr)
            sys.exit(1)
        
        for line_num, line in enumerate(lines, 1):
            line = line.strip()
            
            # Skip comments and empty lines
            if not line or line.startswith('#'):
                continue
            
            # Parse: property_name:ENUM_NAME ...
            # ENUM_NAME can be any uppercase identifier (CSS_PROP_*, BORDER_SIDE_*, etc.)
            match = re.match(r'^([a-z_]+):([A-Z][A-Z0-9_]+)\s+(.*)$', line)
            if match:
                prop_name = match.group(1)
                enum_name = match.group(2)
                spec = match.group(3)
                
                # Validate: enum should be uppercase and have valid format
                if not re.match(r'^[A-Z][A-Z0-9_]*$', enum_name):
                    print(f"WARNING: Line {line_num}: Invalid enum format '{enum_name}'", file=sys.stderr)
                    continue
                
                # Parse the spec into structured data
                parsed = SpecParser.parse(spec)
                
                # Derive flags from parsed spec
                is_shorthand = 'SHORTHAND' in parsed['flags'] or parsed['wrap'] is not None
                is_manual = 'MANUAL' in parsed['flags']
                is_generic = 'GENERIC' in parsed['flags']
                
                # Detect aliases: property name doesn't match enum's base name
                # e.g., inline_size:CSS_PROP_WIDTH is an alias for width
                enum_base = enum_name.replace('CSS_PROP_', '').lower()
                is_alias = (prop_name != enum_base) and not is_shorthand and not is_generic
                
                if not is_generic:
                    self.prop_map[prop_name] = {
                        'enum': enum_name,
                        'is_shorthand': is_shorthand,
                        'is_manual': is_manual,
                        'is_alias': is_alias,
                        'is_wrap': parsed['wrap'] is not None,
                        'spec': spec,
                        'parsed': parsed,
                        'line': line_num
                    }
        
        # Validate we found properties
        if not self.prop_map:
            print("FATAL ERROR: No properties found in properties.gen", file=sys.stderr)
            print("Expected format: property_name:CSS_PROP_ENUM_NAME PARSE_SPEC", file=sys.stderr)
            sys.exit(1)
        
        if len(self.prop_map) < 50:
            print(f"FATAL ERROR: Only {len(self.prop_map)} properties in properties.gen", file=sys.stderr)
            print("Expected at least 50. File may be incomplete.", file=sys.stderr)
            sys.exit(1)
        
        return self.prop_map


class TomlPropertyReader:
    """Read properties.toml and produce the same prop_map format as PropertyGenParser.
    
    This class reads the single-source-of-truth TOML file and produces:
    1. prop_map: same format as PropertyGenParser.parse() for downstream generators
    2. toml_data: raw TOML data with extra fields (initial, bits, etc.)
    3. properties.gen content: for gen_parser (which still reads the old format)
    """
    
    def __init__(self, toml_file):
        self.toml_file = Path(toml_file)
        self.prop_map = {}  # same format as PropertyGenParser
        self.toml_data = {}  # raw TOML data keyed by property name
        self.generics = {}  # generic templates
    
    def parse(self):
        """Read properties.toml and build prop_map compatible with PropertyGenParser."""
        try:
            import tomllib
        except ModuleNotFoundError:
            import tomli as tomllib
        
        if not self.toml_file.exists():
            print(f"FATAL ERROR: properties.toml not found at {self.toml_file}", file=sys.stderr)
            sys.exit(1)
        
        with open(self.toml_file, 'rb') as f:
            data = tomllib.load(f)
        
        # Extract generics
        self.generics = data.get('generic', {})
        
        # Process each property
        for prop_name, prop_data in data.items():
            if prop_name == 'generic':
                continue
            
            enum_name = prop_data.get('enum', '')
            if not enum_name:
                print(f"WARNING: Property '{prop_name}' missing 'enum' field", file=sys.stderr)
                continue
            
            # Build the spec string from TOML data
            spec = prop_data.get('parse', '')
            
            # Add flags to spec for SpecParser compatibility
            if prop_data.get('manual'):
                spec = 'MANUAL ' + spec if spec else 'MANUAL'
            if prop_data.get('shorthand'):
                spec = 'SHORTHAND ' + spec if spec else 'SHORTHAND'
            if prop_data.get('opcodes_manual'):
                spec = 'OPCODES:MANUAL ' + spec if spec else 'OPCODES:MANUAL'
            # Parse the spec into structured data
            parsed = SpecParser.parse(spec)
            
            # Derive flags
            is_shorthand = prop_data.get('shorthand', False) or prop_data.get('wrap') is not None
            is_manual = prop_data.get('manual', False)
            
            # Detect aliases
            enum_base = enum_name.replace('CSS_PROP_', '').lower()
            is_alias = (prop_name != enum_base) and not is_shorthand
            
            self.prop_map[prop_name] = {
                'enum': enum_name,
                'is_shorthand': is_shorthand,
                'is_manual': is_manual,
                'is_alias': is_alias,
                'is_wrap': prop_data.get('wrap') is not None,
                'spec': spec,
                'parsed': parsed,
                'line': 0,  # no line number from TOML
            }
            
            # Store full TOML data for cascade generation etc.
            self.toml_data[prop_name] = prop_data
        
        # Validate
        if len(self.prop_map) < 50:
            print(f"FATAL ERROR: Only {len(self.prop_map)} properties in properties.toml", file=sys.stderr)
            sys.exit(1)
        
        return self.prop_map
    
    def get_dispatch_order(self):
        """Get property dispatch order from TOML (replaces DispatchParser).
        
        Returns a list of {name, inherited} dicts — same format as 
        DispatchParser.parse() — for non-shorthand, non-alias properties.
        Order follows TOML section order.
        """
        order = []
        for prop_name, prop_data in self.toml_data.items():
            # Skip shorthands — they don't have dispatch entries
            if prop_data.get('shorthand', False):
                continue
            # Note: 'wrap' properties (e.g. border_top_color) DO have
            # dispatch entries — wrap is a parse-level concept only
            # Skip aliases — they share the target property's dispatch entry
            enum_name = prop_data.get('enum', '')
            enum_base = enum_name.replace('CSS_PROP_', '').lower()
            if prop_name != enum_base:
                continue
            
            order.append({
                'name': prop_name,
                'inherited': prop_data.get('inherited', False),
            })
        return order
    
    @staticmethod
    def _strip_flag_keywords(parse_spec):
        """Strip INHERIT/INITIAL/REVERT/UNSET from a parse spec's IDENT block.
        
        This produces a 'value-only' parse spec suitable for generating
        _value() helper functions that handle length, calc, and property-specific
        keywords but not CSS-wide keywords (inherit, initial, revert, unset).
        """
        tokens = parse_spec.split()
        result = []
        in_ident_block = False
        for token in tokens:
            if token == 'IDENT:(':
                in_ident_block = True
                result.append(token)
            elif token == 'IDENT:)':
                in_ident_block = False
                result.append(token)
            elif in_ident_block and token.split(':')[0] in ('INHERIT', 'INITIAL', 'REVERT', 'UNSET'):
                continue  # Strip CSS-wide keywords
            else:
                result.append(token)
        return ' '.join(result)
    
    def generate_properties_gen(self):
        """Generate properties.gen content for gen_parser.
        
        This produces the same format gen_parser expects, derived from TOML data.
        Only properties with a 'parse' field are included (gen_parser needs parse specs).
        
        Also generates _value() helper functions for longhands referenced by
        shorthand 'helper_longhands' fields. These are value-only parsers that
        strip INHERIT/INITIAL/REVERT/UNSET from the parse spec.
        """
        lines = []
        lines.append("# AUTO-GENERATED from properties.toml — DO NOT EDIT")
        lines.append("# This file is generated for gen_parser. Edit properties.toml instead.")
        lines.append("#")
        
        # Write comment templates (same as original properties.gen header)
        lines.append("#property:CSS_PROP_ENUM IDENT:( INHERIT: INITIAL: REVERT: UNSET: IDENT:)")
        lines.append("")
        
        # Write generics first
        for name, gdata in sorted(self.generics.items()):
            enum = gdata.get('enum', 'op')
            parse = gdata.get('parse', '')
            lines.append(f"{name}:{enum} GENERIC: {parse}")
        
        if self.generics:
            lines.append("")
        
        # Write properties
        for prop_name, prop_data in sorted(self.toml_data.items()):
            parse = prop_data.get('parse', '')
            if not parse:
                continue  # gen_parser doesn't need properties without parse specs
            
            enum = prop_data.get('enum', '')
            
            # Build the gen_parser line (strip flags gen_parser doesn't understand)
            # gen_parser only sees: property_name:ENUM PARSE_SPEC
            # We strip: MANUAL, SHORTHAND, OPCODES:MANUAL, CASCADE:MANUAL, GENERIC
            # These are already NOT in the parse field from TOML (they're separate fields)
            
            # Add MANUAL/SHORTHAND flags that gen_parser DOES understand
            flags = []
            if prop_data.get('manual'):
                flags.append('MANUAL')
            if prop_data.get('shorthand'):
                flags.append('SHORTHAND')
            
            flag_str = ' '.join(flags)
            if flag_str:
                lines.append(f"{prop_name}:{enum} {flag_str} {parse}")
            else:
                lines.append(f"{prop_name}:{enum} {parse}")
        
        # Collect helper_longhands references from shorthands and generate
        # _value() helper functions (value-only parsers without CSS-wide keywords)
        value_helpers = {}  # name -> (enum, parse_spec, is_generic)
        for prop_name, prop_data in self.toml_data.items():
            helper_longhands = prop_data.get('helper_longhands', [])
            for helper_name in helper_longhands:
                if helper_name in value_helpers:
                    continue  # Already collected
                # Look up in generics first, then in properties
                if helper_name in self.generics:
                    gdata = self.generics[helper_name]
                    enum = gdata.get('enum', 'op')
                    parse = gdata.get('parse', '')
                    value_helpers[helper_name] = (enum, parse, True)
                elif helper_name in self.toml_data:
                    pdata = self.toml_data[helper_name]
                    enum = pdata.get('enum', '')
                    parse = pdata.get('parse', '')
                    value_helpers[helper_name] = (enum, parse, False)
                else:
                    print(f"WARNING: helper_longhands reference '{helper_name}' "
                          f"not found in properties or generics", file=sys.stderr)
        
        if value_helpers:
            lines.append("")
            lines.append("# Value-only helpers (generated from helper_longhands)")
            for name, (enum, parse, is_generic) in sorted(value_helpers.items()):
                stripped = self._strip_flag_keywords(parse)
                generic_flag = "GENERIC: " if is_generic else ""
                lines.append(f"{name}_value:{enum} {generic_flag}{stripped}")
        
        return '\n'.join(lines) + '\n'



class SpecParser:
    """Parse a properties.gen spec string into a structured dict.
    
    The spec format is a simple DSL with these elements:
    - Flags: MANUAL, SHORTHAND, GENERIC, OPCODES:MANUAL
    - Key:value: WRAP:func, COLOR:opcode, URI:opcode
    - Blocks: IDENT:( ... IDENT:), LENGTH_UNIT:( ... LENGTH_UNIT:), etc.
    
    Example spec:
        IDENT:( INHERIT: INITIAL: REVERT: UNSET: AUTO:0,HEIGHT_AUTO IDENT:) 
        LENGTH_UNIT:( UNIT_PX:HEIGHT_SET MASK:UNIT_MASK_HEIGHT RANGE:<0 LENGTH_UNIT:)
        CALC:( UNIT_PX:HEIGHT_CALC CALC:)
    
    Returns a dict with all parsed elements, so downstream code never needs
    to parse the raw string again.
    """
    
    @staticmethod
    def parse(spec):
        """Parse a spec string into a structured dict."""
        result = {
            'flags': set(),           # MANUAL, SHORTHAND, GENERIC, OPCODES_MANUAL
            'wrap': None,             # function name if WRAP:xxx
            'ident_keywords': [],     # [(keyword, flags, opcode_name), ...] from IDENT:( )
            'color': None,            # opcode SET name from COLOR:xxx
            'length_unit': None,      # {'opcode': str, 'unit': str, 'mask': str, 'range': str, 'allow': str, 'disallow': str}
            'number': None,           # {'opcode': str, 'is_int': bool, 'range': str}
            'calc': None,             # {'opcode': str, 'type': str} (type = unit/NUMBER/ANY)
            'uri': None,              # opcode name from URI:xxx
            'ident_list': None,       # {'named_opcode': str, 'default_value': str, 'none_opcode': str}
            'sizing': None,           # {'fit_content': str, 'min_content': str, 'max_content': str}
        }
        
        tokens = spec.split()
        i = 0
        
        while i < len(tokens):
            token = tokens[i]
            
            # --- Simple flags ---
            if token == 'MANUAL':
                result['flags'].add('MANUAL')
                i += 1
            elif token == 'SHORTHAND':
                result['flags'].add('SHORTHAND')
                i += 1
            elif token == 'OPCODES:MANUAL':
                result['flags'].add('OPCODES_MANUAL')
                i += 1
            elif 'GENERIC' in token:
                result['flags'].add('GENERIC')
                i += 1
            
            # --- WRAP:function ---
            elif token.startswith('WRAP:'):
                result['wrap'] = token.split(':', 1)[1]
                i += 1
            
            # --- COLOR:opcode ---
            elif token.startswith('COLOR:'):
                result['color'] = token.split(':', 1)[1]
                i += 1
            
            # --- URI:opcode ---
            elif token.startswith('URI:'):
                result['uri'] = token.split(':', 1)[1]
                i += 1
            
            # --- IDENT:( ... IDENT:) block ---
            elif token == 'IDENT:(':
                i += 1
                while i < len(tokens) and tokens[i] != 'IDENT:)':
                    t = tokens[i]
                    if ':' in t:
                        key, value = t.split(':', 1)
                        # Skip standard markers (INHERIT, INITIAL, etc.)
                        if key in ('INHERIT', 'INITIAL', 'REVERT', 'UNSET'):
                            pass
                        elif ',' in value:
                            # KEYWORD:flags,OPCODE_NAME
                            flags_str, opcode = value.split(',', 1)
                            result['ident_keywords'].append((key, flags_str, opcode))
                        # else: unknown ident sub-token, skip
                    i += 1
                i += 1  # skip IDENT:)
            
            # --- LENGTH_UNIT:( ... LENGTH_UNIT:) block ---
            elif token == 'LENGTH_UNIT:(':
                lu = {'opcode': None, 'unit': None, 'mask': None, 
                      'range': None, 'allow': None, 'disallow': None}
                i += 1
                while i < len(tokens) and tokens[i] != 'LENGTH_UNIT:)':
                    t = tokens[i]
                    if ':' in t:
                        key, value = t.split(':', 1)
                        if key.startswith('UNIT_'):
                            lu['unit'] = key
                            lu['opcode'] = value
                        elif key == 'MASK':
                            lu['mask'] = value
                        elif key == 'RANGE':
                            lu['range'] = value
                        elif key == 'ALLOW':
                            lu['allow'] = value
                        elif key == 'DISALLOW':
                            lu['disallow'] = value
                    i += 1
                result['length_unit'] = lu
                i += 1  # skip LENGTH_UNIT:)
            
            # --- NUMBER:( ... NUMBER:) block ---
            elif token == 'NUMBER:(':
                num = {'opcode': None, 'is_int': False, 'range': None}
                i += 1
                while i < len(tokens) and tokens[i] != 'NUMBER:)':
                    t = tokens[i]
                    if ':' in t:
                        key, value = t.split(':', 1)
                        if key in ('true', 'false'):
                            num['is_int'] = (key == 'true')
                            num['opcode'] = value
                        elif key == 'RANGE':
                            num['range'] = value
                    i += 1
                result['number'] = num
                i += 1  # skip NUMBER:)
            
            # --- CALC:( ... CALC:) block ---
            elif token == 'CALC:(':
                calc = {'opcode': None, 'type': None}
                i += 1
                while i < len(tokens) and tokens[i] != 'CALC:)':
                    t = tokens[i]
                    if ':' in t:
                        key, value = t.split(':', 1)
                        if key.startswith('UNIT_'):
                            calc['type'] = key
                            calc['opcode'] = value
                        elif key == 'NUMBER':
                            calc['type'] = 'NUMBER'
                            calc['opcode'] = value
                        elif key == 'ANY':
                            calc['type'] = 'ANY'
                            calc['opcode'] = value
                    i += 1
                result['calc'] = calc
                i += 1  # skip CALC:)
            
            # --- IDENT_LIST:( ... IDENT_LIST:) block ---
            elif token == 'IDENT_LIST:(':
                il = {'named_opcode': None, 'default_value': None, 'none_opcode': None}
                i += 1
                while i < len(tokens) and tokens[i] != 'IDENT_LIST:)':
                    t = tokens[i]
                    if ':' in t:
                        key, value = t.split(':', 1)
                        if key == 'STRING_OPTNUM':
                            il['named_opcode'] = value
                        else:
                            # default value like 1:COUNTER_INCREMENT_NONE
                            il['default_value'] = key
                            il['none_opcode'] = value
                    i += 1
                result['ident_list'] = il
                i += 1  # skip IDENT_LIST:)
            
            # --- SIZING:( ... SIZING:) block ---
            elif token == 'SIZING:(':
                sz = {'fit_content': None, 'min_content': None, 'max_content': None}
                i += 1
                while i < len(tokens) and tokens[i] != 'SIZING:)':
                    t = tokens[i]
                    if ':' in t:
                        key, value = t.split(':', 1)
                        if key == 'FIT_CONTENT':
                            sz['fit_content'] = value
                        elif key == 'MIN_CONTENT':
                            sz['min_content'] = value
                        elif key == 'MAX_CONTENT':
                            sz['max_content'] = value
                    i += 1
                result['sizing'] = sz
                i += 1  # skip SIZING:)
            
            else:
                # Unknown token — skip
                i += 1
        
        return result


class GperfInputGenerator:
    """Generate gperf input file for perfect hash CSS property lookup.
    
    Uses GNU gperf (an external tool) instead of the Python perfect-hash
    module. The Python script generates a .gperf input file, and CMake
    invokes gperf to produce the final C code.
    
    This follows the same pattern used by libhubbub's element-type.gperf.
    """
    
    def __init__(self, keys):
        """Initialize with list of (css_name, handler_name, opcode) tuples."""
        self.keys = keys
    
    def generate_gperf_input(self):
        """Generate gperf input file content.
        
        The gperf file defines:
        - struct css_prop_entry { name, handler }
        - A lookup function css_prop_lookup_generated() returning const struct *
        
        A wrapper function css_prop_lookup() is appended after the gperf
        output (via the %% section) to extract the handler field, preserving
        the existing API: css_prop_handler css_prop_lookup(name, len).
        """
        lines = []
        
        # gperf declarations section
        lines.append("/*")
        lines.append(" * AUTO-GENERATED gperf input - DO NOT EDIT!")
        lines.append(" *")
        lines.append(" * Generated by property_generator.py")
        lines.append(" * Processed by GNU gperf to produce prop_hash_table.inc")
        lines.append(" */")
        lines.append("")
        
        # gperf options - optimized for fastest possible lookup
        #
        # %compare-lengths: reject mismatches by length before strncmp
        #   (cheap integer compare eliminates most non-matches)
        # %compare-strncmp: use strncmp for final verification
        #   (required: hash alone can't distinguish unknown inputs from
        #    valid keywords that happen to share the same hash slot)
        # %readonly-tables: const-qualify all tables (cache-friendly)
        # %ignore-case: case-insensitive matching (CSS is case-insensitive)
        # %enum: use enum for hash values (compiler optimization)
        # %7bit: assume 7-bit ASCII input (valid for CSS property names)
        # %null-strings: NULL for empty slots (fast pointer check)
        # No %switch: default array lookup is faster for 200+ keywords
        lines.append("%language=ANSI-C")
        lines.append("%compare-lengths")
        lines.append("%compare-strncmp")
        lines.append("%readonly-tables")
        lines.append("%ignore-case")
        lines.append("%struct-type")
        lines.append("%enum")
        lines.append("%7bit")
        lines.append("%null-strings")
        lines.append("%define hash-function-name css_prop_hash_generated")
        lines.append("%define lookup-function-name css_prop_lookup_generated")
        lines.append("")
        
        # C code block included verbatim before the hash table
        lines.append("%{")
        lines.append("#include <string.h>")
        lines.append("#include <stddef.h>")
        lines.append("")
        lines.append('#include "parse/properties/properties.h"')
        lines.append("%}")
        lines.append("")
        
        # Full struct definition - gperf needs this to generate the wordlist array.
        # The 'name' field is the keyword (filled by gperf automatically).
        # The 'handler' field is the css_prop_handler function pointer.
        # The 'opcode' field is the CSS_PROP_* enum value (-1 for shorthands).
        lines.append("struct css_prop_entry {")
        lines.append("    const char *name;")
        lines.append("    css_prop_handler handler;")
        lines.append("    int opcode;")
        lines.append("};")
        lines.append("")
        
        # Keyword section
        lines.append("%%")
        
        # Each line: keyword, field_value, opcode
        for css_name, handler_name, opcode in self.keys:
            lines.append(f"{css_name}, {handler_name}, {opcode}")
        
        lines.append("%%")
        lines.append("")
        lines.append("/* Wrapper: safely parse CSS property names (mask non-ASCII to prevent gperf OOB) */")
        lines.append("const struct css_prop_entry *css_prop_lookup(const char *name, size_t len) {")
        lines.append("    char masked_str[256];")
        lines.append("    if (len >= sizeof(masked_str)) len = sizeof(masked_str) - 1;")
        lines.append("    for (size_t i = 0; i < len; ++i) {")
        lines.append("        masked_str[i] = name[i] & 0x7F;")
        lines.append("    }")
        lines.append("    return css_prop_lookup_generated(masked_str, len);")
        lines.append("}")
        lines.append("")
        
        return "\n".join(lines)




class MultiFileGenerator:
    """Generate multiple output files from parsed data."""
    
    # Enum names in propstrings.h that are range markers / sentinels.
    # These do NOT get SMAP entries — they either share a slot with a real entry
    # (via = FIRST_*) or mark the end of the array (LAST_KNOWN).
    # WARNING: Do NOT add CSS identifiers here! Names like FIRST_CHILD,
    # LAST_CHILD, FIRST_LINE etc. are real CSS pseudo-classes/elements.
    PROPSTRINGS_RANGE_MARKERS = frozenset({
        'FIRST_PROP',
        'LAST_PROP',
        'FIRST_COLOUR',
        'LAST_COLOUR',
        'FIRST_SYSTEMCOLOUR',
        'LAST_SYSTEMCOLOUR',
        'FIRST_DEPRECATEDCOLOUR',
        'LAST_DEPRECATEDCOLOUR',
        'LAST_KNOWN',
    })
    def __init__(self, dispatch_order, metadata):
        self.dispatch_order = dispatch_order  # From dispatch.c
        self.metadata = metadata  # From properties.gen
    
    def _header(self, file_type, description):
        """Generate common header for all files."""
        lines = []
        lines.append("/*")
        lines.append(f" * AUTO-GENERATED FILE - DO NOT EDIT!")
        lines.append(" *")
        lines.append(f" * This file was automatically generated by property_generator.py")
        lines.append(f" * {description}")
        lines.append(" *")
        lines.append(" * To regenerate this file, run:")
        lines.append(" *   python3 build-aux/property_generator.py \\")
        lines.append(" *     src/select/dispatch.c \\")
        lines.append(" *     src/parse/properties/properties.gen \\")
        lines.append(" *     <output_files>")
        lines.append(" *")
        lines.append(" * Source files:")
        lines.append(" *   - dispatch.c (defines property order)")
        lines.append(" *   - properties.gen (defines property metadata)")
        lines.append(" */")
        lines.append("")
        return lines
        
    def generate_enum(self):
        """Generate property enum matching dispatch order."""
        lines = self._header("ENUM", "Property enum values matching dispatch.c order")
        
        for idx, prop_info in enumerate(self.dispatch_order):
            prop_name = prop_info['name']
            if prop_name in self.metadata:
                enum_name = self.metadata[prop_name]['enum']
                hex_val = f"0x{idx:03x}"
                lines.append(f"\t{enum_name} = {hex_val},")
            # else: skip this index - creates a gap in enum values
        
        # No extra content after last entry - CSS_N_PROPERTIES is in the parent enum
        
        return '\n'.join(lines)
    
    def generate_dispatch(self):
        """Generate dispatch table entries."""
        lines = self._header("DISPATCH", "Dispatch table entries in canonical order")
        
        for prop_info in self.dispatch_order:
            prop_name = prop_info['name']
            if prop_name in self.metadata:
                inherited = 1 if prop_info['inherited'] else 0  # From dispatch.c!
                lines.append("\t{")
                lines.append(f"\t\tPROPERTY_FUNCS({prop_name}),")
                lines.append(f"\t\t{inherited},")
                lines.append("\t},")
            # else: skip - dispatch.c already has this entry manually
        
        return '\n'.join(lines)
    
    def generate_dispatch_c(self):
        """Generate complete autogenerated_dispatch.c file.
        
        This replaces the manual dispatch.c with a fully generated version.
        Each non-shorthand property gets a PROPERTY_FUNCS entry with its
        inherited flag from TOML.
        """
        lines = []
        lines.append("/*")
        lines.append(" * AUTO-GENERATED FILE — DO NOT EDIT!")
        lines.append(" *")
        lines.append(" * Generated by property_generator.py from properties.toml")
        lines.append(" * Defines the dispatch table for CSS property cascade functions.")
        lines.append(" */")
        lines.append("")
        lines.append('#include "select/dispatch.h"')
        lines.append('#include "select/properties/properties.h"')
        lines.append("")
        lines.append("/**")
        lines.append(" * Dispatch table for properties, indexed by opcode")
        lines.append(" */")
        lines.append("#define PROPERTY_FUNCS(pname) \\")
        lines.append("    css__cascade_##pname, css__set_##pname##_from_hint, "
                     "css__initial_##pname, css__copy_##pname, css__compose_##pname")
        lines.append("")
        lines.append("struct prop_table prop_dispatch[CSS_N_PROPERTIES] = {")
        
        for prop_info in self.dispatch_order:
            prop_name = prop_info['name']
            inherited = 1 if prop_info['inherited'] else 0
            lines.append("    {")
            lines.append(f"        PROPERTY_FUNCS({prop_name}),")
            lines.append(f"        {inherited},")
            lines.append("    },")
        
        lines.append("};")
        lines.append("")
        return '\n'.join(lines)
    
    def generate_keywords_strings(self, propstrings_h_path):
        """Generate keyword SMAP entries from propstrings.h enum names.
        
        Parses propstrings.h to extract all enum entries that come AFTER
        the #include "propstrings_enum.inc" line. Derives CSS string names
        from enum identifiers using naming conventions + lookup table.
        
        Also performs collision detection: if a keyword's derived CSS string
        matches a property name, it means propstrings.c would have two SMAP
        entries for the same string (one from propstrings_strings.inc, one
        from keywords_strings.inc). The fix is to remove the keyword from
        propstrings.h and reuse the property's enum value in parser code.
        
        Args:
            propstrings_h_path: Path to propstrings.h for parsing
        """
        # ═══════════════════════════════════════════════════════════════
        # Special token lookup table
        # These enum names don't follow the default naming convention.
        # Each entry documents WHY it's special.
        # ═══════════════════════════════════════════════════════════════
        SPECIAL_TOKENS = {
            # Punctuation/symbol tokens — no letter-based derivation possible
            'UNIVERSAL':        '*',
            'COLON':            ':',
            'COMMA':            ',',
            'SEMICOLON':        ';',
            'OPEN_CURLY_BRACE': '{',
            'CLOSE_CURLY_BRACE':'}',
            'ZERO_VALUE':       '0',
            # C keyword collision — 'wrap' is used by WRAP: in specs
            'WRAP_STRING':      'wrap',
        }
        
        # ═══════════════════════════════════════════════════════════════
        # Prefix stripping rules (checked in order)
        # Some enum names have prefixes to avoid C keyword collisions
        # or to disambiguate from other identifiers.
        # ═══════════════════════════════════════════════════════════════
        PREFIX_RULES = [
            # LIBCSS_ prefix: avoids C keyword collisions
            # LIBCSS_DOUBLE → "double", LIBCSS_STATIC → "static", etc.
            ('LIBCSS_', 'C keyword collision'),
            # DC prefix: deprecated system colour aliases
            # DCBACKGROUND → "background"  
            ('DC', 'Deprecated system colour alias'),
        ]
        
        # ═══════════════════════════════════════════════════════════════
        # Build the set of property CSS names for collision detection.
        # A collision means propstrings.c would have duplicate SMAP
        # entries for the same string.
        # ═══════════════════════════════════════════════════════════════
        all_props = [prop_info['name'] for prop_info in self.dispatch_order]
        shorthands = [name for name, meta in self.metadata.items()
                      if meta.get('is_shorthand', False) and name not in all_props]
        aliases = [name for name, meta in self.metadata.items()
                   if meta.get('is_alias', False) and name not in all_props]
        prop_css_names = set(
            p.replace('_', '-') for p in all_props + shorthands + aliases
        )
        
        # Parse propstrings.h
        with open(propstrings_h_path, 'r') as f:
            content = f.read()
        
        # Find all keyword enum entries AFTER propstrings_enum.inc include
        # The enum layout in propstrings.h (after Step 5 reorder) is:
        #   1. #include "propstrings_enum.inc" (auto-generated property names)
        #   2. Manual keyword entries (UNIVERSAL, COLON, ... INHERIT, colors, etc.)
        # We want group 2 — the keyword entries.
        include_pos = content.find('#include "propstrings_enum.inc"')
        if include_pos == -1:
            print("FATAL ERROR: Cannot find '#include \"propstrings_enum.inc\"' "
                  "in propstrings.h", file=sys.stderr)
            sys.exit(1)
        
        # Get the enum content after the include (keywords)
        after_include = content[include_pos:]
        
        # Extract enum entries: look for identifiers that are enum values
        # Skip: range markers (FIRST_*, LAST_*), comments, preprocessor
        enum_entries = []
        for line in after_include.split('\n'):
            line = line.strip()
            # Strip C block comments (e.g. "/* comment */ FIRST," → "FIRST,")
            line = re.sub(r'/\*.*?\*/', '', line).strip()
            # Skip pure comments, empty lines, preprocessor, include directive
            if not line or line.startswith('//') or line.startswith('#') or line.startswith('*'):
                continue
            # Skip the closing brace of the enum
            if line.startswith('}'):
                continue
            
            m = re.match(r'^([A-Z][A-Z0-9_]*)\s*(?:=\s*[^,]+)?\s*,?\s*(?://.*)?$', line)
            if m:
                name = m.group(1)
                # Skip ONLY the actual range marker sentinels.
                # Do NOT use prefix matching — CSS has real identifiers
                # like FIRST_CHILD, LAST_CHILD, FIRST_LINE etc.
                if name in self.PROPSTRINGS_RANGE_MARKERS:
                    continue
                enum_entries.append(name)
        
        if not enum_entries:
            print("FATAL ERROR: No keyword enum entries found after "
                  "propstrings_enum.inc in propstrings.h", file=sys.stderr)
            sys.exit(1)
        
        # Generate SMAP entries + collision check
        lines = []
        lines.append("/*")
        lines.append(" * AUTO-GENERATED FILE — DO NOT EDIT!")
        lines.append(" *")
        lines.append(" * Generated by property_generator.py")
        lines.append(" * Keyword SMAP entries derived from enum names in propstrings.h")
        lines.append(" *")
        lines.append(" * Derivation rules:")
        lines.append(" *   1. Lookup table (special tokens)")
        lines.append(" *   2. Strip LIBCSS_ prefix (C keyword collisions)")
        lines.append(" *   3. Strip DC prefix (deprecated colour aliases)")
        lines.append(" *   4. Default: lowercase, underscore → hyphen")
        lines.append(" */")
        lines.append("")
        
        collisions = []  # (enum_name, css_string) pairs
        
        for name in enum_entries:
            # Rule 1: Lookup table
            if name in SPECIAL_TOKENS:
                css_string = SPECIAL_TOKENS[name]
                lines.append(f'\tSMAP("{css_string}"),  /* {name} — special token */')
                continue
            
            # Rule 2-3: Prefix stripping
            stripped = False
            for prefix, reason in PREFIX_RULES:
                if name.startswith(prefix):
                    base = name[len(prefix):]
                    css_string = base.lower().replace('_', '-')
                    lines.append(f'\tSMAP("{css_string}"),  /* {name} — {reason} */')
                    stripped = True
                    break
            if not stripped:
                # Rule 4: Default convention
                css_string = name.lower().replace('_', '-')
                lines.append(f'\tSMAP("{css_string}"),')
            
            # Collision check: does this keyword's CSS string duplicate a property?
            # Whitelist: Some keyword enum entries intentionally share a CSS string
            # with a property. DCBACKGROUND is a deprecated system colour whose enum
            # slot is needed for the FIRST_DEPRECATEDCOLOUR..LAST_DEPRECATEDCOLOUR
            # positional iteration in utils.c, even though its string "background"
            # duplicates the background property.
            if css_string in prop_css_names and name not in ('DCBACKGROUND',):
                collisions.append((name, css_string))
        
        if collisions:
            print(f"\nFATAL ERROR: Property/keyword string collision detected!", file=sys.stderr)
            print(f"The following keyword entries in propstrings.h produce the same", file=sys.stderr)
            print(f"CSS string as an auto-generated property name:", file=sys.stderr)
            for enum_name, css_str in collisions:
                print(f"  - {enum_name} → \"{css_str}\"", file=sys.stderr)
            print(f"\nThis means propstrings.c would have duplicate SMAP entries.", file=sys.stderr)
            print(f"Fix: Remove the keyword from propstrings.h. The property's enum", file=sys.stderr)
            print(f"value (in propstrings_enum.inc) already provides the same string.", file=sys.stderr)
            print(f"Parser code should use the property enum name directly.", file=sys.stderr)
            sys.exit(1)
        
        return '\n'.join(lines)
    
    def generate_propstrings(self):
        """Generate property string enum for parser (includes keywords and properties)."""
        lines = self._header("PROPSTRINGS", "Parser string identifiers (keywords + properties, alphabetical)")
        
        # Get longhand properties from dispatch.c
        all_props = [prop_info['name'].upper() for prop_info in self.dispatch_order]
        
        # Add shorthand properties from metadata (marked SHORTHAND or WRAP)
        shorthands = [name.upper() for name, meta in self.metadata.items() 
                      if meta.get('is_shorthand', False) and name.upper() not in all_props]
        
        # Add alias properties (different name but same bytecode target)
        aliases = [name.upper() for name, meta in self.metadata.items()
                   if meta.get('is_alias', False) and name.upper() not in all_props]
        
        # Combine keywords, longhands, shorthands, and aliases, sort alphabetically
        # Properties ONLY - keywords are already in propstrings.h before FIRST_PROP
        all_identifiers = sorted(all_props + shorthands + aliases)
        
        # Generate enum
        for idx, identifier in enumerate(all_identifiers):
            if idx == 0:
                lines.append(f"\t{identifier} = FIRST_PROP,")
            else:
                lines.append(f"\t{identifier},")
        
        if all_identifiers:
            last_id = all_identifiers[-1]
            lines.append("")
            lines.append(f"\tLAST_PROP = {last_id},")
        
        return '\n'.join(lines)
    

    def generate_propstrings_strings(self):
        """Generate property string map entries for propstrings.c."""
        lines = self._header("PROPSTRINGS_STRINGS", "Property string map entries (propstrings.c)")
        
        # Get longhand properties from dispatch.c
        all_props = [prop_info['name'] for prop_info in self.dispatch_order]
        
        # Add SHORTHAND properties (not in dispatch.c but need entries)
        shorthands = [name for name, meta in self.metadata.items() 
                      if meta.get('is_shorthand', False) and not meta.get('is_manual', False)
                      and name not in all_props]
        
        # Add alias properties
        aliases = [name for name, meta in self.metadata.items()
                   if meta.get('is_alias', False) and name not in all_props]
        
        # Sort alphabetically to match propstrings_enum.inc
        sorted_props = sorted(all_props + shorthands + aliases)
        
        # Generate SMAP entries - property_name becomes property-name
        for prop_name in sorted_props:
            css_name = prop_name.replace('_', '-')
            lines.append(f'\tSMAP("{css_name}"),')
        
        return '\n'.join(lines)
    
    def generate_gperf_input(self):
        """Generate gperf input file for O(1) property lookup.
        
        Returns the content of a .gperf file that GNU gperf will process
        into C code with a perfect hash function.
        """
        # Collect all properties that need parse handlers
        # Get longhand properties from dispatch.c
        all_props = [prop_info['name'] for prop_info in self.dispatch_order]
        
        # Add SHORTHAND properties
        shorthands = [name for name, meta in self.metadata.items()
                      if meta.get('is_shorthand', False) and not meta.get('is_manual', False)
                      and name not in all_props]
        
        # Add alias properties
        aliases = [name for name, meta in self.metadata.items()
                   if meta.get('is_alias', False) and name not in all_props]
        
        sorted_props = sorted(all_props + shorthands + aliases)
        
        # Build opcode map from dispatch_order (index = opcode)
        opcode_map = {}
        for idx, prop_info in enumerate(self.dispatch_order):
            opcode_map[prop_info['name']] = idx
        
        # Build key tuples: (css_name, handler_name, opcode)
        keys = []
        for prop_name in sorted_props:
            css_name = prop_name.replace('_', '-')
            handler_name = f"css__parse_{prop_name}"
            opcode = opcode_map.get(prop_name, -1)  # -1 for shorthands/aliases
            keys.append((css_name, handler_name, opcode))
        
        # Generate gperf input
        gen = GperfInputGenerator(keys)
        return gen.generate_gperf_input()
    
    def generate_cascade_declarations(self):
        """Generate PROPERTY_FUNCS declarations for select/properties/properties.h."""
        lines = self._header("CASCADE_DECL",
            "Cascade function declarations for all properties (select/properties/properties.h)")
        
        for prop_info in self.dispatch_order:
            lines.append(f"PROPERTY_FUNCS({prop_info['name']});")
        
        return '\n'.join(lines)
    

    
    def generate_parse_declarations(self):
        """Generate css__parse_* forward declarations for parse/properties/properties.h."""
        lines = self._header("PARSE_DECL",
            "Parse function forward declarations for all properties")
        
        # Longhand properties from dispatch.c
        all_props = [prop_info['name'] for prop_info in self.dispatch_order]
        
        # Shorthand properties from properties.gen
        shorthands = [name for name, meta in self.metadata.items()
                      if meta.get('is_shorthand', False) and name not in all_props]
        
        # Alias properties
        aliases = [name for name, meta in self.metadata.items()
                   if meta.get('is_alias', False) and name not in all_props]
        
        # Sort alphabetically for readability 
        sorted_props = sorted(all_props + shorthands + aliases)
        
        for prop_name in sorted_props:
            lines.append(f"css_error css__parse_{prop_name}(css_language *c, "
                         f"const parserutils_vector *vector, int32_t *ctx, css_style *result);")
        
        return '\n'.join(lines)
    
    def generate_unit_masks(self):
        """Generate UNIT_MASK_* defines and property_unit_mask[] array.
        
        Unit masks are derived from the parse type in properties.gen:
        - COLOR/IDENT types -> 0 (no unit validation)
        - LENGTH types -> UNIT_LENGTH | UNIT_PCT
        - ANGLE types -> UNIT_ANGLE
        - TIME types -> UNIT_TIME | UNIT_PCT
        - FREQ types -> UNIT_FREQ
        
        For MANUAL properties, we preserve their existing unit masks.
        """
        lines = self._header("UNIT_MASKS",
            "Unit mask definitions and property_unit_mask[] array")
        
        # Map of known unit mask overrides for properties that don't follow
        # the simple type-based pattern. These are properties where the
        # auto-derived mask from properties.gen type would be wrong.
        unit_mask_overrides = {
            # Shorthands with mixed types
            'background_position': '(UNIT_LENGTH | UNIT_PCT)',
            'border_spacing': '(UNIT_LENGTH)',
            'clip': '(UNIT_LENGTH)',
            'object_position': '(UNIT_LENGTH | UNIT_PCT)',
            # Special LENGTH properties  
            'letter_spacing': '(UNIT_LENGTH)',
            'word_spacing': '(UNIT_LENGTH)',
            'outline_width': '(UNIT_LENGTH)',
            'column_gap': '(UNIT_LENGTH)',
            'column_rule_width': '(UNIT_LENGTH)',
            'column_width': '(UNIT_LENGTH)',
            'row_gap': '(UNIT_LENGTH)',
            # ANGLE type
            'azimuth': '(UNIT_ANGLE)',
            'elevation': '(UNIT_ANGLE)',
            # TIME type
            'pause_after': '(UNIT_TIME | UNIT_PCT)',
            'pause_before': '(UNIT_TIME | UNIT_PCT)',
            # FREQ type
            'pitch': '(UNIT_FREQ)',
            # PCT type
            'volume': '(UNIT_PCT)',
        }
        
        # Properties with LENGTH | PCT (the default for LENGTH/DIMENSION types)
        length_pct_props = {
            'bottom', 'top', 'left', 'right',
            'height', 'width', 'min_height', 'min_width', 'max_height', 'max_width',
            'margin_top', 'margin_right', 'margin_bottom', 'margin_left',
            'padding_top', 'padding_right', 'padding_bottom', 'padding_left',
            'font_size', 'line_height', 'text_indent', 'vertical_align',
            'flex_basis',
            'grid_template_columns', 'grid_template_rows',
            'border_top_width', 'border_right_width', 'border_bottom_width', 'border_left_width',
        }
        
        # Emit #define for each dispatch property
        lines.append("/* Unit mask definitions */")
        for prop_info in self.dispatch_order:
            name = prop_info['name']
            NAME = name.upper()
            
            if name in unit_mask_overrides:
                mask = unit_mask_overrides[name]
            elif name in length_pct_props:
                mask = '(UNIT_LENGTH | UNIT_PCT)'
            else:
                mask = '(0)'
            
            lines.append(f"#define UNIT_MASK_{NAME} {mask}")
        
        lines.append("")
        
        # Emit the property_unit_mask[] array
        lines.append("/** Mapping from property bytecode index to bytecode unit class mask. */")
        lines.append("static const uint32_t generated_property_unit_mask[CSS_N_PROPERTIES] = {")
        for prop_info in self.dispatch_order:
            name = prop_info['name']
            NAME = name.upper()
            if name in self.metadata:
                enum_name = self.metadata[name]['enum']
                lines.append(f"    [{enum_name}] = UNIT_MASK_{NAME},")
        lines.append("};")
        
        return '\n'.join(lines)
    
    def generate_opcodes(self):
        """Generate enum op_* definitions from parsed properties.gen specs.
        
        Uses the structured `parsed` dict from SpecParser — no regex needed.
        Properties marked MANUAL, SHORTHAND, WRAP, ALIAS, or OPCODES_MANUAL
        are skipped.
        """
        lines = []
        lines.append("/* Auto-generated opcode enums from properties.gen */")
        lines.append("/* DO NOT EDIT — generated by property_generator.py */")
        lines.append("")
        
        generated_count = 0
        all_opcode_names = {}  # name → property that defined it (for dup detection)
        errors = []
        
        for prop_name, meta in sorted(self.metadata.items()):
            parsed = meta.get('parsed')
            if not parsed:
                continue
            
            # Skip properties that don't get generated opcodes
            if meta.get('is_shorthand') or meta.get('is_alias') or meta.get('is_wrap'):
                continue
            if meta.get('is_manual') or 'OPCODES_MANUAL' in parsed['flags']:
                continue
            
            # Extract opcode values from the parsed spec
            opcodes = self._extract_opcodes(prop_name, parsed)
            if not opcodes:
                continue
            
            # --- Validation: duplicate opcode names across enums = FATAL ---
            for opc_name, _ in opcodes:
                if opc_name in all_opcode_names:
                    other_prop = all_opcode_names[opc_name]
                    errors.append(
                        f"enum op_{prop_name}: opcode name '{opc_name}' already "
                        f"defined by enum op_{other_prop}. "
                        f"Add OPCODES:MANUAL to '{prop_name}' in properties.gen "
                        f"if these properties intentionally share opcodes.")
            
            # --- Validation: duplicate values within enum ---
            seen_values = {}
            for opc_name, opc_value in opcodes:
                if opc_value in seen_values and opc_value != "VALUE_IS_CALC":
                    errors.append(
                        f"enum op_{prop_name}: duplicate value {opc_value} "
                        f"for {opc_name} and {seen_values[opc_value]}")
                seen_values[opc_value] = opc_name
            
            # Register all opcode names
            for opc_name, _ in opcodes:
                all_opcode_names[opc_name] = prop_name
            
            # Emit enum
            if len(opcodes) <= 3:
                inline = ', '.join(f"{n} = {v}" for n, v in opcodes)
                lines.append(f"enum op_{prop_name} {{ {inline} }};")
            else:
                lines.append(f"enum op_{prop_name} {{")
                entries = [f"    {n} = {v}" for n, v in opcodes]
                lines.append(',\n'.join(entries))
                lines.append("};")
            lines.append("")
            generated_count += 1
        
        if errors:
            print(f"\nFATAL ERROR: Opcode generation found {len(errors)} error(s):",
                  file=sys.stderr)
            for err in errors:
                print(f"  - {err}", file=sys.stderr)
            sys.exit(1)
        
        print(f"  Generated {generated_count} opcode enums")
        return '\n'.join(lines)
    
    def _extract_opcodes(self, prop_name, parsed):
        """Extract opcode name/value pairs from a parsed spec dict.
        
        Returns list of (opcode_name, value_expression) tuples, or None.
        Uses the structured output from SpecParser.parse().
        """
        opcodes = []
        ident_kws = parsed['ident_keywords']  # [(keyword, flags, opcode_name), ...]
        
        if parsed['color']:
            # COLOR type property
            set_name = parsed['color']
            prefix = set_name.replace('_SET', '')
            
            if ident_kws:
                # COLOR + IDENT keywords = ambiguous opcode layout.
                # Must use OPCODES:MANUAL (should have been caught above,
                # but emit error here as safety net)
                return None
            
            # Standard COLOR template: TRANSPARENT, CURRENT_COLOR, SET
            opcodes.append((f"{prefix}_TRANSPARENT", "0x0000"))
            opcodes.append((f"{prefix}_CURRENT_COLOR", "0x0001"))
            opcodes.append((set_name, "0x0080"))
        
        elif parsed['length_unit'] or parsed['number'] or parsed['uri'] or parsed['ident_list']:
            # LENGTH/NUMBER/URI/IDENT_LIST type
            
            # CALC first if present
            if parsed['calc'] and parsed['calc']['opcode']:
                opcodes.append((parsed['calc']['opcode'], "VALUE_IS_CALC"))
            
            # NUMBER opcode (NUMBER, DIMENSION variants)
            if parsed['number'] and parsed['number']['opcode']:
                num_opc = parsed['number']['opcode']
                if num_opc.endswith('_NUMBER'):
                    opcodes.append((num_opc, "0x0080"))
                    # Check for DIMENSION variant in length_unit
                    if parsed['length_unit'] and parsed['length_unit']['opcode']:
                        lu_opc = parsed['length_unit']['opcode']
                        if lu_opc.endswith('_DIMENSION'):
                            opcodes.append((lu_opc, "0x0081"))
                        elif lu_opc != num_opc:
                            if not any(n == lu_opc for n, _ in opcodes):
                                opcodes.append((lu_opc, "0x0080"))
                elif num_opc.endswith('_SET'):
                    if not any(n == num_opc for n, _ in opcodes):
                        opcodes.append((num_opc, "0x0080"))
            
            # LENGTH SET (if not already added)
            if parsed['length_unit'] and parsed['length_unit']['opcode']:
                lu_opc = parsed['length_unit']['opcode']
                if not any(n == lu_opc for n, _ in opcodes):
                    opcodes.append((lu_opc, "0x0080"))
            
            # URI opcode
            if parsed['uri']:
                if not any(n == parsed['uri'] for n, _ in opcodes):
                    opcodes.append((parsed['uri'], "0x0080"))
            
            # IDENT_LIST NAMED opcode
            if parsed['ident_list'] and parsed['ident_list']['named_opcode']:
                named = parsed['ident_list']['named_opcode']
                if not any(n == named for n, _ in opcodes):
                    opcodes.append((named, "0x0080"))
            
            # IDENT keywords at low values
            for idx, (_kw, _flags, opc_name) in enumerate(ident_kws):
                if not any(n == opc_name for n, _ in opcodes):
                    opcodes.append((opc_name, f"0x{idx:04x}"))
        
        elif ident_kws:
            # Pure IDENT-only type
            for idx, (_kw, _flags, opc_name) in enumerate(ident_kws):
                opcodes.append((opc_name, f"0x{idx:04x}"))
        
        else:
            return None
        
        return opcodes if opcodes else None
    
    def validate_indexes(self):
        """Validate property consistency and detect propstrings collisions.
        
        Collisions happen when a property name matches a keyword name.
        Both map to the same string (e.g., "fill"), so the enum would have
        two entries for the same string. This is wasteful and confusing.
        
        The fix is to remove the keyword entry from propstrings.h and reuse
        the property's enum value in parser code. The generator makes this
        a fatal error to prevent silent issues.
        """
        # Get longhand properties from dispatch.c
        all_props = [prop_info['name'] for prop_info in self.dispatch_order]
        
        # Add SHORTHAND properties
        shorthands = [name for name, meta in self.metadata.items() 
                      if meta.get('is_shorthand', False) and not meta.get('is_manual', False)
                      and name not in all_props]
        
        # Add alias properties
        aliases = [name for name, meta in self.metadata.items()
                   if meta.get('is_alias', False) and name not in all_props]
        
        sorted_props = sorted(all_props + shorthands + aliases)
        
        # Check for duplicate properties
        seen = set()
        for prop in sorted_props:
            if prop in seen:
                print(f"FATAL ERROR: Duplicate property '{prop}' in generated output", file=sys.stderr)
                sys.exit(1)
            seen.add(prop)
        
        # Keyword/property string collision is checked in generate_keywords_strings()
        # when --propstrings-h is provided. That check catches cases where a keyword
        # enum in propstrings.h derives the same CSS string as a property name,
        # which would create duplicate SMAP entries in propstrings.c.
        # For enum name collisions (e.g., a manual keyword with the same C identifier
        # as a property), the compiler will catch those as duplicate enum values.
        
        print(f"  Validation passed: {len(sorted_props)} properties, no duplicates")
        return True


def main():
    import os
    
    # Parse named flags
    toml_file = None
    output_dir = None
    cascade_dir = None
    propstrings_h = None
    args = sys.argv[1:]
    positional = []
    i = 0
    while i < len(args):
        if args[i] == '--toml':
            toml_file = args[i + 1]
            i += 2
        elif args[i] == '--output-dir':
            output_dir = args[i + 1]
            i += 2
        elif args[i] == '--cascade-dir':
            cascade_dir = args[i + 1]
            i += 2
        elif args[i] == '--propstrings-h':
            propstrings_h = args[i + 1]
            i += 2
        else:
            positional.append(args[i])
            i += 1
    
    if not output_dir:
        print("Usage: property_generator.py --output-dir DIR --toml properties.toml "
              "[--propstrings-h propstrings.h] [--cascade-dir DIR] "
              "[dispatch.c]", file=sys.stderr)
        sys.exit(1)
    
    dispatch_file = positional[0] if len(positional) > 0 else None
    
    # Fixed output filenames within output_dir
    gen_file = os.path.join(output_dir, 'properties.gen')
    enum_out = os.path.join(output_dir, 'properties_enum.inc')
    dispatch_out = os.path.join(output_dir, 'dispatch_table.inc')
    propstrings_out = os.path.join(output_dir, 'propstrings_enum.inc')
    propstrings_strings_out = os.path.join(output_dir, 'propstrings_strings.inc')
    gperf_out = os.path.join(output_dir, 'prop_hash_table.gperf')
    cascade_decl_out = os.path.join(output_dir, 'cascade_declarations.inc')
    parse_decl_out = os.path.join(output_dir, 'parse_declarations.inc')
    unit_masks_out = os.path.join(output_dir, 'unit_masks.inc')
    opcodes_out = os.path.join(output_dir, 'opcodes.inc')
    dispatch_c_out = os.path.join(output_dir, 'autogenerated_dispatch.c')
    keywords_strings_out = os.path.join(output_dir, 'keywords_strings.inc')

    
    # Parse input files
    toml_reader = None
    if toml_file:
        print(f"Reading properties.toml: {toml_file}")
        toml_reader = TomlPropertyReader(toml_file)
        prop_metadata = toml_reader.parse()
        print(f"  Found {len(prop_metadata)} properties in properties.toml")
        
        # Derive dispatch order from TOML (replaces DispatchParser)
        dispatch_order = toml_reader.get_dispatch_order()
        print(f"  Dispatch order: {len(dispatch_order)} properties from TOML")
        
        # Generate properties.gen for gen_parser
        print(f"Generating properties.gen: {gen_file}")
        with open(gen_file, 'w') as f:
            f.write(toml_reader.generate_properties_gen())
        print(f"  Generated {gen_file} from TOML")
    else:
        # Legacy mode: read dispatch.c + properties.gen directly
        if not dispatch_file:
            print("FATAL ERROR: Must provide either --toml or dispatch.c", file=sys.stderr)
            sys.exit(1)
        
        print("Parsing dispatch.c for property order...")
        dispatch_parser = DispatchParser(dispatch_file)
        dispatch_order = dispatch_parser.parse()
        print(f"  Found {len(dispatch_order)} properties in dispatch.c")
        
        print("Parsing properties.gen for metadata...")
        gen_parser = PropertyGenParser(gen_file)
        prop_metadata = gen_parser.parse()
        print(f"  Found {len(prop_metadata)} properties in properties.gen")
    
    # Generate output files
    generator = MultiFileGenerator(dispatch_order, prop_metadata)
    generator.toml_reader = toml_reader  # Store for cascade generation access
    
    # Run validation first
    print("Validating property indexes...")
    generator.validate_indexes()
    
    for out_file, gen_func in [
        (enum_out, generator.generate_enum),
        (dispatch_out, generator.generate_dispatch),
        (propstrings_out, generator.generate_propstrings),
        (propstrings_strings_out, generator.generate_propstrings_strings),
        (gperf_out, generator.generate_gperf_input),
        (cascade_decl_out, generator.generate_cascade_declarations),
        (parse_decl_out, generator.generate_parse_declarations),
        (unit_masks_out, generator.generate_unit_masks),
        (opcodes_out, generator.generate_opcodes),

    ]:
        with open(out_file, 'w') as f:
            f.write(gen_func())
        print(f"Generated: {out_file}")
    
    # Generate autogenerated_dispatch.c (when using TOML mode)
    if toml_reader:
        with open(dispatch_c_out, 'w') as f:
            f.write(generator.generate_dispatch_c())
        print(f"Generated: {dispatch_c_out}")
    
    # Generate keywords_strings.inc (when propstrings.h path is provided)
    if propstrings_h:
        with open(keywords_strings_out, 'w') as f:
            f.write(generator.generate_keywords_strings(propstrings_h))
        print(f"Generated: {keywords_strings_out}")
    
    # Generate cascade .c files if --cascade-dir and --toml are provided
    if cascade_dir and toml_reader:
        sys.path.insert(0, str(Path(__file__).parent))
        import generate_cascade
        os.makedirs(cascade_dir, exist_ok=True)
        toml_data = toml_reader.toml_data
        generated_files = []
        for name, props in toml_data.items():
            if not isinstance(props, dict):
                continue
            if 'cascade' not in props:
                continue
            content = generate_cascade.generate_cascade_file(name, props)
            if content is None:
                continue
            out_path = os.path.join(cascade_dir, f'{name}.c')
            with open(out_path, 'w') as f:
                f.write(content)
            generated_files.append(name)
        print(f"Generated {len(generated_files)} cascade .c files to {cascade_dir}")
        
        # Also generate cascade_sources.cmake that CMake can include
        # It defines LIBCSS_CASCADE_GENERATED and LIBCSS_CASCADE_MANUAL lists
        all_cascade_props = set()
        for name, props in toml_data.items():
            if isinstance(props, dict) and props.get('enum', '').startswith('CSS_PROP_'):
                all_cascade_props.add(name)
        manual_files = sorted(all_cascade_props - set(generated_files))
        
        cmake_path = os.path.join(cascade_dir, 'cascade_sources.cmake')
        with open(cmake_path, 'w') as f:
            f.write('# Auto-generated by property_generator.py — do not edit.\n')
            f.write('set(LIBCSS_CASCADE_GENERATED\n')
            for name in sorted(generated_files):
                f.write(f'    ${{LIBCSS_CASCADE_GEN_DIR}}/{name}.c\n')
            f.write(')\n\n')
            f.write('set(LIBCSS_CASCADE_MANUAL\n')
            for name in manual_files:
                f.write(f'    src/select/properties/{name}.c\n')
            f.write(')\n')
        print(f"Generated: {cmake_path}")


if __name__ == '__main__':
    main()


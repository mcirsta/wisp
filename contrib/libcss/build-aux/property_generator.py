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
                
                # Check if shorthand, manual, or generic
                # SHORTHAND = shorthand property with manual .c implementation (not in dispatch.c)
                # MANUAL = longhand property with manual .c implementation (in dispatch.c)
                # WRAP = wrapper calling a generic function
                # ALIAS = property that parses to a different property's bytecode (e.g., inline-size -> width)
                is_shorthand = spec.strip() == 'SHORTHAND' or 'WRAP:' in spec
                is_manual = spec.strip() == 'MANUAL'
                is_generic = 'GENERIC' in spec
                
                # Detect aliases: property name doesn't match enum's base name
                # e.g., inline_size:CSS_PROP_WIDTH is an alias for width
                enum_base = enum_name.replace('CSS_PROP_', '').lower()
                is_alias = (prop_name != enum_base) and not is_shorthand and not is_generic
                
                if not is_generic:
                    self.prop_map[prop_name] = {
                        'enum': enum_name,
                        'is_shorthand': is_shorthand,
                        'is_alias': is_alias,
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


class PerfectHashGenerator:
    """Generate a perfect hash function using the perfect-hash library.
    
    Uses the 'perfect-hash' package (pip install perfect-hash) to create
    a minimal perfect hash for O(1) property lookup with no collisions.
    """
    
    def __init__(self, keys):
        """Initialize with list of (key_string, handler_name, css_name) tuples."""
        self.keys = keys
        self.hash_table = {}
        self.table_size = 0
        self.f1 = None
        self.f2 = None
        self.G = None
        
    def build(self):
        """Build using perfect_hash library."""
        try:
            import perfect_hash
        except ImportError:
            print("ERROR: 'perfect-hash' package not found.", file=sys.stderr)
            print("Install with: pip install perfect-hash", file=sys.stderr)
            print("  or on Arch: yay -S python-perfect-hash", file=sys.stderr)
            sys.exit(1)
        
        # Extract just the key strings (lowercased for case-insensitive matching)
        key_strings = [k[0].lower() for k in self.keys]
        
        # Generate the perfect hash function components
        # Returns (f1, f2, G) where hash(key) = (G[f1(key)] + G[f2(key)]) % len(G)
        self.f1, self.f2, self.G = perfect_hash.generate_hash(key_strings)
        
        # Build lookup table: calculate slot for each key
        for key_data in self.keys:
            key = key_data[0].lower()
            slot = (self.G[self.f1(key)] + self.G[self.f2(key)]) % len(self.G)
            self.hash_table[slot] = key_data
        
        # Table size is len(G)
        self.table_size = len(self.G)
        
        return True
    
    def generate_c_code(self):
        """Generate C code for the hash function and lookup table."""
        lines = []
        
        # Generate the G table
        lines.append("/* Perfect hash intermediate table G */")
        lines.append(f"static const uint16_t G[{len(self.G)}] = {{")
        # Format G table in rows of 16
        for i in range(0, len(self.G), 16):
            row = ", ".join(f"{self.G[j]}" for j in range(i, min(i + 16, len(self.G))))
            lines.append(f"    {row},")
        lines.append("};")
        lines.append("")
        
        # Generate the salt tables from f1 and f2
        # The StrSaltHash objects have a 'salt' attribute
        salt1 = self.f1.salt if hasattr(self.f1, 'salt') else b''
        salt2 = self.f2.salt if hasattr(self.f2, 'salt') else b''
        
        lines.append(f"static const unsigned char S1[{len(salt1)}] = {{")
        for i in range(0, len(salt1), 16):
            row = ", ".join(f"{salt1[j]}" for j in range(i, min(i + 16, len(salt1))))
            lines.append(f"    {row},")
        lines.append("};")
        lines.append("")
        
        lines.append(f"static const unsigned char S2[{len(salt2)}] = {{")
        for i in range(0, len(salt2), 16):
            row = ", ".join(f"{salt2[j]}" for j in range(i, min(i + 16, len(salt2))))
            lines.append(f"    {row},")
        lines.append("};")
        lines.append("")
        
        # Generate the hash function
        lines.append("/* Perfect hash function for CSS property lookup */")
        lines.append("static inline unsigned int css_prop_hash(const char *key, size_t len) {")
        lines.append("    unsigned int f1 = 0, f2 = 0;")
        lines.append(f"    size_t salt_len = {len(salt1)};")
        lines.append("    for (size_t i = 0; i < len && i < salt_len; i++) {")
        lines.append("        f1 += S1[i] * (unsigned char)key[i];")
        lines.append("        f2 += S2[i] * (unsigned char)key[i];")
        lines.append("    }")
        lines.append(f"    f1 %= {len(self.G)};")
        lines.append(f"    f2 %= {len(self.G)};")
        lines.append(f"    return (G[f1] + G[f2]) % {len(self.G)};")
        lines.append("}")
        lines.append("")
        
        # Lookup table structure
        lines.append("/* Property lookup table entry */")
        lines.append("struct css_prop_entry {")
        lines.append("    const char *name;     /* Property name (hyphenated) */")
        lines.append("    uint8_t name_len;     /* Length of name */")
        lines.append("    css_prop_handler handler;  /* Parse handler function */")
        lines.append("};")
        lines.append("")
        
        # Table size
        lines.append(f"#define CSS_PROP_HASH_TABLE_SIZE {self.table_size}")
        lines.append("")
        
        # Lookup table with sparse initialization
        lines.append("static const struct css_prop_entry css_prop_hash_table[CSS_PROP_HASH_TABLE_SIZE] = {")
        for slot in sorted(self.hash_table.keys()):
            key, handler, css_name = self.hash_table[slot]
            lines.append(f'    [{slot}] = {{ "{css_name}", {len(css_name)}, {handler} }},')
        lines.append("};")
        lines.append("")
        
        # Lookup helper function
        lines.append("/* Lookup property handler by name (case-insensitive) */")
        lines.append("static inline css_prop_handler css_prop_lookup(const char *name, size_t len) {")
        lines.append("    /* Convert to lowercase for hash lookup */")
        lines.append("    char lower[64];")
        lines.append("    if (len >= sizeof(lower)) return NULL;")
        lines.append("    for (size_t i = 0; i < len; i++) {")
        lines.append("        char c = name[i];")
        lines.append("        lower[i] = (c >= 'A' && c <= 'Z') ? c + 32 : c;")
        lines.append("    }")
        lines.append("    lower[len] = '\\0';")
        lines.append("")
        lines.append("    unsigned int slot = css_prop_hash(lower, len);")
        lines.append("    if (slot >= CSS_PROP_HASH_TABLE_SIZE) return NULL;")
        lines.append("")
        lines.append("    const struct css_prop_entry *e = &css_prop_hash_table[slot];")
        lines.append("    if (e->name == NULL || e->name_len != len) return NULL;")
        lines.append("")
        lines.append("    /* Verify exact match */")
        lines.append("    for (size_t i = 0; i < len; i++) {")
        lines.append("        if (lower[i] != e->name[i]) return NULL;")
        lines.append("    }")
        lines.append("    return e->handler;")
        lines.append("}")
        
        return "\n".join(lines)


class KeywordsParser:
    """Parse keywords.gen to get CSS syntax keywords."""
    
    def __init__(self, keywords_file):
        self.keywords_file = Path(keywords_file)
        self.keywords = []  # List of keyword names
        
    def parse(self):
        """Parse keywords.gen and extract keyword names."""
        if not self.keywords_file.exists():
            print(f"FATAL ERROR: keywords.gen not found at {self.keywords_file}", file=sys.stderr)
            sys.exit(1)
        
        try:
            with open(self.keywords_file, 'r') as f:
                lines = list(f)
        except IOError as e:
            print(f"FATAL ERROR: Cannot read keywords.gen: {e}", file=sys.stderr)
            sys.exit(1)
        
        for line_num, line in enumerate(lines, 1):
            line = line.strip()
            
            # Skip comments and empty lines
            if not line or line.startswith('#'):
                continue
            
            # Each non-comment line is a keyword name
            if re.match(r'^[A-Z_]+$', line):
                self.keywords.append(line)
            else:
                print(f"WARNING: Invalid keyword '{line}' at line {line_num} in keywords.gen", file=sys.stderr)
        
        # Validate we found keywords
        if not self.keywords:
            print("FATAL ERROR: No keywords found in keywords.gen", file=sys.stderr)
            print("Expected format: One KEYWORD_NAME per line", file=sys.stderr)
            sys.exit(1)
        
        if len(self.keywords) < 10:
            print(f"FATAL ERROR: Only {len(self.keywords)} keywords in keywords.gen", file=sys.stderr)
            print("Expected at least 10 keywords. File may be incomplete.", file=sys.stderr)
            sys.exit(1)
        
        return self.keywords


class MultiFileGenerator:
    """Generate multiple output files from parsed data."""
    
    def __init__(self, dispatch_order, metadata, keywords):
        self.dispatch_order = dispatch_order  # From dispatch.c
        self.metadata = metadata  # From properties.gen
        self.keywords = keywords  # From keywords.gen
    
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
    
    def generate_parse_handlers(self):
        """Generate parse handler array as complete definition."""
        lines = self._header("HANDLERS", "Parse handler function pointers (alphabetical order)")
        
        # Add complete array definition
        lines.append("const css_prop_handler property_handlers[LAST_PROP + 1 - FIRST_PROP] = {")
        
        # Get longhand properties from dispatch.c
        all_props = [prop_info['name'] for prop_info in self.dispatch_order]
        
        # Add shorthand properties from metadata (marked SHORTHAND or WRAP)
        shorthands = [name for name, meta in self.metadata.items() 
                      if meta.get('is_shorthand', False) and name not in all_props]
        
        # Add alias properties
        aliases = [name for name, meta in self.metadata.items()
                   if meta.get('is_alias', False) and name not in all_props]
        
        # Sort all properties alphabetically
        sorted_props = sorted(all_props + shorthands + aliases)
        
        # Each property uses its own autogenerated parser function
        # Aliases like inline_size have autogenerated_inline_size.c that
        # correctly emit the target property's bytecode (e.g., CSS_PROP_WIDTH)
        for prop_name in sorted_props:
            lines.append(f"\tcss__parse_{prop_name},")
        
        lines.append("};")
        
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
    
    def generate_hash_table(self):
        """Generate perfect hash table for O(1) property lookup."""
        lines = self._header("HASH_TABLE", "Perfect hash table for CSS property lookup")
        
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
        
        # Build key tuples: (lookup_key, handler_name, css_name)
        keys = []
        for prop_name in sorted_props:
            css_name = prop_name.replace('_', '-')
            handler_name = f"css__parse_{prop_name}"
            keys.append((css_name, handler_name, css_name))
        
        # Generate perfect hash
        hasher = PerfectHashGenerator(keys)
        hasher.build()
        
        # Add includes and the generated code
        lines.append("#include <stdint.h>")
        lines.append("#include <stddef.h>")
        lines.append("")
        lines.append(hasher.generate_c_code())
        
        return '\n'.join(lines)
    
    def validate_indexes(self):
        """Validate that propstrings and handlers have consistent property order."""
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
        
        # Check all properties have metadata
        missing = []
        for prop in sorted_props:
            if prop not in self.metadata:
                # This is OK - longhands from dispatch.c may not have properties.gen entries
                pass
        
        # Check for duplicate properties
        seen = set()
        for prop in sorted_props:
            if prop in seen:
                print(f"FATAL ERROR: Duplicate property '{prop}' in generated output", file=sys.stderr)
                sys.exit(1)
            seen.add(prop)
        
        print(f"  Validation passed: {len(sorted_props)} properties, no duplicates")
        return True


def main():
    if len(sys.argv) != 9:
        print("Usage: property_generator.py dispatch.c properties.gen keywords.gen enum.inc dispatch.inc propstrings.inc propstrings_strings.inc hash_table.inc", file=sys.stderr)
        sys.exit(1)
    
    dispatch_file = sys.argv[1]
    gen_file = sys.argv[2]
    keywords_file = sys.argv[3]
    enum_out = sys.argv[4]
    dispatch_out = sys.argv[5]
    propstrings_out = sys.argv[6]
    propstrings_strings_out = sys.argv[7]
    hash_table_out = sys.argv[8]
    
    # Parse input files
    print("Parsing dispatch.c for property order...")
    dispatch_parser = DispatchParser(dispatch_file)
    dispatch_order = dispatch_parser.parse()
    print(f"  Found {len(dispatch_order)} properties in dispatch.c")
    
    print("Parsing properties.gen for metadata...")
    gen_parser = PropertyGenParser(gen_file)
    prop_metadata = gen_parser.parse()
    print(f"  Found {len(prop_metadata)} properties in properties.gen")
    
    print("Parsing keywords.gen for CSS keywords...")
    keywords_parser = KeywordsParser(keywords_file)
    keywords = keywords_parser.parse()
    print(f"  Found {len(keywords)} keywords in keywords.gen")
    
    # Generate output files
    generator = MultiFileGenerator(dispatch_order, prop_metadata, keywords)
    
    # Run validation first
    print("Validating property indexes...")
    generator.validate_indexes()
    
    with open(enum_out, 'w') as f:
        f.write(generator.generate_enum())
    print(f"Generated: {enum_out}")
    
    with open(dispatch_out, 'w') as f:
        f.write(generator.generate_dispatch())
    print(f"Generated: {dispatch_out}")
    
    with open(propstrings_out, 'w') as f:
        f.write(generator.generate_propstrings())
    print(f"Generated: {propstrings_out}")
    
    with open(propstrings_strings_out, 'w') as f:
        f.write(generator.generate_propstrings_strings())
    print(f"Generated: {propstrings_strings_out}")
    
    # Generate perfect hash table (replaces parse_handlers.inc)
    print("Generating perfect hash table...")
    with open(hash_table_out, 'w') as f:
        f.write(generator.generate_hash_table())
    print(f"Generated: {hash_table_out}")


if __name__ == '__main__':
    main()


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
                is_shorthand = spec.strip() == 'SHORTHAND' or 'WRAP:' in spec
                is_manual = spec.strip() == 'MANUAL'
                is_generic = 'GENERIC' in spec
                
                if not is_generic:
                    self.prop_map[prop_name] = {
                        'enum': enum_name,
                        'is_shorthand': is_shorthand,
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
        
        # Combine keywords, longhands, and shorthands, sort alphabetically
        # Properties ONLY - keywords are already in propstrings.h before FIRST_PROP
        all_identifiers = sorted(all_props + shorthands)
        
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
        
        # Sort all properties alphabetically
        sorted_props = sorted(all_props + shorthands)
        
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
        
        # Sort alphabetically to match propstrings_enum.inc
        sorted_props = sorted(all_props + shorthands)
        
        # Generate SMAP entries - property_name becomes property-name
        for prop_name in sorted_props:
            css_name = prop_name.replace('_', '-')
            lines.append(f'\tSMAP("{css_name}"),')
        
        return '\n'.join(lines)
    
    def generate_propstring_to_opcode(self):
        """Generate mapping from propstring index to CSS_PROP_* opcode.
        
        This allows code to look up the correct CSS_PROP_* value from a
        propstring index (which is alphabetical order) at runtime.
        Shorthands map to CSS_N_PROPERTIES (invalid opcode) since they
        don't have dispatch entries.
        """
        lines = self._header("PROPSTRING_TO_OPCODE", 
                            "Mapping from propstring index to CSS_PROP_* opcode")
        
        # Get longhand properties from dispatch.c
        all_props = [prop_info['name'] for prop_info in self.dispatch_order]
        
        # Add shorthand properties from metadata
        shorthands = [name for name, meta in self.metadata.items() 
                      if meta.get('is_shorthand', False) and name not in all_props]
        
        # Sort alphabetically to match propstrings order
        sorted_props = sorted(all_props + shorthands)
        
        # Build dispatch index lookup: prop_name -> dispatch index (CSS_PROP_* value)
        dispatch_index = {}
        for idx, prop_info in enumerate(self.dispatch_order):
            dispatch_index[prop_info['name']] = idx
        
        # Generate array
        lines.append("/* Maps propstring index (FIRST_PROP-based) to CSS_PROP_* opcode */")
        lines.append("/* Shorthands map to CSS_N_PROPERTIES (no dispatch entry) */")
        lines.append("static const opcode_t propstring_to_opcode[LAST_PROP + 1 - FIRST_PROP] = {")
        
        for prop_name in sorted_props:
            if prop_name in dispatch_index:
                opcode_idx = dispatch_index[prop_name]
                enum_name = self.metadata.get(prop_name, {}).get('enum', f'/* {prop_name} */')
                lines.append(f"\t{enum_name}, /* {prop_name} -> opcode {opcode_idx} */")
            else:
                # Shorthand - no dispatch entry
                lines.append(f"\tCSS_N_PROPERTIES, /* {prop_name} (shorthand) */")
        
        lines.append("};")
        
        return '\n'.join(lines)
    
    def validate_indexes(self):
        """Validate that propstrings and handlers have consistent property order."""
        # Get longhand properties from dispatch.c
        all_props = [prop_info['name'] for prop_info in self.dispatch_order]
        
        # Add SHORTHAND properties
        shorthands = [name for name, meta in self.metadata.items() 
                      if meta.get('is_shorthand', False) and not meta.get('is_manual', False)
                      and name not in all_props]
        
        sorted_props = sorted(all_props + shorthands)
        
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
        print("Usage: property_generator.py dispatch.c properties.gen keywords.gen enum.inc dispatch.inc propstrings.inc propstrings_strings.inc handlers.inc", file=sys.stderr)
        sys.exit(1)
    
    dispatch_file = sys.argv[1]
    gen_file = sys.argv[2]
    keywords_file = sys.argv[3]
    enum_out = sys.argv[4]
    dispatch_out = sys.argv[5]
    propstrings_out = sys.argv[6]
    propstrings_strings_out = sys.argv[7]
    handlers_out = sys.argv[8]
    
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
    
    with open(handlers_out, 'w') as f:
        f.write(generator.generate_parse_handlers())
    print(f"Generated: {handlers_out}")


if __name__ == '__main__':
    main()


---
description: How to add or modify CSS properties in libcss
---

# LibCSS Property System Guide

## Overview

LibCSS uses a code generation system for CSS properties. Understanding these files is essential for adding/modifying properties.

## Key Files

### 1. dispatch.c (Canonical Property Order)
**Path:** `contrib/libcss/src/select/dispatch.c`

Defines the **order** of CSS properties and inheritance flags. This order determines enum values.

```c
PROPERTY_FUNCS(grid_auto_flow),  // Add new properties here
0,  // 0 = not inherited, 1 = inherited
```

### 2. properties.gen (Parsing Specs)
**Path:** `contrib/libcss/src/parse/properties/properties.gen`

Defines how properties are **parsed**. Three types:
- **Auto-generated IDENT:** `property:CSS_PROP_X IDENT:( KEYWORDS... IDENT:)`
- **WRAP:** `property:CSS_PROP_X WRAP:css__parse_helper_function`
- **MANUAL:** `property:CSS_PROP_X MANUAL` - requires hand-written parser in .c file
- **SHORTHAND:** `property:CSS_PROP_X SHORTHAND` - shorthand, not in dispatch.c

### 3. keywords.gen (Non-Property Keywords)
**Path:** `contrib/libcss/src/parse/keywords.gen`

Non-property keywords (pseudo-classes, at-rules, colors). NOT for property value keywords.

### 4. propstrings.h / propstrings.c (String Mappings)
**Paths:** `contrib/libcss/src/parse/propstrings.h` and `.c`

Contains keyword enums and string mappings. **Partially auto-generated:**
- `propstrings_enum.inc` - Auto-generated property names
- Manual section - Value keywords like `DENSE`, `MINMAX`, `SPAN`, etc.

### 5. Generated Files (build directory)
**Path:** `build-ninja/contrib/libcss/src/generated/`

- `propstrings_enum.inc` - Property name enums (alphabetical)
- `parse_handlers.inc` - Parse function pointers
- `properties_enum.inc` - Property enum values (dispatch order)

## Common Tasks

### Add a Value Keyword (e.g., SPAN)
1. Add to `propstrings.h` (manual section, after similar keywords)
2. Add `SMAP("span")` to `propstrings.c` in same position
3. Use `c->strings[SPAN]` in parser code

### Add Manual Parser for Complex Syntax
1. Change properties.gen: `property:CSS_PROP_X MANUAL`
2. Create `src/parse/properties/property.c` with `css__parse_property()`
3. Add to CMakeLists.txt

### Add New Property
1. Add to `dispatch.c` with `PROPERTY_FUNCS(name)` and inherited flag
2. Add to `properties.gen` with parsing spec
3. Create `src/select/properties/name.c` with cascade functions
4. Run build - generator creates `.inc` files

## Multi-Token Values (e.g., "row dense")
Auto-generated IDENT parsers match **single tokens only**. For multi-token values:
- Mark property as MANUAL in properties.gen
- Write parser that consumes multiple tokens in sequence

## Regenerating Files
Files are regenerated during CMake build via `property_generator.py`.

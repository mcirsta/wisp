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

## ⚠️ Unit Conversion Pitfall

LibCSS has **two separate unit systems** that must be kept in sync:

### Bytecode Units (UNIT_*)
**Path:** `contrib/libcss/src/bytecode/bytecode.h`

Bit-packed enums used during parsing and stored in bytecode:
```c
UNIT_LENGTH = (1 << 8),   // Category for lengths
UNIT_PX = (1 << 8) + 0,   // Specific unit within category
UNIT_EM = (1 << 8) + 1,
UNIT_PCT = (1 << 9),      // Percentage category
UNIT_SIZING = (1 << 14),  // Intrinsic sizing keywords
UNIT_FIT_CONTENT = (1 << 14) + 0,
UNIT_MIN_CONTENT = (1 << 14) + 1,
UNIT_MAX_CONTENT = (1 << 14) + 2,
UNIT_MINMAX = (1 << 14) + 3,  // Grid minmax() marker
```

### CSS Units (CSS_UNIT_*)
**Path:** `contrib/libcss/include/libcss/types.h`

Flat sequential enums used in computed styles and exposed to consumers:
```c
CSS_UNIT_PX = 0x0, CSS_UNIT_EM = 0x1, ..., CSS_UNIT_FIT_CONTENT = 0x1a, ...
```

### Translation Function
**Path:** `contrib/libcss/src/select/helpers.h`

`css__to_css_unit()` converts bytecode units to CSS units. **When adding new units:**

1. Add `UNIT_X` to `bytecode.h` in the appropriate category
2. Add `CSS_UNIT_X` to `types.h`
3. Add case to `css__to_css_unit()` in `helpers.h`

**Symptom of missing conversion:** Width/height computes to 0 or unexpected value, because the unit falls through to the assert and returns 0.

---

## ⚠️ Manual Parser Structure (calc() support)

When writing a **MANUAL** parser that needs to support `calc()`, you MUST follow the generated parser structure:

### ✅ Correct Pattern (else-if chain)
```c
token = parserutils_vector_iterate(vector, ctx);

if ((token->type == CSS_TOKEN_IDENT) && /* keyword check */) {
    error = /* handle keyword */;

} else if ((token->type == CSS_TOKEN_FUNCTION) &&
        (lwc_string_caseless_isequal(token->idata, c->strings[CALC], &match) == lwc_error_ok && match)) {
    /* DO NOT reset ctx - css__parse_calc expects ctx at calc token */
    error = css__parse_calc(c, vector, ctx, result, buildOPV(CSS_PROP_X, 0, X_CALC), UNIT_PX);

} else {
    /* Length/percentage fallback */
    *ctx = orig_ctx;
    error = css__parse_unit_specifier(...);
}

if (error != CSS_OK)
    *ctx = orig_ctx;
return error;
```

### ❌ Wrong Pattern (breaks calc)
```c
/* DON'T do this - resetting ctx before css__parse_calc breaks it */
*ctx = orig_ctx;
token = parserutils_vector_iterate(vector, ctx);
if (token->type == CSS_TOKEN_FUNCTION) {
    *ctx = orig_ctx;  // WRONG! calc expects ctx AT the calc token
    return css__parse_calc(...);
}
```

**Key insight:** `css__parse_calc()` expects `ctx` to be positioned **at** the calc function token, not before it. The generated parsers never reset ctx when calling calc.

### Reference
See generated parsers in `build-*/contrib/libcss/src/generated/` for correct structure. Use `gen_parser` tool to generate reference code:
```bash
./build-gcc/contrib/libcss/src/parse/properties/gen_parser/gen_parser "height:CSS_PROP_HEIGHT IDENT:( ... CALC:( UNIT_PX:HEIGHT_CALC CALC:)"
```

---

## 🔧 Extending the Parser Generator

**Path:** `contrib/libcss/src/parse/properties/css_property_parser_gen.c`

When you need new functionality across multiple properties, consider extending the generator instead of writing manual parsers.

### Available Directives

| Directive | Purpose | Example |
|-----------|---------|---------|
| `IDENT:( key:val ... IDENT:)` | Keyword matching | `AUTO:0,WIDTH_AUTO` |
| `LENGTH_UNIT:( ... LENGTH_UNIT:)` | Dimension/percentage values | `UNIT_PX:WIDTH_SET MASK:UNIT_MASK_WIDTH` |
| `CALC:( unit:value CALC:)` | calc() function support | `UNIT_PX:WIDTH_CALC` |
| `SIZING:( key:val ... SIZING:)` | Content-sizing keywords | `FIT_CONTENT:WIDTH_SET` |
| `COLOR:value` | Color parsing | `COLOR:COLOR_SET` |
| `NUMBER:( ... NUMBER:)` | Numeric values | `true:Z_INDEX_SET` |
| `URI:value` | URL values | `URI:LIST_STYLE_IMAGE_URI` |

### Adding a New Directive

1. **Add keyval_list variable** in `main()`:
   ```c
   struct keyval_list MY_DIRECTIVE = {0};
   ```

2. **Add parsing in the main loop:**
   ```c
   } else if (strcmp(rkv->key, "MY_DIRECTIVE") == 0) {
       if (rkv->val[0] == '(') {
           curlist = &MY_DIRECTIVE;
       } else if (rkv->val[0] == ')') {
           curlist = &base;
       }
       only_ident = false;
       do_token_check = false;
   }
   ```

3. **Add output function** (model on `output_sizing()` or `output_calc()`):
   ```c
   void output_my_directive(FILE *outputf, struct keyval *parseid, struct keyval_list *kvlist) {
       for (int i = 0; i < kvlist->count; i++) {
           struct keyval *ckv = kvlist->item[i];
           fprintf(outputf, "if (...) { error = ...; } else ");
       }
   }
   ```

4. **Call the output function** (after IDENT, before terminal blocks):
   ```c
   if (MY_DIRECTIVE.count > 0) {
       output_my_directive(outputf, base.item[0], &MY_DIRECTIVE);
   }
   ```

5. **Add cleanup:**
   ```c
   for (int i = 0; i < MY_DIRECTIVE.count; i++)
       free(MY_DIRECTIVE.item[i]);
   ```

### Example: SIZING Directive

The `SIZING:( )` directive was added for intrinsic sizing keywords (`fit-content`, `min-content`, `max-content`):

**In properties.gen:**
```
width:CSS_PROP_WIDTH IDENT:( ... AUTO:0,WIDTH_AUTO IDENT:) SIZING:( FIT_CONTENT:WIDTH_SET MIN_CONTENT:WIDTH_SET MAX_CONTENT:WIDTH_SET SIZING:) LENGTH_UNIT:( ... )
```

**Generated code pattern:**
```c
if ((token->type == CSS_TOKEN_IDENT) &&
        (lwc_string_caseless_isequal(token->idata, c->strings[FIT_CONTENT], &match) == lwc_error_ok && match)) {
    error = css__stylesheet_style_appendOPV(result, CSS_PROP_WIDTH, 0, WIDTH_SET);
    if (error == CSS_OK)
        error = css__stylesheet_style_vappend(result, 2, (css_fixed)0, (uint32_t)UNIT_FIT_CONTENT);
} else ...
```

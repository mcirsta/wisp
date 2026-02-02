# Task: Unify Bytecode and CSS_UNIT Representations

**Created:** 2026-01-03  
**Status:** Pending  
**Priority:** Low (refactoring, not bug fix)

## Background

LibCSS maintains two separate unit representations that cause complexity and bugs:

1. **Bytecode units** (`UNIT_*` in `src/bytecode/bytecode.h`):
   - Encoded with category bits: `(category << 8) + sub_unit`
   - Example: `UNIT_PX = (1u << 8) + 0 = 256`
   - Used during parsing and stored in bytecode

2. **CSS_UNIT enum** (`CSS_UNIT_*` in `include/libcss/types.h`):
   - Simple sequential enum: `CSS_UNIT_PX = 0x00`
   - Used in public computed style API

## Problem

Cascade handlers must convert bytecode units to CSS_UNIT using `css__to_css_unit()`, 
but this was missing in several grid-related handlers, causing bugs where consumers 
received bytecode values (like 256) instead of CSS_UNIT values (like 0).

**Bug example discovered 2026-01-03:**
- `grid_template_rows.c` and `grid_template_columns.c` cascade handlers were 
  directly casting `(css_unit)raw_unit` instead of calling `css__to_css_unit()`
- This caused `row_tracks[i].unit` to be 256 instead of 0 for CSS_UNIT_PX
- Layout code couldn't match `switch(unit) { case CSS_UNIT_PX: ... }`

## Proposed Solution

Eliminate bytecode unit encoding entirely and use CSS_UNIT directly throughout:

### Phase 1: Audit
- [ ] Identify all places that use `UNIT_*` bytecode values
- [ ] List all cascade handlers that call or should call `css__to_css_unit()`
- [ ] Check if any code relies on the category bits for type checking

### Phase 2: Modify Bytecode
- [ ] Change `src/bytecode/bytecode.h` to use CSS_UNIT values directly
- [ ] Update `UNIT_PX = CSS_UNIT_PX` (0), `UNIT_EM = CSS_UNIT_EM` (2), etc.
- [ ] Consider making bytecode use `uint8_t` for units (CSS_UNIT fits in 8 bits)

### Phase 3: Remove Conversion Layer
- [ ] Remove `css__to_css_unit()` function from `src/select/helpers.h`
- [ ] Remove all calls to `css__to_css_unit()` in cascade handlers
- [ ] Direct assignment: `tracks[i].unit = (css_unit)*style->bytecode;`

### Phase 4: Update Parser
- [ ] Modify `css__parse_unit_specifier()` to store CSS_UNIT directly
- [ ] Update `src/parse/properties/utils.c` unit handling

### Phase 5: Test
- [ ] Run full test suite
- [ ] Verify all computed style unit values are correct
- [ ] Test grid layout with various units (px, em, fr, %)

## Files Affected

### Bytecode Definition
- `src/bytecode/bytecode.h` - UNIT_* enum

### Parser
- `src/parse/properties/utils.c` - `css__parse_unit_specifier()`

### Cascade Handlers (partial list)
- `src/select/helpers.h` - `css__to_css_unit()` function
- `src/select/properties/grid_template_rows.c`
- `src/select/properties/grid_template_columns.c`
- `src/select/properties/line_height.c`
- `src/select/properties/flex_basis.c`
- `src/select/properties/font_size.c`
- `src/select/properties/background_position.c`
- `src/select/properties/border_spacing.c`
- Many more... (grep for `css__to_css_unit`)

## Historical Context

The bytecode encoding was designed when Wisp targeted resource-constrained 
systems (RISC OS, Amiga, old ARM devices). The category-based encoding allowed:
- Type checking via bitmask (is this a length? angle? time?)
- Compact storage (8-bit category + offset)

On modern systems with abundant RAM, this optimization:
- Saves negligible memory (few KB for huge stylesheets)
- Adds conversion overhead on every property access
- Creates maintenance burden and bugs

## Dependencies

None - this is internal refactoring with no API changes.

## Estimated Effort

Medium (2-4 hours): Many files to touch but changes are mechanical.

## Related Commits

The fix that prompted this task converted these files to use `css__to_css_unit()`:
- `src/select/properties/grid_template_rows.c`
- `src/select/properties/grid_template_columns.c`

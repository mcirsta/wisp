mke # CSS Grid Implementation Review

**Date:** 2026-01-03  
**Status:** Review complete, issues identified

## Summary

The `layout_grid.c` implementation provides basic grid layout functionality but has several deviations from the CSS Grid Layout Module Level 1 specification.

---

## Spec Conformance Analysis

### ✅ Correctly Implemented

| Feature | Status | Notes |
|---------|--------|-------|
| `grid-template-columns` fixed sizes (px, em) | ✅ Works | Correctly reads track definitions |
| `grid-template-rows` fixed sizes | ✅ Works | Fixed in this session (was reading bytecode units) |
| `grid-column-start/end` placement | ✅ Works | Items placed in specified columns |
| `grid-row-start/end` placement | ✅ Works | Items placed in specified rows |
| `column-gap` | ✅ Works | Gap between columns correctly applied |
| `row-gap` | ✅ Works | Gap between rows correctly applied |
| Grid items stretch to fill cells | ✅ Works | Fixed in this session |

---

### ⚠️ Partially Implemented / Non-Conformant

#### 1. Auto-Placement Algorithm (§8)
**Spec:** The auto-placement algorithm should fill grid slots based on `grid-auto-flow` (row/column/dense).

**Current:** Simple linear placement - items increment column, wrap to next row.

**Issues:**
- `grid-auto-flow` property is not read
- No support for `dense` packing mode
- Items with explicit positions don't reserve their slots before auto-placement

**File:** `layout_grid.c:731-733, 756-789`

---

#### 2. Implicit Grid Generation (§7.5)
**Spec:** When items are placed beyond the explicit grid, implicit tracks are created using `grid-auto-rows` and `grid-auto-columns`.

**Current:** Implicit tracks exist but use default sizing (content-based).

**Issues:**
- `grid-auto-rows` property is not read
- `grid-auto-columns` property is not read
- Implicit tracks default to 0 height until content fills them

**File:** `layout_grid.c:716-717` - only explicit row heights from CSS are read

---

#### 3. `fr` Unit Track Sizing (§7.2.3)
**Spec:** `fr` units distribute remaining space after fixed tracks are sized.

**Current:** `fr` tracks get equal share of remaining space.

**Issues:**
- `fr` values are not weighted correctly (e.g., `2fr` should get 2x space of `1fr`)
- Min-content constraints not applied to `fr` tracks

**File:** `layout_grid.c:520-528, 586-610`

---

#### 4. Min/Max Track Sizing (`minmax()`) (§7.2.4)
**Spec:** `minmax(min, max)` constrains track size between min and max.

**Current:** Falls back to max value or 1fr.

**Issues:**
- Min constraint not enforced
- Content-based keywords (`min-content`, `max-content`) in minmax not handled

**File:** `layout_grid.c:540-563`

---

#### 5. `align-items` / `justify-items` (§10)
**Spec:** Controls alignment of items within their grid area.

**Current:** Items always stretch (default behavior is correct).

**Issues:**
- `align-items: start/center/end` not implemented
- `justify-items: start/center/end` not implemented
- Items always stretch even when these properties are set

---

#### 6. `align-self` / `justify-self` (§10.5)
**Spec:** Per-item override of alignment.

**Current:** Not implemented.

---

#### 7. Grid Area Naming (§8.3)
**Spec:** Named grid areas via `grid-template-areas` and `grid-area`.

**Current:** Not implemented.

---

#### 8. Subgrid (Level 2)
**Status:** Not implemented (expected - it's Level 2)

---

## Priority Recommendations

### High Priority (Visible bugs)
1. **Fix `fr` weighting** - `2fr` should get 2x width of `1fr`
2. **Support `grid-auto-rows`** - Implicit rows should use this for sizing

### Medium Priority (Common use cases)
3. **Implement `align-items`/`justify-items`** - Center alignment is very common
4. **Improve auto-placement** - Respect `grid-auto-flow`

### Low Priority (Edge cases)
5. Named grid areas (`grid-template-areas`)
6. Dense packing mode
7. Full minmax() implementation

---

## Files Modified in This Session

| File | Change |
|------|--------|
| `layout_grid.c` | Added `init_row_heights_from_css()`, fixed row height initialization, items now stretch to fill cells |
| `grid_template_rows.c` | Added `css__to_css_unit()` calls to convert bytecode units |
| `grid_template_columns.c` | Same as above |

---

## Test File

Created `/mnt/netac/proj/wisp/grid_placement_test.html` for visual testing.

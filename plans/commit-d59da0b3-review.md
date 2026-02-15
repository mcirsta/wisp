# Commit Review: d59da0b3fa594de5d257ba3bc7fe7501b1341c17

## Summary

This commit fixes the `layout_minmax_object_width()` function to properly respect CSS min/max constraints when calculating intrinsic widths for replaced elements. The fix addresses two bugs:

1. **CSS max-height/max-width were ignored** in min/max width calculations
2. **box-sizing was read from the wrong element** for inline replaced elements

---

## Issues Found

| Severity | File:Line | Issue |
|----------|-----------|-------|
| SUGGESTION | `src/content/handlers/html/layout.c:3438` | Similar issue exists in layout pass |

---

## Detailed Findings

### 1. New Function `layout_minmax_object_width()` (lines 475-526)

**Confidence: 95%**

The new function correctly reads CSS constraints:
- `css_computed_max_height(box->style, &v, &u)` - reads max-height
- `css_computed_max_width(box->style, &v, &u)` - reads max-width  
- `css_computed_min_height(box->style, &v, &u)` - reads min-height
- `css_computed_min_width(box->style, &v, &u)` - reads min-width

The check `u != CSS_UNIT_PCT` correctly excludes percentage constraints since the containing block dimensions aren't known during the minmax pass.

**This is correct per [CSS 2.1 Section 10.3.2](https://www.w3.org/TR/CSS21/visudet.html#min-max-widths).**

### 2. Bug #2 Fix: Box-Sizing for Inline Elements (lines 720, 727-728)

**Confidence: 95%**

**Before (bug):**
```c
// Read from containing block
bs = css_computed_box_sizing(block->style);
calculate_mbp_width(&content->unit_len_ctx, block->style, LEFT, ...);
```

**After (fixed):**
```c
// Read from inline element itself
bs = css_computed_box_sizing(b->style);
calculate_mbp_width(&content->unit_len_ctx, b->style, LEFT, ...);
```

This correctly applies `box-sizing: border-box` and margin/border/padding from the inline element (e.g., `<a>` tag in the test case) rather than the containing block.

### 3. SUGGESTION: Similar Issue in Layout Pass

**File:** `src/content/handlers/html/layout.c:3438`

```c
/* No min constraints for block objects */
struct css_size no_min = {.type = CSS_SIZE_AUTO, .value = 0};
layout_get_object_dimensions(
    &content->unit_len_ctx, block, &temp_width, &block->height, no_min, INT_MAX, no_min, INT_MAX);
```

**Problem:** This is in the actual **layout pass** (not minmax), and still uses `INT_MAX` for max constraints. While the comment says "No min constraints," max constraints (max-width, max-height) should likely be respected here too per CSS spec.

**Suggestion:** Consider applying the same fix to the layout pass for consistency.

This is a separate issue from what this commit addresses, but worth noting for completeness.

---

## Test File

The test file `tests/layout-bugs/inline-block-max-height.html` correctly documents both bugs and expected behavior.

---

## Recommendation

**APPROVE** - The fix correctly addresses the CSS min/max constraint handling in the minmax pass. The only issue found is a similar pattern in the layout pass (line 3438) which is a separate concern and could be addressed in a future commit.

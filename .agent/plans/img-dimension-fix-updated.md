# UPDATED Implementation Plan: IMG Width/Height Hint Fix

## Problem: Presentational Hints Being Converted to Percentages

### Diagnostic Results

**Good News:** `css_hint_width()` and `css_hint_height()` ARE working correctly!
- HTML attribute `width="800"` → parsed as `value=800 unit=0` (CSS_UNIT_PX) ✅
- Hint is successfully created with `CSS_WIDTH_SET` status ✅

**Bad News:** Hints are being corrupted somewhere in libcss!
- When `css_computed_width()` is called later → returns `wunit=27` (CSS_UNIT_PCT) ❌  
- `box_image()` checks `wunit != CSS_UNIT_PCT` and fails → REPLACE_DIM not set ❌

### Root Cause Analysis

The hints.c code at line 237 correctly sets `*unit = CSS_UNIT_PX` for unitless values:
```c
if (len > read && data[read] == '%')
    *unit = CSS_UNIT_PCT;
else
    *unit = CSS_UNIT_PX;  // Correctly uses PX
```

**The problem is in libcss itself** - it's converting presentational hints from pixels to percentages!

This is likely happening in libcss's hint application code where it assumes unitless HTML attribute values should be percentages (which is true for some HTML attributes like `width` on `<table>`, but **NOT** for `<img>` width/height).

### Two Possible Solutions

#### Option A: Fix libcss (Proper but Complex)
Modify libcss to correctly handle IMG width/height hints as pixels.
- **Pro**: Architecturally correct
- **Con**: Requires diving deep into libcss internals
- **Con**: May affect other hint handling

#### Option B: Bypass Hints for IMG (Quick Fix) ⭐ RECOMMENDED
Instead of relying on presentational hints,  directly check HTML attributes in `box_image()` and set dimensions via inline style.

**Implementation:**
```c
// In box_image(), after checking css_computed_width/height:
if (!(box->flags & REPLACE_DIM) && box->node) {
    // Check for HTML width/height attributes directly
    dom_string *width_attr = NULL, *height_attr = NULL;
    
    dom_element_get_attribute(node, corestring_dom_width, &width_attr);
    dom_element_get_attribute(node, corestring_dom_height, &height_attr);
    
    if (width_attr && height_attr) {
        // Both attributes present - we know dimensions!
        css_fixed w_val, h_val;
        css_unit w_unit, h_unit;
        
        if (parse_dimension(width_attr, false, &w_val, &w_unit) &&
            parse_dimension(height_attr, false, &h_val, &h_unit) &&
            w_unit == CSS_UNIT_PX && h_unit == CSS_UNIT_PX) {
            
            box->flags |= REPLACE_DIM;
        }
    }
}
```

## Implementation Plan - Option B

### Phase 1: Direct Attribute Check in box_image() ✅ SIMPLE
**File**: `src/content/handlers/html/box_special.c`

1. After `css_computed_width/height` check fails, add fallback
2. Use `dom_element_get_attribute()` to read width/height
3. Parse using `parse_dimension()` 
4. If both are pixels (not %), set REPLACE_DIM

**Pros**:
- Simple, localized change
- No risk to other hint handling
- Easy to test and verify

**Cons**:
- Duplicates some work (hints are still generated but ignored)
- Not the "pure" architectural solution

### Success Criteria

1. ✅ Images with `width="200" height="150"` get REPLACE_DIM set
2. ✅ No box conversion restarts for images with HTML dimensions
3. ✅ hotnews.ro load time improves from ~14s to ~5-7s
4. ✅ Images still render at correct size
5. ✅ CSS dimensions (`style="width:200px"`) still work

## Next Steps

1. Implement Option B (direct attribute check)
2. Remove diagnostic logging
3. Test on hotnews.ro
4. Measure performance improvement

## Alternative: Option A (Future Work)

If we want to fix libcss properly later:
- Investigate libcss hint → computed style conversion
- Find where CSS_UNIT_PX → CSS_UNIT_PCT conversion happens
- Add special case for IMG width/height to preserve unit
- This is a larger refactoring for future consideration

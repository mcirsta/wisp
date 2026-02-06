# CTV News Rendering Analysis

**Date**: 2026-02-07  
**URL**: https://www.ctvnews.ca/  
**Issue**: Header and page renders incorrectly, takes a long time

---

## Executive Summary

The CTV News header fails to render correctly because **CSS Variables (`var()`) are not implemented in libcss**. The site uses variables for critical layout properties like `display: flex`, not just colors/theming.

---

## Site Characteristics (from Chrome Analysis)

| Metric | Value |
|--------|-------|
| Total DOM elements | ~2,100 |
| Flexbox containers | 656 |
| Grid containers | 123 |
| JSON metadata blob | 311 KB (`fusion-metadata` script) |
| Max DOM depth | 19 |
| Hidden elements | 500+ |

---

## Header CSS Requirements

### Layout Structure
```
<header class="c-stack b-single-column-regular__navigation">  → display: flex (column)
  <nav class="b-header-nav-chain">                           → display: var(→flex)
    <div class="top-layout">                                 → flex, space-between
      [Sections button] [Logo] [Search button]
```

### Critical CSS Properties (from Chrome)

| Element | Property | Value | Uses Variable? |
|---------|----------|-------|----------------|
| `<header>` | display | flex | No (hardcoded) |
| `<header>` | position | sticky | Yes (`--b-single-column-regular-navigation-position`) |
| `<header>` | z-index | 10000 | Unknown |
| `<nav>` | display | flex | **Yes** (`--b-header-nav-chain-display`) |
| `<nav>` | justify-content | center | **Yes** (`--b-header-nav-chain-justify-content`) |
| `<nav>` | align-items | center | **Yes** (`--b-header-nav-chain-align-items`) |
| `<nav>` | min-height | 80px | **Yes** (`--b-header-nav-chain-min-block-size`) |

---

## Root Causes

### 1. CSS Variables Not Implemented (Primary)
- libcss does not support `var()` or custom properties (`--name: value`)
- When `display: var(--b-header-nav-chain-display)` is parsed, it returns empty/initial
- The nav falls back to `display: block` instead of `display: flex`
- Result: **Logo and buttons stack vertically instead of horizontal row**

### 2. `position: sticky` Not Handled in Layout
- libcss parses `CSS_POSITION_STICKY = 0x5` correctly
- But `/src/content/handlers/html/layout*.c` has no handler for it
- Falls back to `static` positioning
- Result: **Header scrolls away instead of sticking**

### 3. `light-dark()` Not Implemented
- Modern CSS color function for theme-aware colors
- Not implemented in libcss
- Affects theming/colors, not layout

### 4. AVIF Images Not Supported
- All 170+ errors in `ns-error.txt` are AVIF rejection
- `image/avif` format not in accepted types
- Images fail to load (placeholders shown)

---

## Performance Issue

### Log Size Analysis
| File | Size |
|------|------|
| ns-deepdebug.txt | 662 MB |
| ns-debug.txt | 489 MB |
| ns-info.txt | 167 MB |
| ns-warning.txt | 27 MB |
| ns-error.txt | 55 KB |

The massive log files are caused by **verbose flex/grid layout tracing** in `layout_flex.c` and `layout_grid.c`. The layout code runs correctly but generates excessive debug output.

---

## What's Needed to Fix the Header

### Required: CSS Variables in libcss
1. Parse `--name: value` custom property declarations
2. Parse `var(--name)` and `var(--name, fallback)` function calls  
3. Implement variable resolution during the cascade (inheritance, scope)
4. Handle circular reference detection

**Effort**: Significant (weeks of work)

### Optional Improvements
- Handle `position: sticky` in layout (moderate effort)
- Add AVIF image support (moderate effort)
- Reduce log verbosity for flex/grid traces (easy)

---

## wisp-qt Feature Support Matrix

| Feature | libcss Parsing | Layout Handling |
|---------|---------------|-----------------|
| `display: flex` | ✅ | ✅ |
| `flex-direction` | ✅ | ✅ |
| `justify-content: space-between` | ✅ | ✅ |
| `display: grid` | ✅ | ✅ |
| `position: sticky` | ✅ | ❌ |
| CSS Variables `var()` | ❌ | N/A |
| `light-dark()` | ❌ | N/A |
| `image/avif` | N/A | ❌ |

---

## Recommendations

1. **Short term**: Test with simpler sites that don't use CSS variables for layout
2. **Medium term**: Implement `position: sticky` handling
3. **Long term**: Implement CSS Variables in libcss (major undertaking)

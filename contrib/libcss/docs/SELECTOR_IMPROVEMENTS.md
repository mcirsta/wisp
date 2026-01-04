# LibCSS Selector Parser Improvement Proposals

This document outlines potential improvements to the LibCSS selector parsing,
storage, and matching infrastructure to make future CSS Selectors additions
easier and more maintainable.

## Current Architecture Issues

### 1. Bit-Packed Value Type Field

**Problem:** The `value_type` field in `css_selector_detail` was only 1 bit,
limiting it to 2 values (`STRING` and `NTH`). Adding `:nth-child(An+B of selector)`
required expanding to 2 bits.

**Location:** `src/stylesheet.h:84`

```c
unsigned int value_type : 1; /* Only STRING or NTH */
```

**Impact:** Each new value type requires modifying the bit field width and
potentially restructuring the bit-packed fields.

### 2. Union-Based Value Storage

**Problem:** `css_selector_detail_value` is a union that grows with each new
selector type. The `nth_of` struct duplicates the `a` and `b` fields from `nth`.

**Location:** `src/stylesheet.h:68-80`

```c
typedef union css_selector_detail_value {
    lwc_string *string;
    struct { int32_t a, b; } nth;
    struct { int32_t a, b; lwc_string *filter_class; } nth_of;  // Duplicates a,b
} css_selector_detail_value;
```

**Impact:** Memory waste for simple cases, growing union size, code duplication.

### 3. Monolithic Parser Functions

**Problem:** Pseudo-class parsing is a large if-else chain in `parsePseudo()`.
Each new pseudo-class requires modifying this core function.

**Location:** `src/parse/language.c:1187-1330`

**Impact:** Increasing complexity, harder to maintain, risk of regressions.

---

## Proposed Improvements

### Proposal 1: Extensible Value Storage

Replace the union with a tagged pointer approach:

```c
typedef enum css_selector_value_type {
    CSS_SEL_VALUE_NONE = 0,
    CSS_SEL_VALUE_STRING,
    CSS_SEL_VALUE_NTH,
    CSS_SEL_VALUE_NTH_OF,
    CSS_SEL_VALUE_SELECTOR_LIST,  /* Future: :has(), :is() */
    /* Room for expansion... */
} css_selector_value_type;

typedef struct css_selector_detail_value {
    uint8_t type;
    union {
        lwc_string *string;
        struct css_nth_data *nth;      /* Heap allocated */
        struct css_nth_of_data *nth_of;
        void *data;  /* Generic pointer for new types */
    };
} css_selector_detail_value;
```

**Benefits:**
- Type field has room for 256 values
- Complex data allocated on heap only when needed
- Union stays small (just a pointer)

### Proposal 2: Pseudo-Class Registry

Create a pluggable pseudo-class system:

```c
typedef struct css_pseudo_class_handler {
    lwc_string *name;   /* e.g., "nth-child" */
    bool is_function;   /* Takes arguments? */
    
    css_error (*parse)(
        css_language *c,
        const parserutils_vector *vector,
        int32_t *ctx,
        css_selector_detail *detail
    );
    
    css_error (*match)(
        css_select_ctx *ctx,
        void *node,
        const css_selector_detail *detail,
        css_select_state *state,
        bool *match
    );
} css_pseudo_class_handler;

/* Registration */
css_error css_register_pseudo_class(const css_pseudo_class_handler *handler);
```

**Benefits:**
- New pseudo-classes can be added without modifying core parsing
- Matching logic co-located with parsing logic
- Easier to test individual pseudo-classes
- Could support dynamically loaded handlers

### Proposal 3: Selector Detail Chaining for Complex Data

Instead of expanding the union, store complex data as additional chained details:

```c
/* For :nth-child(2n+1 of .foo) */
/* Primary detail: NTH_CHILD with nth.a=2, nth.b=1 */
/* Chained detail: CLASS with name="foo" (marked as filter) */

typedef struct css_selector_detail {
    /* ... existing fields ... */
    unsigned int next : 1;
    unsigned int is_filter : 1;  /* NEW: Marks as filter for parent */
} css_selector_detail;
```

**Benefits:**
- Reuses existing string/class storage
- No union expansion needed
- Natural extension for :has(), :is(), :where()

---

## Implementation Priority

1. **Short term:** Proposal 3 (chaining) - Minimal changes, handles immediate needs
2. **Medium term:** Proposal 1 (extensible storage) - Better memory efficiency
3. **Long term:** Proposal 2 (registry) - Full extensibility for CSS4/5 selectors

## Related Future Work

- `:has()` selector (CSS Selectors Level 4)
- `:is()` and `:where()` selectors
- `:nth-child(An+B of <selector-list>)` with full selector lists
- `:nth-last-child(An+B of <selector>)`

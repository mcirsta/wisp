# Phase 2: Modern Pseudo-Elements Rendering Support

This document outlines the Phase 2 implementation plan for properly rendering modern pseudo-elements (like `::backdrop`, `::file-selector-button`, `::placeholder`) in the Wisp Engine after Phase 1 (Parsing Support in `libcss`) was completed.

## Background Context
In Phase 1, we updated `libcss` to successfully parse and intern modern CSS pseudo-elements instead of rejecting their entire selector blocks as `CSS_INVALID`. While this fixes critical bugs related to CSS resets (like Tailwind CSS's preflight), the **core engine and fontending** still do not functionally render or apply visual styles to these elements. 

Phase 2 will introduce the actual styling and visual logic across the Wisp pipeline.

## Implementation Plan

### 1. `libcss` Computed Style Extensions
To make the pseudo-element styles queryable down the pipeline, we must ensure `libcss` computes and stores properties specifically for the matched pseudo-elements.
- **`src/select/select.c`**: We already tag elements with enum flags like `CSS_PSEUDO_ELEMENT_BACKDROP`. We need to verify and extend `css_select_style` so nested properties scoped to these pseudo-elements are returned in an accessible structure (e.g., via `css_computed_style *pseudo_styles[CSS_PSEUDO_ELEMENT_COUNT]`).
- **`src/select/properties/`**: Ensure shorthand cascades properly target the mapped pseudo-elements instead of polluting the base element's computed styles.

### 2. Core Engine Layout & Dom Integration (`src/layout/`)
The core layout engine needs to inject these pseudo-elements into the boxtree when they possess valid computed styles. 
- **`box_construct.c`**: Modify the box construction logic to generate anonymous `box` tree nodes for pseudo-elements when `libcss` provides styling.
  - Currently handles `::before` and `::after`.
  - Add logic to construct an overlay box for `::backdrop` (positioning it behind the `dialog` or full-screen element).
  - Add logic to construct an internal child box for `::file-selector-button` for `<input type="file">`.
  - Add logic to construct inline text boxes for `::placeholder` inside `<input>`/`<textarea>`.

### 3. Frontend Integration (Qt / GTK / Monkey)
Once the boxtree possesses nodes for these elements, we need to map them to frontend drawing APIs or native form controls.

#### `::backdrop`
- **Behavior**: An overlay rendered behind `dialog` or full-screen elements.
- **Frontend Action**: Ensure the plotter supports drawing `box` nodes with `z-index` placed between the application layer and the top-level dialog.

#### `::placeholder`
- **Behavior**: Ghost text within text inputs.
- **Frontend Action (Qt)**: We may map the `libcss` computed `color` and `font-style` properties directly to `QLineEdit::setPlaceholderText()` styling via `QPalette::PlaceholderText`. Alternatively, we natively plot the boxtree text node inside the input bounds using the standard Wisp font plotter if the input isn't a native Qt widget.

#### `::file-selector-button`
- **Behavior**: The "Choose File" button resting inside an `<input type="file">`.
- **Frontend Action**: 
   - Core engine: Construct a native button child `box`. 
   - Apply specific `libcss` styling (background, border, padding).
   - Ensure event delegation (clicks on this box trigger the file picker dialog).

#### Webkit Specifics (`::-webkit-scrollbar`, etc.)
- These are highly complex and mostly used to style native browser scrollbars and inputs.
- **Action**: Assess whether to mock these using standard Wisp boxes or map them to native frontend styles (e.g., `QScrollBar` stylesheets in Qt). If too complex, they can be ignored for plotting but kept internally so sites don't break.

## Verification Steps
1. Create dedicated HTML test pages for each pseudo-element (e.g., `test_backdrop.html` using `<dialog>`, `test_placeholder.html`).
2. Verify `ctest` passes to ensure no regressions in standard `box` construction.
3. Open pages in the Wisp Qt frontend to visually confirm the pseudo-elements render strictly according to their CSS rules.

### Phase 5: Caching & Core Optimizations
**Goal:** Keep performance overhead under 10-20% and minimize memory usage.
*   **Value Caching:** Once a `var(--name)` is resolved to its final `css_color` or `css_fixed` value in a specific context, cache the result. Subsequent reads of `--name` in that context return the cached value instantly.
*   **Shared Contexts:** Elements with identical matching variable declarations and the same parent context will share the `css_variable_context` pointer, reducing memory allocations by up to 80%.
*   **String Deduplication:** Rely entirely on `lwc_string` interning for variable names and bytecode bodies to ensure values like `blue` are stringified and stored only once in memory.

### Phase 6: Advanced Features (Cycles & Fallbacks)
**Goal:** Spec-compliant edge cases.
*   **Cycle Detection & Depth Limit:** Implement a resolution stack during `var()` unrolling. If a variable references itself (e.g., `--a: var(--b); --b: var(--a)`), or if the unroll depth exceeds 10 levels, mark it as `CSS_INVALID`.
*   **Fallback Compilation:** Ensure fallback arguments in `var(--a, red)` are parsed completely so they can be swapped in efficiently during resolution if the context misses the variable.

### Phase 7: JavaScript Integration
**Goal:** CSSOM support.
*   Expose `getPropertyValue('--name')` and `setProperty('--name', 'val')` to the `quickjs` DOM bindings.

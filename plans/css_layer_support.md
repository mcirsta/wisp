# CSS `@layer` Support for libcss

## Background

Wisp has zero support for CSS `@layer` (Cascade Layers, CSS Cascading Level 5). Rules inside `@layer` blocks are treated as unknown at-rules and silently skipped. This breaks any site using Tailwind CSS v4+ or modern CSS layers — e.g. hotnews.ro defines all theme variables inside `@layer theme{:root,:host{...}}`.

## User Review Required

> [!IMPORTANT]
> `@layer` changes the cascade ordering: layers declared **first** have **lower** priority than layers declared **later**. Unlayered rules beat all layered rules. For `!important`, this is **reversed** — earlier layers win. This is a fundamental change to how `css__outranks_existing()` works.

> [!WARNING]
> Full spec-conformant `@layer` is a multi-component change touching the parser, stylesheet data structures, selector hash, and cascade comparison. I propose a phased approach where **Phase 1** (transparent parsing) immediately fixes the token resolution errors, and **Phase 2** (cascade ordering) adds correct layer priority semantics.

## Proposed Changes

### Phase 1: Parse `@layer` and `@supports` (transparent)

This phase makes rules inside `@layer` and `@supports` blocks visible to the engine, treating them as if they were at the top level. This immediately fixes all ~750 token resolution errors on hotnews.ro, which uses Tailwind CSS v4 to define variables inside these blocks. Note: We will NOT attempt to parse generic unknown at-rules, as this is error-prone. We will explicitly target known blocks.

---

### Component: Parser String Table

#### [MODIFY] [propstrings.h](file:///mnt/netac/proj/wisp/contrib/libcss/src/parse/propstrings.h)

Add `LAYER` and `SUPPORTS` to the string enum and string table so the parser can match these at-keywords.

---

### Component: Stylesheet Data Structures

#### [MODIFY] [stylesheet.h](file:///mnt/netac/proj/wisp/contrib/libcss/src/stylesheet.h)

- Add `CSS_RULE_LAYER` and `CSS_RULE_SUPPORTS` to the `css_rule_type` enum
- Add `css_rule_layer` and `css_rule_supports` structs, modeled on `css_rule_media`. For example:
  ```c
  typedef struct css_rule_layer {
      css_rule base;
      lwc_string *name;       /* Layer name (NULL for anonymous) */
      css_rule *first_child;
      css_rule *last_child;
  } css_rule_layer;
  ```
  (`css_rule_supports` will be structured similarly, storing its condition string in the future, but for Phase 1 they can just act as transparent block containers).

---

### Component: Language Parser

#### [MODIFY] [language.c](file:///mnt/netac/proj/wisp/contrib/libcss/src/parse/language.c)

In `handleStartAtRule()`:
- Add `@layer` and `@supports` detection after the `@media` branch
- For `@layer`, parse the optional layer name.
- For `@supports`, safely consume tokens up to the `{` block start, ignoring the condition for Phase 1.
- For both block types, create the corresponding `CSS_RULE_*` rule, push it to the context stack, and prepare to parse children.

In `handleBlockContent()`:
- **`@layer` and `@supports`**: Add `CSS_RULE_LAYER` and `CSS_RULE_SUPPORTS` to the `if` branch that calls `handleStartRuleset(c, vector)`. These blocks contain rulesets.

In `handleEndBlock()`:
- Add `CSS_RULE_LAYER` and `CSS_RULE_SUPPORTS` handling to gracefully pop their rules off the parser stack.

---

### Component: Stylesheet Management

#### [MODIFY] [stylesheet.c](file:///mnt/netac/proj/wisp/contrib/libcss/src/select/stylesheet.c)

- Handle `CSS_RULE_LAYER`, `CSS_RULE_THEME`, `CSS_RULE_SUPPORTS` in `css__stylesheet_rule_create()` and `css__stylesheet_rule_destroy()`.
- In `css__stylesheet_add_rule()`: when adding a child rule, attach it to the parent block's child list.

---

### Component: Selector Hash (rule iteration)

#### [MODIFY] [hash.c](file:///mnt/netac/proj/wisp/contrib/libcss/src/select/hash.c)

When inserting rules into the selector hash, recursively descend into `CSS_RULE_LAYER`, `CSS_RULE_THEME`, and `CSS_RULE_SUPPORTS` children, just like `CSS_RULE_MEDIA`. This ensures selectors inside these blocks are findable.

---

### Phase 2: Cascade layer ordering (correct priority)

This phase adds proper per-CSS-spec layer ordering to the cascade.

---

### Component: Layer Registry

#### [MODIFY] [stylesheet.h](file:///mnt/netac/proj/wisp/contrib/libcss/src/stylesheet.h)

Add a **layer order table** to `css_stylesheet`:
```c
typedef struct css_layer_entry {
    lwc_string *name;    /* Fully qualified dotted name */
    uint32_t order;      /* Declaration order (lower = less priority) */
} css_layer_entry;

/* In css_stylesheet: */
css_layer_entry *layers;
uint32_t layer_count;
uint32_t layer_capacity;
uint32_t next_layer_order;  /* Counter for assigning order */
```

Layer order is assigned at **first declaration** (either via `@layer name;` or `@layer name { ... }`). Subsequent blocks with the same name reuse the existing order.

---

### Component: Rule Layer Annotation

#### [MODIFY] [stylesheet.h](file:///mnt/netac/proj/wisp/contrib/libcss/src/stylesheet.h)

Add a `layer_order` field to `css_rule`:
```c
struct css_rule {
    ...
    uint32_t layer_order;  /* 0 = unlayered (highest normal priority) */
};
```

When a selector rule is added inside a `CSS_RULE_LAYER`, it inherits the layer's order value. Unlayered rules get `layer_order = 0`, which is treated as the **implicit highest layer** for normal declarations.

---

### Component: Cascade Comparison

#### [MODIFY] [select.c](file:///mnt/netac/proj/wisp/contrib/libcss/src/select/select.c)

Modify `css__outranks_existing()` to insert layer comparison between importance and specificity:

```
Current: origin → importance → specificity → source order
New:     origin → importance → LAYER ORDER → specificity → source order
```

Key rules per spec:
- **Normal declarations**: higher `layer_order` wins (later-declared layers have priority). `layer_order=0` (unlayered) beats all named layers.
- **`!important` declarations**: **reversed** — lower `layer_order` wins (earlier-declared layers have priority). `layer_order=0` (unlayered) **loses** to all named layers.

Add `current_layer` field to `css_select_state` and set it when cascading each rule.

---

### Component: Selector Hash Ordering

#### [MODIFY] [hash.c](file:///mnt/netac/proj/wisp/contrib/libcss/src/select/hash.c)

The hash chains are currently ordered by `(specificity, rule_index)`. With layers, this needs to become `(layer_order, specificity, rule_index)` so that `_selector_next()` picks selectors in correct cascade priority.

---

## Verification Plan

### Phase 1 Verification
1. Build with `ninja` — no compilation errors
2. Run `wisp-qt -split-logs hotnews.ro` — zero "Error resolving tokens" messages
3. Verify custom properties like `--spacing`, `--color-hotnews-blue` are stored and resolved
4. Run existing test suite: `ctest --test-dir build-gcc -j 8 --output-on-failure`

### Phase 2 Verification
1. Create test CSS files exercising layer ordering:
   ```css
   @layer base, override;
   @layer base { p { color: red; } }
   @layer override { p { color: blue; } }
   /* Expected: color = blue (later layer wins) */
   ```
2. Test `!important` reversal within layers
3. Test unlayered rules beating all layered rules
4. Test anonymous layers
5. Test nested layers (`@layer outer { @layer inner { ... } }`)
6. Run against multiple real-world sites using Tailwind v4

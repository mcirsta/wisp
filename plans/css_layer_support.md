# CSS cascade-layer ordering plan

Status: separate follow-up. Transparent parsing of `@layer` and `@supports`
blocks is already present in libcss. Layer priority and evaluation of
`@supports` conditions are not implemented by that parsing support.

The [CSS variable architecture plan](css-vars-architecture-plan.md) owns variable
storage, inheritance, resolution, and scope isolation. This document retains the
remaining layer-ordering proposal. It does not require reimplementing the parser
work that has already landed, and variable benchmarks must not assume layer
precedence or conditional-rule semantics are complete.

## Remaining work: cascade-layer ordering

This phase adds proper per-CSS-spec layer ordering to the cascade.


### Component: Layer Registry

#### [MODIFY] [stylesheet.h](../contrib/libcss/src/stylesheet.h)

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


### Component: Rule Layer Annotation

#### [MODIFY] [stylesheet.h](../contrib/libcss/src/stylesheet.h)

Add a `layer_order` field to `css_rule`:
```c
struct css_rule {
    ...
    uint32_t layer_order;  /* 0 = unlayered (highest normal priority) */
};
```

When a selector rule is added inside a `CSS_RULE_LAYER`, it inherits the layer's order value. Unlayered rules get `layer_order = 0`, which is treated as the **implicit highest layer** for normal declarations.


### Component: Cascade Comparison

#### [MODIFY] [select.c](../contrib/libcss/src/select/select.c)

Modify `css__outranks_existing()` to insert layer comparison between importance and specificity:

```
Current: origin → importance → specificity → source order
New:     origin → importance → LAYER ORDER → specificity → source order
```

Key rules per spec:
- **Normal declarations**: higher `layer_order` wins (later-declared layers have priority). `layer_order=0` (unlayered) beats all named layers.
- **`!important` declarations**: **reversed** — lower `layer_order` wins (earlier-declared layers have priority). `layer_order=0` (unlayered) **loses** to all named layers.

Add `current_layer` field to `css_select_state` and set it when cascading each rule.


### Component: Selector Hash Ordering

#### [MODIFY] [hash.c](../contrib/libcss/src/select/hash.c)

The hash chains are currently ordered by `(specificity, rule_index)`. With layers, this needs to become `(layer_order, specificity, rule_index)` so that `_selector_next()` picks selectors in correct cascade priority.

## Verification

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

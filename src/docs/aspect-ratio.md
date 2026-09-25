# Preferred aspect ratios

Wisp supports numeric `aspect-ratio` values (`16 / 9`, `1.5`), `auto`,
`auto <ratio>` in either order, CSS-wide keywords, and values supplied by
custom properties. Zero components are valid CSS but give no preferred ratio.
Negative components and units are rejected. The property is not inherited
unless requested explicitly.

## Implementation

`contrib/libcss/properties.toml` drives the property registry, dispatch and
computed storage. The manual parser and cascade handlers store two fixed-point
numbers; generated getters/setters retain them in the normal computed style.
The bytecode walkers for importance and deferred-variable selection also know
about the two operands. Do not edit generated build files.

The layout helpers in `layout_internal.h` transfer a chosen dimension through
the ratio, accounting for content-box versus border-box sizing. Layout chooses
width before deriving an auto height. Explicit width and height win over the
ratio. A layout flag records a ratio-derived height so percentage children and
margin collapsing can treat that height as definite. Visible content can grow
an auto minimum height; explicit `min-height: 0` or scrollable overflow allows
the ratio-sized box to overflow instead. Height constraints use the same
box-sizing convention as width constraints.

Block, inline-block, absolute, flex and grid paths use the shared calculation.
Flex items recompute their ratio height after flexing chooses the final width;
a definite cross size contributes to non-replaced row items' automatic minimum
width. Image sizing selects the CSS ratio, or the natural ratio when `auto` is
present and a natural ratio is available.

Grid now preserves `align-items: normal` separately from `stretch`: normal
alignment preserves a preferred ratio, while explicit stretching can transfer
its definite height back into an auto width. Flex continues to treat normal
alignment as stretch. This initial-value change accounts for the edits to
existing libcss selection expectations.

This does not replace the engine's intrinsic sizing or grid track-sizing
algorithms. Cyclic intrinsic contributions, the full cross-axis min/max transfer
rules and all replaced flex-item automatic-minimum cases are not covered by this
implementation. `calc()` expressions *inside the ratio* are not supported yet
(`calc()` in width/height constraints is supported). HTML width/height attributes
are not newly converted into presentational aspect-ratio hints.

## Regression coverage

- `layout_calc_test`: parsing, invalid values, custom properties, importance,
  inheritance/composition, box sizing, height constraints and content minima.
- `layout_flex_test`: actual flex layout with transferred minimum widths,
  explicit minima and maxima, and repeated reformatting.
- `qt_aspect_ratio`: 28 geometry assertions using the real renderer and
  `test/aspect-ratio.html`, including images, grid, flex and absolute positioning.
  Enabled with `WISP_QT_CAPTURE` and `BUILD_TESTING`; no Chromium dependency.

The saved HotNews snapshot was checked at 1280×900 and 390×900. The lead
story measured 843×549 versus Chromium's 842.25×549.28 on desktop, and
356×623 versus 356×623.98 on mobile. All 28 fixture geometry checks matched
Chromium exactly. The 1280×900 Haiku screenshot was byte-identical to the
pre-change capture. These checks validate the targeted sizing changes, not
complete rendering parity with Chromium.

For a manual reference comparison, build with `WISP_QT_CAPTURE=ON` and run:

```sh
python3 frontends/qt/compare_capture.py test/aspect-ratio.html \
  --wisp build/frontends/qt/wisp-qt --output /tmp/aspect-comparison \
  --id block --id grid-item --id grid-stretch --id image-auto --id flex-transferred
```

The CSS specification is [CSS Sizing Level 4, aspect ratios](https://www.w3.org/TR/css-sizing-4/#aspect-ratio).

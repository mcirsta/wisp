# Logical block spacing

Supported in the engine's current horizontal-tb writing mode:

- `padding-block` and `margin-block`, with one or two values.
- `padding-block-start/end` and `margin-block-start/end`.
- Existing `padding-inline` and `margin-inline` shorthands.

Block start/end map to top/bottom for both horizontal LTR and RTL text.
The existing inline shorthands map their first/second values to left/right;
unequal inline values in RTL remain a known limitation. Individual inline
start/end properties and vertical-writing-mode mapping remain unsupported.
This change does not claim complete CSS Logical Properties support.

`contrib/libcss/properties.toml` registers the properties and generates the
block-edge aliases. `parse/properties/logical_spacing.c` shares one pair parser
between all four axis shorthands, using the existing physical-side parsers.
The emitted top/right/bottom/left bytecode participates in the normal cascade,
including specificity, order, importance and CSS-wide keywords. Variable
substitution reaches the same parser. Layout consumes the existing computed
margins and padding; no extra computed-style storage is needed.

Lengths, percentages and `calc()` follow their physical equivalents. Margins
accept negative lengths and `auto`; padding rejects literal negative values
and `auto`. Percentages resolve against the containing block's inline width.
A CSS-wide keyword must be the entire shorthand value, and invalid multi-value
declarations are discarded as a whole.

`layout_calc_test` covers parsing, variables, calculations, defaults,
inheritance, precedence and invalid values. `qt_logical_spacing`, enabled with
`WISP_QT_CAPTURE` and `BUILD_TESTING`, checks the actual geometry of 15 boxes
plus the effect of a block margin on its following sibling. The fixture is
`test/logical-spacing.html`. The existing aspect-ratio fixture also runs to
catch changes where spacing contributes to a preferred ratio.

The saved HotNews desktop header grows from 57px to 99px (Chromium: 99.44px),
and the main container begins at y=174 (Chromium: 175.94). The mobile header
matches at 68px. The pre-existing offset inside the first news column remains
51px on desktop and 34px on mobile; it is separate from the new block spacing.
The Haiku screenshot remains byte-identical to the aspect-ratio baseline.

For a reference comparison:

```sh
python3 frontends/qt/compare_capture.py test/logical-spacing.html \
  --wisp build/frontends/qt/wisp-qt --output /tmp/logical-spacing \
  --id single --id variable --id percentage --id margins --id edges --id header
```

Specification: [CSS Logical Properties, margins and padding](https://www.w3.org/TR/css-logical-1/#box).

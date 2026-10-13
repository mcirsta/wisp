# Logical block spacing and offsets

Supported in the engine's current horizontal-tb writing mode:

- `padding-block` and `margin-block`, with one or two values.
- `padding-block-start/end` and `margin-block-start/end`.
- `inset-block`, `inset-block-start/end`, including independent declaration edits.
- Existing `padding-inline`, `margin-inline` and `inset-inline` shorthands.

Block start/end map to top/bottom for both horizontal LTR and RTL text.
The existing inline shorthands map their first/second values to left/right;
unequal inline values in RTL remain a known limitation. Individual inline
start/end properties and vertical-writing-mode mapping remain unsupported.
This change does not claim complete CSS Logical Properties support.

`contrib/libcss/properties.toml` registers distinct logical CSS names and generates
block-edge parse wrappers. These are not CSSOM aliases for top/bottom:
`top` and `inset-block-start`, for example, retain separate declarations, order
and values when edited. `parse/properties/logical_spacing.c` shares one atomic
pair parser between all six spacing/offset axis shorthands, delegating values
to the generated longhand parsers. The former separate inset pair parsers are
removed. Allocation failure rolls back the entire pair and token cursor.
The emitted top/right/bottom/left bytecode participates in the normal cascade,
including specificity, order, importance and CSS-wide keywords. Variable
substitution reaches the same parser. Layout consumes the existing computed
margins, padding and offsets; no extra computed-style storage is needed.

Lengths, percentages and `calc()` follow their physical equivalents. Margins
accept negative lengths and `auto`; padding rejects literal negative values
and `auto`. Margin/padding percentages resolve against the containing block's
inline width. Block-offset percentages use its height in horizontal-tb. Offsets
accept negative lengths and `auto`, like top/bottom. A CSS-wide keyword
must be the entire shorthand value, and invalid multi-value declarations are
discarded as a whole.

`layout_calc_test` covers parsing, variables, calculations, defaults,
inheritance, precedence and invalid values. `qt_logical_spacing`, enabled with
`WISP_QT_CAPTURE` and `BUILD_TESTING`, checks the actual geometry of 15 boxes
plus the effect of a block margin on its following sibling and seven positioned
boxes. The offset cases cover RTL, longhand/physical overrides, importance,
variables/calculations and invalid mixed CSS-wide values. Native declaration
tests cover independent readback, partial edits/removals, logical/physical
ordering, pending substitution, and allocation rollback; `libcss_var_resolve`
covers inheritance, invalid substitutions and origin revert. The fixture is
`test/logical-spacing.html`. The existing aspect-ratio fixture also runs to
catch changes where spacing contributes to a preferred ratio.

The saved HotNews desktop header grows from 57px to 99px (Chromium: 99.44px),
and the main container begins at y=174 (Chromium: 175.94). The mobile header
matches at 68px. The separate 51px desktop / 34px mobile offset inside the first
news column was subsequently fixed in margin-collapse look-ahead: block-level
flex/grid children now terminate the chain, including empty containers nested
inside block wrappers. Their external margins can still collapse with the
wrapper, but the wrapper's bottom margin cannot leak above their content.
`layout_margin_collapse_test` covers positive/negative margins, empty/nonempty
containers and nested wrappers; `test/margin-formatting-context.html` is the
rendered reproduction. That layout fix leaves Haiku's screenshot unchanged.

For a reference comparison:

```sh
python3 frontends/qt/compare_capture.py test/logical-spacing.html \
  --wisp build/frontends/qt/wisp-qt --output /tmp/logical-spacing \
  --id single --id variable --id percentage --id margins --id edges --id header
```

Specification: [CSS Logical Properties, margins and padding](https://www.w3.org/TR/css-logical-1/#box).

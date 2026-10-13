# Logical spacing and offsets

Supported in the engine's current horizontal-tb writing mode:

- `padding-block` and `margin-block`, with one or two values.
- `padding-block-start/end` and `margin-block-start/end`.
- `inset-block`, `inset-block-start/end`, including independent declaration edits.
- `padding-inline`, `margin-inline` and `inset-inline`, with one or two values.
- All six corresponding inline-start/end longhands, with independent declaration edits.

Block start/end map to top/bottom for both horizontal LTR and RTL text.
Inline start/end use the element's own computed direction: left/right in LTR,
right/left in RTL. Logical and physical declarations compete by their normal
cascade priority and order. Explicit logical inheritance follows the same logical
edge on the parent, even when parent and child directions differ, as specified
by [CSS Logical 1 §4](https://drafts.csswg.org/css-logical-1/#box).
The draft still lists inheritance as an open CSSWG issue; these tests follow its
current normative text. Vertical writing modes remain unsupported.
This change does not claim complete CSS Logical Properties support.

`contrib/libcss/properties.toml` registers distinct logical CSS names and generates
edge parse wrappers. These are not CSSOM aliases for physical edges:
`top` and `inset-block-start`, for example, retain separate declarations, order
and values when edited. `parse/properties/logical_spacing.c` shares one atomic
pair parser between all six spacing/offset axis shorthands, delegating values
to the generated longhand parsers. The former separate inset pair parsers are
removed. Allocation failure rolls back the entire pair and token cursor.
Block longhands emit top/bottom bytecode. Inline longhands mark left/right
bytecode with a logical flag, preserving the ordinary operand format and native
value parsers. Variable substitution reaches the same parser and cascade.
All shorthand members must now have registered longhands; the generator's
temporary exception for inline pairs has been removed.

Libcss selects reusable partial styles before parent composition. An inherited
direction is not yet known at that point. `select/logical.c` retains LTR and RTL
cascade results for only the six affected physical edges, using the native
cascade/defaulting functions and origin snapshots for both. Composition chooses
the mapping after resolving direction; layout still consumes ordinary physical
slots. A root is resolved immediately. No selector matching is repeated.
The generated computed style gains one pointer; only affected partial styles
allocate the alternatives (104 bytes on the current 64-bit build). Fully composed
styles have no alternative allocation. Selection temporarily carries 1,200 bytes
of alternative priority states across the supported pseudo-element slots.
Style hashing, equality, cloning and destruction include the alternatives and
logical-inherit masks; calc strings have explicit reference ownership.

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

`libcss_var_resolve` additionally covers both inline directions, opposite parent
and child directions, pseudo-elements, direction revert, logical/physical priority,
late variables, partial-style reuse and interning, and calc ownership after the
source sheet is destroyed. `qt_logical_inline` compares nine geometry/pixel cases
with equivalent physical-edge CSS, including relative positioning, flex items,
pseudo-elements and LTR → RTL → LTR checked-state refreshes. Selective CSS refresh
and forced-full refresh must produce identical pixels. Its fixture is
`test/logical-inline.html`; expected values come from CSS rules, not browser snapshots.

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

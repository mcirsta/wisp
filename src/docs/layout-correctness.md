# Block layout correctness

## Empty block height constraints — 2026-10-03

Ordinary leaf blocks now apply the existing min/max-height constraints before
advancing the flow cursor, just as ancestor finalization already did. A positive
used minimum also stops margin collapse through an empty block, while preserving
parent/first-child top-margin collapse. Percentage minima use a definite containing
height; an auto-height ancestor does not supply a percentage basis.

`layout_margin_collapse_test` covers constrained auto/explicit heights, minimum
versus maximum precedence, first/last/only children, sibling positions, ancestor
heights, and repeated layout. `qt_empty_min_height` adds real HTML/CSS coverage for
box sizing, percentages, calc(), whitespace, hidden children and nested margins.
The 60px reduction matches Chromium; Tion's empty `.sky-content` advertising rail
now measures 8000px instead of zero.

The block-context traversal is split into static helpers for diagnostics, sizing,
positioning, independent contexts, table placement, ancestor/sibling advancement,
and final height calculation. Its shared cursor and pending margins remain on the
stack; there are no new allocations or tree passes.

Validation: 66 Debug Qt/layout/grid tests passed, with the established
Fontconfig-only LSan suppression needed for five font captures; five focused
Release checks also passed. All four desktop/mobile Tion/Bihon captures and
inspected box geometries are identical before and after the helper extraction.
The Chromium fixture matches 52 of 53 inspected boxes; the one remaining
coordinate difference is documented below.

## Positioned image sizes — 2026-10-03

Loaded images now resolve intrinsic/CSS-ratio dimensions and min/max-height
before the absolute-positioning equations run, as required by
[CSS replaced-element positioning](https://www.w3.org/TR/CSS22/visudet.html#abs-replaced-height).
Opposing insets position the resolved image instead of stretching its auto size.
Auto margins and bottom anchoring use the constrained size; percentage limits
use the containing block's padding box. Ordinary non-replaced stretching is
unchanged. Embedded HTML and unloaded/broken-image fallback are not redesigned.

The shared image-sizing helper also preserves a resolved width when clamping
an auto height. `object-fit` no longer decides whether a height constraint
changes that width; proportional constraints for two auto dimensions retain
their existing separate path.

`qt_absolute_images` checks 40 raster/SVG and normal-flow control cases, before
and after two resizes: natural and CSS ratios, auto/set dimensions and insets,
auto margins, min/max precedence, padding, border-box sizing and fixed position.
All 40 cases match Chromium exactly. All 27 selected Debug regressions and five
focused Release checks pass without sanitizer suppression.

Bihon's `probe-img-62` now measures 111×99 instead of 111×221 (desktop) or 111×156
(mobile). Chromium measures 111×100; the remaining pixel comes from the existing
fixed-point ratio/whole-pixel rounding, also present in normal-flow controls.
Saved-page inspection changes are confined to image boxes: 14 on Tion desktop,
42 on Bihon desktop, one on Bihon mobile, and none on Tion mobile. Apart from the
main Bihon correction, these are 55 desktop images changing from 202×102 to
202×101: their stored ratio is 1.98047 rather than 1.98, and integer truncation
now becomes visible instead of being masked by stretching to the parent height.
Chromium uses about 202.48×102.25. Keep this existing precision limitation as a
separate follow-up rather than compensating in positioned-image layout. Other
inspected geometry and the initial viewport screenshots are unchanged.

Artifacts: `out/render-captures/absolute-images-2026-10-03/`.

## Margin lookahead receiving box — 2026-10-03

Margin lookahead previously reported whether it had crossed any parent/child
boundary. Layout then applied the accumulated margin to the current box, even
when that boundary belonged to a later sibling. An empty wrapper could receive
that later margin, and an empty inline container inside it could turn the offset
into a false auto height.

Lookahead now records which top edge should receive the margin, separately from
where the adjoining-margin chain ends. Passing an entirely empty sibling moves
that receiving edge forward; descending through a parent's adjoining children
keeps the parent's top edge. The normal traversal applies the margin when it
reaches that box. This adds one pointer to the stack state, with no allocation,
extra tree pass or feature switch.

Tion's desktop `probe-div-10` and `probe-div-11` now have zero height instead of
150px. Its rail starts at y=165 instead of y=315, matching Chromium, while keeping
the 8000px minimum. The same correction removes false height from trailing empty
descendants: Tion's `probe-div-485/486` shrink from 80px to 54px (Chromium 54.40625),
and Bihon's `probe-div-92/93` from 47px to 27px (Chromium 27.203125). Their following
content moves accordingly. Initial viewport screenshots are unchanged at both
1280×900 and 390×900; these affected regions lie outside the captured viewport.

The C margin suite adds 24 cases, each laid out twice: positive/negative margins,
empty block/inline descendants, later nested parents, empty first children, and
empty tails inside nonempty ancestors. `qt_empty_block_margins` also checks these
relationships in real HTML before and after two resizes. The new capture test
fails against the previous Release binary. All 42 new inspected fixture boxes
match Chromium exactly; two older empty-box coordinate discrepancies remain
as described below.

Validation: all 67 selected Debug layout/grid/Qt tests and five focused Release
checks passed. Five Debug font captures use the established Fontconfig-only
LSan suppression; the other 62 run without suppression. The expanded 24-case C
regression also passes in both builds.

Artifacts: `out/render-captures/empty-margin-lookahead-2026-10-03/`.

## Follow-up: positioning a self-collapsing empty block

The `zero` control in `test/empty-min-height.html` still exposes a pre-existing
placement issue. With zero minimum and margins of 15px above / 25px below, Wisp
places the empty box at its parent's content top; Chromium places it 15px lower.
Both engines put the following marker 25px below the content top and give the
wrapper the same height. This is independent of the positive-minimum fix.

The related `mixed-second` and `negative-second` controls in
`test/empty-block-margins.html` also need their own positions to reflect margins
encountered before them, separately from the final collapsed flow spacing.
Removing the premature application of future margins above exposes the same
limitation in additional empty advertising wrappers: their coordinates can move
farther from Chromium even though the enclosing heights and following content's
margin spacing are corrected. Do not claim full empty-box positioning is fixed.

Follow [CSS adjoining-margin positioning](https://www.w3.org/TR/CSS22/box.html#collapsing-margins):
a self-collapsing box whose margins join its parent's top is positioned at that
top; otherwise its top is where it would be with a nonzero bottom border. These
positions must not affect the positions of other boxes. Keep the running margins
before each empty box distinct from lookahead's complete margin chain, and cover
nested chains, negative margins and positioned descendants. Moving the shared
flow cursor to fix these coordinates would double-count sibling spacing; this
needs a separate bookkeeping change rather than another placement exception.

Original reductions: `out/render-captures/empty-min-height-2026-10-03/`.

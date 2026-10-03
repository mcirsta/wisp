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

## Follow-up: positioning a self-collapsing empty block

The `zero` control in `test/empty-min-height.html` still exposes a pre-existing
placement issue. With zero minimum and margins of 15px above / 25px below, Wisp
places the empty box at its parent's content top; Chromium places it 15px lower.
Both engines put the following marker 25px below the content top and give the
wrapper the same height. This is independent of the positive-minimum fix.

Investigate how margin lookahead assigns positions to intermediate self-collapsing
boxes, following [CSS adjoining-margin positioning](https://www.w3.org/TR/CSS22/box.html#collapsing-margins).
Moving the flow cursor again would double-count the already-correct sibling gap;
cover nested chains and negative margins before changing that bookkeeping. The
current regression checks this control's collapsed spacing, not its incorrect
empty-box coordinate.

The saved Tion desktop rail also retains a smaller offset: `probe-div-10` and
`probe-div-11` have 150px auto heights in Wisp versus zero in Chromium, both before
and after this fix. Consequently the rail begins at y=315 instead of y=165 even
though its minimum and content heights are now correct. Reduce this nested empty
wrapper/margin case separately; it is not evidence that the 8000px minimum is
still ignored. The relation to the zero-height placement issue is not yet proven.

Reduction captures, Chromium references, saved-site comparisons and validation
logs: `out/render-captures/empty-min-height-2026-10-03/`.

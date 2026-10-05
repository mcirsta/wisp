# Correctness backlog

Only unresolved work belongs here. Completed changes and validation history are
in Git; the regression fixtures remain in the test suite.

## Grid column track sizing

Confirmed against CSS Grid 1's [track sizing definitions](https://www.w3.org/TR/css-grid-1/#track-sizing),
[fractional units](https://www.w3.org/TR/css-grid-1/#fr-unit), and
[flexible track algorithm](https://www.w3.org/TR/css-grid-1/#algo-flex-tracks).
These are specification requirements, not Chromium-derived expectations.
In `test/grid-column-sizing.html`, each grid is 300px wide with no gaps or item
edges. The intrinsic cases contain a fixed 60px child; the other items are empty.

| Columns | Required widths | Wisp widths (2026-10-05) |
| --- | --- | --- |
| `2fr 1fr` (passing control) | 200 / 100 | 200 / 100 |
| `0.5fr 1fr` | 100 / 200 | 0 / 300 |
| `0.25fr 0.25fr` | 75 / 75 | 0 / 0 |
| `minmax(0px, 2fr) 1fr` | 200 / 100 | 0 / 150 |
| `minmax(200px, 1fr) 1fr` | 200 / 100 | 0 / 150 |
| `minmax(200px, 100px) 1fr` | 200 / 100 | 100 / 200 |
| `min-content 1fr` or `max-content 1fr` | 60 / 240 | 150 / 150 |

The shared cause is the approximate column algorithm in
`layout_grid_compute_tracks()`: it truncates flex factors to integers, skips
dynamic `minmax()` tracks during distribution, ignores their minima, and treats
intrinsic tracks as `1fr`. These branches date to the January grid implementation
(`f40f636a` / `08ad3c58`); the recent row-sizing work did not replace them.
Treat this as one column-sizing task. Preserve fractional factors, establish
track base sizes and growth limits from their definitions and item contributions,
then distribute space subject to those limits. Adding a special case for each
fixture would leave the underlying sizing model incorrect.

Debug and Release captures completed successfully and reproduced all seven
failures; the integer-weight control passed in both. Evidence:
`out/render-captures/standards-audit-2026-10-05/`.

## Find in Page coverage

Cross-node phrases remain unimplemented.
Wrapped fragments of one DOM text node and generated `::before`/`::after` text
are supported.

Generated-text discovery inside skipped subtrees conservatively excludes special
elements which may use a replacement renderer. Resource-dependent fallback needs
a shared decision with element conversion rather than a second implementation
in search. Confirmed reproducer: an `object` with no `data` or `classid`, with
`::before { content:'fallback-token' }`, inside a hidden-until-found ancestor.
Find misses that token before reveal, but finds it after fragment navigation
reveals the ancestor. Images without a usable source and other fallback-capable
elements need the same review. Ordinary generated text is unaffected.

Evidence: `out/render-captures/generated-search-2026-10-03/fallback/steps.json`.

The supported behavior and repeatable checks are in
[hidden-until-found.md](hidden-until-found.md).

## Details compatibility beyond native reading/search

Basic collapse, disclosure markers, mouse activation and search/fragment reveal are implemented;
see [details.md](details.md). Remaining work:

- Keyboard focus/activation through shared document focus handling; the
  investigation and implementation sequence are in [details.md](details.md#keyboard-focus-follow-up).
- Exclusive `name` groups, including parser and DOM attribute changes.
- The `HTMLDetailsElement` scripting interface and queued/coalesced `toggle`
  events, rather than firing an incorrect synchronous substitute.
- Author styling of `::details-content`; the current contents block is anonymous.

## HTML direction and bidi inline ordering

The disclosure-marker comparison exposed two existing direction limitations:
HTML `dir="rtl"` does not select CSS `direction:rtl`, and explicit CSS direction
aligns the line but does not reorder its inline boxes. The disclosure glyph
itself follows CSS direction, but remains before the label in the left-to-right
box sequence. This needs a shared direction/bidi implementation, not a
summary-specific swap. Vertical writing-mode layout is also outside the current
horizontal marker implementation.

Reproducer: `test/disclosure-markers.html`, `#rtl` (explicit CSS direction).
Removing its CSS rule isolates the HTML attribute issue. Initial captures are in
`out/render-captures/disclosure-markers-2026-10-03/chromium/`.

## Remaining marker compatibility

Marker font and colour styles now use the selected `::marker` style for both
inside and outside markers. Custom `::marker { content:... }` remains unused;
generated contents need shared counter/content handling for summaries and lists.

A very narrow inside counter also differs from Chromium: `#spaced-marker` in
`test/disclosure-markers.html` uses a negative Korean counter in an 8px item.
Wisp keeps the generated marker together (48px item height); Chromium wraps it
(144px). CSS Lists specifies a UA `white-space:pre` default for markers and notes
open whitespace issues; Chromium's inspected marker instead has
`white-space-collapse:collapse` and `text-wrap-mode:wrap`. Resolve this with marker
styling/whitespace support, rather than a language-specific exception.

Evidence: `out/render-captures/disclosure-markers-2026-10-03/chromium-extended/`
and `marker-style-reference/inspection.json` in the same artifact directory.

## Collapsed table borders

Collapsed borders are not shared correctly between row/cell edges. In the
`collapsed` case, 2px cell borders give a 44px cell at y=0 in Wisp versus a
42px cell at y=1 in Chromium. Text, marker and overall table geometry now agree;
the remaining difference belongs to collapsed-border geometry and painting.
CSS 2.2 [section 17.6.2](https://www.w3.org/TR/CSS22/tables.html#collapsing-borders)
places collapsed borders on shared grid lines, with half-width edge accounting;
this is not an implementation-defined browser preference.

This reproduces before the baseline changes. Initial evidence:
`out/render-captures/table-baseline-2026-10-04/before/` and its adjacent
`fixture.html`. The permanent `test/table-baseline.html` checks collapsed-table
content alignment without accepting the incorrect cell edges as a baseline.

## Float and inline content paint order

Non-positioned floats paint over overlapping inline-blocks. In
`test/float-inline-paint-order.html`, both brown peers should cover the upper
10px of the orange float; Wisp paints orange over them. The positive-height case
uses a negative inline margin to reproduce the overlap independently of
zero-height float exclusion. Geometry agrees with Chromium in both cases.
CSS 2.2 [Appendix E.2](https://www.w3.org/TR/CSS22/zindex.html#painting-order)
paints ordinary floats before inline content; the overlap therefore violates the
specified painting order independently of the browser comparison.

The existing `html_redraw_box_children()` paints all ordinary descendants,
including their inline content, before its float list. This needs separate
block-background, float and inline painting phases within the correct stacking
scope; simply moving the float loop before all children would put floats behind
block backgrounds. Preserve clipping, positioned descendants and hit-test order
while fixing the shared traversal.

The painter is unchanged from `6b6cdbea`; the positive-height case also uses
the same layout branch as that revision. Evidence:
`out/render-captures/zero-floats-2026-10-05/paint-overlap/`. The zero-height fix
exposes the existing paint-order problem by removing the incorrect horizontal gap.

All artifact paths above are under `out/render-captures/`.

# Correctness backlog

Only unresolved work belongs here. Completed changes and validation history are
in Git; the regression fixtures remain in the test suite.

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

This reproduces before the baseline changes. Initial evidence:
`out/render-captures/table-baseline-2026-10-04/before/` and its adjacent
`fixture.html`. The permanent `test/table-baseline.html` checks collapsed-table
content alignment without accepting the incorrect cell edges as a baseline.

## Grid container heights

`layout_grid_set_dimensions()` overwrites a resolved CSS height with the sum of
row heights unless the grid was stretched or has an aspect ratio. An empty grid
with `height:10px` becomes zero-height; a border-box grid with overflowing children
can grow beyond its specified height. Review definite/auto height handling and
height limits across block, inline, floated and nested grid entry points.

This reproduces in the unchanged `5ae1c124` build, before the intrinsic-width
fix. Evidence: `out/render-captures/grid-intrinsic-2026-10-04/fixture.html` and
`before/`, especially `#empty-fixed-grid` and `#border-maximum-grid`. The permanent
width fixture uses auto heights so it does not accept the incorrect heights.

## Zero-height float exclusion

A zero-height floated grid still moves the following inline-block horizontally
on the same line: Wisp places it at x=40 relative to the case; Chromium places it
at x=0. The grid itself has zero height in both engines. Check same-line float
placement and exclusion accounting, including ordinary block floats; do not
special-case grids.

This also reproduces in the unchanged `5ae1c124` build. Evidence:
`out/render-captures/grid-intrinsic-2026-10-04/zero-float.html`, `#float-grid-peer`,
`zero-float-before/` and `permanent/reference/` in the same directory. The permanent
width regression uses a nonempty float to isolate intrinsic sizing.

All artifact paths above are under `out/render-captures/`.

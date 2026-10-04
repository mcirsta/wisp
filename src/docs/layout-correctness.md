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

The grid baseline comparison also confirmed an existing alignment-keyword gap:
`align-items: start` / `end` are ignored; `flex-start` / `flex-end` work. These
need distinct logical alignment values because aliasing them changes the meaning
in flex containers with `wrap-reverse`. Reproducer: `end` and `late-end` in
`out/render-captures/grid-baseline-2026-10-04/before/`; the permanent grid baseline
fixture uses the supported flex-prefixed spelling to isolate layout behavior.

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

## Table formatting beyond baseline alignment

Two existing limitations were confirmed by the table baseline comparison:

- `display:inline-table` still uses the block-table representation. It starts a
  new block instead of joining the surrounding line. Supporting it needs box
  construction and inline layout integration using the table's exported baseline.
  The `inline` comparison case is 100px tall in Wisp versus 60px in Chromium.
- Collapsed borders are not shared correctly between row/cell edges. In the
  `collapsed` case, 2px cell borders give a 44px cell at y=0 in Wisp versus a
  42px cell at y=1 in Chromium. Text, marker and overall table geometry now agree;
  the remaining difference belongs to collapsed-border geometry and painting.

Both limitations reproduce before the baseline changes. Initial evidence:
`out/render-captures/table-baseline-2026-10-04/before/` and its adjacent
`fixture.html`. The permanent `test/table-baseline.html` checks collapsed-table
content alignment without accepting the incorrect cell edges as a baseline.

All artifact paths above are under `out/render-captures/`.

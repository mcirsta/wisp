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

Outside markers now align with the first in-flow line through ordinary block
children. Remaining cases need distinct layout decisions:

- Items containing only block children with no line baseline still omit the
  fallback marker height. In `empty-markers-2026-10-03/probe.html`, a 40px marker
  with 36px line-height and an empty block child gives Wisp a zero-height item,
  while Chromium reserves 36px. A 20px-tall block child similarly gives 20px
  versus 36px. This block-child fallback has different sizing and child-margin
  rules from the implemented marker-only inline line.
- First baselines from flex/grid/table children are not propagated to the list
  item. A flex child in `outside-marker-2026-10-03/probe.html` gives a 24px item
  versus Chromium's 30px.
  This needs the relevant formatting context's baseline rules, not descent into
  its first DOM child.

Evidence: `out/render-captures/outside-marker-2026-10-03/probe.html`, `probe-fixed/`,
and `complete/`.

The block-child fallback comparisons are in
`out/render-captures/empty-markers-2026-10-03/after/`.

A very narrow inside counter also differs from Chromium: `#spaced-marker` in
`test/disclosure-markers.html` uses a negative Korean counter in an 8px item.
Wisp keeps the generated marker together (48px item height); Chromium wraps it
(144px). CSS Lists specifies a UA `white-space:pre` default for markers and notes
open whitespace issues; Chromium's inspected marker instead has
`white-space-collapse:collapse` and `text-wrap-mode:wrap`. Resolve this with marker
styling/whitespace support, rather than a language-specific exception.

Evidence: `out/render-captures/disclosure-markers-2026-10-03/chromium-extended/`
and `marker-style-reference/inspection.json` in the same artifact directory.

## Remaining parent/child margin cases

Two adjacent cases found while checking interleaved floats remain. Both are
present in the capture made at `8ae6edbe`, before the lookahead fix; these are
existing limitations, not regressions introduced by that fix.

- **Clearance when a child's large top margin would move its parent.** With
  parent `margin:13px 0 17px`, a 40×48 left float, and an empty child with
  `clear:left;margin:100px 0 29px`, Wisp places the child at y=113 and leaves
  the parent zero-height. Chromium places the child at the float's bottom,
  y=61, and gives the parent 48px height. Lookahead currently commits the
  parent's top before placing the float, so the later clearance decision
  tests the child's margin against that already committed position. Resolving
  this needs the hypothetical parent/child collapsed top as well as the actual
  float position; simply skipping the float or clamping the child would be
  incorrect. Reproducer: `#clear-large-case`.
- **Fixed-height parents stop first-child top-margin lookahead.** In the same
  structure with `height:40px` on the parent and an uncleared empty child with
  `margin:23px 0 29px`, Wisp puts the parent at y=13, while Chromium uses y=29.
  `MAKE_HEIGHT` currently stops the scan before the child. Height separates the
  parent's bottom margin but does not by itself separate its top from its
  first child's top. Address that distinction separately, including controls
  without floats and with nonempty children. Reproducer: `#parent-height-case`.

Coordinates are relative to the enclosing flow-root's content edge. Evidence:
`out/render-captures/interleaved-floats-2026-10-04/probe.html`, `before/` and
`after2/`. The other exploratory case with default-font text has a 1px normal
line-height difference; the regression fixture uses an explicit line-height to
isolate margin geometry.

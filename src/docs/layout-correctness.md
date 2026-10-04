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

- **Restore parent/child collapse when a deferred clearance decision finds no
  clearance.** With parent `margin:13px 0 17px`, a zero-height left float and
  an empty child with `clear:left;margin:100px 0 29px`, Wisp leaves the parent
  and float at y=13 and puts the child and following sibling at y=113. Firefox
  moves the whole adjoining chain to y=100. Chromium instead leaves the parent,
  float and child at y=13 and puts the following sibling at y=30, treating this
  float-edge case as clearance. The existing clearance tests follow Firefox's
  ordinary-collapse behavior when no clearance is needed. Supporting it here
  requires resolving the provisional ancestor top again after float sizing,
  with corresponding float placement; do not introduce clearance solely to
  avoid that work. Reproducer: `#zero-float-case` in
  `parent-clearance-2026-10-04/fixture.html`. `before/` and `second-debug/`
  confirm Wisp's geometry is unchanged by the nonzero-float clearance fix;
  `firefox.json` provides the independent reference.

Coordinates are relative to the enclosing flow-root's content edge. All
artifact paths above are under `out/render-captures/`.

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

## Remaining float placement cases

Ordinary float-only blocks now preserve adjoining margins, and place floats
using their containing block's own top rather than a following sibling's margin.
`qt_float_only_margins` covers 24 cases; the margin-position unit tests also run
with floats, including nested positive/negative margin chains.

Two separate pre-existing cases remain:

- A float before a later empty block child still interrupts lookahead too early.
  With a parent margin of `13px 0 17px`, a 40×48 float, then an empty child with
  `margin:23px 0 29px`, Wisp positions the parent at y=13 and its following 8px
  sibling at y=42; Chromium puts both at y=29. The parent's height is zero in
  both. This is `#before-empty` in `float-only-margins-2026-10-04/edge.html`;
  `#between-empty` adds another float after the empty child. Lookahead commits
  the enclosing top before reaching the later child margins. Resolving this
  needs coordination between pending ancestor margins and float placement.
- A float whose containing block starts above the shared formatting context's
  top is clamped to y=0. In `float-only-margins-2026-10-04/probe.html`,
  `#negative-empty` has margins `-13px 0 -17px` and correctly starts at y=-13,
  but its 48px float stays at y=0 rather than following it. Float placement's
  zero-initialized `clear_level` and `cached_place_below_level` are implicated;
  negative margins that leave the float at positive coordinates already pass.

Coordinates above are relative to the enclosing flow-root's content edge.
Evidence is in `out/render-captures/float-only-margins-2026-10-04/`: `before/`,
`extended/`, and `edge-before/` versus `edge/`. The latter confirms that the
interleaved-float cases are unchanged from the prior implementation.

## Empty inline fragments at line breaks

An undecorated span's opening or closing fragment can still reserve a blank line
when its text cannot fit. In `float-boundaries-2026-10-04/probe.html`:

- `#text-zero-target`: a 180×0 float followed by a span containing a 48px word
  in a 200px container puts the text at y=20 instead of Chromium's y=0.
- `#text-oversize-target`: after a 180×40 float, a span containing an unbreakable
  240px word puts the text correctly at y=40, but the closing inline fragment
  adds another 20px below it. The parent becomes 80px tall instead of 60px.

Both examples use a bundled font and `line-height:20px`. Plain text without the
span passes. The non-content fragments need consistent zero-height treatment
in the inline line builder, separately from its float-retry distance.
This predates the float-bottom boundary fix; that fix removes the extra pixel
and retries at the first usable float edge but does not change fragment sizing.

Evidence: `out/render-captures/float-boundaries-2026-10-04/before/` and `after/`.

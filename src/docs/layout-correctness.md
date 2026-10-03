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

## Marker content, outside baselines and narrow counters

Marker font and colour styles now use the selected `::marker` style for both
inside and outside markers. Custom `::marker { content:... }` remains unused;
generated contents need shared counter/content handling for summaries and lists.

Outside markers still use independent vertical layout: `layout_lists` sets text
markers to `y=0`, and their ascent/descent does not contribute to the first line.
In `test/marker-styling.html`, `#outside` has a 24px marker beside 16px text with
24px line-height: Wisp's item is 24px tall, Chromium's is 27px, with the text
baseline shifted to accommodate the larger marker. `#small` and the padded
`#decorated` also expose marker/first-line baseline differences. Fix this through
shared first-line metrics, accounting for first block children, wrapping/floats
and retained inline-layout reuse, rather than a font-size-dependent y correction
in `layout_lists`. Inside markers already participate in normal line metrics.

Evidence: `out/render-captures/marker-styling-2026-10-03/initial/`.

A very narrow inside counter also differs from Chromium: `#spaced-marker` in
`test/disclosure-markers.html` uses a negative Korean counter in an 8px item.
Wisp keeps the generated marker together (48px item height); Chromium wraps it
(144px). CSS Lists specifies a UA `white-space:pre` default for markers and notes
open whitespace issues; Chromium's inspected marker instead has
`white-space-collapse:collapse` and `text-wrap-mode:wrap`. Resolve this with marker
styling/whitespace support, rather than a language-specific exception.

Evidence: `out/render-captures/disclosure-markers-2026-10-03/chromium-extended/`
and `marker-style-reference/inspection.json` in the same artifact directory.

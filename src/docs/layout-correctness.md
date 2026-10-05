# Correctness backlog

Only unresolved work belongs here. Completed changes and validation history are
in Git; the regression fixtures remain in the test suite.

## Find in Page coverage

Cross-node phrases remain unimplemented.

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

Remaining work beyond the [native details implementation](details.md):

- Keyboard focus/activation through shared document focus handling; the
  investigation and implementation sequence are in [details.md](details.md#keyboard-focus-follow-up).
- The `HTMLDetailsElement` scripting interface and queued/coalesced `toggle`
  events, rather than firing an incorrect synchronous substitute.
- Author styling of `::details-content`; the current contents block is anonymous.

## Automatic HTML direction and bidi inline ordering

HTML directionality resolution (`dir="auto"`, `bdi`, and `:dir()` matching) is
still unimplemented. This also leaves directionality-based UA defaults for
invalid/empty `dir` attributes incomplete when HTML ancestry and inherited CSS
direction differ. The follow-up must use the HTML directionality algorithms,
not infer semantic direction from the computed CSS `direction` property.

CSS direction aligns the line but does not reorder its inline boxes. The
disclosure glyph follows CSS direction, but remains before the label in the
left-to-right box sequence. This needs shared bidi isolation/reordering, not a
summary-specific swap. Vertical writing-mode layout is also outside the current
horizontal marker implementation.

Reproducer: `test/disclosure-markers.html`, `#rtl`. Initial captures are in
`out/render-captures/disclosure-markers-2026-10-03/chromium/`.

## Custom marker content

Custom `::marker { content:... }` remains unused. Generated contents need shared
counter/content handling for summaries and lists.

## Table column styling

`display:table-column` and `display:table-column-group` are currently discarded
by box construction. Their styles therefore cannot participate in column sizing,
backgrounds or collapsed-border conflicts. This predates the shared-border fix;
table, row-group, row and cell borders now share resolved grid segments.
Supporting column styles needs retained column/column-group data from construction,
then integration with the existing table grid (CSS 2.2
[17.3](https://www.w3.org/TR/CSS22/tables.html#columns)).

All artifact paths above are under `out/render-captures/`.

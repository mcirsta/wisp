# Correctness backlog

Only unresolved work belongs here. Completed changes and validation history are
in Git; the regression fixtures remain in the test suite.

## Find in Page coverage

Cross-node phrases remain unimplemented.

Remaining resource-dependent fallback coverage:

- Resource discovery for objects that have never been constructed in skipped subtrees.
  Search currently reads known request state without starting downloads.
- Object `error`/`load` events need the specified task lifecycle. Late bitmap
  conversion failures also need to reach the representation decision when an
  image decoder has already reported successful content completion.
- Images without a usable source and other fallback-capable elements still need
  review; skipped image owners conservatively exclude generated text.

Object expectations follow the [HTML object representation algorithm](https://html.spec.whatwg.org/multipage/iframe-embed-object.html#the-object-element),
including its resource-availability, failed-load and image-handler steps.

The supported behavior and repeatable checks are in
[hidden-until-found.md](hidden-until-found.md).

## Details compatibility beyond native reading/search

Remaining work beyond the [native details implementation](details.md):

- Keyboard focus/activation through shared document focus handling; the
  investigation and implementation sequence are in [details.md](details.md#keyboard-focus-follow-up).
- The `HTMLDetailsElement` scripting interface and queued/coalesced `toggle`
  events, rather than firing an incorrect synchronous substitute.
- Author styling of `::details-content`; the current contents block is anonymous.

## Direction changes during editing and bidi inline ordering

Native input/textarea edits update the DOM value and mark general style
invalidation, but do not request immediate restyling. If an edit changes automatic
directionality, `:dir()` and UA direction defaults update on the next restyle.
The follow-up needs prompt selector invalidation while preserving the active
editor's focus, caret and selection; rebuilding the page on each keystroke is
not an acceptable substitute. The semantic resolver already reads current form
values and supports reselection after DOM text, attribute and value changes.

CSS direction aligns the line but does not reorder its inline boxes. The
disclosure glyph follows CSS direction, but remains before the label in the
left-to-right box sequence. This needs shared bidi isolation/reordering, not a
summary-specific swap. `bdi` direction resolution alone does not implement its
Unicode isolation. Also pending are `unicode-bidi:plaintext` per-paragraph
layout and separate directionality for text from directionality-capable HTML
attributes such as placeholders. These follow the
[HTML directionality algorithms](https://html.spec.whatwg.org/multipage/dom.html#the-dir-attribute)
and [bidirectional rendering rules](https://html.spec.whatwg.org/multipage/rendering.html#bidirectional-text).
Shadow trees and vertical writing-mode layout are not currently implemented.

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

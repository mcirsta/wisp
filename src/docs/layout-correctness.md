# Correctness backlog

Only unresolved work belongs here. Completed changes and validation history are
in Git; the regression fixtures remain in the test suite.

## Continuing discovery in later batches

Keep the existing workflow: investigate a page or test, isolate a reproducer,
check the relevant specification, fix the root cause and add regression coverage.
At the start of each batch, define the behavior to fix and its completion checks.
Record adjacent discoveries here for later without automatically extending that
batch. Include dependencies necessary to make its fix correct, and explain any
resulting scope increase.

For new findings, distinguish a confirmed defect from missing feature coverage or
an investigation. Record the reproducer and standards basis when established;
otherwise retain the uncertainty. Label an issue as a regression only when there
is evidence linking it to our changes. Browser comparisons can supply evidence,
but do not replace the standards basis for a fix. Resuming discovery does not
require exhausting every item in this document.

## Find in Page coverage

Cross-node phrases remain unimplemented.

Remaining DOM-based search coverage includes widget-owned text (input, textarea
and select), text rendered through inline SVG, and image alternatives that remain
replaced (while pending or sized in quirks mode). These need matches bound to
their actual painting/selection paths; traversing DOM children alone cannot
provide that correspondence. These are future coverage items, not an assertion
that HTML mandates a particular matching algorithm for every text source.

The supported behavior and repeatable checks are in
[hidden-until-found.md](hidden-until-found.md).

## Resource lifecycle follow-up

Object `error`/`load` events need the specified task lifecycle.

Object expectations follow the [HTML object representation algorithm](https://html.spec.whatwg.org/multipage/iframe-embed-object.html#the-object-element),
including its resource-availability, failed-load and image-handler steps.

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

## Wrapped flex line content alignment

Confirmed on 2026-10-12 while checking the `place-content` parser. The unchanged
`layout_flex__place_lines()` always distributes positive cross-axis free space
by stretching lines; it never reads `align-content`. Explicit longhands reproduce
the failure too, so this is not a regression from the shorthand/CSSOM changes.

Minimal reproducer:

```html
<div style="display:flex;flex-wrap:wrap;width:100px;height:100px;
            align-items:flex-start;align-content:center;justify-content:center">
  <div style="flex:0 0 60px;height:20px"></div>
  <div style="flex:0 0 60px;height:20px"></div>
</div>
```

Both items start at x=20. Their expected y positions are 30 and 50; Wisp places
them at 0 and 50. The two lines occupy 40px, so `center` must split the remaining
60px before/after them, per [Flexbox section 8.4](https://www.w3.org/TR/css-flexbox-1/#align-content-property).
The fix needs actual content-distribution modes in line placement, preserving
nowrap behavior, gaps, reverse axes, auto cross sizes and overflow fallbacks.
Keep this as a separate layout batch; the CSSOM parser regression uses grid
layout to test the shorthand independently of this missing flex behavior.


## Linear-gradient angle rendering

Confirmed by source inspection on 2026-10-12 during CSSOM readback work. The
existing `parse_linear_gradient()` accepts degree angles but quantizes them to
one of four cardinal directions, and `css_linear_gradient` stores only that
enum. Thus `background-image:linear-gradient(45deg,red,blue)` paints a horizontal
gradient instead of the specified diagonal. This predates the readback changes;
the new specified-value serializer retains the authored angle.

[CSS Images 3 section 3.1](https://drafts.csswg.org/css-images-3/#linear-gradients)
requires the actual angle to determine the gradient line and its endpoints.
A proper fix needs an angle-capable parsed/computed representation and matching
native/fallback painting geometry; preserving the angle only in CSSOM does not
fix rendering. Keep this as a separate rendering batch, including corner
keywords, negative/full-turn angles, non-square boxes and length-based stops.

## CSS transform unit conversion

Confirmed by source inspection on 2026-10-12 while adding specified transform
readback. The existing painter uses translation values directly as pixels (apart
from percentages), and treats every rotation as degrees. The cascade retains
the original units; computed-style processing does not convert these transform
arguments. Thus `font-size:20px; transform:translateX(2em)` moves by 2px instead
of 40px, and `rotate(.25turn)` rotates by .25 degrees instead of 90 degrees.
These paths predate the CSSOM work; specified readback correctly preserves units.

There is also premature fixed-point saturation/rounding for percentage scales:
the parser converts the percentage to fixed-point before dividing by 100. For
example, `scale(10000000%)` saturates the percentage before division even though
its equivalent factor of 100000 fits the fixed-point representation.

Follow [CSS Transforms 1 section 7](https://drafts.csswg.org/css-transforms-1/#transform-functions)
and [Transforms 2 section 12](https://drafts.csswg.org/css-transforms-2/#transform-functions).
Resolve lengths with the element's style/unit context and rotations with their
angle unit at the appropriate rendering boundary; normalize percentage scales
before fixed-point quantization. Keep specified values independent of those
conversions. Cover relative/absolute lengths, all supported angle units, negative
and large scales, and equivalent number/percentage scale spellings in a separate
rendering batch. The explicit-zero second scale argument is already fixed and
covered by `test/css-transforms.html`.

## SVG paint transfer and opacity coverage

Confirmed by a Qt capture and source inspection on 2026-10-13 during native
CSSOM work. The following pre-existing rendering paths still discard data;
specified-value readback preserves it. These are separate rendering work, not
new prerequisites for exposing live inline styles.

- External CSS `svg { fill:rgba(255,0,0,0) }` paints a red rectangle. The inline
  SVG bridge's `css_color_to_hex()` drops the alpha byte. It must transfer color
  alpha without conflating it with inherited fill/stroke opacity.
- `svg { stroke-width:1in; fill:none; stroke:black }` leaves a 1px stroke. The
  bridge drops the computed length unit and treats numeric 1 in every unit as
  the default 1px. A 40px rectangle at (60,60) in a 160px viewport must have a
  96px stroke; the sample at (45,80) is white instead of black. Percentage widths
  likewise need their SVG viewport reference, not conversion to bare numbers.
- `svg { fill-opacity:0 }` still paints `<text x="10" y="40"
  font-size="32">MMMM</text>` and a rectangle filled with `url(#gradient)`.
  The text and native-gradient branches do not apply per-paint opacity. Text's
  `none`/currentColor handling and frontend text alpha need reviewing together;
  solid paths and dash rectangles now preserve opacity through their color.

Follow [SVG 2 painting](https://www.w3.org/TR/SVG2/painting.html), including
color alpha, fill/stroke opacity and stroke-width units. Resolve units at the
appropriate CSS/SVG boundary, preserve independently inherited values, and test
solid paths, text, gradients and dash conversion rather than applying a
whole-image opacity workaround. Older frontends also need an alpha-support
audit when they are next tested; the obsolete plot-style opacity fields were
only interpreted by Qt.

## Display color gamut mapping

Existing feature limitation confirmed in the color conversion code: out-of-gamut
colors such as `lab(50 100 100)` reach per-channel sRGB clipping. CSS Color 4
[display gamut mapping](https://drafts.csswg.org/css-color-4/#css-gamut-mapping)
requires mapping that preserves perceptual appearance. The recent Lab parser
cleanup corrects syntax, precision and white-point conversion; it does not
implement general gamut mapping. Specified-value readback preserves the original
coordinates and is independent of this display limitation. Treat gamut mapping
as renderer work, not another prerequisite for live inline-style editing.
The real `color(srgb ...)` parser added on 2026-10-13 uses the same painting
boundary: out-of-range coordinates such as `color(srgb 1.2 .4 -.1)` are preserved
in readback but still clipped for display. In-gamut sRGB painting is covered by
the color capture regression.

## JavaScript-driven HotNews sidebar (deferred)

The live article sidebar headed "Ultima oră" stays on its loading placeholders.
Source inspection on 2026-10-06 confirmed that the initial headline list is empty.
The site's `app2.js` finds `[data-hn2-latest-articles="frontend"]`, fetches
`/wp-json/hn2/v1/dynamic-fragments`, inserts `latest_articles.html` through
`innerHTML`, and changes classes to hide the placeholders and show the list.
The public endpoint returned HTTP 200 with 12 headline items during the check.

This is missing JavaScript/DOM integration, not evidence of blurred text or a
recent layout regression. At the original check, element queries, `innerHTML`
and class operations were stubs. Subsequent JS batches implemented those native
DOM operations; their old absence is no longer the current blocker. The saved
`app2.js` uses `type="module"`, which remains unsupported, and its content-fetching
dependencies still need a site-driven follow-up. Track current capabilities in
the [JS migration notes](../../plans/js_migration_analysis.md). Enabling scripting
alone does not establish that the live sidebar works; no site-specific substitute
has been added.

The saved HotNews homepage under `sites/` already contains headline list items
and `data-hn2-latest-hydrated="1"`. That explains why the saved version can show
the widget without executing its loading script; it does not establish that the
live widget previously worked. Evidence, fetched source and response are in
`out/render-captures/hotnews-sidebar-2026-10-06/`.

## Numeric calc values skipped during cascade

Source inspection on 2026-10-13 confirmed that the existing cascade handlers
for `flex-grow`, `flex-shrink`, `order`, `z-index` and `column-count` consume
calculator operands and return without applying the declaration. Native CSSOM
readback/editing now preserves these accepted expressions, and the shared parser
compiles their arithmetic and property ranges, but this does not make these
older cascade branches evaluate them. This predates the CSSOM work.

A separate rendering batch should evaluate these numeric-only expressions at
computed-value time, preserve cascade priority/inheritance, and apply each
property's integer rounding and range rules. Cover finite/non-finite results,
negative order/z-index, positive column counts and fractional flex factors.
Keep this outside the finite inline-style binding checklist: preserving an
accepted declaration must neither erase it nor invent new rendering support.

## EOF recovery in declaration-list parsing

Confirmed during owned-declaration compilation tests on 2026-10-13: parsing the
inline declaration list `width:var(--missing,calc(1em + 2px` drops the declaration,
while its explicitly closed equivalent is accepted. The ordinary declaration-list
parser reaches `parseVariableDeclaration` with unfinished function nesting, which
it rejects. This path is unchanged by the new compiler. The newer single-property
assignment parser already supplies recovered closing tokens; compiling those
owned tokens passes the new selection regression.

[CSS Syntax's consume-a-function algorithm](https://www.w3.org/TR/css-syntax-3/#consume-function)
returns the function at EOF. Follow up in the shared declaration/stylesheet
component parser, with nested functions/blocks, malformed tokens and chunked input
coverage; do not paper over it in CSSOM serialization or the compiler. Keep this
existing parser recovery gap separate from the finite live-style binding work.

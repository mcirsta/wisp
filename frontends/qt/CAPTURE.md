# Rendering captures

Build the optional Qt developer tool with `-DWISP_QT_CAPTURE=ON` (default OFF).
It reuses the browser's actual Qt widget paint path and existing DOM/box dumps.
All capture code and hooks compile out when the option is OFF. No changes to
the renderer, code generators or threading model are needed.

```sh
cmake -S . -B build -DWISP_QT_CAPTURE=ON
cmake --build build --target wisp-qt
QT_QPA_PLATFORM=offscreen QT_SCALE_FACTOR=1 QT_FONT_DPI=96 \
  build/frontends/qt/wisp-qt \
  --capture-dir /tmp/wisp-haiku --capture-size 1280x900 \
  https://www.haiku-os.org/
```

A local HTML filename also works. The output directory must be new or empty,
so an old successful screenshot cannot be mistaken for a failed new capture.
`--capture-help` lists options. Use `QT_QPA_PLATFORM=xcb` under Xvfb if a platform
needs a display server; capture still includes only the page, without window
decorations, tabs or scrollbars.

Each run writes:

- `page.png`: the content viewport, painted by the normal Qt renderer.
- `dom.txt`: the existing DOM structure dump (elements, IDs/classes and title;
  not serialized HTML).
- `boxes.txt`: the existing layout tree, with box geometry, text and computed
  styles. Coordinates are relative to containing boxes; pointer addresses vary
  between runs. The dump covers the document, including content below the image.
- `capture.json`: outcome, URLs, load state, viewport and document dimensions,
  elapsed/quiet time, zoom, DPI, device pixel ratio, font defaults, JavaScript,
  animation and Qt settings, and which artifacts were saved.

## Text measurement cache comparisons

The Qt frontend keeps width and line-splitting results in one shared LRU across
pages and layout passes. The default budget is 512 KiB of estimated entry cost,
including owned text and an allowance for bookkeeping; it is not a per-page
allocation or an exact process-memory limit. Runs longer than 4096 UTF-8 bytes
bypass the cache. Keys include the selected font, fractional sizing, letter
spacing, metrics DPI, text bytes, operation and available width. Font database
and web-font alias changes clear the entries. Shaping and fitting are unchanged.

`capture.json` includes `text_cache` with width/split hits and misses, current
entry count, estimated cost and budget in bytes. Counts cover the process,
including entries subsequently evicted or invalidated. Misses include requests
that bypass caching. `--capture-text-cache-bytes 0` disables caching for a
reference capture; other nonnegative values allow budget comparisons. This
override and the counters exist only in capture builds.

For engine timing, use a Release build with `WISP_PROFILE_RESTYLE=ON` and request
an inspection (for example `--capture-point 0,0`). Compare its `render_profile`
layout time and call count across alternating cached/uncached runs, checking
that PNGs and box trees agree. Keep resource loading separate from layout time,
and use `steps.json` for resize or tab-switch comparisons.

## Image caching and scrolling comparisons

The decoded-image cache now has its own `image_cache_size` option (32 MiB by
default); it no longer takes one quarter of `memory_cache_size`. This is the
existing cache's preferred retention target, not a hard cap on live image memory.
The Qt frontend also keeps one scaled size per source bitmap in a shared
16 MiB LRU, with estimated bookkeeping included in its cost. Both caches fill
on demand across pages. Mutation, decoder notifications and bitmap destruction
invalidate scaled copies; changing the target dimensions replaces the old size.

`bitmap_cache` in `capture.json` and each settled step reports hits, misses,
entries, estimated cost, budget and cumulative scaling wall time (`scale_ms`).
`--capture-bitmap-cache-bytes 0` disables scaled-image retention for comparison.
Use `steps.json` differences after warm-up to measure repeated paints. Qt can
use worker threads internally when scaling, so renderer-thread `paint_cpu_ms`
alone does not account for all that work; also compare `paint_ms`.

Eligible scrolls reuse Qt's backing-store pixels and redraw the exposed strip.
The engine records eligibility during the existing completed-layout walk;
scrolling adds no box-tree scan. Sticky/fixed boxes, fixed backgrounds (including
additional layers), embedded documents, unfinished rendering, non-default page
zoom, active carets and fractional physical-pixel movement use full repainting.
Resize and content invalidation also require fresh pixels before reuse. Qt can
independently fall back to repainting, for example when another widget overlaps.

`--capture-no-scroll-blit` forces full viewport repainting. `widget_paint` reports
eligible `scroll_blits` requests, fallback `scroll_repaints`, paint calls and
the summed damaged bounding-rectangle area in logical pixels (`paint_pixels`).
Widget counters are sampled before the final capture. Normal `QWidget::grab()`
repaints, which can hide stale pixels, so test scroll correctness with
`--capture-screen` under Xvfb/`QT_QPA_PLATFORM=xcb`. This grabs the displayed
viewport without repainting it and enlarges the surrounding capture window as
needed to keep scrollbars from overlapping the fixed viewport. Screen capture
must be supported by the Qt platform, and the viewport must fit on that screen.
`qt_scroll_reuse` compares these native pixels against full repaints and checks
that eligible cases actually paint less area.

`--capture-repaint 'X,Y,W,H;X,Y,W,H'` requests one or more viewport rectangles
after settling, without changing page content. It is a repeatable action and
uses the widget's normal damage-region painting. Compare `--capture-screen`
output with a full-viewport repaint to find clipping or repeated-alpha errors.
`qt_paint_damage` covers these comparisons, transformed content, embedded frames,
and scrollbar edges crossing the viewport during scrolling. `paint_pixels`
counts the disjoint rectangles sent to the renderer, rather than their bounding
rectangle; `paint_calls` continues to count Qt paint events. Unsupported partial
painting (currently including CSS transforms) uses one full-viewport pass, so its
reported area is the full viewport even when Qt only updates a smaller region.

## Focused inspection

Add `--capture-id ID` (repeatable, literal ID without `#`) and/or
`--capture-point X,Y` to write `inspection.json` alongside the image:

```sh
QT_QPA_PLATFORM=offscreen QT_SCALE_FACTOR=1 build/frontends/qt/wisp-qt \
  --capture-dir /tmp/wisp-inspect --capture-size 1280x900 \
  --capture-id preface-last --capture-id fundraising-progress-bar-filler \
  --capture-point 950,300 https://www.haiku-os.org/
```

The report contains stable DOM paths/IDs, content/padding/border rectangles,
used margins, parent and child boxes, the absolute containing block, computed styles,
and the font selected through Qt's actual font-selection path. Anonymous boxes
also identify their nearest DOM owner. The legacy style dump is supplemented
with flex alignment/direction/growth and grid auto-flow values. This is not a
complete modern CSS serializer. The font report identifies the primary font;
individual glyph fallback can use additional fonts.

Sticky boxes report a `sticky` object: `prepared` includes the containing block,
scrolling ancestor (or `viewport: true`) and resolved `x`/`y` axis constraints in
unscrolled document coordinates. Auto insets are JSON null. `auto` means all four
insets are auto; `unsupported` includes a stable reason; `invalid` means constraints
are unavailable. `offset` is the current sticky correction relative to the
coordinate parent's inherited displacement. Existing rectangles describe visual
positions; `layout_border_box` excludes all scrolling and sticky displacement.
`sticky_layout_valid` describes the completed document pass. `box_storage` counts
structural boxes (including list markers/text clones), allocated sticky records
and their structure sizes; it excludes allocator overhead and embedded documents.
This census runs only in developer inspection, never in production scrolling.

With `WISP_PROFILE_RESTYLE=ON`, `render_profile` reports document-lifetime
`layout_calls`, `paint_calls`, `layout_ms` and `paint_ms`, including initial
loading and scroll-only paints. The corresponding `layout_cpu_ms` and
`paint_cpu_ms` exclude scheduling delays where a thread CPU clock is available;
otherwise they are null. Compare counter differences between settled steps to
separate scrolling from initial loading or restyling. Paint times measure core
HTML redraw, not screen presentation or capture-file writing.
`sticky_queries` counts resolved sticky coordinate queries and
`sticky_ancestor_steps` counts the ancestry frames assembled for them (not every
subsequent constraint lookup). Inspection's own coordinate queries are excluded.
These counters and clocks compile out with the existing profiling option.

Font diagnostics distinguish `weight` (the CSS request), `selected_weight`
(the weight requested from Qt after CSS fallback matching), and `face_weight`
(the static font's OpenType OS/2 weight). `resolved_style` names the loaded face.
`face_weight` is null for variable fonts or fonts without that table; `variable`
identifies variable fonts. Qt's reported numeric weight alone can echo the request
even when it loaded a different face.

`pixel_size` is the underlying integer Qt font size. `effective_pixel_size`
includes the fractional scale used for measurement and painting (before page
zoom), so a 15.9px CSS font can report a 15px base and approximately 15.9px effective.

ID lookup distinguishes `not_found` from `no_box` (for example `display:none`,
a hidden ancestor, or an SVG subelement without its own HTML box). Wrapped
inline elements can own several boxes. Reports include up to 64 matching boxes,
64 immediate children per box, and 12 ancestors; counts expose truncated box
and child lists. Duplicate IDs select the first DOM match.

Visual rectangles use **unscaled document coordinates, before CSS transforms**,
including element scrolling and sticky displacement. Subtract document scroll
and multiply by page zoom to obtain screenshot coordinates. Point queries reuse the core
hit-test walk and report its visible candidates; this is not a guarantee of
correct paint/z-order or transformed hit testing. Queries inspect the top-level
document, not the DOM inside child frames. A missing ID is a valid diagnostic
result and does not make a successfully rendered capture fail.

## Click sequences

Capture builds also accept ordered, repeatable actions. Each action waits for the
previous refresh and resource loads to settle:

```sh
QT_QPA_PLATFORM=offscreen build/frontends/qt/wisp-qt \
  --capture-dir /tmp/wisp-tabs --capture-size 600x700 \
  --capture-id first-panel --capture-id second-panel --capture-id draft \
  --capture-click-id draft --capture-type "written text" \
  --capture-click-label second --capture-click-label first \
  test/checked-interaction.html
```

`--capture-click-id ID` clicks the element's visual box; `--capture-click-label ID`
clicks a visible label whose `for` names that input (even if the input has no box).
Clicks use the normal core mouse path, scrolling the target into view if needed.
`--capture-click-point X,Y` instead clicks a fixed viewport position in unscaled
CSS pixels, without auto-scrolling. `--capture-scroll-point X,Y,DX,DY` sends a
scroll to the content under that viewport point, including a child iframe; signed
deltas use layout pixels. An unconsumed scroll is an action error. Both options
use normal core input dispatch and account for page zoom.
`--capture-drag X1,Y1,X2,Y2` sends a primary-button press, drag, movement and
release between viewport CSS pixel positions. It follows the same core input
sequence as Qt; inspection reports the resulting selected text as `selection`.
`--capture-type TEXT` sends keyboard input to the focused control and
`--capture-scroll X,Y` sets the document scroll position.
`--capture-resize WIDTHxHEIGHT` changes the content viewport through the widget's
normal resize/reformat path and waits for it to settle before the next action.
The final image and requested viewport use the last size in the sequence.
`--capture-scroll-element ID,X,Y` sets an element's existing horizontal/vertical
scrollbar offsets through the normal clamping/redraw API. IDs containing commas
are not supported by this option. A nonzero offset without that scrollbar is an
action error; the diagnostic does not create scrollbars for unpainted elements.
ID-based actions address the top-level document. Point actions can forward into
child frames, but inspection still describes the top-level DOM only. CSS transforms
are outside these helpers' coordinate support.

`steps.json` records the initial inspection and each settled result, including
live checked/input values, scroll offsets, resource counts, and elapsed time.
The usual image/dumps describe the final state. `action_error` identifies a
missing/unusable target or rejected key. JavaScript remains off by default.

### Measuring checked-state refreshes

Configure with both `-DWISP_QT_CAPTURE=ON -DWISP_PROFILE_RESTYLE=ON` to include
`restyle_profile` in inspections and each settled step. The latter option defaults
to OFF: its counters and clock reads are compiled out of normal builds. Keep
`WISP_ENABLE_PERF_TRACE=OFF` for measurements; these counters do not emit log
messages or allocate during the timed loops.

The profile reports wall-clock milliseconds, except the explicitly named CPU metric:

- `invalidate_ms`: choose the update mode, prepare its temporary reuse table and
  invalidate CSS selection caches in the required DOM scopes.
- `detach_ms`: clear interaction state, old gadgets and DOM-to-box mappings.
- `css_ms`: element style selection/composition or result cloning, including
  inline style parsing when selection is needed.
- `boxes_ms`: other active box construction and DOM traversal.
- `normalise_ms`: normalize the constructed box tree.
- `publication_ms`: validate and publish a detached region, including binding
  preparation and old-region retirement; zero for whole reconstruction.
- `construction_wait_ms`: time outside construction slices, including scheduler
  gaps and callback overhead. `construction_ms` includes CSS, boxes and normalization.
- `layout_ms`: the refresh's layout/reformat call, including retained-state reset
  and existing element scrollbar updates when retaining boxes.
- `layout_cpu_ms`: CPU time consumed by the rendering thread over the same phase;
  excludes time while that thread is not running and work on other threads.
  Null if the platform has no usable thread CPU clock. A large wall/CPU gap points
  to waiting or scheduling delays; CPU time can still vary with clock frequency,
  cache behavior and the amount of work.
- `layout_reset_ms`: the retained-state reset portion of `layout_ms`; zero for
  whole reconstruction. Do not add it to layout time a second time.
- `cleanup_ms`: rebind iframe windows, retire old boxes and publish the reformat.
- `total_ms`: refresh callback start through reformat publication, excluding
  painting, capture inspection and the artificial quiet period.
- `paint_ms` / `paint_calls`: core HTML drawing accumulated after refresh start
  and before the inspection. Artifact screenshots are produced later.
- `through_last_paint_ms`: refresh start through the end of the last measured
  draw. This is not physical screen-presentation latency.

`styled_elements` and `construction_slices` describe the amount of work. CSS and
normalization are subtracted from `boxes_ms` to avoid counting them twice. The
`intrinsic_inline_calculations` and `intrinsic_inline_reuses` counters distinguish
fresh inline-container intrinsic measurement from retained measurement reuse.
`inline_layout_calculations` and `inline_layout_reuses` count fresh line layout
and reuse of completed lines retained from the previous update.
`inline_layout_repeat_reuses` counts reuse within the current update's layout:
ancestor sizing may revisit a container without changing its available width.
`inline_layout_repeat_same_width` / `inline_layout_repeat_changed_width` count
revisits with an eligible completed result before attempting that reuse.
`inline_layout_float_exclusions` / `inline_layout_other_exclusions` count fresh
calls which cannot save eligible lines because of incoming floats or other
dependencies. These are calls, not counts of unique containers; the extra
eligibility diagnostics are compiled only into profiling builds.
The `inline_layout_width_misses`,
`inline_layout_float_misses` and `inline_layout_intrinsic_misses` counters explain
why preserved lines had to be discarded. Width takes precedence when both width
and incoming floats changed; containers excluded before preservation are not misses.
For a full reference with both forms of line reuse disabled, combine
`--capture-force-full-restyle` with `--capture-no-inline-layout-reuse`.
The clock uses nanosecond-resolution monotonic timestamps where available, with the
existing millisecond clock as a fallback on other platforms.

`--capture-force-full-restyle` selects the full-refresh reference path at runtime.
`--capture-css-only-restyle` selects the milestone A reference: reuse eligible
styles but reconstruct all boxes. The full override takes precedence if both are set.
Native activation records all changed controls and coalesces their parent
subtrees. Form edits record DOM changes immediately, including edits already
synchronized before a tab click.
The first refresh establishes a full baseline; subsequent eligible refreshes reuse
composed styles outside those scopes. Eligible single-region updates also retain
outside boxes once independent region owners have been established, as described
below. All refreshes still traverse the document for layout; the line
cache described below can skip eligible line work. Reuse requires scripting to
be disabled. Partial updates are part of every build; there is no feature switch.
Libcss's per-sheet scope summary and revision stamp are always available.

With profiling enabled, `update_mode` reports `partial_css`, `partial_boxes` or `full`.
`fallback_reason` is normally `none` on the selective path; full reasons include
`forced_full`, `dom_changed`, `scripting`, `full_request`,
`scope_capacity`, `invalid_scope`, `selector_scope`, `loading`, `no_style_snapshot`,
`inputs_changed`, `no_scope`, `no_reusable_styles`, `reuse_table_failed`,
`detach_changed` and `inputs_changed_during_build`.
`changed_controls` counts successful native state changes in the batch,
`dirty_roots` counts the coalesced candidate scopes, and `invalidated_nodes` counts
libcss node caches actually cleared. `styled_elements` counts fresh selections;
`reused_styles` counts cloned result containers, with `style_references` counting
their non-null style slots. `temporary_bytes` is the peak allocated lookup-table
capacity; `cloned_result_bytes` sums the new containers' allocations. These are
not whole-process peak memory figures, and exclude allocator overhead. `restarts`
counts abandoned partial attempts; their work stays in the counters even though
the final mode is full.

`qt_restyle_reference` replays edits and radio switches in CSS-only and forced-full
captures, checking each inspected state and the final PNG/DOM output. It requires
later toggles to report `partial_css`, so fallback cannot make comparisons pass
without exercising reuse. In profiling builds, `--capture-restart-partial-restyle`
injects a DOM-generation change after selective construction has begun. The same
fixture and `qt_checked_interaction_restart` verify the resulting full restart,
including resource and iframe behavior. Use `--capture-css-only-restyle` to inject
during whole construction; the default injects during a detached retained build.
Saved-page measurements are recorded in
[the partial-update plan](../../src/docs/partial-updates.md).

`--capture-interrupt-restyle MODE` tests document lifetime during a yielded
rebuild. It requires restyle profiling. Use `close` or
`navigate` to close the real window or navigate to `about:blank` while an extra
content observer lets the old document finish. `shutdown` drops that observer
immediately after closing; `shutdown-full` does the same during a forced-full
rebuild. These last two modes check cancellation when the last user leaves,
followed by normal application shutdown. Only this diagnostic mode introduces
short construction slices and delays to make the event ordering deterministic.
It writes `interruption.json` with before/after/release state instead of the
ordinary rendering artifacts. `qt_restyle_lifecycle` exercises all four modes
with styles, resources, forms and an iframe alive, using the CSS-only override.

### Retained boxes

Milestone B is always built, for ordinary browsing as well as capture.
`--capture-retain-boxes` remains a compatibility alias for this default
(and clears an earlier CSS-only override).
Warm both panels first: the style snapshot and independent region owners
are established by the first two refreshes. Later eligible changes rebuild one
stable block/flex/grid region and retain the surrounding boxes, followed by full
document layout.

Active interactions, widgets within the changed region, unsupported resources,
frames and unsafe ownership boundaries retain the conservative fallback. Pages
with existing frames skip the detached attempt and can still use `partial_css`.
Other rejected candidates also preserve outside CSS reuse when inputs are still
valid, re-invalidating their dirty scopes before whole reconstruction. In that
case `fallback_reason` explains the region rejection despite `partial_css` mode;
changed inputs require full selection and increment `restarts`.
Do not infer retention from matching screenshots: check `partial_boxes` in the
profile or unchanged outside `box_address` values. Default captures include box
addresses and `tree_allocations` (talloc block count, not bytes) in inspection
output; the CSS-only and full reference modes omit them.
All inspections also expose selection `text_offset` and element `scrollbars`
presence/offsets. Addresses and allocation counts are diagnostics, not geometry
reference fields.

With profiling enabled, `--capture-restart-partial-restyle` combined with retention
invalidates the detached attempt. `--capture-fail-retained-layout` implies retention
and injects a failure after publication/reset. It must exit with `content_error`,
clean update state, and the failed document detached from the window. A screenshot
may show the ordinary error page; it must never show the incomplete retained tree.
`capture.json` includes the failed document's `restyle_state` for this check.
`qt_retained_restyle` compares the default with independent CSS-only and full
rebuilds. It also checks the error paths, settled resource bindings, outside box
identities, selection offsets and scrollbars.
`qt_retained_scroll` checks nonzero offsets through growth, shrinkage and scrollbar
removal, including updates while the container is off-screen. Maximum offsets and
final pixels are compared with independently constructed full-reference pages.

Combine `--capture-interrupt-restyle MODE` with at least three alternating clicks
to interrupt a detached build. `qt_retained_lifecycle`
covers `close`, `navigate` and `shutdown` with the original tree still live.

### Inline intrinsic reuse

Eligible retained updates also keep intrinsic min/max widths for unchanged
text-only inline containers by default. `--capture-no-intrinsic-reuse` disables
this cache (and dependent line reuse), keeping selective styles and box retention.
The original
`--capture-reuse-inline-intrinsics` option still selects the default behavior;
the last of these two options wins.
Ordinary blocks' min/max fields are reused
for CSS sizing during layout, so they are not treated as intrinsic caches.

The eligible containers have a normal block parent and only text, inline markers
or hard breaks; atomic boxes, floats, replaced content and widgets are excluded.
The cache also records its height contribution and whether minimum word widths
were requested. A changed minimum-width requirement triggers fresh measurement.
Changed regions start without cached sizes, and every ancestor is remeasured.
The existing publication input checks guard styles, fonts, units and viewport
changes; rejected retained updates keep the normal reconstruction fallback.
Configured generic font-family names are compared exactly, with an oversized
snapshot falling back to reconstruction. Qt database/alias changes also advance
the font generation, including on pages with only generic font families.

This first milestone C step skips intrinsic measurement only. Line wrapping,
block/flex/grid/table sizing, positioning, bounds, selection indexing and scrolling
still run normally. No additional per-box fields or heap allocations are needed.
Compare with `--capture-no-intrinsic-reuse` for fully reset retained layout,
and with `--capture-force-full-restyle` for independent full reconstructions.
The retained-restyle comparison includes narrower/wider viewport changes between
switches, viewport-sized text and a resize round trip. A changed input requires
a fresh baseline; later eligible switches must resume reuse.

### Completed inline lines

Eligible retained updates also preserve completed text-only lines by default,
in ordinary browsing and capture. `--capture-no-inline-layout-reuse` selects the
intrinsic-only reference path. The original `--capture-reuse-inline-layout` flag
remains an explicit request for the default; the last of these two options wins.
Line reuse requires the intrinsic cache. The inline container
keeps its available-width input separately from its output/overflow width, along with
its wrapped text fragments, local coordinates and any required baseline.

Reuse requires the same available width, no incoming floats and unchanged
intrinsic minimum-width requirements. Only static text/inline markers and hard
breaks without clearance qualify; positioned inlines, atomic/replaced content,
widgets and list markers remain excluded. A miss rejoins the text fragments and
runs ordinary layout. The parent still calculates sizing, margins and placement;
all flex/grid/table contexts, final positioning, bounds, selection indexing and
scrollbar updates still run. The existing document input guards cover style,
font and viewport changes before retained publication.

During an update, eligible lines can also be reused when flex/grid sizing visits
them again at the same width. This includes full/CSS-only reconstruction: fresh
lines are only reused later in that same pass. It does not skip ancestor sizing
or percentage-height resolution. A pass-local flag expires during final
positioning, or on layout failure; initial loading and ordinary resize reflows
do not enable this additional reuse. It adds no allocation or per-box field.

Compare the default against the intrinsic-only opt-out, as well as forced full
reconstruction. `qt_retained_restyle` requires default cache hits and checks
changed sibling widths, incoming floats, reconstruction fallbacks, resize,
restart and scrolling.
Disabling partial updates removes the cache and its width field entirely.

`action_ms` is the preceding capture action's synchronous duration, including
target lookup and any scroll into view. Deferred restyling happens afterward.
For repeat measurements, warm both panels first and check that resource counts
remain constant. Newly fetched resources can cause follow-up layouts outside the
profile's single refresh; keep those first-reveal samples separate. Compare an
uninstrumented build using identical capture options to check measurement overhead.

## Reference comparison

`compare_capture.py` runs Wisp and an installed Chromium with page JavaScript
disabled, separate temporary profiles, equal drawable viewports, 100% zoom and
device pixel ratio 1. It collects the same requested element IDs/point from
Chromium through [DevTools](https://chromedevtools.github.io/devtools-protocol/),
including computed styles, box quads and used fonts.
It waits for load and network quiet, records failed Chromium resources, and
keeps logs on failure. No browser is downloaded or installed by the script.

It requires **ImageMagick 7** (`magick`) and **Python websocket-client** in addition
to Chromium. These are optional developer dependencies, outside the browser
build. Use `--chromium` or `--magick` for nonstandard executable locations.

```sh
python3 frontends/qt/compare_capture.py test/render-inspect.html \
  --wisp build/frontends/qt/wisp-qt --output /tmp/wisp-comparison \
  --size 1280x900 --id probe --point 60,80
```

Open `report.html` for side-by-side images, a 50% overlay and an absolute RGB
difference image. `comparison.json` records settings and the differing-pixel
fraction. The `wisp/` and `reference/` directories retain their images, metadata
and optional inspection reports. Different pixels do **not** fail the command;
failed captures, mismatched dimensions/settings or tool errors do. Nothing is
rescaled to disguise a viewport mismatch. The output directory must be empty.

For before/after regression checks, use `--reference /path/to/earlier/capture`
with a directory containing `page.png` and `capture.json`. This mode needs no
Chromium or websocket-client. The earlier capture must have completed at the
same viewport with JS off, DPR 1 and zoom 1. Its URL/settings remain in the report
so deliberately comparing different fixtures is possible.

Chromium receives Wisp's resolved default font families and its default/minimum
font sizes rounded to whole pixels. Fractional font sizing and font rasterization
can still differ. CSS animations are paused in Chromium; animated image frames
may differ from Wisp's disabled image animation. Chromium's box quads include
transforms while Wisp's inspection rectangles describe layout before transforms.
Live sites may serve different content/assets between the two loads: use saved
pages for reliable comparisons. A high pixel-difference percentage alone is not
evidence of a particular rendering bug.

## Capture settings and completion

The requested size is the **drawable content area**, in logical pixels. At
device pixel ratio 1, PNG dimensions match it exactly. Match this area when
comparing with Chromium, whose screenshot size can include its scrollbar.
Use the same assets, fonts, zoom, JavaScript setting and viewport. For stable
regressions, prefer a saved site and keep its source revision alongside the
capture; live pages and network resources can change between runs.

Capture uses a fresh temporary settings/cookie/history/cache profile and 96 DPI.
JavaScript and image animation default to OFF; existing core command-line
options can override these (for example `--enable_javascript=1`). Ordinary
interactive launches retain their usual settings even in a capture-enabled
build. Do not override profile file paths when you want an isolated run.

Completion requires the core's DONE state, stopped loading indicator (including
child frames), redraw readiness, and no viewport paint/resize/invalidation for
`--capture-settle` milliseconds (default 500). The deadline is
`--capture-timeout` milliseconds (default 30000). Exit status is 0 for a complete
capture, 1 for timeout/load/output/viewport failure, or 2 for bad arguments or
initialization failure. On timeout, any safely drawable partial page is saved;
read `result` in the JSON before trusting the image. Browser-generated error or
authentication pages are reported as `load_error`.

This is a rendering aid, not a full network recorder: DONE does not guarantee
that every optional image/font loaded successfully, or that an HTTP response
was 200. Missing resources can still produce a complete capture. Dumps apply
to HTML; standalone images have no DOM/box dump. A JS timer may mutate a page
after the quiet period. The in-process deadline also cannot interrupt a stuck
renderer; an automated caller should impose an outer process timeout. Capture
timings include loading, settling and instrumentation, so use the performance
tracer for engine benchmarks.

## Development workflow

Capture the saved site, identify a visible problem, then reduce it to a small
page under `test/`. Inspect the matching DOM IDs and box dimensions to decide
whether the problem is parsing/style, layout or painting. After fixing it,
capture both the small page and the whole site at the same viewport. Keep a few
known-good sites (including Haiku) as broader checks. Pixel differences help
locate regressions but font rasterization differences need visual judgment.

The integration check uses Python's standard library, Qt's offscreen plugin and
a local HTTP server. It needs no Check library or internet access:

```sh
python3 frontends/qt/test_capture.py build/frontends/qt/wisp-qt
```

It checks exact viewport sizes with scrolling content, repeatable PNG output,
tree dumps, a delayed stylesheet and redirect, unfinished loads, browser error
pages, output failures, and invalid/reused output arguments. If CTest is enabled,
it is also registered as `qt_render_capture`.

The optional Chromium comparison checks additionally exercise JS blocking,
element/point inspection, delayed CSS, identical before/after baselines and
rejection of mismatched or failed captures:

```sh
python3 frontends/qt/test_compare_capture.py build/frontends/qt/wisp-qt
```

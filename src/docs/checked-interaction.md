# Native checked-state interaction

Radio/checkbox CSS tabs work without JavaScript. This is a document-state
refresh, separate from parsing and page loading; it is not general dynamic DOM
or JavaScript application support.

## Activation and selection

The mouse path resolves an input or its explicit/implicit label through the
DOM, including `display:none` controls. It honors disabled inputs and disabled
fieldsets (including the first-legend exemption). Radio groups use case-sensitive,
nonempty names and form ownership, including formless and explicitly associated
controls. Live checkedness does not change the default `checked` attribute.

The hit-test result retains label candidates like links and gadgets. Text boxes
retain their DOM owner because inline layout flattens nested elements into
siblings; this reference does not make text an element box for CSS transforms
or painting. When checkable candidates overlap, their stacking levels keep a
transparent input from intercepting a label painted above it. This does not
redesign general hit-test stacking order.

Activation dispatches click events, then updates state if not cancelled, and
emits input/change on a change. It schedules rendering after the mouse callback
returns. Multiple requests before that callback runs are coalesced. Full HTML
legacy pre-click activation/cancellation semantics for script listeners,
programmatic clicks, keyboard radio-group navigation, and form reset are separate
work; this change does not claim to implement them.

## Presentation lifetime

`html_request_restyle()` schedules a refresh on the existing main event loop:

1. Invalidate all libcss per-node selection data, including custom-property
   environments and sharing caches. Keep the stylesheet selection context.
2. Clear box-owning selection, caret/focus, drag, search and select-menu state.
   Input values live in the existing DOM. Drop old gadget objects and DOM-to-box
   mappings, including descendants that may become hidden.
3. Construct a new box tree from the same document. Construction still yields
   through the existing scheduler. Painting/input cannot use a partial tree.
4. Reuse resource handles, lay out the new tree, rebind iframe windows, retire the old
   allocation context, and broadcast a reformat. The window retains its scroll
   offset, subject to ordinary clamping if the document becomes shorter.

Cache invalidation and gadget/box detachment each use a single depth-first DOM
walk, including hidden descendants. The shared iterator uses constant storage
without recursion and reads children after its callback, so textarea value
synchronization may replace text children safely.

There is no navigation, stylesheet reload, parser restart, or replay of page-load
scripts/events. Fonts and inline SVG source caches also survive. Destruction
cancels both queued refreshes and in-progress box conversion.

Objects keep a DOM identity/request URL and cache handle while their box is
absent. Reappearing images, backgrounds (including multiple layers), SVGs, and
embedded objects reuse those handles. Anonymous generated images can reuse the
same unbound URL/type/layer entry. First-time content in a revealed panel may
start new fetches; completion schedules layout through the existing deferred
reformat path. Hidden-resource completion has no box to redraw.

Iframe window addresses remain stable. Their array reserves space for the
existing document's iframe elements, including initially hidden ones, but only
visible frames get initialized/loaded. A hidden frame keeps its browsing context
and is rebound when shown. Arbitrary DOM insertion/removal of iframe elements is
outside this state-only refresh contract.

This deliberately rebuilds the whole presentation. It costs one CSS selection,
box-construction and layout pass per changed control; retaining the old allocation
context until rebinding also temporarily increases peak box memory. Selective
invalidation can be added at this boundary if measurements justify it. Neither
parallel rendering nor a new dependency graph is required now.

## Measured refresh cost (2026-09-29)

Release + LTO, 1280 × 900, scripts disabled, Intel Xeon E5-2697A v4. Each
page was loaded in three fresh processes; each process performed 12 alternating
native label clicks. The first two clicks warm both panels and are excluded,
leaving 30 samples per page. Selected-panel state, geometry and stable resource
counts were checked throughout. No compiler jobs ran alongside timed captures.

The optional `WISP_PROFILE_RESTYLE` counters use monotonic wall-clock timestamps
and aggregate in memory. These internal durations exclude the capture quiet
period, inspection and artifact writing. Baseline medians before replacing the
indexed DOM enumeration loops, in milliseconds:

| Wisp phase | Tion | Bihon |
| --- | ---: | ---: |
| Invalidate CSS caches, including DOM enumeration | 62.4 | 133.7 |
| Detach old gadgets and box mappings, including DOM enumeration | 63.7 | 137.3 |
| Element CSS selection/composition, including inline style parsing | 91.5 | 151.2 |
| Other active box construction | 3.53 | 5.63 |
| Box normalization | 0.045 | 0.090 |
| Scheduler/callback gaps during construction | 0.005 | 0.009 |
| Layout/reformat | 110.3 | 172.0 |
| Retire old boxes, rebind iframes and publish reformat | 0.78 | 1.11 |
| **Complete refresh, before painting** | **331.7** | **599.2** |
| Subsequent core HTML drawing (one call) | 3.54 | 4.75 |
| Refresh start through the end of that draw | 337.4 | 606.2 |

Medians of individual phases do not necessarily add to the median total. The
90th-percentile refresh times were 341.9 and 620.3 ms. Tion reselected styles for
1,327–1,341 elements per click; Bihon for 1,912–1,926, depending on the active panel.
The construction phase's large inclusive duration is mostly CSS selection; it
should not be interpreted as box allocation/structure overhead.

### Repeated DOM enumeration

Both the cache-invalidation loop and the old-box-detachment loop originally called
`dom_nodelist_item(list, i)` for every element. Libdom's implementation in
`contrib/libdom/src/core/nodelist.c` restarts at the root for each index. Enumerating
all indices therefore does quadratic traversal work. Both loops now use
`libdom_iterate_elements()` for linear traversal through the public DOM APIs.

An isolated benchmark parsed each saved document and compared indexed enumeration
with one depth-first walk through the public DOM APIs. Both methods found the
same element count. Seven repetitions, median milliseconds:

| Page | DOM elements | Indexed NodeList enumeration | Single tree walk |
| --- | ---: | ---: | ---: |
| Tion | 1,493 | 66.88 | 0.111 |
| Bihon | 2,099 | 136.77 | 0.176 |

This microbenchmark isolates enumeration; the renderer measurements below include
the actual cache invalidation and gadget detachment work as well.

### Renderer after linear traversal

The same Release configuration and 30 warmed samples per page, after replacing
both indexed scans with the shared iterator. Median milliseconds:

| Phase | Tion before | Tion after | Bihon before | Bihon after |
| --- | ---: | ---: | ---: | ---: |
| Invalidate CSS caches | 62.40 | 0.70 | 133.71 | 1.11 |
| Detach old gadgets and box mappings | 63.72 | 0.25 | 137.33 | 0.37 |
| Combined invalidation + detachment | 125.09 | 0.96 | 268.94 | 1.47 |
| CSS selection/composition | 91.55 | 89.77 | 151.23 | 151.66 |
| Layout/reformat | 110.29 | 109.41 | 172.01 | 171.53 |
| **Complete refresh, before painting** | **331.72** | **205.08** | **599.23** | **332.51** |

Combined rows are medians of per-sample sums. Total refresh time fell by 38.2%
for Tion and 44.5% for Bihon; the 90th percentiles fell to 213.2 and 348.1 ms.
Refresh through the end of drawing fell to 210.8 and 339.4 ms. CSS selection and
layout remain whole-document work and account for almost all the remaining cost;
the traversal fix does not introduce selective invalidation or parallelism.

All six final screenshots and DOM dumps are byte-identical to their baseline
captures. All 13 inspected states per run match the baseline panel geometry,
control state and scroll offsets; resource counts remain stable. The focused
DOM/CSS/form/box and Qt capture tests pass in Debug (ASan/UBSan) and Release.
The traversal unit tests also pass with LeakSanitizer enabled.

### Chromium comparison and measurement limits

Chromium 153.0.8010.52, same files/viewport, page scripts disabled, aligned font
preferences, three fresh processes and 30 warmed native clicks per page. DevTools
`Performance.getMetrics` with `timeDomain=timeTicks` measured deltas around the
press/release, forced panel geometry and screenshot completion:

| Chromium phase | Tion median (ms) | Bihon median (ms) |
| --- | ---: | ---: |
| Style recalculation | 1.75 | 2.16 |
| Layout | 1.36 | 1.87 |
| Combined style + layout | 3.15 | 4.06 |

Each sample recorded four style passes and one layout pass. These counters are
internal work durations, not complete click-to-screen latency. Chromium paint,
raster and display presentation are not included; Wisp's drawing figures are
also offscreen CPU measurements. The timing boundaries of the two engines differ,
so this is evidence of the scale of their work, not a precise overall speed ratio.

The capture loop still waits at least 50 ms after activity to obtain a painted,
settled state. Internal profile timers exclude that wait. Using identical capture
options, uninstrumented/instrumented interval medians were 396/397 ms for Tion and
654/667.5 ms for Bihon. This is a rough overhead check, including run-to-run noise,
not a precise estimate of timer cost. The earlier 450–830 ms figures included a
100 ms quiet period and should not be used as renderer-only timings.

Local raw samples, reproducible drivers and the DOM microbenchmark are under
`out/render-captures/checked-timing-2026-09-29/` (`baseline.json`, `profiled.json`,
`linear.json`, `chromium.json`, `nodelist-*.json`, `run_wisp.py`, `run_chromium.py`,
`nodelist_benchmark.c`). See the capture guide for counter definitions.

## Checks

- `libdom_iteration_test`: document/subtree order, hidden descendants, non-element
  nodes, early callback errors, child replacement, empty roots and deep trees.
- `form_state_test`: groups, explicit/implicit labels, interactive descendants,
  disabled controls/fieldsets, explicit form ownership and live/default state.
- `css_checked_test`: repeated checked-state invalidation with the same selection
  context.
- `qt_checked_interaction`: real mouse/keyboard input with JS disabled, repeated
  radio/checkbox switches, CSS variables and geometry, typed values in hidden
  panels, scroll preservation, generated/layered images, SVG reuse, and hidden/
  newly revealed iframes. Runs under the normal Debug sanitizers.

See [Qt capture instructions](../../frontends/qt/CAPTURE.md) for ordered actions
and `steps.json`. Saved-site checks use Tion's Live/Comentate and Bihon's
Live/Distribuiri sidebar labels; these are native radio/CSS tabs. Four alternating
desktop clicks agree with Chromium's script-disabled behavior. Bihon also works
at 390 px; Tion's containing element is hidden there in both browsers.

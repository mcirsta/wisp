# Performance assessment

Original assessment: 2026-10-01. Reassessed against the implementation and
saved benchmark results on 2026-10-02.

Scope: page-load and rendering performance of the Qt frontend. The rendering
engine remains single-threaded. JavaScript execution is outside this assessment.
The reassessment initially used saved benchmarks; the network-event comparison
below was then measured during implementation on 2026-10-02. Section 6 records
a fresh Release profile after the polling migration. Section 7 records the
subsequent CSS-variable improvement and its before/after measurements.

## Current status

All four priorities from the original assessment are implemented in Qt and
enabled in normal builds. Network readiness adapters now cover every frontend
present in this tree: Qt, GTK, Monkey, native Windows and BeOS/Haiku. The shared
polling fallback has been removed. Windows and Haiku runtime validation is
explicitly deferred to their native builds; see section 4 for validation limits.
The other observations are candidates for measurement, not evidence that another
broad rewrite is needed.

| Original finding | Status | Current position |
|---|---|---|
| Inline CSS/JavaScript fetch queues process one item per poll | Done | Bounded batches process queued local work without a periodic timer. |
| Repeated Qt text measurements | Done for repeated width/split results | A shared 512 KiB cache avoids reshaping identical inputs; expensive misses remain possible. |
| Images rescaled on every paint | Done | A shared 16 MiB cache retains scaled images. |
| Full viewport repaint on every scroll | Done for eligible scrolling | Pixel reuse includes sticky content and embedded frames; unsupported cases repaint fully. |
| Small decoded-image cache tied to source cache | Done | Decoded images have an independent 32 MiB default budget. |
| Network driven by a 10 ms poll | Migrated across frontends | Socket readiness and libcurl deadlines replace the polling API and fallback; native Windows/Haiku validation remains. |
| Repeated work collecting CSS variables | Unused pseudo evaluation removed | HotNews variable setup falls from about 97 ms to 42 ms in the paired follow-up. Parent inheritance remains shared; further local-result caching is deferred. |
| Final full reflow after loading | Policy retained | Text caching reduces its cost; further layout reuse requires evidence. |
| Scheduler list scans and font-metrics setup | Low priority | No current measurement justifies making these the next project. |

These optimizations do not require an experimental feature switch. Capture-only
controls that disable reuse or caches remain useful as correctness and performance
references. Full repaint fallbacks are part of the implementation's correctness,
not an obsolete development path.

## Measurement context

The original investigation used Release without LTO, Qt offscreen at 1280x900
and 96 DPI, performance tracing, callback timers and C-side gprof. It covered
saved Phoronix, CNX Software, Tion, Bihon and HotNews pages. No sampled Qt/libc
profile was available. HotNews loaded some images from the live network, making
its load-completion timings unsuitable for controlled comparisons.

Later implementation benchmarks used different configurations, described below.
All results are from one desktop. Do not add the savings together or interpret
layout/paint CPU time as total page-load time or delivered frame rate.

## 1. Fetch queue batching: complete

The CSS and JavaScript fetchers previously removed the ring head and then
compared the next item against the new head. That ended the loop after one
item, spreading inline styles across successive 10 ms polls.

Commit `489e728f` snapshots the pending count and processes that bounded batch.
This also avoids an unbounded loop if callbacks enqueue more work. Regression
coverage includes callbacks, aborts and reentrancy in
[`fetcher_queue_test.c`](../test/fetcher_queue_test.c).

The original experiment measured time to first layout:

| Saved page | Before | After batching |
|---|---:|---:|
| Tion | 1153 ms | 319 ms |
| Bihon | 1752 ms | 412 ms |
| CNX | 381 ms | 207 ms |
| HotNews | 868 ms | 745 ms |

These are historical experimental measurements, not a new measurement of HEAD.
The later production validation also found identical images, normalized box
trees and DOM dumps. Its separate settled-capture results are preserved in
`out/render-captures/fetcher-poll-2026-10-01/benchmark.json`.

No further work is indicated for this bug. Network readiness was a separate
change, described in section 4.

## 2. Text measurement: cache complete; expensive misses remain

Commit `bf390ff9` adds a shared 512 KiB cache in
[`frontends/qt/layout.cpp`](../../frontends/qt/layout.cpp). It caches width and
split results across layout passes, using the selected font, DPI, relevant style
settings, text and available width. Font changes invalidate the cache. Entries
own their text; inputs over 4096 UTF-8 bytes bypass the cache to bound entry cost.

The original HotNews profile recorded 7,200 split calls but only 851 distinct
inputs, and 11,774 width calls but only 2,148 distinct inputs. Flex sizing,
min/max measurement and final reflow explain much of that repetition.

The later cache benchmark measured core layout CPU time with caching disabled
and with the chosen 512 KiB budget:

| Saved page | Initial layouts, cache off | Initial layouts, cache on | Two resizes, cache off | Two resizes, cache on |
|---|---:|---:|---:|---:|
| HotNews | 502.9 ms | 142.3 ms | 478.8 ms | 73.3 ms |
| Tion | 77.7 ms | 47.9 ms | 53.6 ms | 9.7 ms |
| Bihon | 140.9 ms | 65.3 ms | 113.8 ms | 15.5 ms |
| CNX | 72.8 ms | 46.6 ms | 59.8 ms | 27.2 ms |
| Phoronix | 26.3 ms | 22.2 ms | 8.8 ms | 1.4 ms |

Each initial figure totals two layout calls. Method: Release with LTO, restyle
profiling enabled, one CPU affinity, offscreen 96 DPI; fresh process at 1280x900,
then resize to 1200x900 and back. Live HTTP requests were forced to fail through
a localhost proxy. Figures are medians of five alternating runs after discarding
the first sample. Images and normalized box trees matched across cache sizes.
Evidence: `out/render-captures/text-cache-2026-10-01/layout/benchmark.json`.

Remaining candidates:

- On a split-cache miss, Qt still measures the whole remaining string. Repeating
  this for successive lines of a long paragraph can approach quadratic work.
  The cache especially does not solve this for inputs above its size limit.
- `PrefixWidths` already reuses the full-string width and four recent prefix
  measurements within one callback. Position fitting estimates a starting point
  and adjusts it; the original description of an entirely naive character scan
  was incomplete. Distinct prefixes can still require shaping.
- Creating `QFontMetricsF` and finding the metrics device still happens before
  cache lookup. The original estimate was about 5% of measurement work, and the
  first Qt/font initialization cost about 50 ms. Neither is a current profile.

The fresh profile in section 6 confirms the long-paragraph scaling issue, while
all split requests in the saved pages are at most 1,078 bytes. Prioritize the
measured HotNews CSS work first. A text-fitting change must preserve kerning,
fractional fitting and fallback-font behavior; cache size alone does not solve
first-pass processing of long, distinct suffixes.

## 3. Images and scrolling: complete within supported cases

The image-cache work in `6bce3b8f` provides two independent budgets:

- **32 MiB decoded-image retention**, configurable through `image_cache_size`,
  replacing the previous approximately 3 MiB allowance derived from the 12 MiB
  source-cache budget.
- **16 MiB scaled-image cache** in the Qt frontend, shared across bitmaps, with
  one retained target size per source image. Source changes invalidate it.

These are shared retention budgets, not allocations made for every page or a
hard ceiling on all live image memory. The source cache remains 12 MiB.

Warm redraw comparisons confirmed savings on pages with scaled images, and no
expected gain on a Phoronix viewport with no scaled-cache hits. Output comparison
results are in `out/render-captures/image-cache-final-2026-10-01/comparison.json`;
the warm-redraw method and timings are in
`out/render-captures/image-cache-2026-10-01-warm-confirm/benchmark.json`.

Scrolling now reuses existing viewport pixels and repaints exposed strips plus
the areas affected by sticky content and embedded frames. Commits `f1043368`
and `77bfca1a` complete paint clipping and those additional cases. See
[`sticky-positioning.md`](sticky-positioning.md) for implementation boundaries.

The completed scroll benchmark measured core HTML paint CPU time per scroll:

| Saved page | Device pixel ratio | Full repaint | Pixel reuse |
|---|---:|---:|---:|
| Tion | 1 | 8.39 ms | 1.53 ms |
| Tion | 2 | 17.31 ms | 1.30 ms |
| Bihon | 1 | 3.28 ms | 0.83 ms |
| Bihon | 2 | 6.06 ms | 0.95 ms |

Method: Release with LTO and aggregate profiling, native Qt under Xvfb, 1280x900,
warm caches, one CPU affinity, three alternating full/reuse pairs and fifteen
measured 30 px scrolls per run. These are medians of each run's average. They
exclude Qt's pixel copy, presentation and capture costs. All compared viewport
pixels matched. Measured optimized scrolls performed no layout or image scaling
misses and repainted approximately 12% of Tion and 15% of Bihon's viewport.
Evidence: `out/render-captures/sticky-scroll-stage3/measurements.json`.

Full repaint remains appropriate for cases such as unsupported root transforms,
root fixed content/backgrounds, unsuitable zoom/device-pixel movement, unfinished
painting or excessive damage. Extending reuse to those cases is optional follow-up
work, not a prerequisite for the measured gains. Relevant coverage also passed
the 74-test ASan/UBSan selection, with leak detection disabled.

## 4. Network polling: removed across frontends

The 2026-10-02 implementation uses libcurl's socket and timer callbacks.
[`gui_fetch_table.socket_watch`](../../include/wisp/fetch.h) connects the shared
backend to frontend readiness notifications without introducing a Qt dependency
in the core. The socket adapter is mandatory at frontend registration, with no
feature flag or compatibility fallback. `fetch_fdset()`, the fetcher `poll` and
`fdset` operations, and `curl_multi_perform()` have been removed. Local fetchers
use an explicit queued-work `run` operation.

| Frontend | Readiness integration |
|---|---|
| Qt | `QSocketNotifier`, on the existing engine thread |
| GTK | Persistent GLib I/O sources; canceled/replaced timer sources are removed immediately |
| POSIX Monkey | Blocking `select()` over registered sockets and command input |
| BeOS/Haiku | Blocking `select()` over registered sockets and the GUI event pipe |
| Native Windows | Winsock event notifications joined with the Windows message wait |
| Windows Monkey | Winsock notifications joined with a command-ready event |

Libcurl supplies a replaceable, one-shot deadline. A zero deadline queues work
instead of recursively entering libcurl. An idle event loop has no network
polling timer. The native wait adapters share registration and safe dispatch code
in [`utils/socket.c`](../utils/socket.c); GTK and Qt use their own event sources.

Local file/data/resource/inline-style work is scheduled immediately when there
is work to do. Finishing or aborting a transfer schedules queued starts when a
slot becomes free. An abort from inside a curl callback gets explicit deferred
cleanup; it cannot depend on a later packet arriving. Multi-handle errors fail
outstanding transfers and release their slots rather than leaving loads hung.

Controlled Release comparison, four alternating pairs, fresh processes:

| Metric | Previous polling | Event-driven |
|---|---:|---:|
| First-to-last request in a 16-file CSS import chain | 322.2 ms | 5.3 ms |
| Settled capture elapsed time | 502.5 ms | 144.0 ms |

The fixture uses local HTTP/1.1 with TCP_NODELAY; Qt runs offscreen at 640x480 and
96 DPI with LTO. The first metric measures request arrivals at the server. Capture
elapsed time includes a 100 ms settling period and observation cadence. These are
medians, not predictions of gains on arbitrary live sites. Images and normalized
box trees matched in every run. Evidence:
`out/render-captures/network-events-2026-10-02/benchmark.json`.

Regression coverage exercises bounded concurrency, starts from completion
callbacks, local work during a stalled network transfer, queued/self/other-request
cancellation, descriptor reuse, watcher registration failure and recovery, TLS
connection timeout, redirects and chained CSS. It also checks that completed
fetching has no remaining watches or timer wakeups.

Initial Qt/Monkey validation: all 63 selected Qt/Monkey/fetch tests passed with ASan/UBSan (leak
detection disabled). Debug Qt/Monkey and production Release Qt builds succeeded,
including capture/profiling disabled. A live HTTPS capture of the Haiku homepage
completed successfully. Logs are in
`out/render-captures/network-events-2026-10-02/`.

All-frontend migration validation and remaining platform work:

- Qt/Monkey Debug and production Qt Release builds succeeded. All 65 selected
  Linux Qt/Monkey/fetch/adapter checks passed across the main run and clean
  rerun. Two Monkey startup checks initially overlapped a relink and failed to
  load the incomplete shared library; after the build finished, all 12 focused
  event/Monkey checks passed. Logs preserve both runs. ASan/UBSan were enabled,
  with leak detection disabled.
- The POSIX socket adapter and GLib adapter tests cover read/write readiness,
  peer close, removal and replacement during dispatch, and cleanup. GLib tests
  also verify timer replacement, cancellation, and deferred execution.
- The changed GTK event-loop, fetch adapter, socket source and scheduler objects
  compile. A complete GTK Debug build is currently blocked by existing header
  issues in `save_pdf.h`, `toolbar.h`, `page_info.h` and `about.h`. GTK runtime
  verification is still needed when that frontend is brought up to date.
- Windows and Haiku are best-effort source migrations, with native testing
  deferred at the user's request. A preliminary Windows adapter check under Wine
  reached a readiness-wait timeout; it is unresolved and must be investigated on
  native Windows. No further Windows checks were run after that request.
  `socket_events_test` includes more-than-64-sockets coverage on Windows;
  `monkey_input_test` covers partial commands, EOF, an unterminated final command,
  and quitting while the input pipe remains open. These native tests still need
  to be run there. Haiku's frontend has no current CMake target in this tree.
- Windows Monkey no longer uses `PeekNamedPipe`, short sleeps or capped waits.
  A blocking input-only reader hands a complete command to the main thread and
  waits for acknowledgement. QUIT joins it before another read starts, so
  shutdown does not require polling or racing a cancellation of blocked I/O.
- Rendering, command dispatch and fetch callbacks still run on the engine thread.
  The input reader never invokes engine code. A future network worker would need
  explicit ownership and completion-message boundaries; the socket callback API
  is not permission to invoke the engine concurrently.

Migration logs: `out/render-captures/network-all-frontends-2026-10-02/`.
The Monkey driver's existing `POLL BLOCKING` / `POLL TIMED` protocol messages
remain compatible; they now describe event waits, not periodic activity checks.

API references: [libcurl socket action](https://curl.se/libcurl/c/curl_multi_socket_action.html),
[libcurl timer callback](https://curl.se/libcurl/c/CURLMOPT_TIMERFUNCTION.html), and
[Qt QSocketNotifier](https://doc.qt.io/qt-6/qsocketnotifier.html),
[GLib I/O sources](https://docs.gtk.org/glib/func.io_add_watch_full.html),
[WSAEventSelect](https://learn.microsoft.com/en-us/windows/win32/api/winsock2/nf-winsock2-wsaeventselect), and
[WSAEnumNetworkEvents](https://learn.microsoft.com/en-us/windows/win32/api/winsock2/nf-winsock2-wsaenumnetworkevents).

## 5. Other observations: corrections and remaining candidates

### CSS custom properties

The original HotNews profile counted 607,795 calls to
`css__variables_ctx_cascade()` for 2,397 style selections. Style selection took
about 144 ms of the roughly 300 ms DOM-to-box stage. Those are historical counts
and timings, not a measurement after all the recent changes.

The original allocation description was too broad. In
[`variables.c`](../../contrib/libcss/src/select/variables.c), child
contexts reference their finalized parent; parsed values are reference-counted.
An element with no local declarations discards its temporary empty context and
shares the parent's finalized context. This architecture predates the assessment.

Allocations remain for locally cascaded variable names and finalization scratch
storage. Broad selectors declaring custom properties on many elements can still
generate substantial local work. A large cascade-call count alone does not show
that inherited tables are being duplicated.

Earlier work also skips stylesheets/rules without custom properties, shortens
import scans, grows class lookup tables selectively and uses stack scratch for
small selector lists. Partial native checked-state updates retain unaffected
styles. The fresh profile below separates rule selection, local collection and
finalization. It supports work on repeated local builders, while retaining the
immutable parent/value sharing architecture.

### Reflow and smaller costs

The default load policy still allows an initial layout, suppresses intermediate
reflows while dependent objects load, and performs a final full reflow. Caching
now reduces the repeated text work, but the old 220–290 ms HotNews final-pass
figure should not be quoted as its current cost. The table above measures combined
initial layout calls, not the final pass alone.

Retained boxes and eligible inline-layout reuse for partial updates are already
implemented; see [`partial-updates.md`](partial-updates.md). They do not remove
all full-page layout work. Revisit load-time reflow policy only if current traces
show a material cost or a responsiveness problem.

The Qt scheduler still rescans an unsorted list after callbacks. Queue sizes did
not justify replacing it in the original investigation. Font-metrics setup is
another small candidate to remeasure. HTTP/2 is already requested by the curl
backend; asynchronous DNS depends on the linked libcurl build, so the original
machine's DNS result is not a guarantee for every deployment.

## 6. Fresh Release profile after the polling migration

Measured on 2026-10-02, including the uncommitted polling migration, before the
deferred pseudo-variable change in section 7. The baseline uses the Release/LTO capture build with
aggregate restyle profiling and no PERF trace logging. Detailed probes exist
only in an ignored, separate source snapshot; no new production instrumentation
or behavior switch was added.

Method: one CPU affinity (CPU 4), Qt offscreen at 1280x900 and 96 DPI, JavaScript
disabled, fresh process/settings/cache per capture, and live requests rejected
through a localhost:9 proxy. Six saved pages and four synthetic paragraph sizes
were measured. Each case has one warmup per mode, then five baseline captures,
three coarse-probe captures and three detailed-probe captures, with mode order
alternating. OS file/font caches are warm; these are not cold-machine timings.

The coarse probe times substantial scopes and counts inner calls without timing
the hottest variable-cascade operations. The detailed probe additionally times
those operations. This distinction matters: HotNews variable collection measured
about 63 ms with coarse probes; detailed per-declaration clocks push it to about
86 ms. Use baseline CPU times for layout performance and coarse probes for the
CSS breakdown. Probe layout CPU totals were within approximately 3.3% of baseline
on the saved pages; this is an overhead check, not a correction factor for CSS.

| Saved page | CSS selection, coarse wall ms | Initial HTML layout, baseline CPU ms | Split-cache-miss bodies, coarse wall ms |
|---|---:|---:|---:|
| HotNews | 222.7 | 137.6 | 35.7 |
| Bihon | 104.4 | 62.8 | 18.3 |
| Tion | 60.0 | 45.9 | 11.3 |
| CNX Software | 13.3 | 44.1 | 24.0 |
| Phoronix filesystem article | 1.8 | 21.5 | 2.9 |
| Phoronix Razer article | 1.6 | 22.1 | 2.1 |

These columns must not be added. CSS selection and text callbacks use monotonic
wall clocks in the probe; the layout column is renderer-thread CPU time from the
baseline. Text fitting is included in layout, and some font/width work occurs
outside HTML layout, including SVGs and initialization. Initial layout totals
both load-time layout calls on each saved page. Capture elapsed time additionally
includes image/font loading, painting, a 100 ms settling wait and the capture
observer's cadence, so it is not the CPU-stage breakdown.

### HotNews: repeated local variable builders are a real cost

The profile confirms 607,795 variable-cascade calls across 2,397 style selections
and 605,746 local bindings across 9,588 element/pseudo-element environments.
HotNews's saved `app.css` declares **63 defaults** on
`*,:before,:after,::backdrop`. Applying that rule to four environments for each
styled element accounts for 604,044 local declarations alone. The repeated work
is explicitly declared local values; it is not duplication of the inherited
parent table.

Coarse timings:

- Variable-rule collection and cascading: **63.4 ms**.
- Environment finalization: **30.6 ms**.
- Combined collection/finalization: **94.0 ms**, about 42% of CSS selection time.
- Ordinary stylesheet selection/cascading: **50.5 ms**. This includes ordinary
  properties that resolve `var()`; variable resolution is a nested cost.

The variable implementation makes approximately **752,000 allocation requests**
during this load, including a separate candidate allocation for each new local
binding. The probe counts around 134 MiB of cumulative requested allocation
sizes, including realloc requests. This is **not resident memory or memory
retained per page**; much is temporary cascade/finalization storage.

The follow-up in section 7 measures which declaration sets repeat and which
pseudo environments actually supply a value. It avoids unused construction
before introducing an allocator optimization or shared-result cache. Parent
environments and parsed values remain immutable and shared.

### Tion and Bihon: ordinary rule selection dominates variable processing

Variable collection/finalization costs **11.4 ms** on Tion and **23.0 ms** on
Bihon, while ordinary stylesheet selection/cascading costs **37.5 ms** and
**61.9 ms**, respectively. They finalize only 52 and 51 local bindings across the
whole document; parent sharing is already effective. HotNews's variable-storage
allocation pattern is therefore not a general explanation for every site's CSS
cost. General selector candidate enumeration/matching is a separate follow-up
if these pages become the priority.

### Long paragraphs: confirmed scaling issue, outside this saved-site workload

No saved page submits a split request longer than 1,078 bytes; HotNews's maximum
is 198 bytes. There are no over-4096-byte split requests on these pages. The
saved Phoronix articles each contain only about 700 words, so separate synthetic
fixtures were used to exercise genuinely long uninterrupted text nodes.

Each synthetic fixture has one ASCII paragraph, a 640 px content width and
16 px/24 px sans-serif text. It uses the normal 512 KiB text cache. There is one
layout call per fixture. Baseline medians:

| Paragraph size | Layout CPU ms | Bytes passed to split callbacks across the paragraph |
|---|---:|---:|
| 2 KiB | 7.7 | 26,790 |
| 4 KiB | 22.3 | 105,085 |
| 8 KiB | 77.6 | 416,200 |
| 16 KiB | 271.1 | 1,656,614 |

Doubling paragraph length nearly quadruples the cumulative suffix bytes handed
to fitting. Each new line measures the full remaining suffix before finding its
break, and these first-pass suffixes are distinct cache keys. This explains the
near-quadratic trend even below the cache's 4096-byte admission limit. Increasing
the cache budget does not address that cause. At 16 KiB, split-miss work accounts
for roughly 267 ms of the 271 ms layout cost. This is a valid future optimization,
but the synthetic results should not be presented as current saved-site delays.

### Evidence and reproduction

All **110 measured captures**, plus 30 warmups, completed successfully. Within
each fixture, all measured baseline/coarse/detailed outputs match exactly in PNG,
DOM and normalized box trees. No browser processes were left running.

Evidence: `out/render-captures/fresh-profile-2026-10-02/` contains `results.json`,
`method.json`, per-capture logs and images, the source snapshot, the temporary
instrumentation patch/header, and the build logs. `prepare_probe.py` recreates
the isolated source from the current working tree; the exact CMake configuration
is in `method.json`. Build with Ninja's default parallelism, then run
`run_profile.py` after all builds finish. The script runs captures sequentially,
compares output hashes and records the raw samples and medians.

## 7. Deferred pseudo-element variable evaluation: implemented

The follow-up found **48 distinct ordered custom-property rule sets** on HotNews.
Each set includes matched stylesheet declaration identity, origin and specificity;
this count does not imply the completed environments have identical inherited
values. All 7,191 pseudo-element sets contain the same universal defaults, but
only **11** of those pseudo environments are consumed by an ordinary `var()`
property. The other pages do not have this universal-variable pattern.

Selection now saves references to matched pseudo declaration blocks. The first
ordinary property using `var()` builds and finalizes that pseudo's environment,
then later uses share the completed result within that selection. It replays
all matching declarations, including ones later than the consumer, preserving
source order, origin, specificity and importance. Unused environments are never
constructed. Ordinary pseudo styling still runs, including literal generated
content; this is not conditional on visibility or a particular pseudo type.
Base element environments are still finalized eagerly for child inheritance.

This is a general evaluation change in `libcss/src/select/select.c`, enabled
unconditionally. The pending records live only for one selection and borrow
stylesheet data that remains alive throughout it. There is no persistent cache,
new invalidation mechanism or generator/bytecode change. It preserves the
[custom-property inheritance and computed-value rules](https://www.w3.org/TR/css-variables-1/).

Paired Release/LTO results, medians of five alternating before/after runs after
a warmup for each build, using the same capture settings as section 6:

| HotNews measurement | Before | After |
|---|---:|---:|
| Custom-property declaration applications | 607,795 | 155,455 |
| Local variable bindings built | 605,746 | 153,406 |
| Variable collection and finalization | 96.6 ms | 41.7 ms |
| Entire CSS selection | 230.5 ms | 175.9 ms |
| Variable-storage allocation requests, including pending rule records | 751,965 | 201,503 |

Declaration applications decrease by **74%** and total CSS selection time by
about **24%**. The remaining time includes normal selector/cascade work and base
element variables; this does not claim a 24% improvement in total page loading.
The uninstrumented captures measured 707 to 668 ms median settled-capture time,
including the configured 100 ms wait and capture-observer cadence. Their layout
CPU time stayed near 136 ms, as expected for a CSS-selection optimization.

The paired CSS selection medians for Tion, Bihon, CNX and the two Phoronix articles
were respectively 61.4 to 60.7, 109.3 to 111.2, 15.3 to 14.8, 2.0 to 1.8 and
1.6 to 1.8 ms. These small variations do not indicate a consistent improvement
on pages without repeated pseudo defaults. An earlier probe batch had several
noisy samples; the paired results above supersede those timings.

All **144 measured captures** across the investigation, uninstrumented
comparison and two probe comparisons have identical PNGs, DOM dumps and
normalized box trees within each of the six saved pages. The 85 selected CSS,
layout, construction and Qt interaction/restyle tests pass across the full run
and a focused rerun. The full run exposed an existing parent-node-data leak in
the `layout_calc_test` helper; its cleanup is fixed and the rerun passes with
LeakSanitizer enabled. New variable tests exercise late declarations, specificity,
origins, `!important`, `revert`, inherited aliases, cycles, independent pseudo
scopes and imports.

Evidence: `out/render-captures/variable-reuse-2026-10-02/` contains raw runs,
`reuse-summary.json`, `comparison.json`, `paired-summary.json`, captured output,
the isolated instrumentation and before/after probe binaries, and test/build
logs. Profiling code exists only in the ignored probe copies. Reproduce the
paired comparison with `run_paired.py` after all builds/tests have finished.

## Recommended next work

1. The unused pseudo-variable work is addressed. Before adding variable-result
   caching, profile the remaining element setup and ordinary CSS work separately;
   identical local declaration sets alone do not prove safe result reuse.
2. Address long-paragraph suffix measurement if long articles or synthetic scaling
   become the next priority; it is confirmed, but does not explain the saved-site
   timings above.
3. Investigate ordinary selector work on Tion/Bihon only as a separate measured
   optimization. Scheduler replacement and broader layout/scroll reuse remain
   lower priorities.
4. Validate the migrated event loops on native Windows and Haiku, and complete
   GTK build/runtime checks when those frontends are in scope. The original four
   Qt performance priorities are complete.

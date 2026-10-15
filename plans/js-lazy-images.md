# Tion and Bihon: bounded JavaScript image-loading work

Started 2026-10-14; bounded goal verified complete 2026-10-15. This batch belongs to the
[JavaScript migration](js_migration_analysis.md).

## Finish line

Article images appear on initial load and as the reader scrolls on both sites,
with correct sizing and completion state. Test desktop and narrow viewports,
slow and cached images, failures, and navigation during a pending load. No
site-name exceptions or automatic promotion of `data-src` attributes.

Other broken scripts remain outside this batch unless a reproduced failure
prevents the article image loader from running. Advertising, analytics, location
APIs, full framework support and general observer support are not goals.

## Reproducible starting point

The 2026-09-28 snapshots' `original.html` retains the lazy-image markup and script
URLs. Their `index.html` deliberately removes scripts for static rendering tests;
keep those baselines unchanged. Both sites currently serve byte-identical
lazySizes 4.0.3, preserved with provenance in `test/js-lazy-images/vendor`.

The loader immediately exits without `document.getElementsByClassName`. Its
normal path also needs geometry, computed style, viewport dimensions, image
properties and completion events. This version uses scroll/resize listeners and
has timer fallbacks for optional observers and animation-frame scheduling. It
does not require IntersectionObserver. Missing APIs must remain absent until
their behavior is real; do not select an unsupported branch with dummy objects.

## Implementation sequence

1. **Loader initialization.** Add Document/Element `getElementsByClassName` on
   the existing live collection implementation. Native class queries split on
   ASCII whitespace, match every requested token, exclude the root, and honor
   document quirks mode. Add standards-defined `Document.createEvent` aliases
   for interfaces already implemented, reusing the existing event lifecycle.
   Test conversion, liveness, ownership, dispatch and document retirement.
2. **Geometry and viewport.** Centralize safe layout synchronization and expose
   `getBoundingClientRect`, required offset metrics, `innerWidth/innerHeight`
   and document viewport measurements. Use CSS pixels and the existing box,
   fragment, scroll and transform calculations. A JS read after a mutation must
   see current layout; avoid a full layout for every read in a loader scan.
   Validate detached/hidden elements, scrolling and narrow viewports.
3. **Resolved style reads.** Provide read-only, live `getComputedStyle` backed by
   libcss and shared property serialization. Support the loader's visibility,
   opacity and overflow decisions through actual style data, not hard-coded
   answers. Preserve inline CSSStyleDeclaration ownership and generated property
   metadata. Specify disconnected/pseudo-element behavior and conversion errors.
4. **Image requests and state.** Implement the HTMLImageElement properties used
   by the loader (`src`, `srcset`, `sizes`, `complete`, `naturalWidth/Height`),
   resolving URLs and reflecting attributes through the normal DOM mutation
   path. Connect image resource completion to native DOM load/error events and
   truthful request state. Follow HTML's image-request lifecycle: changing URLs,
   cache hits, failures, removed nodes and navigation cannot deliver stale events.
   Separate element request ownership from transient layout boxes where needed.
   Use the existing responsive-image selection/sizing machinery and correct
   underlying issues exposed by tests.
5. **Scroll/resize and real loader.** Queue/coalesce native document/element
   scroll notifications at a safe main-thread task boundary. Dispatch standards
   events without reentering layout/painting. Reuse the existing event/timer and
   teardown machinery. Run the unmodified preserved loader against reduced
   Tion/Bihon markup with local image resources; test initial and scrolled loads,
   auto sizes, cache hits, failed images and repeated navigation.
6. **Both sites and closure.** Run scripted variants of both saved pages plus
   live probes. Fix only image-loading blockers uncovered there. Record loaded
   image counts, final geometry, completion/events, outstanding requests and
   scroll screenshots. A timer firing or a placeholder box is not success.
   Retain deterministic regression tests and remove temporary probes. Document
   unrelated errors without adding them to this batch.

Steps may overlap where they share an engine lifecycle, but each should remain
reviewable and tested. Keep rendering and DOM execution on the main thread.
Consult historical Duktape bindings for intent; standards and current ownership
rules determine the implementation.

## Validation and sources

Native libdom and QuickJS tests cover semantics and lifetimes; Qt capture tests
cover rendered pixels, image resources and actual scrolling. Run Release plus
ASan/UBSan debug coverage for touched lifecycle code. Chromium comparisons are
diagnostics, not normative expectations. Log JS attribute writes and resource
transitions clearly enough to distinguish them from static rendering failures.

- [DOM class queries](https://dom.spec.whatwg.org/#dom-document-getelementsbyclassname)
  and [event factories](https://dom.spec.whatwg.org/#dom-document-createevent).
- [CSSOM View](https://drafts.csswg.org/cssom-view/) for viewport, geometry and
  scroll delivery.
- [CSSOM resolved values](https://drafts.csswg.org/cssom/#resolved-values).
- [HTML images](https://html.spec.whatwg.org/multipage/embedded-content.html#the-img-element)
  and [image processing](https://html.spec.whatwg.org/multipage/images.html).

## Progress

### 2026-10-14: loader initialization implemented

- Document/Element class queries use live HTMLCollection wrappers and libdom's
  shared descendant walk/class cache. Tests cover multiple tokens, duplicates,
  ASCII whitespace, namespace elements, root exclusion, document modes, adoption,
  DOM edits, conversion, GC and retirement.
- `createEvent` implements the DOM alias table for our exposed Event, CustomEvent,
  UIEvent and MouseEvent interfaces. Unknown/unexposed interfaces throw
  NotSupportedError. Events start uninitialized and untrusted; dispatch checks
  initialization before changing their target.
- Fixed native class-cache parsing to preserve embedded NUL bytes in DOM strings
  and avoid releasing an uninitialized interned-string slot on allocation failure.
- Historical Duktape `Document.bnd` was consulted: its factory always made a base
  event, so that behavior was not copied. Current factory semantics follow DOM.
- The unmodified preserved loader now initializes in a QuickJS test, receives a
  live image collection, and dispatches/cancels its actual custom event helper.
  This checks the loader's foundations, not rendered-image completion.
- Release: all 227 QuickJS checks plus class/event/tree/clone native suites pass.
  Debug: the same suites pass ASan/UBSan/LSan; Qt clone/style/DOM rendering suites
  also pass (Qt leak detection disabled for existing platform allocations).
- `test/js-lazy-images/loader.html` is a reduced rendering fixture with initial
  and below-the-fold images, the real loader, placeholder `src`, `data-srcset`
  and auto sizes. The initial foundation-only capture stayed on placeholders;
  the subsequent request-state work below allows them to be replaced.

### 2026-10-14: element-owned image requests and completion

- Image requests now belong to their DOM element and reuse the existing object
  list and box-rebinding paths. Parser discovery and layout share one request
  record instead of separate speculative/layout consumers. Backgrounds and
  embedded objects keep their existing resource paths.
- HTMLImageElement exposes reflected `src`, `srcset`, `sizes`, plus `complete`,
  `currentSrc` and natural dimensions. URL-valued reflections use USVString;
  `sizes` remains DOMString. Shared URL normalization preserves embedded controls
  by escaping them rather than truncating/discarding them.
- Source changes made by JS join its FIFO microtask checkpoint. Success/error
  notifications run as separate native element tasks, with trusted Event objects
  and no Window-load target override. The current image remains available while
  a replacement is pending; obsolete requests cannot deliver stale events.
- Document completion waits for image events and the final reformat. Images can
  download while initial layout proceeds. Detached images in the active document
  can load; navigation cancels their queued work. Disabled images stay unavailable
  and emit neither load nor error.
- Request state is preserved across box rebuilds. Rebinding a failed image must
  not request another restyle; otherwise alt-text fallback loops. Late pixel
  decode failures also invalidate already-settled images, close their presentation
  and release the failed cache consumer outside its callback.
- A loopback HTTP Qt test checks delayed requests, cached/same-URL replacements,
  coalesced source writes, superseded requests, failures/recovery, detached images,
  navigation, disabled images, final intrinsic geometry and completion counts.
  Dynamic-image pixels must match equivalent static markup.
  Its pass/fail result is rendered into the captured DOM, so Release logging is
  not required. Native coverage includes image brands/reflection, partial versus
  complete availability, late decode failure and existing resource binding tests.
- Verified in Release and Debug: all 228 QuickJS checks; object binding,
  fallback and image-cache native tests; Qt image lifecycle, image fallback,
  deferred decode failures and base-URL resources/submission. Native Debug tests
  pass ASan/UBSan/LSan; Qt uses ASan/UBSan with leak detection disabled for existing
  platform allocations. The broader Debug JS lifecycle/click/navigation/clone/
  style/DOM and image sizing/object fallback captures also passed.
- Made the base-URL test verify script execution through its captured DOM instead
  of a console message that is intentionally absent from Release builds. The
  decode-failure resource-count assertion now expects one IMG consumer, not the
  removed speculative/layout duplicate.
- The reduced unmodified lazySizes page now replaces placeholders with real
  images and reaches `lazyloaded`. It still chooses the eager fallback because
  Window `onscroll` is absent. Both image boxes are 320×240 and outstanding
  resources reach zero; the first image was visually inspected. Diagnostic
  capture: `out/js-lazy-images/request-milestone` (ignored). This is **not**
  evidence of correct scroll loading.

### 2026-10-14: responsive source selection and natural dimensions

- Replaced the fixed 800px selector with HTML source-set parsing: width/density
  descriptors, malformed-entry recovery, commas inside URLs, default `src`
  participation, duplicate density ordering and width-to-density normalization.
  Selection uses the smallest density meeting a 1x target, otherwise the largest
  available candidate. The 1x target is an explicit bandwidth-oriented UA policy;
  it does not misrepresent the selected source's actual density.
- `sizes` uses a small libcss entry point sharing its lexer, typed `calc()`
  grammar, unit conversion and media-condition matcher. No fake stylesheet or
  JS-only parser is involved. CSS lengths, initial-font units, viewport units,
  supported math/media syntax, invalid-entry fallback and default 100vw are
  tested. `data-sizes=auto` remains the script loader's responsibility; once the
  loader writes a pixel `sizes` value it uses this same parser. Native
  `loading=lazy`/eligible `sizes=auto`, additional math functions and broader
  media-feature support are not added by this increment.
- Source selection obtains the actual frontend viewport even before initial
  layout, using a shared read-only viewport query. It fills a local unit context
  and does not overwrite an in-progress layout's container/root style context.
  The previous viewport helper's uninitialized width/height fallback is removed.
- Selected density lives on the element's image request. Natural dimensions,
  layout and object-fit use density-corrected sizes; the shared decoded image
  cache stays independent of per-element choices. Same-URL density changes
  update presentation without downloading the bitmap again. Intrinsic aspect
  ratio stays independent of rounded natural lengths. Zero source size produces
  zero natural dimensions; an empty object is not painted as a failed image.
- Image state lookup now uses borrowed DOM user data, avoiding repeated scans
  of the object list. The object list still owns each request and retains its
  node. Tests verify clones do not inherit the lookup and destruction clears it;
  teardown does not clear a replacement lookup owned by another document.
- Source-size tests exposed a shared libcss range-query error: name-first ranges
  read the name as their value and reversed strictness when swapping operands.
  The parser now handles the correct operand and keeps `<`/`>` distinct from
  `<=`/`>=`. Boundary and chained-range cases follow
  [Media Queries 4](https://www.w3.org/TR/mediaqueries-4/#mq-range-context).
- Verified 97 source/sizes checks, all 228 QuickJS checks, image ownership tests
  and the shared CSS declaration suites in Release and ASan/UBSan/LSan Debug.
  Qt HTTP tests cover desktop/narrow source choice, shared bitmap/different
  density, changed density, natural dimensions, empty objects, object-fit,
  resource counts and completion. Rendered pixels match equivalent explicit-size
  references. Release and Debug image fallback, decode failure, absolute and
  flex image sizing captures pass; Debug base-URL, aspect-ratio and clone captures
  also pass. Qt leak detection remains disabled for existing platform allocations.

### 2026-10-14: synchronous layout foundation and client dimensions

- Added one engine entry point for synchronous layout reads. It finishes the
  existing converter, including restyle fallback/restart, without pumping the
  scheduler, resource callbacks or JavaScript checkpoints. Dirty documents reuse
  the ordinary restyle implementation; unchanged reads reuse settled layout.
- Parser-time reads build a provisional presentation. It does not destroy the
  parser, publish iframe windows, send READY/DONE or unlock an ongoing initial
  conversion. Conversely, a read during unlocked HTML data delivery restores
  that unlocked state so the download-completion callback can begin conversion.
  Parser edits and completed stylesheets invalidate the provisional presentation.
  Normal conversion retires its bindings and CSS node caches before rebuilding.
- Added real `Element.clientWidth/clientHeight`: padding-box dimensions, inline
  and absent-box handling, document viewport rules, scrollbar exclusion and CSS
  pixel conversion. These follow [CSSOM View](https://drafts.csswg.org/cssom-view/#dom-element-clientwidth).
  The rest of the geometry API is still pending; these two properties do not
  claim completion of step 2.
- Geometry-induced construction does not execute queued image updates early.
  The HTTP image test now verifies that a measurement between `src` assignment
  and the microtask checkpoint retains the previous complete image and source.
  Measurements in image-load listeners also see the current image dimensions.
  Consuming a pending reformat still publishes extents and checks page completion;
  canceling the scheduled callback must not strand the document in READY.
- UA stylesheets now participate in the classic-script readiness gate because
  they are loaded through the cache and must exist before synchronous layout.
  This adds no event-loop pumping inside a script.
- New Qt tests cover parser-time reads, same-entry inline-style changes, later
  parser stylesheets/elements, detached and hidden elements, DOMContentLoaded
  before box-conversion completion, post-load restyles, and a page without author
  CSS. Reads preserve readiness/event ordering; final pixels equal a static
  reference. Debug tracing also verifies 100 unchanged reads perform no layout.
- Verified the new geometry and HTTP image captures in Release and ASan/UBSan
  Debug, plus object ownership/fallback and all 97 source-size checks in both
  builds (Debug with leak checking). All 228 QuickJS checks passed in both builds.
  Debug script ordering, lifecycle, clone, inline-style and DOM captures passed.
  The older script-order/lifecycle/DOM capture tests depend on console output
  suppressed in Release; their Release failures are test-oracle limitations,
  not a pass claim. The new geometry/image tests use DOM state and pixels instead.

### 2026-10-14: bounding rectangles, offsets and Window dimensions

- Added DOMRectReadOnly/DOMRect value objects and Element.getBoundingClientRect,
  using the current layout through the same synchronization path. Rectangle
  objects are snapshots, support their standard constructors/conversions and
  retain no layout pointers. HTMLElement offsets use untransformed border
  fragments and DOM ancestry for offsetParent, including inline ancestors.
- Shared transform calculation between painting and measurements. Percentages
  and the default transform origin now use the border box, and length units
  such as em go through the existing unit converter. Non-replaced inline boxes
  are not transformed. Measurements account for ancestor scroll, viewport scroll,
  fixed/sticky positioning, nested transforms, and wrapped/split inline fragments.
  Zero-area fragment union rules have a native regression test.
- Added Window innerWidth/innerHeight and scrollX/scrollY/pageXOffset/pageYOffset,
  in CSS pixels, with Web IDL replaceable-property behavior. Qt provides a
  viewport query including visible native scrollbars; client/layout dimensions
  stay separate. Core-managed frames distinguish scrollbar space too. Other
  frontends retain their layout-dimension fallback until they implement the
  optional viewport callback; their scrollbar-inclusive values are not validated.
- Native QuickJS: all 229 checks pass in Release and ASan/UBSan/LSan Debug,
  including allocation-failure registration tests. Native fragment tests pass
  in both. Qt rectangle/offset/scroll, synchronous-layout, HTTP image lifecycle
  and transform pixel tests pass in both builds. Debug fixed-position and
  positioned-stacking captures also pass. Qt leak detection is disabled for the
  existing platform allocations. These tests cover HTML box geometry; this work
  does not add SVG DOM geometry or the getClientRects/DOMRectList surface.

### 2026-10-15: resolved styles and native viewport events

- `getComputedStyle` now returns live, read-only CSSStyleProperties backed by
  libcss. Opacity, visibility and both overflow axes/shorthand use selected
  values and shared serialization. Canonical longhand names come from generated
  metadata. Invalid/unsupported pseudos and disconnected elements return empty
  declarations; valid hidden descendants receive inherited style selection.
  Known properties without a value serializer report NotSupportedError instead
  of inventing a computed value. This is bounded computed-style support.
- Fixed hidden-node reselection through the existing CSS-node invalidation API;
  display:none nodes can already own selector caches even without boxes. Mixed
  overflow axes now compute according to CSS Overflow before layout consumes
  them. Added pseudo-tokenization, fixed-number boundary, read-only conversion,
  liveness, GC/retirement and generator coverage.
- Native viewport/element scrolling and viewport resizing queue coalesced events
  at a main-thread task boundary. The queue retains DOM nodes, not layout boxes,
  and is canceled on stop/close/destruction. Document scroll bubbles; element
  scroll and Window resize do not. Native events are trusted and noncancelable.
  Initial and later HTML layout establish one shared resize baseline.
- Window `onscroll`/`onresize` are real event-handler attributes: replacement
  preserves listener order, null removes, non-object conversion follows Web IDL,
  noncallable objects do nothing, and handler storage participates in GC/teardown.
  Exposing `onscroll` now selects lazySizes' normal viewport path. There is no
  feature switch, site exception, or eager data-src promotion.
- Qt and Monkey notify the core when their viewport offsets change; core-managed
  frame scrollbars and HTML overflow scrollbars notify it too. Other frontends'
  native UI scroll notifications have not been migrated/validated in this batch.
  Historical Duktape Window.bnd only declared these handlers; its EventTarget
  bridge informed the shared dispatcher, not the new handler semantics.
- Qt capture inspection now reports each requested IMG's complete/currentSrc
  and density-corrected natural dimensions. It reads native state without JS or
  layout side effects, and remains in the existing optional capture build.
- All **234 QuickJS checks pass in Release and ASan/UBSan/LSan Debug**, including
  allocation-failure and retirement coverage. The full Debug run used Check's
  no-fork mode to avoid its short per-test alarm on the allocation sweep.
  The computed-style, geometry, bounds and new view-event Qt tests pass; native
  CSS serialization/generator and affected layout suites pass too.
- The unmodified loader's deterministic HTTP test passes in Release and Debug:
  initial versus distant images, real scroll notifications, resize auto sizes,
  delayed downloads, cache reuse, failed resource completion and hidden/clipped
  images. Existing HTTP request replacement, stale events, detached images,
  disabled images and navigation tests still pass. Qt sanitizer runs disable
  leak detection for existing platform allocations; native tests retain it.

### Site validation and batch boundary

The full saved-page check is `test/js-lazy-images/check_sites.py`. It creates
separate scripted variants from the localized static snapshots, adds the pinned
unmodified image loader and traverses overlapping viewports to the bottom.
It checks native image dimensions and completion, not just lazyloaded classes.
The snapshots' transparent 1×1 PNGs are local files too and are explicitly
excluded from successful article-image counts. Static `index.html` files remain
unchanged. Run with a capture-enabled Qt binary and a fresh output directory:

```sh
python3 test/js-lazy-images/check_sites.py /path/to/wisp-qt --output /tmp/wisp-lazy-sites
```

The completed Release sweeps cover 183 viewport checkpoints. No visible article
image was missing, no failed image requests remained, and every checkpoint had
zero outstanding resources. All four pages reached load completion.

| Saved page | Viewport | Real images initially | After full scroll | Checkpoints |
| --- | --- | ---: | ---: | ---: |
| Tion | 1280×900 | 5 | 69 | 37 |
| Tion | 390×900 | 2 | 56 | 40 |
| Bihon | 1280×900 | 4 | 114 | 45 |
| Bihon | 390×900 | 2 | 105 | 61 |

Initial counts depend on the loader's preload timer; final counts differ by
layout because hidden sidebars/duplicates are not visible articles. Results,
per-image native state and screenshots are in the ignored
`out/js-lazy-images/site-validation/` directory. `--check-existing` reruns the
same assertions against those saved captures without reopening Wisp.

Live **unmodified** Tion and Bihon homepages also completed at both widths, with
initial and 1200/2400/4000px scroll checkpoints and zero pending resources.
The final desktop DOMs contained 27/30 lazyloaded images (Tion/Bihon); narrow
DOMs contained 10/12. These live counts are transient content diagnostics, not
test expectations. Screenshots were visually checked. Separate live articles
on each site also completed; initial hero images and Bihon's lazy sidebar images
were visually checked. These captures are under `out/js-lazy-images/live-*`.

Live Debug probes exposed unrelated jQuery initialization and downstream theme,
weather/advertising script exceptions. The image loader still runs independently.
Those errors remain outside this batch. Verbose Debug probes initially exhausted
their 45-second capture limit; optimized runs without layout tracing completed.
Timing comparisons between those configurations would be misleading.

**The original image-loading goal is met for these tested pages.** Do not expand
this batch into general site-script compatibility. The implementation, regression
tests and validation notes form the completed image-loading batch.

### Explicit limits outside this batch

- Computed serialization covers the loader's properties, not every CSS property
  or custom property. Synchronous replacement of `<style>` text still uses the
  pre-existing asynchronous stylesheet path. Tested same-entry reads cover
  inline attributes/classes and parser-loaded sheets, which this loader uses.
- Native `loading=lazy`, picture/source art direction, observer APIs, complete
  CSSOM serialization, and full event-handler IDL coverage are separate work.
  Responsive selection reacts to source/sizes mutations, including lazySizes'
  resize callback. Automatic environment-only reselection of an unchanged
  srcset/sizes pair is not introduced here; HTML permits deferring that algorithm.
- Unrelated jQuery/theme/advertising script failures are not prerequisites for
  these images and do not expand this batch. No general web-app support claim.

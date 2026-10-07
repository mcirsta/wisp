# Wisp JavaScript state and Duktape reuse

Reviewed 2026-10-07 against Wisp `206b8a03`, plus the QuickJS-ng update and
XHR conversion fix described below. This replaces the earlier migration analysis.
It is a browser-integration review, not an exhaustive security or ECMAScript
conformance audit. Batches 1–2 and four increments of batch 3 are implemented
within the scope described below. Real DOM lookups, class lists, attribute/text
mutations and basic node creation/tree edits now reach rendering and carry JS action
traces. Live child-node/element collections and script-dispatched events on DOM nodes,
Document, Window and standalone EventTarget are implemented, along with the core
AbortController/AbortSignal API and signal-bound listener removal. Native
input/lifecycle delivery and the other capabilities below remain future work.

## Assessment

Keep QuickJS-ng and libdom. The missing part is the browser binding and execution
layer between them, rather than the JavaScript language engine. The current
integration executes scripts, but most exposed Web APIs are placeholders. A
modern syntax upgrade cannot make those placeholders behave like a document.

We can build a useful, bounded JS implementation for a reading browser without
changing the renderer's threading model or implementing all web-app APIs. The
right boundary is a small set of correctly implemented browser capabilities,
with execution limits and reliable teardown. Expanding the number of functions
that silently do nothing makes failures harder to diagnose and can defeat sites'
feature detection.

## Engine update completed

QuickJS-ng is now **v0.17.0**, upstream commit
`6d46d07d04041b40f4f49eaa7fdebe44c314c699` (latest stable when checked).
The previous source was a post-v0.11.0 snapshot, not exactly the v0.11.0 tag.
See [upstream release](https://github.com/quickjs-ng/quickjs/releases/tag/v0.17.0)
and [vendor provenance and local patches](../contrib/quickjs-ng/README.wisp.md).

Preserved document-job cancellation and Wisp's CMake integration. Removed the
obsolete const-pointer casts and C11/x87 adjustment now addressed by upstream.
Imported matching generated builtin headers and bytecode. Wisp's own
Window/Console generator still runs from its normal source files.

Validation:

- Wisp Qt and `test_quickjs` built with Debug ASan/UBSan; the integration suite
  passes, including mixed Promise/queueMicrotask cancellation, GC/finalization,
  and the new XHR conversion regression.
- `qt_js_navigation_cleanup` passes for form navigation and window shutdown.
- Vendored C API and regexp tests pass in both sanitizer and Release builds.
- Upstream local JS suite: **116 tests, 0 errors, 9 excluded** in both builds.
  This is not the separate, full ECMAScript test262 suite.
- Standalone Release CLI/compiler build reports version 0.17.0; the generated
  `function_source` executable passes.
- Local Qt page probes confirm the integration findings below. GTK and native
  non-Linux builds were not tested.

A probe also found an existing crash in `xhr_open`: `JS_ToCString` can throw and
return NULL, which was passed to `js_strdup`. Fixed both argument conversions,
cleanup and allocation failure handling before changing request state. A test
checks that each argument's original exception survives and the object remains
usable. This does not add XHR networking.

## Current capability map

Paths in this section are relative to `src/content/handlers/javascript/quickjs/`
unless stated otherwise.

| Area | Actual state | Consequence |
|---|---|---|
| JavaScript language | Real QuickJS execution via `JS_Eval`, modern syntax and engine builtins | Useful base; host services are separate |
| Runtime/document lifetime | `quickjs_bindings.c`: runtime per browser window, context per document, reference-counted runtime lifetime and closed-document guards | Keep this basic ownership structure; `jsthread` is not an OS thread |
| Resource limits | 64 MiB runtime allocation limit, 1 MiB JS stack limit and monotonic execution deadline using `script_timeout` (default 10 seconds) | Cooperative interruption and recovery are tested. Native DOM allocations are outside the JS heap limit, and arbitrary native work is not preempted |
| Promise/async continuations | The outer script or timer entry drains FIFO jobs under the same deadline; aborted entries discard their pending jobs | Promise, `await` and engine `queueMicrotask` callbacks progress; this is not yet a complete browser event loop |
| Timers | `timers.c`: per-document ordered queue on the frontend scheduler, callback arguments/string handlers, shared cancellation IDs and nesting delays | Real `setTimeout`/`setInterval`; callbacks and their checkpoints share execution limits; document retirement cancels them |
| JS diagnostics | Optional `js_trace` records document/entry IDs, script sources, timer causality, navigation, exposed node IDs, actual DOM operations and coalesced render requests | Separates supported JS host effects from static processing; this is not complete paint attribution |
| Document/nodes | `dom.c`/`document.c`/`tree.c`/`collections.c`: real libdom wrappers with stable identity, roots, ID lookup, traversal, live `childNodes`/`children` and element/text/comment/fragment factories | Document-lifetime wrapper ownership and native reference cleanup are tested; selectors, document-wide collections, namespace-aware creation and cross-document operations are not exposed |
| Mutation/CSSOM | Real attributes, `id`/`className`, live `classList`, text/data and append/insert/replace/remove; connected edits request a full restyle after the entry/checkpoint | Changes affect boxes and pixels, including moves into detached trees; `style` and `innerHTML` remain omitted |
| Events | `event.c`/`event_target.c`: real Event/CustomEvent values and script-dispatched listeners on DOM nodes, Document, Window and standalone EventTarget | Browser-generated input, handler attributes and lifecycle delivery remain pending; `js_fire_event`/`js_handle_new_element` are still stubs |
| Cancellation | `abort.c`: AbortController, AbortSignal state/reason, throwIfAborted, static abort, onabort and the listener signal option | Listener cancellation uses private abort steps before the public event; timeout, any and network integration remain absent |
| Lifecycle information | `document.readyState` is omitted | Correct loading state and lifecycle events belong to batch 4 |
| Networking/storage | XHR fabricates DONE/200 without a request; storage discards writes; no `document.cookie` accessor or browser fetch binding | XHR/storage still advertise unsupported behavior. Native HTTP cookie handling is a separate subsystem |
| Navigation | `location.c`: real URL getters and calls to navigation for assign/replace | Partial support: href has no setter, reload is a stub, relative URL resolution/history replacement need review; assign and replace currently use the same flags |
| Navigator | Hard-coded Windows Chrome 91 UA, English language and cookies-enabled value | JS identity differs from Wisp's actual HTTP UA and capabilities |
| Generated bindings | `utils/qjs_binding_generator.py` generates Console and a generic Window stub | Not a complete WebIDL binding generator; see below |
| Script loading | Parser-blocking classic scripts wait for fetches and preceding blocking stylesheets; async scripts run in readiness order, defer scripts in document order after parsing | Cached and failed fetches are covered; modules, dynamic script insertion and complete script-element processing remain unsupported/incomplete |
| `javascript:` URLs | `javascript/fetcher.c` returns 204 | Does not execute the URL's code |

### Confirmed host problems, in priority order

1. **Execution controls are now connected.** The outer `js_exec` entry starts a
   monotonic deadline; QuickJS's interrupt callback stops runaway execution.
   Nested entries share the deadline, and subsequent independent entries receive
   a fresh budget. Timer callbacks and microtask checkpoints use this same
   scope: renewing it for every job would let infinite Promise chains evade the
   limit. There is no nested GUI event pumping or new execution thread.

2. **Classic script ordering and checkpoints are connected.** The earlier local
   probe ran inline code before the preceding external script because
   `exec_src_script` deliberately skipped parser pausing. This is corrected,
   together with stylesheet blocking, async/defer order and load completion.
   A real speculative preload scanner is still a separate optimization: stopping
   the actual parser can delay discovery of later resources, but preserves script
   semantics. See the implemented scope and regressions in batch 2 below.

3. **The real document binding is established.** The original probe returned
   false for repeated-root identity and lookup of an existing element ID. Both
   now use actual libdom nodes, with document-owned wrappers, traced JS references
   and native-reference cleanup. This fixes the ownership boundary; it does not
   imply every Node/Element/Document member is implemented.

4. **Supported mutations now reach rendering.** All implemented attribute/text
   setters and tree edits share one invalidation interface. Connected edits advance the DOM
   generation immediately, then coalesce a full renderer refresh after the outer
   entry and its microtasks. Before box construction, the initial build covers
   the changes. Existing partial updates are deliberately not reused without
   mutation/dependency tracking. Resource-loading and element-specific reactions
   still need separate review; this is not complete HTML mutation processing.

5. **Advertised capabilities and tests need to become truthful.** The initial
   probe saw no timer/Promise callbacks; both now run. XHR still reports 200
   without fetching anything.
   Replace placeholders as each capability is implemented; for deferred APIs,
   decide deliberately whether to omit them or expose a specified unsupported
   behavior. Do not keep fabricated success as a compatibility strategy.

The probes are saved under ignored `out/js-review-2026-10-07/confirmed/`.
These are diagnostic observations, not new passing conformance tests. The script
ordering and DOM limitations predated the engine update. Subsequent batches below
address the supported subsets; the original probe directory records the earlier
state, not current expected failures.

### Generator and test findings

`generate_generic_binding()` calls `parser.find(interface_name)` and treats every
named member as a method. It does not assemble a usable binding from base and
partial interfaces, inherited members, attributes, mixins, conversions or
exception rules. In this build it selects the final partial Window declaration,
so generated Window methods are only `captureEvents` and `releaseEvents`.
Handwritten modules then add timers, location and other globals. This explains why
an IDL declaration is not evidence of support.

The old boolean-expression tests have been corrected. `js_exec` still reports
normal completion, not the truth of its JS result; test predicates now explicitly
throw on false, and the helper has its own true/false/undefined/throwing tests.
The formerly passing alert test actually described a missing API. Unsupported
dialogs and placeholder bindings now have a separately named **Stub API surface
(not conformance)** case. These tests must be replaced as real APIs are added.

Initialization now discards an incomplete context and returns failure. Handwritten
registrations use checked QuickJS property tables; Console/Window generator
changes handle allocation and registration failures. A bounded allocation-budget
sweep checks each binding and fresh-context recovery within the same runtime.
It is not exhaustive failure injection across all browser code. These tests found
and led to a fix for QuickJS's own property-list error propagation; see the
[upstreaming notes](../src/docs/quickjs-upstreaming.md).

Console argument conversion now propagates exceptions rather than swallowing them,
including interrupts raised in a page's `toString`. Error diagnostics identify the
script URL (the document URL for inline scripts, with script-relative line numbers)
and report messages and the engine backtrace. The pristine native stack getter is
retained at context creation so reporting never invokes a page's replacement
getter, proxy trap or `toString`. Non-Error thrown objects are left unformatted.
Unhandled Promise rejections are now logged after the checkpoint; handlers added
during that checkpoint suppress the diagnostic. Real PromiseRejectionEvent
dispatch remains part of the event-binding work.

### Batch 1: execution controls and test results

Validated 2026-10-07: Qt Debug ASan/UBSan build, **36 Check tests** in
`test_quickjs`, and the extended `qt_js_navigation_cleanup` capture test pass.
Vendored C API and regexp tests pass in Debug sanitizers and standalone Release;
the local JS suite passes **116 tests, 0 errors, 9 excluded** in both builds.
No full external test262, GTK or native non-Linux run is claimed.

- `script_timeout` defaults to 10 seconds. Nonpositive values explicitly disable
  the elapsed-time limit; allocation and stack limits remain active. The timeout
  aborts the current execution, skipping JS catch/finally, and clears the exception
  before a later entry. Already-completed script effects are not rolled back.
- Checks cover repeated infinite loops, fresh budgets, sibling-document recovery,
  native-to-JS console coercion, safe diagnostics, memory/stack failures, closed
  documents and existing job-cancellation/finalization regressions.
- The Qt navigation regression also serves a runaway script with a one-second
  option value, verifies a later inline script runs, renders and submits a form,
  and checks that retired Promise callbacks never navigate during teardown.
- Batch 1 enabled no new Web API or job draining. Script ordering, checkpoints
  and timers were subsequently implemented in batch 2 below, followed by the
  first DOM increment in batch 3. Event dispatch remains future work.
- QuickJS interrupts are cooperative. Long native operations and page-wide totals
  across many separate scripts are not bounded by this per-entry deadline. Future
  checkpoints must share the outer scope, and new native bindings must propagate
  exceptions and avoid unbounded native work. This is not a complete hostile-code
  sandbox or browser-wide resource audit.

### Batch 2, first part: classic scripts and microtasks

Implemented 2026-10-07, followed by the timer work below.

- Ordinary external classic scripts pause the DOM parser until ready, execute,
  drain their microtasks, then resume parsing. Ready async scripts use a FIFO
  queue populated by fetch completion; `async` takes precedence over `defer`.
  Deferred scripts run in document order after parsing, waiting for an earlier
  unfinished deferred fetch. Cached callbacks only schedule processing; they
  never reenter the parser directly.
- Parser-blocking scripts wait for preceding applicable author sheets, and
  deferred scripts wait for pending applicable sheets, including inline sheets and imports. A cache handle with no
  content yet is still pending, even though `content_get_status()` returns
  ERROR for it. Real stylesheet failures clear the handle and unblock execution.
  Existing stylesheet media/alternate selection limitations are not redesigned.
- Script fetch failure releases the parser/order slot. Non-success HTTP status
  responses do not execute as JavaScript. An empty `type` is classic; modules
  and data blocks cannot accidentally enter the classic external-script path.
  This does not implement modules, all legacy MIME aliases, dynamic insertion,
  CSP/CORS/SRI or the whole script-element preparation algorithm.
- Rendering may begin with pending async/defer resources, but content DONE waits
  for their downloads and queued execution. Load/DOMContentLoaded JS events and
  `document.readyState` remain unimplemented and belong to batch 4.
- The outer `js_exec` drains Promise, `await` and `queueMicrotask` jobs in FIFO
  order, including jobs enqueued by jobs. It retains the original deadline and
  checks between jobs, so chains of tiny jobs cannot evade the interrupt budget.
  Ordinary exceptions are reported without suppressing subsequent jobs.
- A timeout discards that context's pending jobs with `JS_DiscardContextJobs`,
  allowing a later entry to enqueue normally. Permanent document retirement still
  uses `JS_CloseContextJobs`; teardown never drains page callbacks. Active entries
  and jobs retain their document context until return, even if navigation releases
  the document's owner reference. An abandoned parser reaches an error state
  without resuming JS, so cache cleanup can release it before forced shutdown.
  Runtime ownership remains reference-counted.
- Unhandled rejections use the same safe error diagnostics as synchronous errors.
  A handler installed during the checkpoint suppresses the diagnostic. There is
  no PromiseRejectionEvent delivery yet, and callbacks outside the implemented
  script entry points are not a complete HTML task/checkpoint implementation.

Regression coverage: **40 Check tests** in `test_quickjs`, plus Qt
`qt_js_script_order`, `qt_js_navigation_cleanup`, `qt_browser_loading` and
`qt_fetch_events`, using Debug ASan/UBSan. Ordering fixtures exercise real HTTP
fetches, repeated cached scripts, HTTP failures, reversed defer/async readiness,
CSS imports, Promise diagnostics and navigation from a Promise. Unit tests also
check infinite short-job chains, a single runaway job, a shared script/checkpoint
deadline, recovery, queue discard and finalizer suppression. Vendored C API,
regexp and local JS suites are also checked; the latter is not full test262.

The old Duktape `in_use`/deferred-destruction mechanism informed the ownership
review. Its code and timeout behavior were not copied. The implementation uses
QuickJS values/contexts and current HTML ordering rules.

### Batch 2, second part: timers and JS action tracing

Implemented 2026-10-07. Timers run on the existing main-thread frontend scheduler;
there is no new rendering or JS thread.

- Each document owns a stable ordered timer queue and one frontend wakeup. A
  wakeup runs one callback; callbacks queued during a Qt scheduler pass wait for
  its next event-loop turn. This lets the UI progress even when a Promise inside
  a timer keeps creating zero-delay timers, which reset HTML's timer nesting.
- Callable handlers retain their arguments and receive the global object as
  `this`; string handlers execute as global scripts. Handler and delay conversion
  preserve exceptions and their Web IDL order. Timeout and interval IDs share a
  namespace, including cancellation through either clear method. The HTML 4 ms
  minimum applies once the timer nesting level exceeds five.
- Timer records and retained argument arrays use the QuickJS allocator, so they
  count against its memory limit. Cancellation releases retained values; active
  callbacks stay alive until return. Navigation retires pending timers and jobs
  without executing them. Frontend scheduling failure cancels the stranded queue
  and is reported; another independent timer can subsequently be scheduled.
- Scripts and timer callbacks share one entry/checkpoint implementation. An
  ordinary thrown exception does not stop an interval. A callback or checkpoint
  hitting Wisp's execution limit cancels that interval, preventing repeated
  freezes; other timers can continue. Completed effects are not rolled back and
  a chain of separate tasks still has no page-wide CPU budget. Timers do not keep
  the page's resource-loading indicator active.
- `location.assign`/`replace` now preserve URL-conversion exceptions. This small
  fix does not complete relative URL, history replacement or Location setter
  semantics; those limitations remain in the capability map.

The old Duktape `Window.bnd` supplied useful ownership, cancellation and
scheduler examples. Its forced 10 ms delay and the use of zero delay to mean
"not an interval" were not carried over. The QuickJS implementation follows the
current timer algorithm and uses the existing bounded execution machinery.

Validation: **46 Check tests** in `test_quickjs`, the option serialization tests,
and Qt `qt_js_timers` pass in Debug ASan/UBSan. Coverage includes argument
identity, FIFO tasks/checkpoints, coercion exceptions, shared cancellation IDs,
self-cancelling and throwing intervals, nesting delays and their microtask reset,
ID wrap, scheduler failure, active-callback document destruction, sibling-document
recovery, runaway callbacks/jobs, navigation, trace causality and UI progress
during an endless timer chain. Existing loading, navigation, script-ordering,
fetch, font-document, retained-restyle and sticky lifecycle/resource regressions
pass as well (nine Qt CTests in total).
GTK and native non-Linux frontend execution are not claimed.

### Batch 3, first increment: real nodes and visible mutations

Implemented 2026-10-07, using the old Duktape Node/Document bindings as references
for libdom calls, not as templates for ownership or error handling.

- Node/Element/HTMLElement, Document/HTMLDocument, CharacterData/Text/Comment and
  DocumentType have real prototype relationships and native receiver checks.
  `window.document` is read-only and non-configurable. Document roots, direct
  HTML head/body children, ID lookup, basic node traversal and identity use the
  actual parser-created document. Node constants and `instanceof` are supported;
  creation constructors are not implemented and throw.
- A private **strong document-lifetime cache** preserves exposed wrappers and
  their JS properties. It is not a weak cache: detached wrappers stay until
  document retirement. Native references are released while the HTML owner is
  still alive; surviving JS wrappers reject access after closure. GC marks the
  wrapper/document/prototype cycles, and finalizers tolerate either order.
  Earlier reclamation of detached wrappers is a later memory improvement.
- Implemented mutations are attributes, `id`, `className`, `textContent`,
  `nodeValue` and CharacterData `data`. Conversions preserve exceptions, argument
  order, byte lengths and the relevant null/undefined rules. Calls recheck the
  document after string conversion because conversion can execute JS. Native
  errors become DOMException; allocation failures remain allocation errors.
- Connected changes invalidate the DOM generation immediately and coalesce one
  full restyle request after the outer script/timer and its microtasks. Detached
  edits do not invalidate the page. Completed mutations still render after a
  later exception or execution timeout; retirement discards pending requests.
  Changes before box construction are covered by the initial build. Changes
  during a yielded build request a subsequent refresh.
- Old dummy document APIs were removed, including fake selectors/collections,
  `classList`, style/geometry objects, tree edits, creation, `innerHTML`, cookie,
  readyState and document.write. Missing APIs no longer fabricate DOM success.
  XHR, storage and event stubs remain separate work. Cross-document wrappers,
  element-specific resource reactions and a full DOM/HTML binding are not claimed.
- Console and XHR native class IDs now belong to each runtime. Registration-order
  tests with two live runtimes caught the old process-global-ID assumption.
  Dynamic property tables are materialized before their storage expires because
  QuickJS otherwise retains them for lazy initialization. The Console/Window
  changes are in the binding generator, not generated output; Window no longer
  registers an unused native class.
- Native fixes make empty `textContent` leave no children, propagate text getter
  errors, and validate attribute local names according to DOM rather than XML
  QName rules. Form synchronization now recognizes the corrected empty subtree,
  avoiding unnecessary full restyles. Allocation tests also exposed two small
  QuickJS cleanup bugs; fixes and standalone regressions are recorded in the
  [upstreaming notes](../src/docs/quickjs-upstreaming.md).

Validation: **52 Check tests** in `test_quickjs` pass under Debug ASan/UBSan.
New cases cover real parsed nodes, wrapper identity and GC, detached nodes,
native exceptions, coercion and retirement during coercion, Unicode/embedded NUL,
text replacement, per-runtime class IDs and removal of dummy APIs. `qt_js_dom`
compares boxes and exact captured pixels to an equivalent static page for inline,
timer, Promise, exception and timeout edits, and verifies trace causality.
The nine earlier Qt regressions and six native DOM/form/restyle regressions pass;
the retained-restyle tests caught the empty-textarea regression before completion.
Vendored API and local JS suites pass in sanitizer and Release builds; the local
JS suites report **116 tests, 0 errors, 9 excluded**, not full test262.

The next increment implements `classList` below. Node creation/tree editing and
live node collections remain, with the same ownership, error and invalidation
rules. Events and accurate loading state remain the following batch. Renderer
reuse should wait for measured full-refresh costs and sound dependency tracking.

### Batch 3, second increment: live class lists

Implemented 2026-10-07. `Element.classList` now returns a stable, live
`DOMTokenList`, including for SVG elements. It provides `length`, indexed access,
`item`, `contains`, variadic `add`/`remove`, `toggle`, `replace`, `value`, string
conversion and standard iteration/`forEach`. `supports` throws TypeError for
`class`, which has no specification-defined supported-token vocabulary.
Assignment to `element.classList` forwards to the list's `value` property using
Web IDL's ordinary property semantics.

- The shared libdom token implementation handles all five ASCII whitespace
  characters, byte lengths, ordered deduplication and replacement. All supplied
  arguments are converted before token validation, and all tokens are validated
  before a variadic mutation. Each edit writes the attribute once. Empty versus
  absent attributes, normalization, optional `toggle` force and no-op operations
  follow the DOM algorithms, not browser-specific expectations.
- Reads validate a cached parse against the real immutable attribute string.
  Direct class/attribute changes and removal are immediately visible without
  relying on deprecated mutation-event listeners. Failed writes invalidate the
  parse, so a later read cannot expose an uncommitted token set.
- The element owns its list wrapper and native token list; the wrapper traces its
  element and JS properties. Native token references are released on document
  retirement alongside native nodes. Detached lists, wrapper cycles and coercion
  that retires the document are covered.
- Actual writes use the existing JS mutation trace and coalesced renderer update.
  A forced no-op toggle or unsuccessful replacement logs no mutation and requests
  no refresh. Rendering still runs on the main thread.
- Indexed properties obey Web IDL's live property descriptors and enumeration
  order. Array iterator methods are the actual engine intrinsics, as Web IDL
  specifies. Arbitrary JS properties retain their ordinary descriptors. QuickJS
  has a small host-class extension to reject `preventExtensions`, needed for
  live platform objects, plus a fix for throwing flags on rejected exotic
  property definitions. Both have standalone engine API tests and are documented
  as separate [upstreaming candidates](../src/docs/quickjs-upstreaming.md).

The old Duktape `Element.bnd` and `DOMTokenList.bnd` informed ownership and native
calls. Their reversed unforced-toggle branches, suppressed exceptions,
argument-by-argument updates and old DOMSettableTokenList model were not copied.
The native libdom parser's ASCII-space-only splitting and character/byte-length
confusion were corrected at their source.

Validation: **60 Check tests** pass with Debug ASan/UBSan. Eight new cases cover
live state, Unicode/embedded NUL, atomic validation, coercion, forced/no-op edits,
replacement ordering, indexed-property behavior, iteration, GC and retirement.
The existing allocation-budget sweep includes the new class initialization.
`qt_js_dom` verifies class-list edits against static boxes/pixels and checks that
only actual mutations appear in the trace. JavaScript timer, ordering, loading,
navigation and retained-restyle captures, plus DOM/form/restyle tests, are also
checked. Engine API, regexp and local JS suites pass in sanitizer and Release
builds; the local suites report **116 tests, 0 errors, 9 excluded**.

The following increment adds basic node creation/tree editing. Live JS node
collections remain separate work.

### Batch 3, third increment: node creation and tree editing

Implemented 2026-10-07. `tree.c` adds `document.createElement`, `createTextNode`,
`createComment`, `createDocumentFragment`, and Node's `appendChild`, `insertBefore`,
`replaceChild` and `removeChild`. DocumentFragment has its own prototype and real
`textContent`. All methods use the existing identity cache and retirement boundary.
String conversions and creation-option getters preserve exceptions and recheck the
document before native access. Normal element creation uses libdom's HTML factory,
now with an HTML namespace and the current DOM local-name validation rules.

The old Duktape `Document.bnd` and `Node.bnd` informed native calls and reference
ownership. Their suppressed errors, prototype-based argument checks and incomplete
nullable-reference handling were not carried over. Semantics were checked against
the [DOM creation and mutation algorithms](https://dom.spec.whatwg.org/#mutation-algorithms).

Native fixes accompany the bindings:

- Insertion/replacement share pre-mutation validation. Invalid fragments leave
  both trees intact; document root uniqueness and doctype ordering are checked
  for the whole operation. Valid document-root removal/replacement is supported.
- Empty-fragment replacement reconnects neighboring siblings. Inserting a
  fragment keeps the emptied fragment on libdom's detached-node list; it must
  continue to keep its native owner document alive. Insertion-hook failures
  cannot leave both the fragment and destination claiming the same child list.
- Native document collections are anchored to the document, following root
  changes and remaining usable when there is no root. This also fixes the forms
  collection assertion reached when conversion starts after JS removes the root.
  These native collections are not yet exposed through JS wrappers.
- Conversion/restyling accepts a document without a root, paints it blank and
  clears its extents. A later insertion can rebuild it. Full rebuilds discard
  the old propagated body background along with the old boxes. Debug captures
  can dump an empty document without treating it as an output error.

Tree operations report parent/child/reference/former-parent IDs through
`dom.tree`. Existing `dom.mutation` records identify each affected parent; a move
between parents can produce a destination record plus `moveFrom`. Both use the
same coalesced update boundary. A move into a detached fragment therefore still
invalidates the connected source parent. Detached-only edits do not rebuild the
page. Native validation errors do not request rendering; allocation failures
conservatively do because an insertion hook can fail after linking.
Empty-fragment insertion and self-insertion/replacement are traced as calls but
do not produce mutation notifications or page rebuilds.

Validation: **67 Check tests** in `test_quickjs`, plus **5 native tree tests**, pass
under Debug ASan/UBSan. Added checks cover creation, conversions and retirement,
invalid names/arguments/trees, sibling moves, empty and populated fragments,
document constraints, wrapper identity/GC and native document lifetime without
JS ownership. `qt_js_dom` compares inline/timer-created content with static pixels
and geometry, tests removal into a detached fragment, and tests root removal and
restoration before/after initial rendering. Native DOM, forms, construction and
restyle regressions and the Qt loading/script/timer/navigation/retained-update
tests are checked. No engine patch or generated-file edits are needed here.

Scope remains deliberate: cross-document adoption/import, `createElementNS`,
cloning, live JS collections, selectors, convenience methods such as `append`,
custom elements and public node constructors remain unimplemented. Customized
built-in creation options throw an explicit NotSupportedError; ordinary/default
options and the legacy string argument are accepted. This does not implement
dynamic script execution or complete stylesheet/resource/element lifecycle
reactions. DOM allocations remain outside QuickJS's heap budget. Next: live node
collections, then the event/lifecycle work in batch 4.

### Batch 3, fourth increment: live child collections

Implemented 2026-10-07. `Node.childNodes` returns a stable live `NodeList`;
`Element.children`, `Document.children` and `DocumentFragment.children` return
stable live `HTMLCollection` objects. Collections support length, indexed access
and `item`; HTMLCollection additionally supports ID/name lookup and `namedItem`.
They follow the [DOM collection algorithms](https://dom.spec.whatwg.org/#old-style-collections)
and [Web IDL legacy platform objects](https://webidl.spec.whatwg.org/#legacy-platform-objects).

- Text/comments appear only in childNodes. Element collections include foreign
  elements; IDs work there, while `name` matching requires the HTML namespace.
  Named lookup uses the first ID-or-name match in tree order. Empty names do not
  match. ID/attribute edits, parser writes, fragment moves, text replacement and
  document-root removal/restoration are immediately visible through saved lists.
- Each lazily created list traces its root wrapper and expando properties. The
  root traces its cached lists. No extra native references survive individual
  operations, so document retirement uses the existing release boundary. Detached
  trees and GC cycles remain valid; coercion/prototype callbacks that retire the
  document cannot access freed native nodes.
- Indices are enumerable and read-only; named properties are non-enumerable and
  yield to ordinary own/prototype properties. Definitions, deletion, assignment,
  receiver checks and required-argument conversions follow Web IDL. Live objects
  reject freezing. NodeList uses Array's entries/keys/values/forEach intrinsics;
  HTMLCollection exposes the standard iterator without inventing those methods.
- `platform.c` shares the existing class-list index parsing and iterator setup.
  The renderer and binding generator are unchanged. Collection reads do not
  produce mutation records or request rendering; edits performed through returned
  nodes use the existing attributed JS mutation log and coalesced update.
- Traversal reads direct siblings, with no stale snapshot or second invalidation
  mechanism. Length/item/namedItem are linear scans, and repeated indexed loops
  or full named-property enumeration can be quadratic. Access to ordinary
  built-in members avoids unnecessary attribute scans. This is a correctness
  baseline, not a collection-cache optimization; measure real use before adding
  native mutation generations/cursors. No full-document scans are introduced.

The old Duktape NodeList/HTMLCollection bindings informed the native-ownership
review. Their suppressed errors and incomplete JS property surface were not
copied. The new wrappers retain the existing JS root instead of retaining a
second native collection whose teardown would need separate coordination.

Two isolated engine changes support this work: an optional descriptor lookup for
ordinary assignment lets Web IDL ignore virtual names without reimplementing
ECMAScript setters/receivers in Wisp; an allocation regression found that failed
lazy initialization could permanently replace a builtin such as Reflect with
undefined. Initializers now survive failure and can be retried. Both have native
API regressions and are recorded in the [upstreaming notes](../src/docs/quickjs-upstreaming.md).

Validation: **79 Check tests** cover the integration, including twelve new cases
for liveness, filtering, ID/name rules, descriptors/enumeration, prototype setters,
Proxy prototypes, iteration, coercion, GC, allocation failure and retirement.
`qt_js_dom` verifies saved lists across parser continuation and timer edits against
static pixels, and checks that read-only collection access triggers no rebuild.
Debug ASan/UBSan builds and the engine API/local JS suites are checked, along with
Qt script ordering, timers, loading, navigation and retained rendering. The local
engine suite is not full test262. GTK is not tested.

This completes the planned minimal child-collection surface. Document-wide
collections (`forms`, `images`, etc.), getElementsBy* queries and selectors remain
unexposed; later query APIs can build on the ownership/property rules here.
Next is batch 4's EventTarget/Event and lifecycle work, not a claim of full DOM
or dynamic resource/script support.

### Batch 4 prerequisite: native event dispatch

Reviewing the old Duktape `EventTarget.bnd`, `Event.bnd` and `dukky.c` bridge
confirmed that JS listeners should use libdom's propagation. Before connecting
callbacks, however, the shared native dispatcher needed repair. New regression
tests reproduced wrong target phases, duplicate invocation and use-after-free
when listeners removed registrations or released objects during callbacks.

The dispatcher now implements the ordinary node-tree part of
[DOM event dispatch](https://dom.spec.whatwg.org/#concept-event-dispatch):

- Build and retain the complete ancestor path before callbacks. Reparenting does
  not change that dispatch's path; adding a listener to a later ancestor works.
- Invoke capture and bubbling separately, with both target invocations reporting
  `AT_TARGET`. Non-bubbling events still invoke target listeners.
- Snapshot registrations for each invocation, retain removed entries until
  snapshots finish, suppress duplicate type/listener/capture registrations, and
  preserve registration order on removal. A listener removed and re-added during
  an invocation waits for a later invocation.
- Honor cancellation only for cancelable events. Stopping propagation does not
  suppress native default actions. Clear dispatch/propagation state, current
  target and phase afterwards; retain cancellation until reinitialization.
- Keep the event, document, path and saved target alive. Reinitialization releases
  the previous target and strings and is ignored during dispatch. An initialized
  event may have an empty type; recursive dispatch of the same event is rejected.

The implementation uses stack storage for ordinary paths and listener snapshots,
with heap fallback for deep trees or many matching listeners. It remains on the
main thread. Existing native default-action hooks and internal mutation-event
bookkeeping are preserved; this does not expose legacy mutation events to JS.

`dom_event_test` covers 21 cases, including mutation during dispatch, saved-event
ownership, released caller references, reentrancy and both heap fallbacks. The
native cases pass ASan/UBSan and leak checking. Existing QuickJS, DOM tree,
class/details, user-data, construction, form and Qt JS/interaction regressions are
also checked. GTK is not tested.

This prerequisite initially left the JS APIs as stubs; the following increment
implements script-dispatched standalone/Window events. DOM-to-Window propagation,
specialized native event initialization and shadow trees are not implemented by
the node-tree repair.

Two integration issues identified for the next increment must be handled before
enabling click listeners: native checkbox/details activation already consults
dispatch cancellation, but navigation/form actions need the same cancellation
gate; callback-triggered mutations/navigation must not leave interaction code
using stale boxes, links or form pointers. DOMContentLoaded/load and readyState
also need a separate lifecycle pass: the current `js_fire_event("load")` call is
before the initial box build. Do not simply attach callbacks to these existing
hooks and assume the complete behavior is correct.

### Batch 4 increment: script-dispatched events

`Event`, `CustomEvent` and `EventTarget` are real constructors. Scripts can register
and remove listeners on `new EventTarget()` and `window`, and synchronously dispatch
events to them. Supported semantics include:

- Type and option conversion, constructor inheritance, interface constants,
  CustomEvent detail identity, unforgeable `isTrusted`, a monotonic millisecond
  timestamp relative to context creation, and legacy initialization/accessors.
- Function listeners and callback objects with `handleEvent` resolved at invocation
  time. Duplicates/removal match type, callback identity and capture only. `once`
  removes before invocation, including before reading `handleEvent`.
- Per-invocation snapshots, cancellation, stop flags and passive listeners. Window
  uses DOM's passive default for wheel/mousewheel/touchstart/touchmove; explicit
  options override it. Signal-bound listener removal is implemented in the later
  AbortController/AbortSignal increment below; invalid signals fail interface
  conversion rather than being silently ignored.
- Listener exceptions are reported while later listeners continue. Execution
  interruption escapes the surrounding script, stops dispatch and discards jobs.
  Nested callbacks share the existing deadline and outer microtask checkpoint.
- GC-visible callback/target/detail ownership. A saved event retains its target;
  callback cycles collect. All targets release registrations at document closure;
  retained targets reject calls from retired or foreign document contexts.

libdom now has a standalone target implementation and virtual target ref/unref/
invocation operations. Nodes and standalone targets use one dispatcher. This avoids
representing Window/custom targets as fake DOM nodes or copying propagation into
the JS layer. Optional listener-owned cleanup waits until native invocation
snapshots release the listener, so self-removal and closure cannot free callback
data still in use. The JS side stores registration metadata and delegates dispatch
and cancellation to libdom.

The generator still owns Window's generated properties; event constructors and
semantics are hand-written, registered separately and included in both production
and test source lists. No generated output is edited. The old Duktape bindings were
consulted for integration patterns, not copied as the semantic implementation.

There are 16 new JS event cases (95 total QuickJS cases) and 22 native event cases.
They cover conversion/error behavior, dispatch, listener edits, passive/once,
microtask ordering, timeouts, document retirement and collection of callback cycles.
`qt_js_dom` additionally compares event-driven DOM mutations against static pixels
and verifies coalescing and event-entry trace attribution. Native DOM/form and Qt
loading, navigation, scripts, timers, interaction and retained-rendering regressions
are checked. GTK is not tested.

The following increment extends script dispatch to DOM nodes. Neither increment
turns on native clicks or lifecycle events.

### Batch 4 increment: DOM-node script dispatch

Node and its derived interfaces inherit EventTarget. The same add/remove/dispatch
methods now work on elements, Document, Text, Comment and DocumentFragment. Node
wrappers allocate listener storage lazily; it owns a native node reference and
participates in QuickJS GC through the wrapper. Document closure clears all native
registrations and storage references before retiring the DOM wrappers.

The native document retains an optional host event parent, connected lazily to the
page's Window and removed on closure. The native dispatcher appends it only when
an ancestor path reaches Document, and excludes that edge for the exact event name
`load`, following [Document's get-the-parent rule](https://dom.spec.whatwg.org/#document).
Detached subtrees and fragments stop at their own root. Retained paths survive
reparenting, removal and clearing the Window association during a callback.

Event exposes the dispatcher's frozen path through `currentTarget` and
`composedPath()`. Before the first JS listener, wrappers for that path are retained
in a private GC-visible array. Each composedPath call returns a fresh array;
callback receivers and path entries keep their existing JS identities, including
if a callback closes the document. Dispatch clears the temporary array afterwards.
The JS adapter does not walk a second ancestor tree or implement propagation.
There is still no shadow DOM or retargeting support.

Default passive handling now includes Document and its actual document/body
elements. Same-named detached elements do not acquire that default. Native internal
mutation events remain private: only an explicit script dispatch enters JS here.
Scripts may dispatch an arbitrary event name, but cannot use a legacy mutation
name to trigger internal insertion/resource processing. The HTML default-action
hooks require a trusted native notification. Script events own their wrappers and
do not enter the old native cleanup callback, which could refer to a retired page.

The old Duktape node/event identity maps and native-listener bridge were reviewed
again. This implementation reuses the existing QuickJS node cache and libdom
snapshots, rather than introducing another identity map or copying Duktape's
callback exception/return-value behavior. No generated files are edited.

Validation: 105 QuickJS cases and 25 native event cases, with ASan/UBSan and leak
checks. Added cases cover inheritance, full phase/identity/path behavior, load and
detached roots, reparenting, late ancestor listeners, nested events, passive root
identity, deep paths, retirement and allocation failures. Qt tests compare delegated
listener mutations with static reference pixels, check coalesced render requests
and trace attribution, and navigate from a DOM listener. A synthetic insertion of
a detached stylesheet link must not cause a download. Existing loading, script,
timer, native interaction and retained-rendering regressions are checked. GTK is
not tested.

### Batch 4 increment: abort signals and listener cancellation

`new AbortController()` exposes a stable `signal`. `abort(reason)` preserves the
first reason, defaults undefined to an AbortError DOMException, and fires one
trusted, non-bubbling, non-cancelable `abort` event. AbortSignal inherits EventTarget
and exposes `aborted`, `reason`, `throwIfAborted()`, static `abort(reason)` and
`onabort`. Direct AbortSignal construction is illegal. These follow the
[DOM abort algorithms](https://dom.spec.whatwg.org/#interface-abortcontroller).

The `addEventListener` signal option now accepts a real AbortSignal. An already
aborted signal prevents registration; subsequent abortion removes registrations
before public abort listeners run. A stopped public event cannot prevent removal.
Duplicate registrations retain their original signal because identity still
depends only on type, callback and capture. Explicit removal and `once` also
detach the abort step, including during an active native invocation snapshot.

AbortSignal uses private EventTarget storage and the existing libdom dispatcher.
The public signal keeps its own identity in `this`, target/currentTarget and the
composed path. Host-created events use an intrinsic Event factory; page overrides
of Event or DOMException do not intercept their construction. Signals and
registrations trace both directions of their ownership through QuickJS GC.
Collection and document closure detach host registrations without invoking page
code. Retired or foreign-context signals reject further operations under the
existing document boundary.

`onabort` uses one listener slot, so replacing a handler preserves its order;
clearing and re-adding moves it to the end. Its conversion follows HTML's
[EventHandler](https://html.spec.whatwg.org/multipage/webappapis.html#eventhandler)
and Web IDL's [LegacyTreatNonObjectAsNull](https://webidl.spec.whatwg.org/#LegacyTreatNonObjectAsNull):
non-objects clear the handler, whereas non-callable objects retain their identity
but do nothing on invocation. A literal false return cancels a synthetic cancelable
event. Exceptions and deadlines use the existing bounded callback handling.
This does not introduce general element/Window handler attributes.

Validation: 116 QuickJS cases pass with Debug ASan/UBSan and leak checks. The 11 new
cases cover reasons/identity, interfaces, abort event order, duplicate and active
listener edits, handler replacement/conversion, GC cycles, document retirement,
execution interruption and allocation-failure recovery. Qt's DOM fixture compares
abort-driven mutations with static reference pixels and checks cancellation,
microtask processing, trace attribution and one coalesced render request. Qt
loading, navigation cleanup, script ordering and timer tests also pass. GTK is not
tested.

Scope remains deliberately bounded: `AbortSignal.any`, `AbortSignal.timeout`,
fetch/XHR cancellation and cross-realm callback/settings handling are absent.
Aborting a signal does not interrupt arbitrary JavaScript or cancel unrelated
timers; Wisp's execution deadline remains a separate resource limit. The old
Duktape event bridge was reviewed for native identity and protected callbacks;
abort semantics come from the current standards.

### Next: native input delivery and default actions

Keep the script-dispatch-stack guard until wrapping, default actions and content
lifetime are handled together. The focused click review found these prerequisites:

1. Replace interaction pointers saved before callbacks with owned state or
   validated DOM identities. Link URLs/targets and form controls currently survive
   only because page listeners cannot run there. A callback can remove them or
   navigate. `hlcache_handle_clone()` is an unimplemented stub, so it cannot serve
   as a content-lifetime guard.
2. Honor cancellation for deferred navigation and submission as well as existing
   checkable/details actions. Review early link-download actions that currently
   precede click dispatch. Preserve native behavior for pages without listeners.
3. Implement the HTML input [activation rules](https://html.spec.whatwg.org/multipage/input.html#the-input-element):
   checkbox/radio pre-activation before click listeners, cancellation rollback,
   checked/indeterminate state and radio-group revalidation. Current checkbox
   toggling happens after dispatch, and the old Duktape generated accessors are
   insufficient for these semantics.
4. Deliver correctly initialized input events through the shared dispatcher, with
   explicit callback/checkpoint boundaries and safe teardown. Test cancellation,
   callback mutation, navigation, labels and radio groups before enabling delivery.

Inline handlers, readyState and HTML lifecycle timing remain subsequent work. The
current `load` call precedes the initial box build and cannot simply be exposed to
JS. Cross-document JS objects remain outside the current DOM binding scope.

### Reading the JS trace

Enable verbose logging and `js_trace` when investigating a JS/rendering difference:

```sh
./build/frontends/qt/wisp-qt -v --js_trace=1 https://example.com/ > run.log 2>&1
rg '\[JS ' run.log
```

`-v` must be the first argument. Tracing is off by default and requires no rebuild.
Records use stable document IDs and per-document entry IDs, for example:

```text
[JS doc=1 entry=1] begin kind=script source=https://example.com/app.js
[JS doc=1 entry=1] timer.schedule id=1 repeat=0 delay_ms=0 nesting=1
[JS doc=1 entry=2] begin kind=timer source=https://example.com/app.js
[JS doc=1 entry=2] timer.fire id=1 scheduled_by=1
[JS doc=1 entry=2] dom.node node=2 type=1 name=DIV id=target
[JS doc=1 entry=2] dom.mutation node=2 type=1 operation=className attribute= result=0
[JS doc=1 entry=2] render.request first_entry=2 changes=1 mode=full-restyle
[JS doc=1 entry=2] navigation.request method=assign target=https://example.com/next
```

`scheduled_by` connects a timer to its creating entry; the timer's source is the
scheduling entry's source, not necessarily where the callback function was
defined. Microtask begin/end records remain within the active entry. End records
distinguish normal completion, exceptions and interruption. Document closure and
timer cancellation are recorded; entry zero means no active script/callback.
URLs appear in the log, but script bodies and arbitrary JS object values do not.
Details are bounded and control characters are flattened into one line; tracing
does not call page getters or string conversions.

`dom.node` maps each newly exposed wrapper ID to its initial native node name and
HTML ID (bounded strings); it is not updated when the ID attribute changes.
`dom.tree` records the tree operation and its parent, child, reference and former
parent IDs (zero means absent); the IDs refer to `dom.node` entries.
`dom.mutation` names the affected node, attribute/text/tree operation and native result:
zero means success. Failed native calls may have partially changed a subtree, so
they conservatively invalidate it where needed. `render.request` records the
coalesced operation count and initiating entry. `initial-build` means the initial
box build will cover the edits; `full-restyle` requests the existing full update.
Detached-node edits are traced but produce no page update, unless moving a node
also changes a connected source parent (reported as `moveFrom`). The count is
affected-node notifications, not necessarily JS method calls.

`event.add` records a listener ID, event type and capture/once/passive settings;
`event.remove` records explicit removals. `event.dispatch`/`event.complete` bracket
script and host abort dispatch. `event.listener` identifies the invoked registration and its
creating entry; the following `begin kind=event` opens the bounded callback entry.
DOM mutations inside that callback use its entry ID. As with timers, the recorded
source is the registration source, not necessarily the function's definition site.
`signal.abort` records the first controller abortion before its removal steps and
public event; `signal.handler` records activation/clearing of `onabort`. Neither
records arbitrary reason or handler values.

This is an action trace, not a DOM snapshot or complete paint attribution. It
records no arbitrary text/attribute values and never calls page getters or
coercion to format diagnostics. New mutation APIs must use this same boundary.
Compare JS-enabled and disabled captures when isolating a discrepancy.

## What survives from Duktape

The old bindings are not in the working tree. They were removed by `9a4873f1`;
the final pre-removal revision is
`80c4aca5f50180d14377a773fb731df6ab272497`. For example:

```sh
git show 80c4aca5f50180d14377a773fb731df6ab272497:src/content/handlers/javascript/duktape/Node.bnd
```

Useful reference material includes:

| Old component | Reuse |
|---|---|
| `Node.bnd`, `Element.bnd`, `Document.bnd` | libdom traversal, attributes, text, creation and tree mutation call sequences; reference ownership |
| `DOMTokenList.bnd`, `NodeList.bnd`, `HTMLCollection.bnd` | Real collection/token operations and their object relationships |
| `dukky.c` | Node-to-wrapper identity map, tag-to-interface selection, event bridging and protected execution ideas |
| `Window.bnd` | Main-thread scheduler integration, retained callback arguments, cancellation and document cleanup |
| `EventTarget.bnd`, `Event.bnd`, handler bindings | Connection to libdom event propagation and default actions |
| Existing WebIDL and old nsgenbind backend | Interface inventory and examples of generated registration/accessors, subject to current-spec review |

This was meaningful browser integration, but it was **not a complete DOM**.
Verified limitations and defects include:

- `createElementNS` reads both namespace and qualified name from argument 0.
- Event registration/removal compares the wrong stack operands and matches all
  option flags. The DOM matching key is type, callback and capture, not passive
  or once. Do not port that code verbatim.
- `innerHTML` getter is empty; its setter uses a real fragment parser but still
  needs context/cleanup/spec review. CSSStyleDeclaration was incomplete.
- No implemented querySelector family was found in these bindings, and the
  present libdom does not supply the previously claimed
  `dom_element_query_selector` API. Selector querying needs its own design.
- The old node map roots wrappers in a normal JS object. It is not a weak map.
- Old error paths often return undefined instead of propagating the required
  exception. Stack manipulation is interwoven with DOM ownership and control
  flow; it is not a one-for-one API substitution.

The previous analysis's reuse percentages, completeness grades, fixed-day
estimates and blanket memory-safety claims were not supported. Its alleged
`calloc` argument-order bug is also not a meaningful bug. They are removed here.

### Recommended reuse approach

Port selected DOM operations manually into a small QuickJS-native wrapper layer,
using the old code to avoid rediscovering libdom behavior. Preserve attribution
when copying source. Do not restore Duktape itself, all old files, or nsgenbind's
entire backend to the production build.

Establish node identity, document/realm ownership, prototype inheritance,
exception conversion, finalizers and cycle tracing before porting many methods.
A C wrapper containing JS callback references needs appropriate QuickJS GC
marking; a C table of strong JSValues is not automatically a weak identity cache.
Test detached nodes, listeners closing over nodes, repeated lookup, navigation
and GC. Keep native objects valid while callbacks execute, even if navigation
retires the document during a call.

Use generation where it is predictable: interface/class registration, method and
accessor tables, argument counts and explicit supported-member metadata. Keep
semantic libdom operations as readable C. First make a few representative
bindings correct; then generalize repeated boilerplate. A regex transpiler of
arbitrary Duktape C bodies would hide ownership and exception mistakes and is
not recommended. Generated output stays in the build directory; changes belong
in generator inputs/templates.

## Proposed bounded sequence

These are separate reviewable batches, not a commitment to full web-app support.

1. **Execution controls and trustworthy tests — implemented.** See the batch 1
   scope and regression coverage above. No added Web APIs.
2. **Script/task correctness — implemented within the scope above.** Classic
   parser ordering, microtasks, main-thread timers and navigation-safe context
   ownership share bounded entry/checkpoint handling. Teardown runs no page
   callbacks. This is not a complete HTML event loop or script-element algorithm.
3. **Minimal real DOM plus visible mutations — four increments implemented.**
   Real wrappers, roots, ID lookup, traversal, attributes/text, live `classList`,
   basic node creation/tree edits, live child collections, coalesced full refresh
   and mutation traces pass identity/GC and rendered-page tests. Extend the same
   ownership/conversion/invalidation layer for further APIs. Reuse
   partial updates only with sound mutation/dependency tracking.
4. **Events and practical document interaction — native dispatch and explicit
   DOM/Window/standalone JS events and abort signals implemented; native
   input/lifecycle pending.** Click
   listeners and supported handler attributes, DOMContentLoaded/load and accurate
   readyState; then further element lifecycle integration. Validate propagation,
   preventDefault, capture, once, callback identity and navigation cleanup.
5. **Choose the next capability from measured page needs.** Selector queries,
   inline style/CSSOM and media queries are plausible next steps. Implement
   standard semantics and selector errors, not ID/class shortcuts disguised as
   full querySelector. Network APIs, modules, storage, observers and cross-frame
   objects are distinct projects. Origin/CORS/credentials, cancellation and
   document lifetime must be designed before exposing network or shared data.

The first useful milestone is a page with small scripts that can reliably reveal
content, change a class and respond to a click. Modern framework hydration, such
as DuckDuckGo's current homepage, is a later compatibility exercise. Re-run real
pages after each coherent batch, but do not let their first missing API expand
that batch indefinitely.

All proposed work can execute on the existing main thread. Correct scheduling
and interrupt limits do not require rendering threads or a second JS engine.
Keep DOM/render access centralized so future concurrency does not depend on
scattered binding-specific exceptions.

## Standards used for the next work

- [HTML script preparation and execution](https://html.spec.whatwg.org/multipage/scripting.html#prepare-the-script-element)
- [HTML microtask checkpoints](https://html.spec.whatwg.org/multipage/webappapis.html#perform-a-microtask-checkpoint)
  and [resource limits](https://html.spec.whatwg.org/multipage/webappapis.html#killing-scripts)
- [HTML timers](https://html.spec.whatwg.org/multipage/timers-and-user-prompts.html#timers)
- [DOM nodes and mutation](https://dom.spec.whatwg.org/#interface-node),
  [DOMTokenList](https://dom.spec.whatwg.org/#interface-domtokenlist),
  [EventTarget](https://dom.spec.whatwg.org/#interface-eventtarget) and
  [selector queries](https://dom.spec.whatwg.org/#dom-parentnode-queryselector)
- [Web IDL object bindings](https://webidl.spec.whatwg.org/#idl-objects)
  and [legacy platform objects](https://webidl.spec.whatwg.org/#legacy-platform-objects)

Use reduced fixtures with specification-derived expectations. Chromium/Firefox
comparisons remain useful diagnostics, not the authority for the behavior.

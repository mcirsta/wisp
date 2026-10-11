# Wisp JavaScript state and Duktape reuse

Reviewed 2026-10-07 against Wisp `206b8a03`, plus the QuickJS-ng update and
XHR conversion fix described below. This replaces the earlier migration analysis.
It is a browser-integration review, not an exhaustive security or ECMAScript
conformance audit. Batches 1–2 and four increments of batch 3 are implemented
within the scope described below. Real DOM lookups, class lists, attribute/text
mutations and basic node creation/tree edits now reach rendering and carry JS action
traces. Live child-node/element collections and script-dispatched events on DOM nodes,
Document, Window and standalone EventTarget are implemented, along with the core
AbortController/AbortSignal API and signal-bound listener removal. UIEvent and core
MouseEvent values share that dispatcher. The 2026-10-11 increments implement native
checkbox/radio pre-activation, cancellation rollback, label forwarding and trusted
primary-click delivery to JS. This click adapter is enabled whenever JavaScript is
enabled; it needs no experimental option. Keyboard events, the wider pointer/mouse
stream and lifecycle delivery remain separate work. Native SubmitEvent delivery
is implemented; validation and FormDataEvent remain incomplete. Earlier increment
notes describe their historical scope; the current status is recorded below.

## Current priority: basic JS that makes sites readable (2026-10-11)

The user's priority is scripts that initialize pages and populate visible content.
After the native click/SubmitEvent batch, defer further form-API completeness,
keyboard/pointer coverage and Location APIs unless a concrete reading-site blocker
requires them. Do not make those features prerequisites for useful page rendering.
Fix shared underlying defects found along the way when a focused root-cause fix
is possible; an incomplete surrounding API is not a reason to leave such bugs.

Next, establish document readiness and correctly timed DOMContentLoaded/load
callbacks, then use saved/live reading sites to identify their first unsupported
DOM operation. Prioritize real element queries, common DOM mutations and small
style/HTML operations according to those failures. Confirm the needed behavior
against standards and use focused fixtures plus visible full-page checks.
Content-fetching APIs become a priority when a page needs them to populate the
article or navigation; avoid fabricated success from the existing XHR stub.
Keep execution limits, callback lifetime safety and JS-attributed mutation logs.

This priority overrides the earlier batch ordering below. It does not imply all
scripts or application APIs need to be supported before a page can be useful.

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
| Events | Event/CustomEvent/UIEvent/core MouseEvent/PointerEvent/SubmitEvent values; shared script/native primary-click and SubmitEvent dispatch; checkable input/change delivery | Keyboard, other pointer/mouse events, full form submission callbacks, handler attributes and lifecycle remain pending |
| Cancellation | `abort.c`: AbortController, AbortSignal state/reason, throwIfAborted, static abort, onabort and the listener signal option | Listener cancellation uses private abort steps before the public event; timeout, any and network integration remain absent |
| Lifecycle information | `document.readyState` is omitted | Correct loading state and lifecycle events belong to batch 4 |
| Networking/storage | XHR fabricates DONE/200 without a request; storage discards writes; no `document.cookie` accessor or browser fetch binding | XHR/storage still advertise unsupported behavior. Native HTTP cookie handling is a separate subsystem |
| Navigation | `location.c`: real URL getters and relative/absolute navigation for assign/replace | Partial support: href has no setter, reload is a stub, history replacement needs review; assign and replace currently use the same flags |
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

### Batch 4 increment: UIEvent and MouseEvent values

Added UIEvent and MouseEvent constructors with the Event → UIEvent → MouseEvent
inheritance chain, including subclass construction. UIEvent supplies view/detail;
MouseEvent adds client/screen coordinates, x/y aliases, button/buttons, the four
primary modifiers, getModifierState and relatedTarget. The interface/conversion
rules come from [UI Events](https://w3c.github.io/uievents/#interface-uievent) and
[MouseEvent](https://w3c.github.io/pointerevents/#mouseevent-interface). Coordinates
use the finite doubles required by [CSSOM View](https://drafts.csswg.org/cssom-view/#extensions-to-the-mouseevent-interface),
including fractions and negative zero. Button fields use Web IDL's signed/unsigned
16-bit conversion; detail uses signed 32-bit conversion.

The new input data extends the existing event allocation. Base Event/CustomEvent
values do not allocate the larger input payload. All types share native libdom
propagation, cancellation, dispatch snapshots, deadlines and JS finalization/GC.
Input data is kept in the JS adapter because libdom's old mouse representation uses
integer coordinates and lacks a buttons mask; no second event dispatcher is added.
View and relatedTarget are traced JS references, preserving identity and retaining
their objects until the event is released. No generated files are edited.

Dictionary conversion follows inherited/alphabetical member order and preserves
conversion exceptions. The legacy initUIEvent/initMouseEvent methods convert
arguments before checking whether dispatch is active; active dispatch prevents
reinitialization. Their explicitly initialized fields update together after
successful conversion. Script-created events remain untrusted. The old Duktape
Event/KeyboardEvent bindings were consulted for native access and wrapper lifetime,
not copied as modern constructor or modifier semantics.

Validation: 127 QuickJS cases pass with Debug ASan/UBSan and leak checks. Nine new
cases and two added allocation-sweep variants cover values, conversion order and
errors, modifiers, legacy initialization, DOM propagation, GC cycles, saved
related targets and retirement during callbacks/conversion. The Qt DOM fixture
compares a delegated MouseEvent-driven update to static pixels and checks mutation
coalescing and trace delivery. Loading, navigation, script ordering and timer
integration tests also pass. GTK is not tested.

This is input-event data support, not native input delivery. Native clicks and
script-dispatched MouseEvent click activation/default actions still require the
coordinated work below. PointerEvent/keyboard/wheel interfaces, geometry-dependent
page/offset/layer coordinates, movement and legacy which are not exposed. Only the
current Window identity is accepted as view; cross-document WindowProxy and shadow
DOM retargeting remain outside the existing document model.

### Batch 4 increment: native checkable activation

Native checkbox/radio clicks now apply their provisional state before dispatching
click, following HTML's [input activation rules](https://html.spec.whatwg.org/multipage/input.html#the-input-element).
A canceled checkbox restores checkedness and indeterminateness; a canceled radio
restores the saved selected peer only when it still belongs to the current group.
The input and saved peer are retained across callbacks. Nested activations keep
separate snapshots. Radio traversal stays within the current tree, including
detached subtrees, rather than reaching into the owner document unconditionally.

Libdom now stores indeterminateness separately from checkedness, initializes it to
false and copies it during cloning. This supplies the native state needed for
rollback; it does not yet expose HTMLInputElement JS accessors or indeterminate
painting. An accepted, connected checkable activation dispatches input then change;
changing the type or removing the control in the click listener is rechecked.
Disabling the control during the listener does not suppress these activation
steps. Existing native label forwarding uses the same transaction for its input.

Validation: 23 native form cases and 127 QuickJS cases pass with Debug ASan/UBSan
and leak checks. Added cases cover provisional state, event order/flags,
cancellation, nested clicks, saved-peer lifetime, group edits, detached trees,
disabled-state changes and cloning. Seven Qt integration tests cover native
checked interaction/selectors, JS DOM rendering, timers, navigation cleanup,
script ordering and loading. GTK is not tested.

This increment prepared JS input delivery. The native adapter below now provides
typed click wrappers and composed metadata. Broader form-owner
reset on reparenting and radio synchronization on arbitrary DOM/IDL edits are
also pending: the existing resolver honors explicit form attributes but otherwise
uses libdom's parser association. The old Duktape accessors are not sufficient to
implement those semantics. No generated files are edited.

### Batch 4 increment: label forwarding and click cancellation

Native form clicks now use one `html_form_click` transaction in both interaction
code and C tests. The state-only activation entry point was removed. The original
label/input activation element is retained before dispatch, following
[DOM dispatch](https://dom.spec.whatwg.org/#concept-event-dispatch), while a label's
control is resolved after listeners return. Changes to `for`, control type,
disabledness, removal and tree membership therefore affect forwarding. Reparenting
the clicked node does not substitute another label as the activation element.
Canceling a forwarded input click restores that input without retroactively
canceling the original label event.

Association follows HTML's [label rules](https://html.spec.whatwg.org/multipage/forms.html#the-label-element):
explicit IDs use the first match in the same tree, including detached trees;
implicit labels use the first labelable descendant. Meter, output and progress
now participate in that choice. Interactive descendants suppress label forwarding;
media controls and image maps are included. Relevant attributes/ancestry are
rechecked at activation. This remains checkbox/radio forwarding, not general
label focus, custom elements or other control activation.

The primary-click cancellation result now also gates deferred link navigation,
form submission and JavaScript-URL actions. Forced downloads moved from before
click dispatch into the same deferred-action phase. The link helper no longer
takes unused window/coordinate arguments. File-picker and select-menu opening
still happen earlier and need separate default-action integration.

Validation: 60 native form cases pass ASan/UBSan and leak checks; 37 new cases
cover callback edits, retained label identity, detached/duplicate IDs, labelable
and interactive content, and cancellation versus propagation stopping. Existing
form cases now use the production click transaction. The 127 QuickJS cases and
nine Qt checks pass, including native checked interaction/selectors, details,
form navigation, JS DOM rendering, timers, script ordering and loading. GTK is
not tested. The shared controls/usemap strings were added to the core-string
source list; no generated output was edited.

Native JS delivery remains guarded. The old Duktape native-listener bridge was
consulted again, but its callback handling does not solve content/window lifetime
or modern activation semantics. The checks above do not yet exercise JS-canceled
browser navigation end to end; that test belongs with the delivery bridge.

### Batch 4 increment: input content lifetime and retirement

Mouse clicks now retain the HTML content through the existing content-user list
until dispatch and default actions return. The cache cannot evict a document
whose last browser handle is released inside a callback. Nested dispatch gets
independent temporary users, released on every return path. Both primary and
secondary clicks are covered because either can execute a `javascript:` URL;
ordinary pointer motion does not allocate a user. Failure to retain content
returns `NSERROR_NOMEM` before dispatch. This uses neither a second reference-count
system nor the unimplemented `hlcache_handle_clone()`.

HTML closure advances an input generation. After callbacks, the handler checks
both that generation and the original window association before continuing.
Reopening the same content in the same window does not revive an older click.
Retired clicks skip deferred navigation/submission and details activation;
checkable rollback does not schedule restyling of the closed document. Status
and pointer callbacks are also followed by retirement checks. Embedded HTML
objects, like iframes, now return after child dispatch instead of additionally
dispatching a click into the parent document.

Validation: 19 `interaction_lifetime_test` cases use the real native mouse path,
DOM listeners, content-user ownership and HTML close/open handlers. Tests free
boxes during callbacks and cover cancellation, nested clicks, closure/reopening,
secondary-click navigation, checkbox rollback, details, frontend retirement and
embedded-document handoff. They pass ASan/UBSan and leak checks, along with the
60 form-state and 127 QuickJS cases. All nine Qt interaction/JS/loading/details
checks pass. GTK is not tested.

This completes the content-retention/retirement part. The input generation does
**not** detect ordinary DOM edits in an active document; action resolution needs
its own ownership, implemented for links in the following increment. Submit
controls still need that work. Keyboard/other input callback lifetimes remain
part of their future delivery work.

### Batch 4 increment: hyperlink identity and live action data

Native link activation now retains the original anchor or image-map area before
click dispatch. After listeners return, it reads that element's current `href`
and target into owned storage. Replacing the element or moving the clicked child
under another anchor does not transfer the original activation. Removing `href`
suppresses navigation; adding it to an existing anchor can enable navigation;
an empty `href` remains a URL reference. A detached anchor can still navigate,
whereas a detached area cannot, following HTML's explicit distinction.

The resolver observes the live element target, otherwise the first HTML `base`
with a target, including removal of that base. Empty targets resolve to `_self`;
targets containing both an ASCII tab/newline and `<` become `_blank`. It passes
an explicit default so the window helper cannot substitute a stale cached base
target. Target-window creation is followed by another input-retirement check.
Image-map entries retain their DOM node and no longer duplicate target strings;
destroying the map table or boxes during dispatch does not invalidate activation.

URL normalization moved from box construction to a shared hyperlink helper,
still used by image, object, frame and image-map construction. The old heuristic
which guessed a navigation URL from the first quoted string inside a
`javascript:` URL when scripting was disabled is removed. Disabled scripting now
leaves such link activation inert. Enabled scripting still uses the existing
`html_exec` path; this increment does not implement complete `javascript:` URL
navigation, decoding, target-realm or string-result semantics.

Ordinary content activation is distinguished from browser-handled actions, so
save-link and scrollbar clicks cannot accidentally start link navigation. Forced
downloads use the updated URL and honor click cancellation.

Validation: the native input suite now has 43 cases, including listeners changing
or removing href/target, replacing/detaching anchors, moving the clicked child,
switching between network and script URLs, changing base targets, image-map
activation, downloads and browser-handled clicks. Mutation cases deliberately
destroy the presentation storage during dispatch. These tests use real libdom
listeners; they do not claim end-to-end QuickJS native-input delivery. ASan/UBSan
and leak checks pass. The 60 form-state and 127 QuickJS cases, box-construction
context and object-fallback checks also pass, along with 12 Qt checks covering
input, JS, loading, details, image/object fallback and iframe sizing. GTK is not
tested.

Remaining URL work: document `base_url` is still cached by legacy parser handling.
Dynamic `<base href>` insertion, removal and attribute edits need HTML's frozen
base-URL/document-base rules at the document mutation boundary, not a click-only
rescan. URL preprocessing and image-map geometry remain legacy code, not a claim
of full URL/image-map conformance; the coordinates parser currently tokenizes
DOM string storage and should be corrected before mutable-map support.

Standards: [DOM dispatch and activation targets](https://dom.spec.whatwg.org/#concept-event-dispatch),
[HTML hyperlink activation](https://html.spec.whatwg.org/multipage/links.html#links-created-by-a-and-area-elements),
[following hyperlinks](https://html.spec.whatwg.org/multipage/links.html#following-hyperlinks),
[element targets](https://html.spec.whatwg.org/multipage/semantics.html#get-an-element's-target)
and [base elements](https://html.spec.whatwg.org/multipage/semantics.html#the-base-element).

### Implemented: live form ownership and document teardown

The shared libdom form-owner resolver replaces raw parser-era owner pointers for
input, button, select and textarea. Ordinary ownership is read from current
ancestry or the connected control's explicit `form=` attribute. Explicit lookup
uses the first matching ID in document order; a non-form match or unresolved ID
leaves the control unassociated. Detached controls use their nearest ancestor
form. Fieldset and object getters use the same ordinary resolution.

Only parser-created identity is stored. Non-ancestor parser owners are retained;
ancestor references are borrowed and checked against live ancestry before use,
avoiding reference cycles. Moving the control separately or changing its `form`
attribute resets that identity; moving a common subtree preserves it. Clones
start without parser ownership. Attribute-node, namespace-aware and fragment
mutation paths share these rules. This preserves HTML's historical parser
associations instead of replacing them with an ancestor-only shortcut.

Form collections retain the form that supplies their predicate context and
follow its current tree after removal/reinsertion. Their membership and native
radio-group owner lookup use the shared resolver. The duplicate `form=` lookup
in Wisp's checkable-state code is removed. Empty-ID handling also moves from the
QuickJS binding into libdom's ID lookup: an empty `id` attribute does not establish
an ID, including when resolving `form=""`.

The lifetime tests exposed two document-level defects. Releasing a retained form
during document destruction could recursively destroy the same tree, and HTML
metadata was freed while surviving nodes still needed it. The document finalizer
now pins the document during tree teardown, retains HTML metadata until pending
nodes are gone, and retries the dynamic document destructor for deferred cleanup.
These repairs apply at the shared lifecycle boundary, not in individual getters.

The focused native suite covers 64 cases: live ownership, duplicate/empty IDs,
parser-created associations, cloning, fragment moves/replacement, attribute-node
edits, live collections surviving caller reference release, and controls surviving
document teardown. Tests use public libdom APIs and real Hubbub parsing.

Validation: all 64 cases pass with Debug ASan/UBSan and leak detection, together
with the 127 QuickJS, 60 form-state and 43 native interaction cases. All ten
selected DOM/JS/form CTest suites pass. Nine Qt checks covering JS, interaction,
loading and details also pass (Qt leak detection disabled); the QuickJS and Qt
DOM checks were rerun after consolidating empty-ID lookup. GTK is not tested.

Scope: native submission now uses the DOM-based transaction described below.
Radio-group side effects on owner/name/type or checkedness edits remain pending.
Submission now uses the separate candidate snapshot described below, and the
public collection excludes image inputs. A complete HTMLFormControlsCollection
still needs fieldset/output/object membership, the corresponding parser-owner
coverage and RadioNodeList behavior. Fragment-parser exclusions and cross-document
adoption are not claimed by this increment. Native click delivery is covered by the later adapter increment.

Standards: [HTML form-owner association and reset](https://html.spec.whatwg.org/multipage/form-control-infrastructure.html#association-of-controls-and-forms),
[HTML insertion/removal steps](https://html.spec.whatwg.org/multipage/infrastructure.html#dom-trees),
[parser-created elements](https://html.spec.whatwg.org/multipage/parsing.html#creating-and-inserting-nodes)
and [DOM element IDs](https://dom.spec.whatwg.org/#concept-id).

### Implemented: DOM-based native submission

Native clicks retain the original input/button before listeners run. Submission
then checks that node's current type, disabledness and form owner, and reads the
current action, method, enctype and target with submitter overrides. Attribute
presence matters: an empty override does not fall back to the form attribute.
Missing/empty actions use the document URL; nonempty actions resolve against the
existing document base URL. Links and forms share current element/base target
resolution.

The submission request owns its URL, referrer, target and serialized data before
target selection can call the frontend. Content pins and retirement checks cover
those callbacks. No rendered control or cached form record is read after click
listeners run. The obsolete action/target/method/charset caches and initial
conversion pass are removed. The remaining presentation registries retain their
DOM form/control nodes until the records are freed, including after DOM removal.

Enter-key submission uses the same path. It chooses the first submit button in
DOM order, including externally associated controls, and dispatches its click.
Cancellation, a disabled default button and document retirement stop submission.
Without a default button, the HTML limit on text-like blocking inputs applies.
Native keyboard event delivery to JS is still pending.

Serialization now uses live DOM form/submitter identity, skips disabled-fieldset
controls except the first-legend exemption, omits ordinary empty-name controls,
and includes image-button coordinates with the required named/unnamed keys.
An absent submitter no longer silently selects the first button. Keyboard image
activation uses zero coordinates rather than retaining an earlier mouse click.
GET, urlencoded POST and multipart POST remain supported. `method=dialog` does
not navigate; dialog closing is not implemented. A `text/plain` POST reports
unsupported instead of sending a differently encoded request.

The old Duktape HTMLFormElement binding was inspected for inspiration. It provides
generated accessors, not a submission transaction suitable for reuse here.

Validation: all seven selected native/JS suites pass with Debug ASan/UBSan and
leak detection: 90 native interaction cases (47 new), 64 DOM form-owner, 60
form-state, 127 QuickJS, 256 box-construction context, 22 form-binding and 22
restyle-tracking cases. Native tests deliberately discard all rendered controls
and cached forms during listeners and cover owner/attribute changes, removal,
disabling, cancellation, navigation retirement, Enter and retained-node lifetime.
Five Qt checks also pass: loading, JS navigation cleanup, JS DOM updates,
checkable interaction and checked selectors (Qt leak detection disabled).
GTK is not tested.

Scope at this stage: validation, callback delivery/guards and entry-list
construction were pending. The later increments below implement Unicode entries
and native SubmitEvent delivery; validation and FormDataEvent remain pending. Legacy charset selection,
other serialization details and URL-scheme-specific submission behavior are not
claimed as fully conforming. Full form collections, dialog/text/plain support,
document base-URL updates and label activation beyond checkables remain pending.
This native submission increment did not itself connect JS callbacks.

Standards: [HTML form submission](https://html.spec.whatwg.org/multipage/form-control-infrastructure.html#form-submission-algorithm),
[implicit submission](https://html.spec.whatwg.org/multipage/form-control-infrastructure.html#implicit-submission),
[constructing the entry list](https://html.spec.whatwg.org/multipage/form-control-infrastructure.html#constructing-the-entry-list)
and [button states](https://html.spec.whatwg.org/multipage/form-elements.html#the-button-element).

### Implemented: SubmitEvent values and shared constructor retirement checks

QuickJS now exposes `SubmitEvent` as an `Event` subclass, with a readonly nullable
`submitter` reference. Its initializer accepts any HTMLElement, including a
detached or disabled element; eligibility to initiate a real submission belongs
to the submission algorithm, not this constructor. Native wrapper identity checks
reject non-HTML elements, unrelated nodes, proxies and forged prototypes without
consulting page-controlled `instanceof` behavior. Dictionary conversion preserves
exceptions and follows inherited/member order before new-target prototype lookup.

The event retains its original submitter wrapper through removal, replacement,
redispatch, base `initEvent`, garbage collection and document retirement. The
reference participates in GC marking and finalization. Synthetic SubmitEvents
use the existing dispatcher and cancellation rules; dispatch alone does not
submit a form or navigate. Cross-document wrapper exposure remains unsupported.
The old Duktape bindings have no SubmitEvent implementation to reuse.

The event adapter no longer infers UIEvent inheritance from enum ordering. This
keeps the new event's allocation, getters and cleanup separate from mouse/UI
payloads without enlarging every base event. Constructor retirement checks now
live at the shared allocation boundary. This also fixes Event/CustomEvent
construction after page retirement from a dictionary getter or new-target
prototype getter, a boundary previously checked only by input-event constructors.

Validation: all 150 QuickJS cases pass with Debug ASan/UBSan and leak detection
(23 added cases). Coverage includes conversion errors/order, interface brands,
subclasses, propagation with DOM removal/replacement, cancellation, reference
cycles after context retirement, allocation-failure recovery and retirement at
three conversion stages across all five event constructors. Two Qt checks pass:
JS navigation cleanup and JS DOM rendering. The latter verifies pixels and
listener-attributed mutation logs for SubmitEvent edits (Qt leak detection
disabled). GTK is not tested.

Scope: this completes SubmitEvent's script-visible data layer. Native delivery
and its reentrancy guard are implemented in the later increment below. Constraint
validation and FormData/FormDataEvent sequencing remain pending.

Standards: [HTML SubmitEvent interface](https://html.spec.whatwg.org/multipage/form-control-infrastructure.html#the-submitevent-interface)
and [submission sequencing](https://html.spec.whatwg.org/multipage/form-control-infrastructure.html#form-submission-algorithm).

### Implemented: submission candidates and successful-control filtering

Native submission and Enter-key default-button lookup now use a libdom-owned
snapshot of the form's submittable built-in elements: input, button, select and
textarea, including externally associated controls and image inputs, in tree
order. Each candidate retains its native node until the snapshot is released.
Later moves, removal or document teardown cannot turn those references into
dangling pointers. A subsequent snapshot observes the current DOM and owners.
Allocation or owner-resolution errors release the partial result and propagate
to the caller instead of silently submitting fewer controls.

This list is distinct from public `form.elements`, which must exclude image
inputs and also list non-submittable elements. The image exclusion is implemented
without affecting submission or the choice of an image as the default button.
Other listed-element membership and RadioNodeList are still pending; the public
collection is not claimed as complete or newly exposed to QuickJS.

Serialization applies disabled-control and datalist-ancestor filtering once at
the shared dispatch boundary. Selected disabled options are omitted, including
options beneath a disabled optgroup through a wrapper. Option disabledness follows
the current HTML ancestor boundaries; the reflected `option.disabled` getter
still reports only its own attribute. Foreign elements with HTML-like names do
not participate. The shared safe node classifier also serves form ownership.

The old Duktape HTMLFormElement and HTMLCollection bindings were reviewed again.
The former contains generated accessor declarations, without a submission
transaction; the latter retains/releases its libdom collection. Native ownership
remains at that boundary here, with current HTML algorithms determining behavior.
No Duktape stack machinery or obsolete submission behavior is copied.

Efficiency: collection now walks the tree once rather than restarting it for
every indexed control. It allocates one growable pointer array per snapshot and
keeps no persistent cache. Owner resolution still has its own ancestor/ID-lookup
cost, so this is not a claim that all form processing is linear. An isolated
Release build (GCC 16.2.1, LTO off), comparing enumeration of the current live
collection with the snapshot for flat forms of unnamed text inputs, measured:

| Controls | Live collection enumeration | Candidate snapshot |
| --- | ---: | ---: |
| 10 | 9.91 µs | 1.04 µs |
| 100 | 621 µs | 8.38 µs |
| 1,000 | 59.91 ms | 84.66 µs |

These are medians of seven alternating measurements (1,000/100/10 repetitions
respectively), including collection creation and cleanup but excluding fixture
construction, serialization and navigation. They demonstrate the removed repeated
scan, not a whole-page speedup or a timing comparison against an older commit.
The temporary benchmark is not part of the browser build.

Validation: 85 DOM form-owner cases and 101 native interaction cases pass with
Debug ASan/UBSan and leak detection (21 and 11 added). The 150 QuickJS, 60 form-state
and 22 form-binding cases also pass. Release passes 86 form-owner cases, including
failure of successive snapshot allocations after retaining earlier entries.
Two Qt checks pass: JS navigation cleanup and JS DOM rendering (toolkit leak
detection disabled). Tests cover tree order, live owner/type changes, detached
forms, retained nodes after teardown, foreign namespaces, option boundaries,
GET/POST filtering, and image-button Enter activation/cancellation by disabledness.
GTK is not tested.

Scope: this implements candidate enumeration and these filtering rules. The
Unicode entry/transport separation described below builds on it; option
selectedness/list membership, charset rules and remaining control-specific
details still need review before exposing complete FormData construction. Native
SubmitEvent delivery and its guard are implemented below; constraint validation
and FormDataEvent sequencing remain pending.

Standards: [constructing the entry list](https://html.spec.whatwg.org/multipage/form-control-infrastructure.html#constructing-the-entry-list),
[form.elements](https://html.spec.whatwg.org/multipage/forms.html#dom-form-elements)
and [option disabledness](https://html.spec.whatwg.org/multipage/form-elements.html#concept-option-disabled).

### Implemented: Unicode form entries and transport encoding

Native submission now constructs an owned array of Unicode entries before
choosing URL-encoded or multipart transport. `form_data.c` owns extraction and
entry lifetime; `form_encode.c` owns byte conversion. The old combined serializer
has been removed from `form.c`. Names and values retain length-bearing libdom
strings, preserving embedded NULs, duplicate names and original newlines. Lone
surrogates from JS DOMStrings become U+FFFD at the entry boundary without changing
the DOM. Already-scalar strings are retained without copying, and the entry array
grows geometrically. No persistent cache or extra threads are introduced.

File entries currently copy the existing native picker path and retain a separate
display filename. URL-encoded requests send that filename rather than the local
path. An unselected file remains an empty file entry even if its HTML value
attribute contains a filename. This representation supports the existing native
picker; it does not yet provide immutable File/Blob payload storage or multiple
selected files required by the public APIs.

Both transport adapters preserve explicit lengths. They normalize newlines at
serialization, apply scalar replacement before encoding, and leave the stored
entries reusable. URL encoding now uses its specified safe-character set,
including unescaped `*`. Legacy output conversion uses decimal character
references for unencodable scalars rather than transliteration; each component
has isolated conversion state. UTF-8 bypasses iconv. Multipart lengths survive
request cloning and reach libcurl, so NULs inside text values are sent intact.
Libcurl's C-string field-name/filename API still cannot send NULs in those
headers: the adapter rejects the whole result with `NSERROR_NOT_IMPLEMENTED`
instead of truncating or sending a partial form. A byte-oriented multipart
header implementation is needed before claiming support for those names.

An on-wire test exposed a separate initialization bug: constructing a hidden
input treated its truncated presentation copy as a user edit and wrote it back
to the DOM. Hidden inputs now establish the same DOM-sourced synchronization
baseline as text/password inputs. The sync path also releases its old retained
DOM value and handles removal of the value attribute. These fixes preserve
script edits before and after gadget creation instead of repairing submission
values after corruption.

Historical Duktape HTMLInputElement, HTMLSelectElement and HTMLTextAreaElement
bindings were reviewed for their shared libdom value model. They mainly contain
generated accessor declarations; no FormData/File/Blob bindings were found in
that snapshot. They informed the ownership boundary, while current HTML, URL
and Encoding specifications determine the new conversions.

Validation: Debug ASan/UBSan with leak detection passes 12 form-data checks,
60 form-state checks, 22 form-binding checks, 101 interaction-lifetime checks and
256 box-construction checks. Release passes 20 form-data checks, including
allocation-failure cleanup and unchanged inputs after failure. Qt checks cover
JS DOM rendering, navigation cleanup and actual GET/URL-encoded POST/multipart
requests to a local server, including Unicode, NULs, lone surrogates, newline
normalization, empty files and absent/empty/whitespace-only `accept-charset`
(nine request cases; toolkit leak detection disabled). GTK is not tested.

Remaining before the public FormData layer: complete option membership and
selectedness, textarea value/wrapping rules, `dirname`, File/Blob storage, and
the remaining encoder review noted below. Encoding-label selection and `_charset_`
are now implemented in the following batch. Complete the remaining entry rules
before exposing FormData/FormDataEvent and connecting submission callbacks.
Native primary-click delivery is covered by the adapter below; public FormData remains pending.

Standards: [HTML entry construction and submission encoding](https://html.spec.whatwg.org/multipage/form-control-infrastructure.html#constructing-the-entry-list),
[URL form serialization](https://url.spec.whatwg.org/#application/x-www-form-urlencoded-serializer)
and [Encoding's HTML error mode](https://encoding.spec.whatwg.org/#encode).

### Implemented: submission encoding selection and `_charset_`

The old substring/first-token charset routine has been removed. Submission now
uses HTML's ordered label selection, with exact ASCII case-insensitive matches
against all 228 labels for 40 encodings in the vendored WHATWG registry. It
preserves the distinction between an absent attribute and an explicitly empty
one, skips unknown labels, respects label order, treats only ASCII whitespace
as separators, and converts UTF-16/replacement output to UTF-8. Length-bearing
attributes prevent NUL-containing labels from becoming valid truncated labels.

`utils/encoding/encodings.json` is the source of truth; its license and update
instructions are alongside it. CMake generates the sorted table into the build
directory. The shared lookup uses a bounded stack buffer and binary search,
returning static canonical names without per-call allocation. Builds do not
download the registry. Canonical names remain separate from iconv backend names:
Shift_JIS uses CP932, EUC-KR uses CP949, and ISO-8859-8-I shares the ISO-8859-8
encoder. Tests include extension characters from the standard's indexes that
the older platform SHIFT_JIS/EUC-KR converters cannot encode.

The shared encoding utility also implements `x-mac-cyrillic` and `x-user-defined`
directly. The former uses a sorted reverse table generated from the vendored
WHATWG index, including the Euro mapping missing from platform MACCYRILLIC; the
latter uses the specified private-use range calculation. Scalar-to-byte
conversion is independent of the DOM and transport. Form submission supplies
the HTML error policy and buffer ownership around it. These encoders have no
platform codec dependency, mutable cache, or per-character allocation.

Hidden inputs named `_charset_` (ASCII case-insensitively) contribute the selected
canonical encoding name while preserving their DOM values. Other input types
and similarly prefixed names remain ordinary entries. Native submission supplies
its selected encoding; entry construction without an encoding uses UTF-8 for
the eventual FormData constructor.

On-wire tests exposed a deeper libdom bug: the memoized strings were generated
from punctuation tokens, so formatting spaces had changed `accept-charset` into
`accept - charset`, `http-equiv` into `http - equiv`, and both select type values
similarly. Explicit spellings are now quoted literals in the shared definitions,
and their consumer no longer stringifies them. Getter/setter and select-type
checks cover this source-level correction. The historical Duktape form binding
used the same libdom accessors; its generated declarations were reviewed again,
but did not contain an encoding-selection algorithm to reuse.

Validation: 45 encoding cases and 21 form-data cases pass Debug ASan/UBSan
with leak detection. The 150 QuickJS, 85 form-owner, 60 form-state, 101 interaction
and 22 form-binding cases also pass after the libdom correction. Release passes
35 form-data cases including allocation failures in both native and UTF-8
encoding paths, plus the encoding cases. Checks cover all 128 Mac Cyrillic
index entries, ASCII and all 128 private-use byte mappings, empty values, NULs,
and unencodable scalars. Qt passes 30 real HTTP submissions (GET, URL-encoded POST
and multipart), JS DOM rendering and navigation cleanup; toolkit leak detection is disabled. GTK is not
tested. This verifies selection and the tested byte conversions, not complete
Encoding Standard conformance of iconv.

Follow-up: review the remaining platform legacy mapping tables against the
standard before claiming full transport encoding coverage. Unsupported platform
encoders report an error rather than silently changing the selected encoding.
The two concrete missing encoders found in this batch are fixed in the core.
This batch adds no JS-visible API or native callback delivery.

Standards: [HTML submission encoding](https://html.spec.whatwg.org/multipage/form-control-infrastructure.html#selecting-a-form-submission-encoding),
[encoding labels and output encodings](https://encoding.spec.whatwg.org/#names-and-labels),
[JIS0208 index](https://encoding.spec.whatwg.org/index-jis0208.txt),
[EUC-KR index](https://encoding.spec.whatwg.org/index-euc-kr.txt),
[single-byte encoding](https://encoding.spec.whatwg.org/#single-byte-encoder)
and [x-user-defined encoding](https://encoding.spec.whatwg.org/#x-user-defined-encoder).

### Implemented: native primary clicks (2026-10-11)

Primary mouse clicks now reach `addEventListener("click", ...)` on DOM nodes,
Document and Window. They share the existing libdom dispatcher with synthetic
events, including capture/bubbling, cancellation, listener snapshots and removal.
The old script-dispatch guard remains a dispatch-ownership check: native clicks
now enter through that same owned dispatch frame. Legacy mutation events and the
old lifecycle stubs are not inadvertently exposed.

Clicks use trusted PointerEvent values inheriting MouseEvent/UIEvent/Event, with
client/screen coordinates, modifiers, button/buttons, click detail, view and core
pointer fields. The Qt release event carries its actual screen coordinates and
held buttons through a small shared input snapshot, including frame routing.
Legacy frontend callers retain the convenience mouse API; those callers do not
yet supply screen coordinates. Non-pointing implicit form clicks have pointerId
-1, empty pointerType and zero detail. Core PointerEvent construction and getters
are exposed; pen angle conversion, coalesced/predicted samples and pointer capture
are not implemented or claimed by this increment.

Each native dispatch shares one execution deadline across all its listeners.
Native callbacks keep their individual microtask checkpoints, allowing a queued
microtask to cancel before later listeners or the default action. Script-dispatched
callbacks still nest under their caller's existing checkpoint. Exceptions are
reported without canceling ordinary dispatch; interruptions stop further listeners.
The adapter retains the JS context, while the existing HTML input transaction pins
content and validates its generation before deferred actions. The trace now includes
`input.dispatch ... origin=browser` followed by listener-attributed DOM mutations.

Checkbox/radio legacy pre-activation and cancellation rollback use the same native
adapter, including forwarded label clicks and accepted input/change events. Disabled
native controls suppress clicks. Link and submit-button clicks respect cancellation
and use live DOM attributes after callbacks. Select/file picker opening now happens
after accepted click delivery and re-resolves the current connected gadget instead
of reusing a pre-callback pointer. Native submissions retain their existing supported
behavior; this increment does not claim the complete HTML submission event sequence.

Qt capture click actions now send real Qt press/release events, exercising the same
frontend route as interactive browsing. Regression coverage includes visible pixels,
trace attribution, disabled JS/controls, delegated listeners, callback checkpoints,
label forwarding and checkbox rollback, picker cancellation/removal, link edits,
script navigation, canceled submissions, scrolling and iframe routing. Native tests
cover payloads, retained event/path identity after removal, nested synthetic events,
exceptions, retirement, deadline recovery and allocation failures. Existing native
interaction, checkable, DOM-rendering and real HTTP submission tests remain relevant.
Validation: Debug ASan/UBSan and LeakSanitizer pass 164 QuickJS cases, 60
form-state cases and 101 native interaction-lifetime cases. Eight Qt regressions
pass: native clicks, DOM rendering, checked interaction, navigation cleanup,
30 HTTP form submissions, details search/groups and frame-scroll/picker reuse
(the latter under Xvfb). Qt leak detection is disabled for toolkit allocations.
No GTK build is part of this validation.

Standards: [DOM event dispatch](https://dom.spec.whatwg.org/#concept-event-dispatch),
[Pointer Events click requirements](https://w3c.github.io/pointerevents/#click-auxclick-and-contextmenu),
[HTML checkable activation](https://html.spec.whatwg.org/multipage/input.html#the-input-element).

### Implemented: native SubmitEvent delivery (2026-10-11)

Native submit-button activation and implicit Enter submission now dispatch a
trusted, bubbling, cancelable SubmitEvent on the form before collecting entries.
The event retains the original submitter wrapper (null for implicit submission
without a button). Capture/bubbling, cancellation, exceptions and per-listener
microtask checkpoints share the bounded native dispatch scope used by clicks.
Page constructors are not consulted. `form.dispatch type=submit origin=browser`
identifies the source in the JS trace; mutations retain their listener entry IDs.

HTML's firing-submission-events flag lives on the libdom form element, survives
box rebuilding, starts clear on clones and blocks recursive submissions of the
same form. Other forms can still submit from a callback. The flag is cleared on
cancellation, interruption and document closure. Content/generation checks and a
fresh connectivity check suppress deferred work on a retired or removed form.

Listeners can change controls, action, method, target and encoding before the
owned request is constructed. Moving/removing/changing the submitter does not
change which form supplies entries: submission attributes are resolved using
its current submit-button state and current form owner. A missing owner uses the
attribute defaults and document base target. Disabled JS retains native behavior.

This increment adds the submit stage to existing native submissions. Constraint
validation is still absent, so invalid controls are not yet blocked from firing
submit or navigating. Add validation immediately before SubmitEvent within the
same per-form guard. FormData/FormDataEvent, direct form.submit()/requestSubmit()
IDL methods, and the separate constructing-entry-list guard remain pending.
The legacy libdom form_submit function is not a modern submission transaction
and must not be exposed directly as the JS submit() method.

The historical Duktape HTMLFormElement binding was reviewed: it contains
attribute declarations but no submission transaction to reuse. The event order,
flags and guard follow the [HTML form submission algorithm](https://html.spec.whatwg.org/multipage/form-control-infrastructure.html#form-submission-algorithm)
and [SubmitEvent interface](https://html.spec.whatwg.org/multipage/form-control-infrastructure.html#the-submitevent-interface).

Validation: Debug ASan/UBSan and LeakSanitizer pass 167 QuickJS, 86 form-owner and
115 interaction-lifetime cases. Native coverage includes retained submitter
identity, native factory isolation, allocation recovery, per-form reentrancy,
clone state, removal/reparenting, presentation rebuilding and close/reopen.
The 30 HTTP encoding submissions now edit fields and encoding inside submit
listeners and pass with the final request bytes checked. The Qt submission suite
passes 18 additional cases, including cancellation/checkpoints, repeated canceled
submissions, exceptions, timeout, relative navigation and implicit submission.

### Implemented: shared parser-time base-URL correction (2026-10-11)

The submit callback probe exposed two underlying bugs: Location parsed relative
references as standalone URLs, and the HTML base-element handler did the same
for relative href. HTML also let later base elements overwrite the first one.
Base href now resolves against the document URL, with only the first connected
HTML base carrying href taking effect. Empty href uses the document URL; data:
and javascript: bases use the fallback, without allowing a later base to win.
This corrects shared CSS, script, image and form resolution with JS disabled too.
Location assign/replace now use the same document base after argument conversion
and the existing live-context check. The work adds no new Location API.

Scope: parser-time base selection and reference resolution. Dynamic base href
changes/removal/reordering, srcdoc/about:blank fallback inheritance, CSP base-uri,
URL encoding and a complete WHATWG URL parser are not completed by this fix.
The malformed-host probe `http://[::1` also exposed a separate existing parser
inconsistency: nsurl_create rejects it, but nsurl_join accepts it and curl later
rejects the request. Record this under URL-parser validation work; do not add a
base-only host validator. This invalid-host probe is not a passing base test.

Validation: Qt passes 12 resource/submission cases covering relative/absolute
bases, first-href selection, empty href, forbidden schemes and CSS/image/script
requests, with scripting disabled and enabled. Existing browser-loading and script
ordering regressions pass. Across the completed callback/base batch, nine Qt
suites pass (the preceding cases plus native clicks, DOM rendering, checkables,
navigation cleanup and form encodings). Toolkit leak detection is disabled; native
sanitizer results are recorded above. No GTK build was run.

Standards: [HTML base element](https://html.spec.whatwg.org/multipage/semantics.html#the-base-element),
[document base URLs](https://html.spec.whatwg.org/multipage/urls-and-fetching.html#document-base-urls)
and [Location URL resolution](https://html.spec.whatwg.org/multipage/nav-history-apis.html#dom-location-assign).

### Deferred interaction completeness

- Constraint validation belongs before SubmitEvent in the same per-form guard.
  FormData/FormDataEvent entry mutation needs its separate constructing-entry-list
  guard and the remaining Unicode-entry/File/Blob work. Do not gate basic page
  rendering on complete form collections or an exhaustive encoding audit.
- Keyboard delivery needs a frontend/core boundary preserving key/code,
  press/release, modifiers, repeat and composition, with cancellation before edits.
- The pointer stream still needs down/up/motion, boundary/auxiliary/context-menu/
  double-click events, capture and press/release target reconciliation. Complete
  synthetic MouseEvent activation/default actions when this work is resumed.
- Live radio owner/name/type changes, further label focus/activation, dynamic
  document base URLs and cross-document JS objects remain independent work.

Document readiness and lifecycle now take priority over this deferred list. The
current load call precedes the initial box build and cannot simply be exposed to
JS. Inline handler support should be driven by concrete site initialization or
reading interactions, rather than completing the whole event API first.

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
   DOM/Window/standalone JS events, abort signals, UI/mouse event data and native
   checkable pre-activation, primary-click and SubmitEvent delivery implemented.**
   Per the current reading-site priority, DOMContentLoaded/load and accurate
   readyState come next, followed by concrete page initialization blockers.
   Further form and input completeness is deferred. Preserve propagation,
   cancellation, callback identity and navigation cleanup in each increment.
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

# Wisp JavaScript state and Duktape reuse

Reviewed 2026-10-07 against Wisp `206b8a03`, plus the QuickJS-ng update and
XHR conversion fix described below. This replaces the earlier migration analysis.
It is a browser-integration review, not an exhaustive security or ECMAScript
conformance audit. Batch 1 is implemented below; batches 2–5 remain future work.

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
| Promise/async continuations | Jobs can be enqueued, but production code never calls `JS_ExecutePendingJob` | Promise callbacks, `await` continuations and engine `queueMicrotask` callbacks do not progress |
| Timers | `timers.c`: returns constant IDs, schedules nothing | `setTimeout`/`setInterval` callbacks never run |
| Document/nodes | `document.c`: selectors return null/empty arrays; roots and created nodes are new plain objects | No JS-to-libdom wrapper layer or stable node identity |
| Mutation/CSSOM | Dummy `style`, `classList`, text and tree operations | JS cannot change the rendered DOM through these bindings |
| Events | `event_target.c` and `js_fire_event`/`js_handle_new_element` are stubs | No real JS listeners, inline event handlers or load-event delivery |
| Lifecycle information | `document.readyState` always returns `complete` | Scripts receive incorrect loading state |
| Networking/storage | XHR fabricates DONE/200 without a request; storage discards writes; `document.cookie` is empty/ignored; no browser fetch binding | APIs appear available without providing their promised behavior. Native HTTP cookie handling is a separate subsystem |
| Navigation | `location.c`: real URL getters and calls to navigation for assign/replace | Partial support: href has no setter, reload is a stub, relative URL resolution/history replacement need review; assign and replace currently use the same flags |
| Navigator | Hard-coded Windows Chrome 91 UA, English language and cookies-enabled value | JS identity differs from Wisp's actual HTTP UA and capabilities |
| Generated bindings | `utils/qjs_binding_generator.py` generates Console and a generic Window stub | Not a complete WebIDL binding generator; see below |
| Script loading | `html/script.c` fetches classic scripts and has async/defer bookkeeping | Ordinary external scripts do not block the parser; ordering is wrong. Browser module loading is not implemented |
| `javascript:` URLs | `javascript/fetcher.c` returns 204 | Does not execute the URL's code |

### Confirmed host problems, in priority order

1. **Execution controls are now connected.** The outer `js_exec` entry starts a
   monotonic deadline; QuickJS's interrupt callback stops runaway execution.
   Nested entries share the deadline, and subsequent independent entries receive
   a fresh budget. Future callback/microtask checkpoints must remain inside this
   scope: renewing it for every job would let infinite Promise chains evade the
   limit. There is no nested GUI event pumping or new execution thread.

2. **Classic script ordering is broken.** A local page containing an external
   script followed by an inline script logs the inline script first; the external
   variable is still undefined. `exec_src_script` deliberately skips parser
   pausing, calling this speculative parsing. Allowing the actual DOM parser and
   later scripts to continue is not a speculative preload scanner. Restore the
   required parser-blocking behavior; a separate preload optimization can wait.
   Async/defer ordering, parser completion and reentrant navigation need explicit
   fixtures before claiming those paths are conformant.

3. **There is no real document binding.** A local page confirms both
   `document.documentElement === document.documentElement` and lookup of an
   existing element ID return false. A cached dummy object would fix only the
   first symptom. The root fix is to wrap actual libdom nodes, preserve identity
   and own references correctly.

4. **Mutation must reach rendering.** Existing partial-update work is useful but
   is not a generic JS mutation pipeline. `html_restyle_note_dom_change` advances
   a generation; it does not itself schedule style selection, box construction
   or paint. Current DOM default actions handle selected cases such as title,
   style and form content, not arbitrary class/attribute/text/tree changes.
   Connect all supported mutation paths to one invalidation interface. Start with
   the existing full-update fallback, coalesced at a safe boundary, then optimize
   measured cases. Never assume `dom_element_set_attribute` alone updates pixels.

5. **Advertised capabilities and tests need to become truthful.** A probe sees
   no timer/Promise callbacks, but XHR reports 200 without fetching anything.
   Replace placeholders as each capability is implemented; for deferred APIs,
   decide deliberately whether to omit them or expose a specified unsupported
   behavior. Do not keep fabricated success as a compatibility strategy.

The probes are saved under ignored `out/js-review-2026-10-07/confirmed/`.
These are diagnostic observations, not new passing conformance tests. The script
ordering and DOM limitations predate the engine update; their implementing Wisp
paths were not changed by it.

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
Promise rejection reporting remains part of the future checkpoint work.

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
- No new Web API or job draining is enabled. Script ordering, timers, DOM binding,
  mutation delivery and event dispatch still require subsequent batches.
- QuickJS interrupts are cooperative. Long native operations and page-wide totals
  across many separate scripts are not bounded by this per-entry deadline. Future
  checkpoints must share the outer scope, and new native bindings must propagate
  exceptions and avoid unbounded native work. This is not a complete hostile-code
  sandbox or browser-wide resource audit.

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
2. **Script/task correctness.** Restore classic script ordering and define
   microtask checkpoints, then implement real timers through the existing main
   event-loop scheduler. Cover ordering, cancellation, self-cancelling timers,
   navigation during execution, exceptions and runaway job chains. Update the
   navigation regression: it currently intentionally depends on Promise jobs
   remaining unexecuted during normal operation, which must change when normal
   checkpoints work. Teardown itself must continue to run no page callbacks.
3. **Minimal real DOM plus visible mutations.** Node/Element/Document wrappers;
   document roots, ID lookup, traversal, attributes/classList and text changes;
   coalesced renderer invalidation. A small page should change a class and text
   and produce the correct DOM, boxes and pixels. Require identity and GC tests.
   Use a correct full refresh initially; reuse partial updates only with sound
   mutation/dependency tracking.
4. **Events and practical document interaction.** EventTarget/Event, click
   listeners and supported handler attributes, DOMContentLoaded/load and accurate
   readyState; then selected tree/fragment operations. Validate propagation,
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
  [EventTarget](https://dom.spec.whatwg.org/#interface-eventtarget) and
  [selector queries](https://dom.spec.whatwg.org/#dom-parentnode-queryselector)
- [Web IDL object bindings](https://webidl.spec.whatwg.org/#idl-objects)

Use reduced fixtures with specification-derived expectations. Chromium/Firefox
comparisons remain useful diagnostics, not the authority for the behavior.

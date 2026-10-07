# QuickJS-ng upstreaming candidates

Recorded 2026-10-07. This is a proposal for later work; no issue or pull request
has been submitted and acceptance has not been discussed with maintainers.

## Current baseline

Wisp vendors QuickJS-ng **v0.17.0**, commit
`6d46d07d04041b40f4f49eaa7fdebe44c314c699`.
See [vendor provenance and upgrade procedure](../../contrib/quickjs-ng/README.wisp.md)
and the [JavaScript integration review](../../plans/js_migration_analysis.md).
The remaining upstream-file changes are in `quickjs.c`, `quickjs.h`,
`CMakeLists.txt` and `api-test.c`. Wisp's browser bindings are a separate
integration layer.

## Newly found candidate: property-list error propagation

The execution-controls work found another small engine bug in v0.17.0:
`JS_SetPropertyFunctionList` can return success after allocation or property
installation fails. Its internal helper ignored the returns of lazy-property,
accessor and value definition; it also passed failed getter/setter allocations
onward. Wisp's allocation-budget tests reproduced this while initializing
Navigator (success returned with an outstanding allocation exception).

The local fix propagates these failures and releases temporary values correctly.
`api-test.c` now has a standalone regression covering allocation budgets and a
non-configurable property conflict. Wisp tests all nine binding initializers,
including generated Console/Window, under limited allocation budgets.

This is likely the simplest proposal to pursue first: a general C API correctness
fix, without a new public API. Verify it against current upstream, run its suites,
and submit independently of cancellation. The regression is already in upstream
API-test style, but has not been submitted.

## Newly found candidates: host object allocation failures

The DOM-wrapper work found two further small allocation-failure candidates in
v0.17.0, also fixed locally and covered by `host_object_allocation_errors()` in
`api-test.c`:

- `JS_SetPrototypeInternal` increments the proposed prototype's reference count
  before an operation that can fail while copying a shared shape. Move that
  increment after the fallible operation; otherwise runtime cleanup detects a
  leaked object. This also affects the public JavaScript prototype-setting path.
- `JS_AddIntrinsicDOMException` ignores failures while creating its prototype,
  accessors, constructor and legacy constants. Propagate those failures, release
  temporary values and publish the intrinsic only after setup succeeds.

Propose these independently of document-job cancellation. They need no new API,
and the browser uses the existing DOMException intrinsic rather than maintaining
its own exception class. No upstream submission has been made.

## Context job cancellation

`JS_CloseContextJobs(ctx)` permanently closes a context's job queue and releases
its pending jobs without executing callbacks. Other contexts sharing the runtime
retain their jobs and their relative order. Subsequent enqueues for the closed
context are discarded. Closing twice is harmless.

The checkpoint work also adds `JS_DiscardContextJobs(ctx)`: one-time queue
removal after an execution timeout. It suppresses enqueues while arguments are
released, then restores the previous open/closed state. Later independent entries
can enqueue jobs again; an already-closed context stays closed. Include this
separate use case when discussing the upstream API shape, rather than conflating
aborting one entry with permanently retiring a context. Wisp tests finalizers
during discard, sibling jobs, new enqueues and discard after permanent closure.

A browser needs permanent closure when retiring a document: draining jobs during teardown can
run page callbacks that navigate or touch objects being destroyed. Destroying the
whole runtime is inappropriate when another document still uses it. The API also
has potential value to other embedders that dispose of individual contexts.

This is a plausible upstream contribution, not guaranteed acceptance. The
[merged JS_GetPendingJobContext proposal (#1419)](https://github.com/quickjs-ng/quickjs/pull/1419)
is evidence that the project accepts small embedding APIs for job management.
That API only identifies the next job's context; it does not cancel jobs.

Before proposing this API:

1. Rebase the isolated engine patch onto current upstream and check for an
   existing equivalent API or discussion.
2. Explain permanent closure versus one-time queue removal. Confirm whether
   silently ignoring later enqueues is the interface upstream wants.
3. Document ownership and reentrancy. Mark the context closed before releasing
   arguments: their finalizers can try to enqueue more jobs. Later GC must also
   be unable to repopulate the closed queue.
4. Move focused tests into upstream `api-test.c`: mixed Promise/queueMicrotask
   jobs, unaffected contexts, argument release, repeated closure, enqueues after
   closure and FinalizationRegistry during cancellation and later GC. Wisp
   already covers these basic cases. Add cross-context callback and reentrant
   native-finalizer cases before claiming the semantics are fully covered.
5. Run upstream API/local JS suites and applicable CI configurations. Include a
   small embedder reproducer and a clear lifecycle rationale in the proposal.

Queue cancellation does not interrupt a currently executing script. Execution
limits belong to Wisp's use of the existing interrupt-handler API, independently
of this proposal. Keep the local patch until an accepted equivalent is available
and tested in the browser.

## Separate candidate: optional build targets

Offer generic CMake options for embedders to omit CLI/compiler/test targets while
retaining upstream's existing default behavior. Wisp embeds the library and does
not normally need these tools in its default build.

The [upstream CMake file](https://github.com/quickjs-ng/quickjs/blob/master/CMakeLists.txt)
already has `QJS_ENABLE_INSTALL`; at the review date it still defines the CLI,
compiler and test executables without matching optional-build controls. Recheck
this before preparing a patch. Proposed option names and interactions with
generated bytecode, examples, tests and installation need upstream agreement.

Keep this proposal separate from cancellation so each can be reviewed on its own.
Do not upstream `WISP_*` options, Wisp library-type choices, parent Windows policy,
or Wisp-specific installation rules. Those belong in the parent integration.

## Expected benefit and order

Pursue the property-list and host-object cleanup fixes first, cancellation next,
then generic build controls if still useful. Acceptance of those engine changes
would remove Wisp's remaining engine-source patches; build options could further
reduce the CMake patch. These
proposals do not replace the browser work
in the integration review. Continue testing the vendored version in Wisp after
each upstream update, even when a local patch is no longer needed.

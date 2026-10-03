# Correctness backlog

Only unresolved work belongs here. Completed changes and validation history are
in Git; the regression fixtures remain in the test suite.

## Find in Page coverage

The DOM search mode used for documents containing hidden-until-found does not
discover generated `::before`/`::after` text. Ordinary documents' box search can
find it. Add stable generated-content identity across rebuilding; do not keep
stale box pointers. Cross-node phrases and revealing closed `details` elements
also remain unimplemented. Wrapped fragments of one DOM text node already work.

The supported behavior and repeatable checks are in
[hidden-until-found.md](hidden-until-found.md).

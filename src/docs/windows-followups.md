# Windows frontend follow-ups from the matthewb2 fork

Reviewed on 2026-11-12 at
[`fd6e88e80e12a27dbe1f14a4ea25bd0e7f17d124`](https://github.com/matthewb2/wisp/tree/fd6e88e80e12a27dbe1f14a4ea25bd0e7f17d124).
These are deferred candidates for native Windows development. The review was
source-only; neither improvement has been imported or tested on Windows here.
They concern `frontends/windows`, not the Qt frontend running on Windows.

The fork's explicit UTF-8 read in `parser_wrapper.py` has been adopted separately.
It matches the encoding written by `property_generator.py` and avoids failures
under Windows locale encodings such as CP949.

## Reuse the parent console for logging

Our `frontends/windows/main.c:nslog_ensure()` allocates a console when its output
stream has no usable handle. The fork first tries
`AttachConsole(ATTACH_PARENT_PROCESS)` and falls back to `AllocConsole()`, allowing
logs to appear in the terminal that launched the browser.

Reference:
[fork main.c](https://github.com/matthewb2/wisp/blob/fd6e88e80e12a27dbe1f14a4ea25bd0e7f17d124/frontends/windows/main.c#L142-L158).

Extract this small change rather than its containing commit, which also includes
unrelated JS changes. Preserve redirected streams, check console attachment and
stream reopening failures, and handle an already attached console correctly.
Validate launches from Command Prompt/PowerShell and Explorer, plus stdout/stderr
redirection to files and pipes. Use the existing logging system without the
fork's unconditional diagnostic output.

## IME composition and committed text

The fork adds Windows IME handling for Korean input, including composition
start/update/end messages, an inline preview, caret tracking, and conversion of
committed UTF-16 surrogate pairs to the core's Unicode code points. Keeping
uncommitted composition separate from text inserted into the document is useful.

References at the reviewed revision:

- [window.c](https://github.com/matthewb2/wisp/blob/fd6e88e80e12a27dbe1f14a4ea25bd0e7f17d124/frontends/windows/window.c#L66-L245): composition lifecycle and committed text.
- [drawable.c](https://github.com/matthewb2/wisp/blob/fd6e88e80e12a27dbe1f14a4ea25bd0e7f17d124/frontends/windows/drawable.c#L396-L700): preview measurement and painting.
- [input-test.html](https://github.com/matthewb2/wisp/blob/fd6e88e80e12a27dbe1f14a4ea25bd0e7f17d124/src/resources/input-test.html): reusable manual input/textarea fixture.

Treat this as an implementation reference, not a patch ready to import:

- Replace the fixed 256-unit buffers and 255-unit read limits with storage sized
  from the IME result. Preserve complete Unicode characters and handle failures.
- Account for the composition cursor and attributes/selection. The fork tracks
  only its text and moves the caret to the end of the preview.
- Decide whether native composition UI can meet our needs before maintaining a
  custom overlay. The fork hardcodes Malgun Gothic, black text, a white background
  and font metrics inferred from caret height. A custom preview should use the
  focused control's actual style, metrics and clipping.
- Integrate caret and candidate positioning with scrolling, zoom and DPI changes.
  Clear composition state and repaint correctly on cancellation, focus changes,
  navigation and window destruction.
- Audit interaction with `WM_CHAR`, `WM_IME_CHAR` and default message processing
  so committed text is inserted exactly once. Keep composition previews out of
  the document and use the normal input path for committed text.

Validate on native Windows with Korean input, and Japanese/Chinese composition
where available: inputs and textareas, insertion in the middle of text, selection
replacement, cancellation, candidate selection, long results and supplementary
Unicode characters. Include styled controls, zoom/HiDPI, scrolling during
composition, and changing focus or navigating away. Check existing input/change
event behavior as well as visible text; add focused automated coverage for any
shared core changes.

Do not bring over the fork's TLS paths, JS lifecycle changes, forced logging or
bundled installer as part of either task. Those are unrelated to these candidates.

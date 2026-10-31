# Large-file highlighting observations and withdrawn plan

The owner withdrew the policy below in #41/#44 and closed #42/#43 as not planned.
It remains an historical experiment record. Production retains the selected
language's lexer at every document size; manual Plain Text uses the existing
Null lexer and style-buffer release. There is no independent highlighting
preference or size-dependent pause. Successful first Save and every explicit
Save As reidentify from the filename, including the same path or extension;
ordinary Save retains the current language choice.

The existing original core can avoid and later release its document-sized style
buffer. A policy in the document session can use this capability while retaining
the user's language choice; a new core storage implementation is not proposed.

The owner approved three planned implementation slices and an initial Automatic
threshold of **32 MiB of decoded UTF-8 document content**, after BOM removal.
Formal policy/UI implementation has not started and requires a later instruction.

## Baseline and experiment

The previously verified syntax/folding and architecture work was fast-forwarded
into main at `48739f80dc3128db3a8288d063bb700ed40454e6` before this investigation.
The standalone probe is retained outside main at
[`2e0efdace6a4ded6a17920ef86a3b86c1fb244c7`](https://github.com/1298345095/wxnotepad/tree/2e0efdace6a4ded6a17920ef86a3b86c1fb244c7/experiments/large-file-styling),
on `codex/probe-large-file-styling`. Its build switch is off by default. Production
source files are unchanged on that branch.

The experiment uses the real editor and document/search sessions with hidden
native windows. It measures synchronous Open plus initial 4096-byte styling,
process memory, and pause/resume transitions. It is not a full application startup
or rendering-latency benchmark. Windows uses MSVC and wxWidgets 3.3.3#1; Linux uses
the native vcpkg wxWidgets build and isolated X11/Wayland displays.

The normal case opens a C++ file through the actual automatic-language path. The
no-style case opens identical bytes with a `.txt` filename, through the same file
operation: it is a proxy for loading without styling, not an implemented deferred-
language feature. Later direct runtime-style toggles preserve session metadata.
The future product implementation must preserve the resolved C/C++/Python/JSON
language even when its active lexer is temporarily disabled.

## Results

Each row is the median of three fresh processes. Fixtures contain short repeated
C++ lines and one unique search marker. Samples are warm in the filesystem cache;
current memory is sampled after 300 ms. All 54 processes completed, with nine
phase snapshots each, and all 54 independently read complete output hashes matched
their original input hashes.

Memory columns are MiB. Windows reports private committed bytes and its process
peak commit; Linux reports RSS and VmHWM. Compare paired cases within a backend,
not absolute memory values between platforms. Peak columns are process lifetime
high-water marks observed immediately after the initial Open phase, before edits.

| Backend | Document MiB | Styled memory | No-style memory | Styled peak | No-style peak | Styled Open ms | No-style Open ms |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| windows | 10 | 25.88 | 15.05 | 35.14 | 25.37 | 25.78 | 21.56 |
| windows | 32 | 71.86 | 37.77 | 101.95 | 70.93 | 53.37 | 54.92 |
| windows | 100 | 216.16 | 109.63 | 310.12 | 212.78 | 171.41 | 143.13 |
| x11 | 10 | 107.72 | 97.24 | 116.68 | 107.19 | 20.44 | 15.60 |
| x11 | 32 | 153.85 | 120.18 | 183.52 | 152.39 | 66.21 | 50.76 |
| x11 | 100 | 296.50 | 190.37 | 389.83 | 292.98 | 190.35 | 159.69 |
| wayland | 10 | 53.79 | 42.70 | 62.66 | 52.46 | 19.05 | 16.82 |
| wayland | 32 | 99.33 | 65.74 | 128.63 | 97.68 | 61.92 | 51.60 |
| wayland | 100 | 241.67 | 135.47 | 334.77 | 237.79 | 189.24 | 151.62 |

For 100 MiB, avoiding initial styling reduces steady-state memory by approximately
106 MiB on every backend, and the observed opening peak by about 97 MiB. Opening
without styles avoids the allocation peak; enabling first and pausing later cannot
erase a peak already reached.

Disabling the lexer after re-enabling it released a median 103.33 MiB of Windows
private commit and 100.00 MiB RSS on each Linux backend for the 100 MiB case. No
heap-trimming operation was forced. An earlier edit can grow text/style capacities,
so the amount reclaimed depends on allocation history; pausing does not promise a
return to the exact memory level of a freshly loaded plain document.

The main observed benefit is memory. Most no-style API timings were lower, but
the Windows 32 MiB pair was slightly slower without styling. Three repeats and
these API measurements do not establish a startup-speed requirement or an optimal
threshold. The approved 32 MiB value is an initial product policy: the measured
style-memory increment there was approximately 34 MiB.

## State and behavior observations

The probe records and checks document length, dirty state, Undo/Redo availability,
anchor/caret, document and selection revisions, language, encoding/BOM, active
lexer, first style index and end-styled position. Pausing and resuming retained
the document state while changing the active lexer as expected.

The sequence also exercises an edit, Undo while paused, resume with a pending redo
branch, a second pause, Redo/Undo, literal search/replacement and Undo, and Save As.
Complete saved-byte hashes match the source. These observations support the core
mechanism. Formal tickets additionally require native menu/status integration,
fold visibility, regular-expression regressions, language transitions and file
failure/cancellation cases; this probe does not claim those new UI behaviors exist.

## Pinned-source evidence

The inspected source files match original revision
`89b8b36e0ee2c23f73be922315f17261ccfbbe1a`:

1. `LexState::SetLexer` in `scintilla/src/ScintillaBase.cxx:707` calls
   `Document::LexerChanged(language != SCLEX_NULL)`.
2. `Document::LexerChanged` in `scintilla/src/Document.cxx:2616` calls
   `CellBuffer::EnsureStyleBuffer`.
3. `CellBuffer::EnsureStyleBuffer` in `scintilla/src/CellBuffer.cxx:574` allocates
   by text length when enabled and calls `style.DeleteAll()` when disabled.
4. `SplitVector::DeleteAll` delegates to full-range deletion, whose `Init()` calls
   `clear()` and `shrink_to_fit()` (`scintilla/src/SplitVector.h:111,352,365`).

The runtime observations confirm reclamation for the measured allocations and
platform libraries. The original checkout and core patch were not modified.

The probe passed clang-format 23.1.2 checking. Its translation unit was checked
with native clang-tidy 23.1.2 on Windows and 18.1.3 on Linux; both processes exited
zero with no reported probe-source diagnostics. Dependency diagnostics were
suppressed by the experiment header filter, so this is not a whole-project scan.

## Approved implementation breakdown

Published overview: [#41](https://github.com/1298345095/wxnotepad/issues/41).
The three open implementation tickets have verified native dependencies:
[#42](https://github.com/1298345095/wxnotepad/issues/42) ->
[#43](https://github.com/1298345095/wxnotepad/issues/43) ->
[#44](https://github.com/1298345095/wxnotepad/issues/44).
Publication does not start implementation.

1. **Manual highlighting control**, no blockers: Automatic/On/Off, distinct from
   language choice; pause/resume, status, fold-command availability and existing
   text/history semantics. Automatic initially retains existing normal styling
   until the size-policy slice lands.
2. **Automatic large-file opening**, blocked by 1: pause at 32 MiB or more of
   decoded UTF-8 content, before inserting into a styled buffer or enabling the
   lexer; preserve safe file replacement and allow manual On.
3. **Naming and language transitions**, blocked by 2: re-evaluate Automatic after
   successful first Save/path-changing Save As, an actual language change, or
   explicitly choosing Automatic; retain manual On/Off.

Successful New/Open resets highlighting preference to Automatic. Typing,
replacement, Undo/Redo and ordinary same-path Save do not unexpectedly toggle
styling or reset folds. Plain Text remains unstyled regardless of preference.
Manual On/Off survives file naming and language changes. Pausing reveals folded
text; previous fold expansion choices need not be restored on resume.

Persisted settings, threshold configuration UI and background threshold monitoring
are outside this batch. Linux IME remains deferred. The initial threshold is a
product default, not a performance limit or a guarantee about total file-I/O memory.

## Evidence preservation

Raw results, phase CSVs, process/build/analyzer logs, source archives and hashes
are retained at `E:/NewNotepad4/wxnotepad-evidence/large-file-styling/`.
Windows measurements ran from `build-style-probe`; Linux source, builds and samples
were on `/home/ctrls/.cache/wxnotepad-style-probe`. Measurements ran sequentially
after builds, without concurrent compiler work. No screenshots or shared-desktop
input were used. Saved scratch outputs were removed only after their hashes were
verified; fixture recipes, input hashes and result logs remain.

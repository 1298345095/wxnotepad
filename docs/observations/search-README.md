# Search and replacement validation

Date: 2026-10-30, America/Los_Angeles.
Spec: [#21](https://github.com/1298345095/wxnotepad/issues/21), with implementation
tickets #22–29. Integration branch: `codex/implementation-search`.
Validated code revision: `04537629dae25d2a1d739e42751823959fbe91cf`.

## Delivered behavior

The shared modeless Find/Replace view supports literal next/previous search,
wrapping, case and whole-word options, single replacement and full-document
replacement. Regex mode uses the pinned original Boost matcher and the original
capture formatter. A small maintained core patch retains document-start
assertion context when the search candidate range begins later in the document.
Previous search also retains cross-line and lookahead context.

Document and selection revisions invalidate stale regex confirmations. Empty
matches advance on UTF-8 character boundaries, including the eligible EOF match.
Replacement counts remain visible after queued document-state updates. Replace
All groups actual changes into one Undo action and preserves history for no-op
operations. Saved bytes are independently checked through the file-operation seam.

No new runtime dependency or GoogleTest dependency was added. Linux IME remains
deferred. This work does not establish a new performance budget or remeasure the
earlier full-application resource observations.

## Final verification

| Check | Result |
| --- | --- |
| Windows native Release build | Passed |
| Windows related regression suite | 20/20 passed |
| Linux native Release build | Passed |
| Private X11 full regression suite | 22/22 passed |
| Private Wayland full regression suite | 22/22 passed |
| clang-format 23.1.2, tracked project C++ files | Dry run with `--Werror` passed |
| Standards review | Two small refactors resolved; no outstanding findings |
| Spec review | No actionable findings; no drift after review refactors |

Windows covered all eight search/replacement tests plus existing editing, file,
encoding, repaint, font, IME-geometry and DPI checks. Its unchanged cross-process
clipboard and three native file-dialog suites were not rerun in this final batch.
Both isolated Linux runs covered the full suite, including those native workflows.
Tests use owned native windows/events and independent expected text/ranges; no
screenshots were inspected. Final commands use `--no-tests=error`.

Review refactoring extracted the six common native test operations into one
header used by eight independent GUI test programs. Their test bodies and expected
literals were verified unchanged. The cached empty-match identity is one optional
record instead of three separate sentinel fields.

## clang-tidy

Each native scan analyzed seven production and eight newly added test translation
units using its CMake compilation database. Checks were
`clang-analyzer-*`, `bugprone-*`, `performance-*` and `portability-*`.
All 15 analysis processes exited successfully on each platform; warnings remain.

| Platform/tool | Production before search | Final production | Final including tests |
| --- | ---: | ---: | ---: |
| Windows clang-tidy 23.1.2 | 66 | 74 | 85 |
| Linux clang-tidy 18.1.3 | 47 | 48 | 57 |

The additional Windows production reports concern valid wxWidgets integer
style/modifier masks and the existing `#pragma once` convention. Retained test
reports concern positional test-case arguments, GTK's valid zero modifier mask,
the Win32 LPARAM callback protocol and header conventions. No swapped-argument
or invalid-pointer defect was established, and no global suppression was added.

Linux additionally reports a null query pointer at `ShowFind` after tracing a
trivial optional match-record destruction as assignment to the unrelated query
field. The real lifecycle creates the query before binding its callback and only
hides the dialog on close; the integral match record cannot assign that pointer.
The complete infeasible-state trace and rationale are retained in the review-fix
evidence. Earlier retained production diagnostics are described in the
[previous clang-tidy report](clang-tidy-README.md).

## Build reliability and evidence

The Windows Ninja validation exposed mismatched console code pages: PowerShell
input CP936 and output CP65001 made CMake decode MSVC's include trace incorrectly,
so Ninja recorded no header dependencies. Fresh configure/build with both console
encodings set to UTF-8 fixed the reproduced problem. Final dependency records
include the editor and shared test headers, and a header-only rebuild was verified.
Changing `VSLANG` alone did not fix this installed toolchain. The normal Visual
Studio generator build instructions are unchanged.

Logs, per-ticket red/green evidence, native display logs, analyzer output and
triage, source snapshots, review reports, runtime binaries and checksums are
preserved outside the checkout:

```text
E:/NewNotepad4/wxnotepad-evidence/search-phase2
```

Key records are `windows-reviewed/`, `linux-reviewed/`, `review-fixes/`,
`review-standards.md`, `review-spec.md`, `msvc-dependencies/`, `completion.json`
and `CHECKSUMS.sha256`. The review-fix source tree and integrated tree are identical;
the final native static scans therefore apply to the integrated code without a
redundant scan of that same tree.

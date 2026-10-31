# Original scheme behavior validation

The owner replaced the proposed automatic large-file highlighting extensions
with pinned original Notepad4 behavior. Revised
[overview #41](https://github.com/1298345095/wxnotepad/issues/41) is delivered by
[ticket #44](https://github.com/1298345095/wxnotepad/issues/44).
The independent highlighting preference and 32 MiB policy in #42/#43 were
withdrawn, closed as not planned, and not merged. Their unfinished source/logs
remain archived separately.

The preceding verified everyday-editing work was fast-forwarded into main and
pushed at `f0a33845dcbe5bcee70e5bc4cf5643d82d9c1009`. This correction builds on
that baseline on `codex/original-scheme-behavior`.

## Behavior and original-source comparison

- First Save and Save As initially select the current scheme type, followed by the
  other supported types and All Files. Specific types force their scheme as an
  explicit document language, including unchanged extensions and identical paths.
  All Files clears the override and classifies the returned filename.
- The untitled default/current/selected Plain Text exception also classifies the
  filename. A named Plain Text document selecting Plain Text has no exception;
  neither does an untitled non-Plain document selecting Plain Text. The default
  supported scheme is Plain Text. Path-only APIs represent the current scheme;
  callers can explicitly supply All Files or a specific language.
- Ordinary Save retains the current manual/automatic choice and fold/view state.
  Failed/cancelled file operations retain the existing transactional state. The
  override changes only after successful disk replacement.
- The selected language's lexer remains active above the former 32 MiB cutoff.
  Plain Text uses the existing null-lexer/style-buffer-release path. No separate
  highlighting preference, pause state, threshold or delayed allocation was added.
- Encoding/BOM/EOL bytes, final-newline presence, history/save points, safe writes
  and the existing window-level view/indentation choices retain their contracts.

Pinned upstream revision: `89b8b36e0ee2c23f73be922315f17261ccfbbe1a`.
Its [normal existing-file Save and successful naming branch](https://github.com/zufuliu/notepad4/blob/89b8b36e0ee2c23f73be922315f17261ccfbbe1a/src/Notepad4.cpp#L7094-L7129)
separate ordinary saving from first Save/Save As. The successful naming branch
honors the type set by [SaveFileDlg](https://github.com/zufuliu/notepad4/blob/89b8b36e0ee2c23f73be922315f17261ccfbbe1a/src/Notepad4.cpp#L7302-L7319)
before falling back to `Style_SetLexerFromFile`. [Style_GetFileDialogFilter](https://github.com/zufuliu/notepad4/blob/89b8b36e0ee2c23f73be922315f17261ccfbbe1a/src/Styles.cpp#L3187-L3305)
maps current/favorite/text schemes and All Files; the port uses its supported
catalog and retains the current extension in its first filter. Native extension
handling remains platform-owned. The original source does not establish persisted
initial-selection behavior: its COM helper reads GetFileTypeIndex but does not
call SetFileTypeIndex. wxnotepad explicitly starts on the current scheme. Its
[scheme installation](https://github.com/zufuliu/notepad4/blob/89b8b36e0ee2c23f73be922315f17261ccfbbe1a/src/Styles.cpp#L1428-L1442)
selects the scheme's lexer without a 32 MiB pause policy.

This correction aligns those selection and activation rules within wxnotepad's
current C/C++/Python/JSON catalog and existing extension mapping. It does not add
the original command-line forced-scheme option, additional content detection,
settings migration or the separate original large-document indexing mode.

## Final integration validation

Tested integration: `c883b316b34cc6b32722344f3089a4e6b07d18d3`.
Production source is unchanged from `d48cd724391c6a43b08abd77910a80f4cb293461`;
the final three-file correction changes the Windows test driver and reuses existing
test file helpers. This report update changes documentation only.

| Check | Windows | Private X11 | Private Wayland |
| --- | --- | --- | --- |
| Full related/native suite | 40/40 | 42/42 | 42/42 |
| Independent actual-app startup/native edit/Save/disk bytes | Passed | Passed | Passed |
| Final affected save/large-language tests | 2/2 | 2/2 | 2/2 |

The native full-suite provenance and subsequent test-only JSON wait are detailed
below. Final integrated affected tests pass on both native backends. Windows'
final full suite and actual-app protocol ran from the integration checkout after
the driver correction. Its owned ComboBox selection now completes with
CBN_SELENDOK, and filename editing uses EM_SETSEL/EM_REPLACESEL. No global input
is used. Explicit native suffix assertions retain Windows `copy.any.odd` versus
GTK `copy.any` under the current `.odd` filter, with independently read exact bytes.

Canonical clang-format 23.1.2 passes 80 hand-maintained C/C++ inputs. Broader
clang-tidy covers all 12 changed translation units against the preceding main,
using LLVM 23.1.2 on Windows and 18.1.3 on Linux with the checks listed below.
All analyzer commands complete without compiler errors. Windows retains 38
normalized distinct diagnostics. Three land on changed lines: the existing wx
dialog flag expression relocated into a helper (`bugprone-signed-bitwise`) and two
required synchronous Win32 callback LPARAM-to-context casts
(`performance-no-int-to-ptr`). These are classified API conventions; no suppression
or extra abstraction was added. Native Linux retains seven classified findings in
unchanged code. Complete locations/messages are archived; this is not a
warning-free repository claim.

Independent Spec review has zero cumulative findings. Independent Standards
review's duplicate test-helper finding was fixed; zero findings remain outstanding.
Both reviewers rechecked the final three-file correction.

Final parent results are in `windows-filter-verified/` and
`linux-filter-verified/`; driver red/probes/green are in
`save-filter-review-fixes/`. Final reviews are `review-filter-spec.md` and
`review-filter-standards.md`; final diagnostic classification is
`filter-final-tidy-classification.json`. Earlier filename-only reports below
are superseded and retained for provenance.

## Corrected filter implementation validation

The earlier unconditional filename-only interpretation omitted the original
dialog's selected-type path. The historical evidence below demonstrates agreement
with that incomplete spec, not full original save-type parity. Corrected source,
red/green/native filter checks and analysis are retained under external
`save-filter-44/`; the prior source/logs remain preserved.

The corrected implementation passes 42/42 tests on private native X11 and 42/42
on private Weston/Wayland. The real GTK file chooser checks current-first initial
selection, catalog order/no duplicates, catalog patterns plus retained current
extension, specific Plain/Python choices despite `.cpp`, All Files classification,
and the untitled Plain exception. Specific choices on identical paths apply every
supported language. Cancelled chooser, refused overwrite, write failure and GBK-loss
cancellation exercise a selected type different from the document's current type.

The JSON test's newly added direct All Files transition initially read styles
before Wayland's natural paint pass. It now waits using the existing paint helper;
the final full Wayland suite and affected X11 JSON test pass. The full X11 suite
passed before this test-observation-only wait. Production source is the same.
The large-file test still observes exactly **33,554,444 bytes**, with C++ token
styles, manual Plain Text/null lexer, default-current Save As retention and explicit
All Files reidentification. UTF-8/UTF-16LE/GBK/BOM/EOL/final-newline, safe writes,
history/folds/search and window view/indentation regressions remain substantive.

Independent actual-app startup/native-edit/Save checks pass on both backends.
Their actual disk files and native test byte snapshots are retained; a separate
Python literal comparison verifies 30 snapshots, including encoding fallback.
Canonical clang-format 23.1.2 passes all 83 tracked C++ inputs. Broader native
clang-tidy 18.1.3 covers all 12 changed translation units relative to the preceding
main baseline, using the checks below. New enum-size and parameter-order findings
were fixed; seven distinct remaining findings concern unchanged enum declarations,
existing native-driver copy conventions and an existing test stream-size conversion.
No new changed-line finding or compiler error remains in that scope.

Active native source/build is under
`/home/ctrls/.cache/wxnotepad-original-save-filter-44` (`src`, `build`), using the
existing wxWidgets 3.3.3#1 installation and two build/analyzer workers. The final
Windows native selector verification is included in the parent results above.
The four Windows shared-desktop exclusions below remain reported; Linux IME
remains deferred. This is current-catalog save-type coverage, not a claim of
blanket upstream feature parity.

## Historical intermediate validation (superseded save interpretation)

The full integration at `803920932ece4b5712fe615e095e0202cb40ee79` has the same
tree as implementer source `f4393330bde60910c21b453eee68141b77414407`.
The final source `844e098025f73e51d41118b91c6047b49618ee0f` changes one test-size
arithmetic expression only; production code is unchanged after the full suites.

| Check | Windows | Private X11 | Private Wayland |
| --- | --- | --- | --- |
| Full related/native suite | 40/40 | 42/42 | 42/42 |
| Independent actual-app startup/native edit/Save/disk-byte protocol | Passed | Passed | Passed |
| Final affected large-language test | 1/1 | 1/1 | 1/1 |

The native full suites and actual-app checks ran in the implementer's native
cache. Parent verification matched 98 runtime/test/build inputs byte for byte
against the integrated source, avoiding redundant full native suite runs.
Windows was independently built and checked from the integration checkout.

The new test opens a real **33,554,444-byte** `.CPP` file, checks its C++ lexer and
token style, selects Plain Text, checks the null lexer/cleared style, verifies
ordinary Save retention, and verifies Save As reidentification and independently
read exact disk bytes. The other checks cover all manual language choices,
first/same-path/unchanged-extension naming, native menu/status and chooser paths,
overwrite refusal/write failure/GBK-loss cancellation, encoding/BOM/mixed EOL and
history, syntax/folds/search, and existing everyday controls. Original red/green
failures for first Save and explicit Save As were retained.

Windows excludes the same four shared-desktop tests as the preceding delivery:
`system_clipboard`, `gbk_native_choices`, `open_new_native` and `save_as_native`.
Both private Linux suites include them. Linux IME remains deferred.

## Historical intermediate static analysis and review (superseded)

Canonical clang-format 23.1.2 passed 80 hand-maintained C/C++ files. The implementer
also checked all 83 tracked C++/header files including generated scheme headers.
Parent native clang-tidy used LLVM 23.1.2 on Windows and 18.1.3 on Linux for all
10 changed translation units, with:

```text
-*,clang-analyzer-*,bugprone-*,performance-*,portability-*
```

The broader check found one new widening-of-multiplication diagnostic in the test
fixture length expression. The first operand now uses `size_t`, retaining exactly
33,554,444 bytes. Targeted reanalysis and the affected test passed on all platforms.
There are zero new changed-line findings or compiler errors. The final normalized
sets for this 10-unit scope retain 37 Windows and 6 Linux findings in unchanged
code/header/native API conventions; complete locations and messages are archived.
This is not a whole-project warning-free claim or a same-scope comparison with the
preceding 25-unit report. No suppression was added.

Independent Standards review found zero documented violations or smell findings.
Independent Spec review found zero mismatches. Both confirmed the final one-line
test cleanup preserves behavior and coverage.

## Evidence and reusable builds

Evidence root:

```text
E:/NewNotepad4/wxnotepad-evidence/large-file-highlighting-implementation/
```

`original-issue-44/` contains the original red/green logs, full native suites,
actual-app results, independent bytes, source archives and patch. Parent Windows,
broader/final tidy, source-identity verification, reviews and merge records are
alongside it. `superseded-42/` holds the separately verified unmerged snapshot;
it is not part of this product delivery. Source/history/runtime archives and a
checksum manifest preserve evidence before disposable workspaces are retired.

The retained native source/build is
`/home/ctrls/.cache/wxnotepad-original-save-filter-44` (source in `src`, CMake build
in `build`). Windows reused `build-everyday/cmake` in the integration checkout.
Deliverable configurations disable manifest installation and use existing installed
dependencies. An initial configure accidentally started a manifest restore and was
stopped; its logs are retained in `save-filter-44/`. No new runtime or GoogleTest
dependency is needed. Final delivery and cleanup state is recorded in the external
`completion.json` and manifests.

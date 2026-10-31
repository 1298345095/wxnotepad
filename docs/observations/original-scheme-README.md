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

- Successful first Save and every explicit Save As reidentify the language from
  the resulting filename and clear the document's manual override. Unchanged
  extensions and identical-path Save As are included.
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
separate ordinary saving from first Save/Save As; the latter invokes
`Style_SetLexerFromFile`. Its
[scheme installation](https://github.com/zufuliu/notepad4/blob/89b8b36e0ee2c23f73be922315f17261ccfbbe1a/src/Styles.cpp#L1428-L1442)
selects the scheme's lexer without a 32 MiB pause policy.

This correction aligns those selection and activation rules within wxnotepad's
current C/C++/Python/JSON catalog and existing extension mapping. It does not add
the original command-line forced-scheme option, additional content detection,
settings migration or the separate original large-document indexing mode.

## Validation

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

## Formatting, static analysis and review

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
`/home/ctrls/.cache/wxnotepad-original-scheme-44` (source in `src`, CMake build in
`cmake`). Windows reused `build-everyday/cmake` in the integration checkout. No new
runtime, GoogleTest or installed system dependency was required. Final delivery
and cleanup state is recorded in the external `completion.json` and manifests.

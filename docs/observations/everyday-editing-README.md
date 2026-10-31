# Everyday editing validation

Implemented [overview #50](https://github.com/1298345095/wxnotepad/issues/50) and
[position status #51](https://github.com/1298345095/wxnotepad/issues/51),
[line endings #52](https://github.com/1298345095/wxnotepad/issues/52),
[indentation options #53](https://github.com/1298345095/wxnotepad/issues/53), and
[block indentation #54](https://github.com/1298345095/wxnotepad/issues/54).

Integration branch: `codex/everyday-editing`, starting at
`7b7dcfb02a74d761b8cb5ccc00e90ddba394b2cf`. The completed navigation/view work is
retained. Large-file highlighting #41–#44 remains a separate, unimplemented batch.

## Behavior and ownership

- Status keeps document metadata in field 0, logical caret/selection information
  in field 1 and search feedback in field 2. Core columns follow tab stops.
  Selected Unicode scalars are counted only for the selected range, with cached
  observation invalidated by relevant content/selection changes. No polling
  timer, whole-document mirror or status-driven minimum window size was added.
- EOL commands reuse the original core's conversion/history operations. Existing
  CRLF/LF/CR bytes normalize without an added final newline. Undo/Redo restores
  text exactly; the explicitly chosen insertion preference remains. Checked
  commands are mutually exclusive but can be reactivated in wxGTK, so the same
  mode can normalize mixed text again after Undo. The EOL label describes Enter's
  mode rather than claiming uniform existing bytes.
- Indentation options read core settings directly. Tabs/Spaces and widths 2/4/8
  apply live, with wrap layout recalculated before revealing the caret. Startup
  defaults and window-lifetime retention are preserved without a settings store.
- Indent/Unindent reuse core line operations and adjust selection coordinates
  where needed to include an unselected current line at column zero. Tab and
  Shift+Tab preserve native dialog traversal outside the focused editor. No new
  whitespace transformation algorithm was introduced.

No new runtime, GoogleTest or installed system dependency was required.

## Verification

The agreed boundaries remain real editor public commands/state, application file
operations plus independently read disk bytes, and independent application
processes. Implementers used private native Linux builds and retained incremental
red/green logs, fixture bytes, patches and source archives. Final integration
checked native Windows and private Xvfb/X11 and nested Weston/Wayland.

| Check | Windows | X11 | Wayland |
| --- | --- | --- | --- |
| Integrated suite at `cf620dbd08503766cc5481d48bc04739d6ca6715` | 39/39 | 41/41 | 41/41 |
| Independent actual-app startup/Open/native edit/Save/disk-byte protocol | Passed | Passed | Passed |
| Final four affected tests at `7142c1c20f8dfe08d278f38b33addda01349903b` | 4/4 | 4/4 | 4/4 |

The Windows suite excludes the same four shared-desktop tests as the preceding
delivery: `system_clipboard`, `gbk_native_choices`, `open_new_native` and
`save_as_native`. Both private Linux suites include them. Linux IME remains
explicitly deferred. Windows existing font/DPI checks passed; private Wayland
used its ordinary scale-1 output, without claiming scale-2 coverage.

The final four tests are `live_position_selection_status`, `convert_line_endings`,
`indentation_options` and `block_indentation_commands`. They cover actual native
menu/key dispatch, Unicode/tab/CRLF positions, selection/cache invalidation,
Find/Go To Line, saved bytes for UTF-8/UTF-16LE/GBK, no-op history, reverse and
column-zero selections, wrapping/folds and settings retention. The final source
diff after the full suite consists of test-helper cleanup, explicit bounded test
API size conversions, and portable unsigned status-format arithmetic. The final
targeted runs cover all affected behavior.

The native build is retained at
`/home/ctrls/.cache/wxnotepad-everyday-integration`; Windows validation used
`build-everyday/cmake` in the integration checkout. Disposable implementer builds
are archived separately before removal.

## Formatting, static analysis and review

Canonical clang-format 23.1.2 passed all 79 hand-maintained C/C++ files. Native
clang-tidy used LLVM 23.1.2 on Windows and 18.1.3 on Linux with:

```text
-*,clang-analyzer-*,bugprone-*,performance-*,portability-*
```

Each platform's broad scan covered 25 translation units. Five affected units were
reanalyzed after fixing seven new conversion diagnostics and the new helper's
Windows include-guard diagnostic. No targeted new conversion/header finding or
compiler error remains. No suppression was introduced.

After replacing the five earlier results with their final results and normalizing
path separators, the retained diagnostic set contains 100 unique Windows findings
and 60 Linux findings. These include unchanged codec/platform code, existing
header conventions, pinned enum bit masks and native event-test idioms. The scope
is broader than the previous 19-unit report, so counts are not a same-scope trend
or a warning-free claim. Complete per-unit logs, fixes exports and the effective
diagnostic JSON retain each location and message.

Independent Standards review found two test-code heuristics: duplicated
filesystem helpers and a vague Right-key helper name. Both were fixed and
rechecked; zero hard violations or heuristics remain. Independent Spec review
found zero actionable mismatches and confirmed the final cleanup preserves
behavior and assertions.

## Evidence

External evidence root:

```text
E:/NewNotepad4/wxnotepad-evidence/everyday-editing-implementation/
```

It retains published issue bodies, per-ticket original red/green and native
display logs, saved-byte fixtures, source archives and patches, merge records,
both review reports, Windows/X11/Wayland CTest XML and detailed logs, actual-app
protocol results, broad/final tidy outputs and classification, the final source
archive, Git bundle and checksums. Native implementer configuration/log evidence
is additionally verified in `implementer-native-evidence.tar.gz`; compiled object
files and libraries are rebuildable. Final delivery and cleanup state is recorded
in `completion.json` and the cleanup manifests beside the evidence.

# Navigation and view controls validation

Implemented [overview #45](https://github.com/1298345095/wxnotepad/issues/45)
and children #46–#49 on `codex/navigation-view-controls`, starting from
`f24b49ad724321f55c98a240244a3bcbb8354c8d`.
The reviewed implementation is `95ea6d889bbb2b1537e9840b75ff37e834adf153`.

## Delivered behavior

| Command | Shortcut | Behavior |
| --- | --- | --- |
| View → Line Numbers | Ctrl+Shift+N | Starts on. The logical-line margin grows and shrinks with digit count, font and DPI while the separate fold margin remains usable. |
| Edit → Go To Line | Ctrl+G | Validates a 1-based absolute logical line against the current document, reveals its folded ancestors and positions the caret. Cancel preserves selection, viewport and folds. |
| View → Word Wrap | Ctrl+Shift+W | Starts off. Original-core word wrapping follows client width, including long unbroken text, without changing newline bytes. |
| View → Show Whitespace | Ctrl+Shift+8 | Starts off. Original-core markers distinguish spaces and tabs across plain and syntax-colored text. |

View choices belong to the window and survive New, Open, Save As and language
changes there. A fresh window uses the defaults. No persisted settings, settings
import, new runtime dependency or text/history/layout model was added.

The core remains authoritative for text, history, selections, folds and layout.
The navigation operation owns range validation and fold/caret message ordering;
the app provides native menus and a small dialog. Margin and resize changes
rewrap before revealing the caret, including Windows' deferred paint lifecycle.

## Verification and provenance

| Environment | Full related suite at `9400053` | Review cleanup at `6da2239` |
| --- | --- | --- |
| Windows, MSVC Release, wxWidgets 3.3.3#1 | 35/35 passed | Four affected native tests: 4/4 |
| Ubuntu, private X11 | 37/37 passed | Four affected native tests: 4/4 |
| Ubuntu, private Wayland | 37/37 passed | Four affected native tests: 4/4 |

The final merge `95ea6d8` has the exact tree of `6da2239`. The review cleanup
changes only the four tests and their shared helper; production files have no
delta from the full-suite commit. Full-suite evidence is retained alongside the
final four-test runs instead of claiming another complete run at the final SHA.

Windows excludes the existing `system_clipboard`, `gbk_native_choices`,
`open_new_native` and `save_as_native` shared-desktop cases, as in the architecture
validation. Both private Linux suites include all of them. No screenshot or
browser inspection was used. Native test messages target owned windows.

Independent actual-application processes also passed startup-file, visible state,
native editing, Save and independently read disk bytes on all three backends.
The combined view test covers nested folds, unrelated folds, logical-line
navigation, whitespace markers, both margins, wrapping/resizing, search reveal,
selection/history, font/metadata retention and exact saved bytes.

## Fixes driven by native checks

- A Windows menu test now refreshes update-UI state before native command
  dispatch, matching real menu opening.
- The owned-dialog Escape driver reaches the public dialog key-hook path and
  bounds failed dismissal instead of hanging its modal loop.
- Windows exposed stale wrap geometry after restoring the line-number margin.
  Reusing the core's wrap-layout operation before caret reveal fixed the unchanged
  visibility assertion. Waiting for more paint events did not fix the red case.
- Four Windows shortcut drivers now share one narrow native-test operation;
  GTK paths, targets, modifiers and acceptance assertions remain intact.

## Formatting, analysis and review

Canonical clang-format 23.1.2 passes all 74 hand-maintained C/C++ files. The full
native clang-tidy scans each cover 14 production translation units and five
feature tests with `clang-analyzer-*`, `bugprone-*`, `performance-*` and
`portability-*`. Windows uses LLVM 23.1.2; Linux uses LLVM 18.1.3.

Ten newly observed conversion diagnostics were fixed. The final full Linux scan
retains the same 47 diagnostics by check as the architecture baseline. The
Windows scan records 88 diagnostics including the extra test headers: existing
platform/API findings, intentional signed wx layout flags and the project's
`#pragma once` convention remain classified rather than hidden. The final
test-helper cleanup also passed targeted native analysis on both platforms; it
introduced no diagnostic in the extracted Windows helper. This is not a
whole-project warning-free claim.

Independent Standards review found no documented-standard breach and one P3
duplication judgment, which was fixed and rechecked. Independent Spec review
found no missing requirement, scope creep or incorrect behavior. Both reviewers
confirmed the final cleanup; outstanding findings are zero on each axis.

## Measurements and remaining limits

A bounded implementation-stage observation used an actual app process and a
240,000-byte Unicode/tab single line on private X11 and Wayland. Native wrapping
on/off, observed row counts and byte-identical saves passed. Its timings include
key dispatch, save, repaint and polling; it is not a layout-only benchmark,
startup benchmark or performance threshold for the final build.

The new line-number check passes at actual X11 DPI 192 / scale 2. An existing
font test fails at that scale in both the changed and unchanged baseline
executables; logs preserve the comparison. The nested Wayland output remained
scale 1 despite a requested `GDK_SCALE=2`, so it does not establish actual
scale-2 Wayland coverage. Linux IME remains deferred. Large-file highlighting
policy #41–#44 remains separate from this batch.

## Retained evidence

Machine-local archive:
`E:/NewNotepad4/wxnotepad-evidence/navigation-view-implementation/`.
It holds issue snapshots, per-ticket red/green logs, source archives and patches,
native build/test and analyzer outputs, review reports, merge records, delivery
source/bundle/runtime artifacts and a verified SHA-256 manifest. The original
Notepad4 revision and portability patch were not changed by this batch.

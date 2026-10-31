# Closing unsaved changes

Date: 2026-10-30. Scope: [#15](https://github.com/1298345095/wxnotepad/issues/15).

Normal window close checks the original core's modified state. Clean documents
close directly; dirty documents show native Save / Discard / Cancel buttons.
Save closes only after a successful write. Cancelling the path chooser or failing
the write vetoes close and retains the editable document, path, dirty state and
history. Discard closes without writing. The minimal named-document save reuses
the existing safe replacement and establishes a save point only on success.
The existing Save menu's named-document behavior remains for #17.

Tests use the agreed public application file-operation, document-state and
independent disk-byte seams. TDD first failed named-path Save, then passed after
reusing the safe-write path. A real `wxFrame::Close()` test next failed Cancel
veto, then passed with native confirmation. A close-Save test failed its exact
disk-byte assertion before connecting Save to the close event.

`close_unsaved` drives actual native dialogs via process-local Windows/GTK
widget APIs; it sends no global keyboard or pointer input. It covers clean
close, Cancel and continued editing, Discard with unchanged disk bytes, first
Save with a native path chooser, cancelling that chooser and continued editing,
Save after a prior save, and a failed replacement with a readable error. Failure
keeps the target sentinel, document identity, text and save-point/Undo history.
`first_utf8_save` additionally checks exact bytes and clean state after updating
an existing named document.

Windows uses MSVC 14.51 and wxWidgets 3.3.3, with the Release build under
`build/cmake`. Linux uses Ubuntu 24.04 WSLg, GCC 13.3, GTK3 and the same wxWidgets
version. Its independent source/build/data live under
`/home/ctrls/.cache/wxnotepad-issue15/` on the native Linux filesystem.

Build with the repository's `tools/build.py`, then run
`ctest --test-dir build/cmake -C Release --output-on-failure`. Set
`GDK_BACKEND=wayland` or `GDK_BACKEND=x11` for Linux. `ctest -R close_unsaved`
isolates the seven close flows; `ctest -R first_utf8_save` isolates the hidden
file-state/byte checks. On the shared implementation desktop, run every GUI
check through the external `desktop_run.py` serialization wrapper.

These are native-widget activation checks, not physical keyboard coverage.
Linux IME remains untested; no Linux IME was installed.

# Windows inline IME and horizontal viewport

The Windows adapter now uses the pinned Notepad4 `ScintillaWin` inline
composition algorithm. The user explicitly selected inline composition as the
Windows default. `SetIMEInteraction(Windowed)` remains available to API callers.
The IMM implementation lives in `src/editor/platform/windows_ime.cpp`.

Preedit characters use the core's tentative document text, normal styles and
wrapping. The system composition window is suppressed in inline mode; native
candidate positioning follows the original Chinese/Japanese/Korean rules.
Cancellation rolls back tentative text. A committed result uses the original
IME-result insertion and undo operations.

Windowed composition font height now comes from the current style's zoomed
point size and window DPI. The previous adapter used the complete line height,
including leading: at 144 DPI it requested -28 pixels instead of -24.

New/replaced documents start tracked horizontal width at one pixel instead of
the core's 2000-pixel initial estimate. Native horizontal page size uses the
text viewport, excluding margins. Wrapped views expose no horizontal range.

The requested View > Auto-Shrink Horizontal Scrollbar option is off by default,
retaining original deletion behavior. Enabling the option or deleting text
requests a new measurement on the next unwrapped paint, using the core's
visible-line widths and clamping stale horizontal offsets. Subsequent paints
retain that width, avoiding a visibility/viewport feedback loop at the bottom edge.
Offscreen long lines expand the range when displayed, as in original width
tracking. This avoids scanning the full document after every edit. The choice
is saved as `AutoShrinkScrollWidth` in the separate `[wxnotepad]` INI section,
with the existing Save Settings On Exit / Save Settings Now rules.

## Verification

The new `windows_inline_ime_input` test uses actual native Pinyin keyboard
input and observes the OS composition/result messages. It checks that preedit
belongs to the editor text and wraps, Esc preserves the save point/history,
and committing a word supports one Undo followed by Redo. It requires
`WXNOTEPAD_TEST_PINYIN=1`, a Simplified Chinese Pinyin input method and access
to the input desktop; it returns CTest skip code 77 without opt-in or when no
Simplified Chinese layout is installed.
It verifies ownership/focus before injecting keys and does not use the clipboard.
Directed native mouse-down and focus-loss checks verify completion at the
original insertion point before moving the selection, followed by public Undo.

Before the inline port the native preedit/text assertion failed. Afterward the
complete composition/cancel/commit/Undo/Redo sequence passed. The user confirmed
matching font size and baseline, normal wrapping without covering line numbers,
and correct Undo with Microsoft Pinyin at 150% scaling.

`windows_ime_geometry` also checks the actual IMM font height. `word_wrap_view`
checks empty, long, replaced-short and wrapped native scroll ranges on Windows,
X11 and Wayland. The Wayland test waits for presentation and the observable
scroll range instead of assuming that the first paint has already completed.
The input and search-wrap fixtures likewise wait for the actual scroll range
before sending or snapshotting offsets, preserving their original assertions.
It also checks default width retention, the shrink toggle, deletion and a
remaining long line, offset repair and Undo/Redo. Independent application
processes verify the setting's default, save/restart, disabled exit-save and
explicit-save behavior without losing unrelated INI entries.

Raw logs, the unviewed user screenshot and original public-API scroll captures
are retained under
`E:/NewNotepad4/wxnotepad-evidence/windows-ime-layout-2026-11-07`.
Linux human IME validation remains deferred.

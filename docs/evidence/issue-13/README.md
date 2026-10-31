# Windows IME validation

Issue: [#13](https://github.com/1298345095/wxnotepad/issues/13).

## Automated checks

The agreed seams are the actual editor's public commands/character events and
the native Windows IMM geometry. No editor core or IMM mocks are used.

The geometry checks cover the first and second lines, a viewport scrolled to
line 38, native focus transfer away and back, and a
native composition-start message before a pending caret move has painted.
Expected coordinates come from the core's public position-to-point messages;
actual coordinates, exclusion rectangle and font come from `ImmGetCompositionWindow`,
`ImmGetCandidateWindow` and `ImmGetCompositionFontW`.

The existing `editor_input_undo` check replays `wxEVT_CHAR` for `A中文` and checks
the complete UTF-8 document bytes, byte-based caret position and undo/redo. Its
conversion owner remains alive until insertion completes. This is character
entry evidence, separate from real IME commit/cancel evidence.

Windows Release: MSVC 14.51, wxWidgets 3.3.3#1, pinned core
`89b8b36e0ee2c23f73be922315f17261ccfbbe1a`. Build entry:

```powershell
python -X utf8 tools/build.py --source E:/NewNotepad4/notepad4
ctest --test-dir build/cmake -C Release --output-on-failure
```

2026-10-30: after merging #14 integration `66314f1`, all four Windows checks passed
at `9eb4a09`: `editor_input_undo`, `display_repaint`, `windows_ime_geometry` and
`first_utf8_save`. Before that merge, a native Linux filesystem Release build
with GCC 13 and wxWidgets 3.3.3#1 passed the existing editor/display checks under
Wayland and X11. The Windows-only IME target is absent from Linux builds; those
runs provide regression evidence only.

## Microsoft Pinyin human validation: passed

On 2026-10-30 the user verified all four cases as normal in Microsoft Pinyin
Chinese mode using the Release fixture built at `9eb4a09`:

```powershell
./build/cmake/Release/ime_smoke.exe --manual
```

This test-only mode starts with 100 blank lines, a clean undo history and the
caret on the first line. The normal application does not contain this fixture.
Its three positioning buttons use the real control's public commands and return
focus to the editor; they do not depend on #11 keyboard or mouse navigation.

1. First line: `nihao` + Space committed `你好`; `ceshi` + Esc left no preedit text;
   one Ctrl+Z made the line blank. Composition/candidates followed that caret.
2. Second line: `nihao` composition/candidates followed the second-line caret;
   Esc cancelled.
3. Scrolled last line: composition/candidates followed the visible caret after
   scrolling to line 100; Esc cancelled.
4. Focus target → First line: composition/candidates followed the new caret after
   refocus; Esc cancelled.

Linux IME, other IMEs and other display scaling combinations remain unverified.

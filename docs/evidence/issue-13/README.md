# Windows IME validation

Issue: [#13](https://github.com/1298345095/wxnotepad/issues/13).

## Automated checks

The agreed seams are the actual editor's public commands/character events and
the native Windows IMM geometry. No editor core or IMM mocks are used.

The first geometry test failed against the #10 host because IMM did not expose
the required composition, candidate and font geometry. After the Windows bridge
was implemented, the first-line check passed. Further checks cover the second
line, a viewport scrolled to line 38, native focus transfer away and back, and a
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

2026-10-30 results: all three Windows checks passed (`editor_input_undo`,
`display_repaint`, `windows_ime_geometry`). A native Linux filesystem Release
build with GCC 13 and wxWidgets 3.3.3#1 passed both existing editor/display checks
under Wayland and X11. The Windows-only IME target is absent from Linux builds;
those runs provide regression evidence only.

## Microsoft Pinyin human validation: pending

Start a fresh manual fixture and select Microsoft Pinyin Chinese mode:

```powershell
./build/cmake/Release/ime_smoke.exe --manual
```

This test-only mode starts with 100 blank lines, a clean undo history and the
caret on the first line. The normal application does not contain this fixture.
Its three positioning buttons use the real control's public commands and return
focus to the editor; they do not depend on #11 keyboard or mouse navigation.

1. Click First line. Type `nihao` and commit `你好` with Space. Observe composition
   and candidates beside the first-line caret. Type
   `ceshi`; when candidates appear press Esc. Confirm `你好` remains with no
   preedit text, then press Ctrl+Z once and confirm the first line becomes blank.
2. Click Second line and type `nihao`. Check that composition/candidates move to
   the second-line caret. Cancel with Esc.
3. Click Scrolled last line. Type `nihao`; confirm
   composition/candidates follow the visible caret after scrolling. Cancel with Esc.
4. Click Focus target, then First line. Type `nihao` and confirm
   composition/candidates follow the new
   caret after refocus. Cancel with Esc and close the fixture.

Record the human result separately. Automated geometry does not complete this
acceptance criterion. Linux IME, other IMEs and other display scaling combinations
remain unverified.

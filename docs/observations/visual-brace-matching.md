# Automatic visual brace matching

Issue: [#151](https://github.com/1298345095/wxnotepad/issues/151).
Reference: Notepad4 `89b8b36e0ee2c23f73be922315f17261ccfbbe1a`.

## Reference observations

Windows captures are preserved in
`E:/NewNotepad4/wxnotepad-evidence/editor-navigation-controls-2026-11-07/151/`:

- `capture_original.py` and `original.json`: 72 caret/selection/syntax cases
  plus four native toggle/paint observations.
- `capture_lifecycle.py` and `original-lifecycle.json`: deletion, Undo/Redo,
  rectangular selection, read-only state, stationary syntax change and New.
- `original-capture.log` and `original-lifecycle.log`: successful capture runs.

The capture queries the original application's public `BraceMatch`, guide,
document and selection interfaces. The original host's candidate precedence and
matched/bad overlay style are source-confirmed in `Notepad4.cpp::SCN_UPDATEUI`.
Native DC samples independently observe decoration removal/restoration. Exact
wx decoration positions are read from the actual inherited `braces[2]` and
`bracesMatchStyle` consumed by Scintilla's drawing path, through a test-owned
friend observer. No mirrored decoration state is used.

Observed original rules:

- The brace at the caret wins; otherwise `PositionBefore` supplies the candidate.
  The recognized set is `()[]{}<>`. A bad caret brace prevents fallback.
- Core nesting/style matching supplies the mate, including quotes/comments and
  UTF-8 byte positions. Selection contents/direction do not disable feedback.
- A match uses style 34 and the lesser logical column as guide. A bad brace uses
  style 35 and guide zero. No adjacent brace clears both positions and guide.
- Disabling clears the positions but retains the guide. Re-enabling refreshes
  the stationary context. All four original syntax captures retained guide 2
  across off/on for `  (x)` at byte 2.
- Original green alpha50 fill pixels around that opening brace occurred 66,
  206, 221 and 206 times for Plain Text/C++/Python/JSON; the corresponding off
  captures each contained zero. The tests use this narrow colour class to
  exclude ClearType glyph fringes.

Indicator styles come from `EditLexers/stlDefault.cpp` and
`Styles.cpp::Style_DefineIndicator`: RoundBox 9/10, RGB `#02EA0A`/`#FF0000`,
fill alpha 50 and outline alpha 200. Occurrence indicator 8 stays independent.
The original boolean parser accepts a leading `0` or `1` and otherwise uses
the true `MatchBraces` default (`Helpers.cpp::GetBoolImpl`,
`Notepad4.cpp::LoadSettings`).

## Implementation and verification

The existing UpdateUI boundary refreshes non-scroll changes. A style-only
change in the pinned core does not queue a content UpdateUI; the editor therefore
also refreshes after the real native styling/paint pass. This fixes stationary
C++ to Plain Text transitions for `["]"]` (mate byte 4 becomes byte 2).
Windows `wxDC` ignores colour alpha; `AlphaRectangle` uses the same native
graphics-context approach already used for translucent fills.

Registered tests:

- `original_visual_brace_matching`: actual native checked menu, exact accelerator,
  acknowledged Ctrl+Shift+V and independent Ctrl+B/Ctrl+Shift+B, 72 reference
  cases, matched/bad/off/on native pixels, edits/history, rectangle/read-only,
  font/zoom/DPI retention, stationary syntax changes, New/empty replacement,
  and independently read UTF-8/UTF-16LE/CP936 saved bytes with Undo/Redo/reopen.
- `original_visual_brace_preference`: independent application processes for
  seed import, default/prefix parsing, explicit save, exit-save on/off, restart
  and preservation of unrelated INI fields/comments.

The native test runs on Windows and the existing private X11/Wayland drivers.
Windows evidence uses the owned, never-activated desktop runner, with no
clipboard access. The existing Paste/clipboard and full backend regressions
remain part of integration validation.

Red/green logs are preserved beside the captures: missing native command,
missing indicator style, opaque Windows indicator drawing, stale stationary
syntax context and ignored imported preference each produced a failing test
before its corresponding implementation change.

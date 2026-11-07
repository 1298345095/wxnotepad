# Navigation controls implementation preparation

Research date: 2026-11-07. Scope: [#150](https://github.com/1298345095/wxnotepad/issues/150),
[#151](https://github.com/1298345095/wxnotepad/issues/151), [#152](https://github.com/1298345095/wxnotepad/issues/152),
under [#149](https://github.com/1298345095/wxnotepad/issues/149). All four live issues have no comments;
all three children say “None (can start immediately)” under Blocked by.
This preparation preserves the agreement to publish specifications without implementing them.

## Evidence and interpretation

- **W**: wxnotepad `b9dcf9db7d993f06288a25c03a83eeab99675621`, branch `codex/navigation-preparation`.
  `git diff b9dcf9d b41dc256305e8984c7d0c29f88a72b9b5d85d968 -- src`
  is empty, so the source findings also describe that active CI revision.
- **N**: original Notepad4 checkout `E:/NewNotepad4/notepad4`, pinned exactly to
  `89b8b36e0ee2c23f73be922315f17261ccfbbe1a`. N paths below are relative to that checkout.
- **SDK**: installed source `E:/NewNotepad4/wx-v3.3.3`; `include/wx/version.h:28–32`
  declares wxWidgets 3.3.3. This extracted SDK has no Git revision identifier.
- Reuse settled findings in `E:/NewNotepad4/wxnotepad-evidence/post-utilities-2026-11-06/tickets/research.md`.
- **Source-confirmed** means verified in these revisions; **capture pending** means an original-application
  observation still needs recording. This session ran no builds/GUI; relevant source secrets scans passed.
- Follow `GLOSSARY.md`'s editing-behavior compatibility definition and accepted ADRs 0002/0003:
  preserve editing outcomes and original settings while using native platform UI.

## #150: block traversal and selection

**Source-confirmed original contract.** N `src/Edit.cpp::EditGotoBlock:7880–7980` starts from
the caret's logical line and uses its header or fold parent. Start uses that header; End uses
`GetLastChild`. Previous/Next scan fold headers; sibling modes compare numeric fold levels and
retain a first-header fallback when the preferred level is absent. Next Sibling skips a current
header's children. A destination equal to the current line is a no-op; a different destination
uses `EditJumpTo(line + 1, GetColumn(currentPosition, currentLine) + 1)`: preserve visual column and fold reveal.

N `src/Edit.cpp::EditSelectLines:4502–4552` uses the caret's header/parent and its last child.
No parent calls Select All. A reversed selection stays reversed; for an empty selection the
caret goes toward the nearer block edge (the strict distance comparison determines ties).
Line-selection mode changes whether the opposite endpoint advances one logical line.
N `src/Notepad4.cpp:3176–3179` supplies `iLineSelectionMode & LineSelectionMode_VisualStudio`:
use the existing mode bit, including Old Visual Studio, rather than a new selection preference.

N `src/Notepad4.rc:398–403,1015–1026` defines the six shortcuts and physical Windows OEM keys;
`:374,1018` defines Alt+Shift+]. `src/Notepad4.cpp:2383–2430` gates these menu commands on a
nonempty document, and `:3785–3792` routes all six to the same traversal. The algorithms add
no read-only or rectangle rejection; capture their resulting selection before choosing a gate.

**Current seams / proposed work.** W `src/app/editor_frame_commands.cpp::AppendNavigationCommands:598`
already owns Go To Line, explicit brace navigation and bookmarks. Extend that existing menu seam.
Add the shared block operation to `EditorControl` beside `JumpToPosition` / `MatchBrace`
(`src/editor/editor_control.h:79–83`), porting traversal over public messages. Reuse
`FindColumnEx(ColumnType::Default)` as in `src/editor/go_to_location.cpp::GoToLocation:37–48`,
then `EditorControl::JumpToPosition:232–245`, which enforces visibility and restores caret policies.

**Capture pending.** Record the seven real original commands on small C++, Python and JSON
documents with nested blocks, siblings, trailing whitespace and a final line without EOL.
Sample header/body/closing line, document boundaries, no-parent text, collapsed parent and nested
child; include forward/reversed/empty selections and equal-distance ties. Cross all four line-selection preferences,
rectangle plus virtual space, multiple selections and read-only mode. Check Plain Text and the
actual available highlighting-policy transitions separately; do not substitute guessed fold levels.
Record source text, syntax, fold levels, collapsed/visible lines, anchor/caret byte positions,
selection mode/virtual space, preferred column, encoding, dirty state and undo/redo before/after.
Capture base shortcuts and added Shift/Ctrl variants, particularly punctuation combinations that
overlap original selection-boundary shortcuts (N `src/Notepad4.rc:1021,1026`).

## #151: automatic visual brace matching

**Source-confirmed original contract.** N `src/Notepad4.cpp::SCN_UPDATEUI:4652–4727` refreshes
on updates containing more than scroll-only flags. It tries the caret character first, then
`PositionBefore`; a brace at the caret wins even when unmatched. It calls `BraceHighlight`
and sets the guide to the lesser column for a match, or `BraceBadLight` and guide zero for an
unmatched brace; no adjacent brace clears both. This path has no empty-selection condition.
The recognized set is `()[]{}<>` (`IsBraceMatchChar:2637–2646`). The core's
`scintilla/src/Document.cxx::BraceMatch:3049` owns nesting, style matching and UTF-8 boundaries;
quotes/comments must be observed with their actual lexer styles, not a second host parser.

N `src/Notepad4.cpp:4032–4043` enables through custom UpdateUI and disables through
`BraceHighlight(-1,-1)` without explicitly resetting the guide; capture the resulting guide/render state.
`MatchBraces` defaults true (`:5162,5464`); menu/key binding is N `src/Notepad4.rc:435,953`.

N uses indicator 9 for matches and 10 for errors, distinct from occurrence indicator 8
(`src/Edit.h:289–292`; `scintilla/include/ScintillaTypes.h:242`). Setup calls
`BraceHighlightIndicator` / `BraceBadLightIndicator` (`src/Notepad4.cpp:1815–1817`).
`src/Styles.cpp::Style_DefineIndicator:1391–1405` and `:1595–1596` install RoundBox styles;
`src/EditLexers/stlDefault.cpp:12–13` specifies green #02EA0A / red #FF0000, alpha 50,
outline 200. These decorations are paint overlays, not ordinary document indicator ranges
(`scintilla/src/EditView.cxx:2173–2188`); `IndicatorValueAt` cannot verify their positions.

**Current seams / proposed work.** Keep W `src/editor/matching_brace.cpp::MatchBrace` and
its Ctrl+B/Ctrl+Shift+B command behavior independently usable. Add automatic feedback in the
editor layer, using `Core::NotifyParent`'s actual UpdateUI notification and flags
(`src/editor/editor_core.cpp:662–691`), with explicit refresh after enabling and style/document
replacement. W `EditorControl::Send:262–280` already treats empty `ClearAll` as replacement;
`DocumentSession::Replace:388–415` and `SelectLanguage:424–429` identify lifecycle boundaries.
`ApplySyntaxLanguage:72–89` clears/reinstalls styles, so install brace indicator styles there
without replacing the font or size. Reuse the existing state event for app/menu refresh.
Add the checked native View command with Ctrl+Shift+V; verify Ctrl+V stays Paste
(`editor_frame_commands.cpp:155–185`, `Core::HandleApplicationKey:525–532`).

**Capture pending.** Use paired caret-before/after cases, `()[]` precedence, `()(` unmatched
precedence, EOF, nested pairs, comments/strings in each supported syntax and `中(a😀b)尾`.
Run with a nonempty/reversed/rectangular selection, read-only text, edit/undo/redo, New/Open,
syntax changes and available highlighting-policy changes. Capture matched/bad core positions,
guide, preference/menu state and numerical rendering observations; confirm disabling and
re-enabling at the same stationary caret. Preserve document bytes, encoding, selection and history.

## #152: folding margin and current-block marker feedback

**Source-confirmed original contract.** N `src/Notepad4.cpp:3953–3959` updates width and
expands all folds when hiding; showing does not restore a saved collapse map. Existing original
fold commands test `bShowCodeFolding` (`:3962–3999`). Highlight Current Block calls
`MarkerEnableHighlight` (`:4046–4049`); it emphasizes folding markers, not the current-line fill.
Both preferences default true (`:5163,5222`) and save as Settings booleans (`:5465,5498`).

N `src/Styles.cpp::UpdateFoldMarginWidth:3167–3180` computes the enabled width from the
CodeFolding style size scaled by DPI*zoom plus `TextWidth(STYLE_LINENUMBER,"+_")`;
the default style size is absent (`src/EditLexers/stlDefault.cpp:23`). Original margin numbers
are line=0/bookmark=1/fold=2 (`src/Edit.h:295–297`); W currently uses line=0/fold=1.
Adapt the current layout rather than copying original numeric margin IDs into commands.
N `src/Styles.cpp:1685–1740` installs the original marker line/fill colors #8080FF/#ADD8E6
and red selected background. Current W `ApplySyntaxLanguage:45–68` instead installs a fixed
`FromDIP(16)` fold width and gray/white colors, with no selected-background assignment.

The existing core computes current-block delimiters from the main caret while painting
(`N scintilla/src/MarginView.cxx:508–514`, `Document.cxx::GetHighlightDelimiters:695–759`).
`Editor.cxx:7611–7625` already handles selected-marker color and enablement with margin redraw.
Reuse that implementation; do not calculate a second current-block tree in the host.
W `Core::DpiChanged:247–256`, `NotifyParent` Zoom handling, `UpdateWrapLayout:242–245`,
and `EditorControl::UpdateLineNumberMargin:298–314` identify geometry/reveal integration points.
`Core::RepairFoldSelection:405–423` repairs hidden endpoints after real fold operations.

**Capture pending.** In original C++/Python/JSON, collapse nested/unrelated parents, hide/show
the margin, and record every header's expansion and every line's visibility. Compare caret and
anchor when they are on headers, in bodies and on whitespace/closing lines; capture Plain Text
and non-folding contexts before changing current language-based command gates. Measure enabled
width at default and changed zoom/DPI, marker samples at header/body/tail, and outside-block
pixels with highlight on/off. Repeat language switch, New/Open and resize while preferences persist.

## Shared persistence, native probes and an achievable test matrix

- #151/#152 extend `src/settings/original_settings.h::OriginalSettings`,
  `SettingsStore::Load` / `Save`, and `EditorFrame` initialization / `SaveSettings:88–108`.
  Use original keys/defaults and the existing parser: leading 0/1 is accepted; invalid values
  fall back (`SettingsStore::ReadBool:151–166`, N `Helpers.cpp::GetBoolImpl:261–269`).
  Reuse `tests/settings_process.py::Application` and `current_line_process.py`'s independent
  process lifecycle: seed/import, explicit save, exit save on/off, restart, and unrelated-key retention.
- Start GUI tests with `NativeKeys::AfterPresentation`; use `NativeKeys::Shortcut`'s acknowledged
  activation and receiver trace, plus `NativeSearch::Command`'s native menu/update-UI dispatch.
  The shared key helper currently converts only F-keys on Windows and letters/F-names on Linux
  (`tests/native_key_helpers.h:174–242`). Extend this one seam for `[ ] , .`: Windows OEM translation
  must agree with SDK `src/msw/accel.cpp:105–119` and `wxMSWKeyboard::WXToVK`
  (`src/msw/window.cpp:7222–7258`, layout lookup then OEM fallback); Linux needs verified
  `bracketleft`, `bracketright`, `comma`, `period` names through the existing private driver's
  `key:` path (`tools/clipboard_isolated.py:274–278`). Check actual key-up receipt and command result.
- Reuse `NativeFold::Paint`; make `NativeFold::MarginClick` target the queried actual margin
  geometry, with original width independently asserted from captures. Reuse `tests/fold_cpp.cpp::PixelReader`
  / `MarkerPixels` for margin output and `current_line.cpp::PaintObservation` for actual GTK paint;
  Windows uses the owned control's DC. Compare bounded numerical regions; preserve font/DPI settings.
- The public core interface has no brace-position or marker-highlight-enabled getters
  (`N scintilla/include/Scintilla.iface:452,1275–1278,2220–2236`). For #151's exact-position
  requirement, use a narrow test-owned observation of actual core `braces[2]` and `bracesMatchStyle`
  at the core/drawing boundary, alongside the public guide value and independent paint evidence.
  This missing observation is implementation work; expected ranges alone do not observe core state.

| Layer | #150 | #151 | #152 |
| --- | --- | --- | --- |
| Original fixture capture | Seven commands, traversal/selection edges above | Adjacency, styles, lifecycle, off/on guide | Expansion, widths, marker emphasis and gates |
| Windows + private X11 + private Wayland registered native tests | Actual menus and all punctuation shortcuts; byte positions, visibility, directed selection, text/history | Native toggle and Ctrl+Shift+V; actual core decoration plus paint; explicit brace/Paste regression | Native toggle, hit geometry, expanded lines, selected-marker paint; resize/zoom/DPI |
| Independent settings processes | Existing selection preference retained | MatchBraces boolean import/save/restart | Both booleans import/save/restart, retained through syntax/file changes |
| Focused regressions | `original_navigation_bindings`, `original_matching_brace_commands`, bookmarks, fold/search reveal | Explicit brace, clipboard/Paste, current-line/guides and syntax tests | `cpp_fold_margin`, Python/JSON folding, `fold_all_commands`, line numbers, wrap, current-line/guides |

Register tests through existing CMake helpers (`CMakeLists.txt:329–330,358–366`); follow the parent's
build/format/native clang-tidy, Standards/Spec review and
hosted checks. Linux builds/tests stay native Linux; Linux human IME stays deferred. Record actual DPI/backend values.

CI readiness: [PR #157](https://github.com/1298345095/wxnotepad/pull/157) introduces compilation-database
analysis and separates fixture coverage accounting from application coverage. The application gate
requires 80% new-code coverage, while the current workflow has no application coverage collector.
Prepare genuine report collection alongside reference captures before integrating production changes;
successful native assertions alone do not provide Sonar coverage data. This is shared CI preparation,
not a dependency between the three feature tickets.

## Parallel ownership and merge order

All three can start together: #150 owns block operations/navigation fixtures/shared punctuation;
#151 owns feedback refresh/brace styles; #152 owns fold preference/geometry/marker styles.
Real overlap is `editor_frame_commands.cpp`, `editor_control.h/.cpp`, `editor_core.cpp`, CMake and
native helpers; #151/#152 also overlap `syntax_scheme.cpp`, `OriginalSettings`, SettingsStore and
`editor_frame.cpp`. Coordinate these edit regions while retaining existing modules.
Suggested merge order is #150, #151, #152: land shared keys first, then visual feedback, then the
margin geometry change and its shared hit-test update. This orders integration edits, not blockers.

| Follow-on candidate | Bounded coordination point |
| --- | --- |
| #153 bookmark/history margins | Coordinate final margin positions with #152; retain original undo-history enable gate (existing research, N `Notepad4.cpp:3877–3888`). |
| #154 caret appearance | W `EditorControl` constructor owns a Windows wxCaret and sets core caret style 0; later work must cover native/IME restoration and applicable Microsoft Pinyin human validation. |
| #155 occurrence marking | Coordinate indicator 8/bookmark 0 with Find All; preserve original scheduling/limits (existing research, N `Notepad4.cpp:4660–4686`). |
| #156 encoding/shebang insertion | Independent smaller fourth parallel candidate; reuse codec/document insertion paths and original encoding/shebang rules from the existing research. |

# Native dialog layouts and resizing

The requested change is to audit every existing dialog against Notepad4, adopt
its control grouping and button placement, and use its resizing axes. In
particular, Find/Replace must resize horizontally, rather than remaining fixed
or allowing vertical dragging. The English, Simplified Chinese and Traditional
Chinese interfaces retain their native fonts, translated captions and controls.

The reference is `src/Notepad4.rc`, `Edit.cpp`, `Dialogs.cpp` and `Helpers.h`
in the local Notepad4 source. `ResizeDlg_InitX` locks the original fitted height;
`ResizeDlg_InitY2` divides additional height between two text inputs. Initial
wx dialog sizes use translated native-control measurements rather than copied
Win32 dialog-unit pixel values.

## Complete application dialog inventory

| wxnotepad dialog | Notepad4 reference | Resize policy and layout |
| --- | --- | --- |
| Find, Replace | IDD_FIND, IDD_REPLACE; EditFindReplaceDlgProc | Horizontal only; query/replacement above two option columns, vertical action column at right, mode link below |
| Run Command | IDD_RUN; RunDlgProc | Horizontal only; command and browser in one row, bottom action row |
| Open Document With | IDD_OPENWITH; OpenWithDlgProc | Both axes; expanding program list above directory selection and bottom buttons |
| Recent Files | IDD_FILEMRU; FileMRUDlgProc | Both axes; expanding list, remember/clear row, capacity and bottom buttons |
| Current/default encoding selection | IDD_SELECT_ENCODING; SelectEncodingDlgProc | Both axes; expanding native tree above bottom buttons |
| Reload With Encoding | IDD_SELECT_ENCODING | Both axes; expanding native list above bottom buttons |
| Modify Lines | IDD_MODIFYLINES; EditModifyLinesDlgProc | Both axes; equal growth of the paired inputs; substitutions below; skip-empty and action buttons share the footer |
| Enclose Selection | IDD_ENCLOSESELECTION; EditEncloseSelectionDlgProc | Both axes; equal growth of the paired inputs, bottom action buttons |
| Insert HTML/XML Tag | IDD_INSERTTAG; EditInsertTagDlgProc | Both axes; 75% of additional height for opening tag, 25% for closing tag, initially equal input heights |
| Align Lines | IDD_ALIGN | Fixed; radio choices left, OK/Cancel at upper right |
| Sort Lines | IDD_SORT | Fixed; choices and options left, OK/Cancel at upper right |
| Column Wrap | IDD_COLUMNWRAP | Fixed; boundary label/input left, OK/Cancel right |
| Long Lines Settings | IDD_LONGLINES | Fixed; boundary and display choices left, OK/Cancel right |
| Go To Line | IDD_LINENUM | Fixed; line/column fields left, OK/Cancel right |
| Line Endings | IDD_DEFEOLMODE | Fixed; mode/options left, OK/Cancel right |
| Inconsistent Line Endings | IDD_WARNLINEENDS | Fixed; counts/question above mode and suppression option, Yes/No at their right |
| File Change Notification | IDD_CHANGENOTIFY | Fixed; monitoring choices left, OK/Cancel right |
| Default Encoding | IDD_DEFENCODING | Fixed; selected encoding, chooser and detection options, bottom buttons |
| Tab Settings | IDD_TABSETTINGS | Fixed; Global/Scheme/File groups stacked vertically, widths and inheritance arranged in rows |
| Word Wrap Settings | IDD_WORDWRAP | Fixed; indent, before/after symbols, mode, subline options, bottom buttons |
| Auto Completion Settings | IDD_AUTOCOMPLETION | Fixed; word settings/scopes/completion keys in left column, insertion and scan scope in right column |
| AutoSave Settings | IDD_AUTOSAVE | Fixed; backup options, folder and action buttons in one footer |
| Zoom Level | IDD_ZOOMLEVEL | Fixed; editable zoom and Apply share a row, OK/Cancel below |
| Page Setup | IDD_PAGESETUP | Fixed; paper/margins/header/footer/color groups left, paper/margin preview and zoom right, bottom actions |
| Search/encoding feedback and suppressed warnings | IDD_INFOBOX | Fixed; icon/message above suppression option and action buttons in one footer |
| Character Info | Original information message | Fixed; technical text above acknowledgement button |

Open/Save, directory selection, font/color choosers, paper/printer dialogs and
ordinary confirmation/error messages remain native system dialogs. Their own
platform sizing rules are retained, as in the original's system dialogs. Menus
and editor-owned calltips/completion lists are popups, not resizable dialogs.
There are no implemented Favorites, full style-customization, system-integration,
CSV-options or About dialogs to migrate in the current application.

Page Setup keeps its existing native paper/orientation chooser. Its new preview
only displays paper orientation and current margins; it neither prints nor
changes the existing pagination/selection/output rules. Cancel retains the
existing settings behavior. Native paint coordinates avoid a scaled raster
preview.

## Shared fitting and regression checks

`DialogLayout` centralizes translated fitting, minimum sizes, horizontal maximum
height and the original side-button pattern. Find/Replace releases the previous
height hints before a mode change and retains expanded width across mode
switches and result updates. A horizontal drag grows the text fields and moves
the entire action column by the same amount. Other dialogs keep their own
layout and state-commit logic.

`native_dialog_layout_{1033,2052,1028}` opens 25 actual production modal entry
points per language. It checks native resizing properties, containment and
non-overlap of all visible controls, minimum/expanded/restored geometry and the
50/50 or 75/25 input-height allocation. It cancels each dialog and does not write
the clipboard. The existing `native_replace_layout_*` cases additionally check
the modeless search layout, Windows native track-height constraints, horizontal
field/action motion, retained width and all original ordered-row assertions.
Existing functional and localization regressions retain their assertions,
timeouts and concurrency. Character Info and native system dialogs remain
covered by their existing functional/packaged-application tests.

The page-preview renderer is shared by the native paint callback and raster
checks for standard A4, rotated custom paper, metric/inch margins, margins larger
than the paper, and unavailable paper dimensions. One displayed inch uses
25.4 mm, with the real `{1000, 3, false}` inch-display format supplied by
`EditorPrinting`. Pixel coordinates stay exact; dark ink detection accommodates
native stroke antialiasing. `WXNOTEPAD_PAGE_PREVIEW_IMAGE` optionally retains the
actual A4 raster. Both metric and inch-display dialogs verify cancellation
leaves stored margins unchanged.

New child sizers retain temporary ownership until they are added to their
parent, and new root layouts transfer ownership at `SetSizer`. Native controls
continue using wx parent-window ownership. The preview's printer draft is copied
once; accepting its native paper chooser is tracked separately from committing
the outer dialog's settings.

Use the canonical native runner documented in `docs/ci.md`, selecting
`^(native_dialog_layout|native_replace_layout)_` for the layout-specific checks.
Evidence for this change is stored outside the repository under
`F:/wxnotepad-evidence/native-dialog-layout-2026-11-22/`.

# Original behavior audit

This is a source audit of implemented editor workflows, not a claim of complete editing-behavior compatibility or a runtime certification. It compares wxnotepad **`0b972edc3a5c87d631e253c23eb11bb29ded383f`** with upstream Notepad4 **`89b8b36e0ee2c23f73be922315f17261ccfbbe1a`**. Both local checkouts were at these revisions when inspected. Citations below pin those revisions. No GUI observations, screenshots, builds, or test reruns were used in this audit.

The distinction matters for ADR 0002: preserving editing behavior is the migration goal; staged implementation does not establish equivalence of every operation. Notepad4 also has user-configurable policies. A conditional upstream behavior below means the specified setting is enabled, not that every upstream installation behaves that way.

## Confirmed source-level differences in existing workflows

### 1. Ordinary Open uses a much narrower encoding decision

**Trigger:** Open a non-UTF-8 file without explicitly choosing an encoding. For example, a valid GBK file with non-ASCII bytes, on an upstream installation configured with CP936 as its fallback/default encoding.

**wxnotepad:** Automatic Open chooses UTF-16LE only for `FF FE`; otherwise it chooses UTF-8, strips the applicable BOM, and rejects invalid UTF-8. GBK is available through an explicit encoding choice. **Notepad4:** Open consults explicit/weak encoding choices, BOMs, file encoding declarations, configured fallbacks, and the system ANSI code page; it can select a legacy encoding and load the file. The same GBK content therefore need not require an explicit Open As step upstream. This does not mean upstream always guesses GBK correctly.

Evidence: [wxnotepad `DocumentSession::Open`, lines 184–210](https://github.com/1298345095/wxnotepad/blob/0b972edc3a5c87d631e253c23eb11bb29ded383f/src/document/document_session.cpp#L184-L210); [Notepad4 encoding decision, lines 2367–2397](https://github.com/zufuliu/notepad4/blob/89b8b36e0ee2c23f73be922315f17261ccfbbe1a/src/EditEncoding.cpp#L2367-L2397), [fallback selection, lines 2426–2505](https://github.com/zufuliu/notepad4/blob/89b8b36e0ee2c23f73be922315f17261ccfbbe1a/src/EditEncoding.cpp#L2426-L2505), and [legacy decoding, lines 1099–1120](https://github.com/zufuliu/notepad4/blob/89b8b36e0ee2c23f73be922315f17261ccfbbe1a/src/Edit.cpp#L1099-L1120).

### 2. Confirming a lossy GBK save changes the visible document differently

**Trigger:** Save a GBK document containing a character CP936 cannot represent, and accept the lossy-save choice.

**wxnotepad:** It encodes the replacement bytes, decodes them back, replaces the document buffer inside an undo action, then establishes a save point. The visible text therefore reflects the bytes actually saved. It also offers switching the save to UTF-8. **Notepad4:** The legacy save path warns with OK/Cancel and writes the converted bytes; it establishes a save point without replacing the editor text with the lossy conversion. Thus the displayed text and subsequent undo behavior differ even when both write a lossy legacy file.

Evidence: [wxnotepad GBK conversion and buffer replacement, lines 126–159](https://github.com/1298345095/wxnotepad/blob/0b972edc3a5c87d631e253c23eb11bb29ded383f/src/document/document_session.cpp#L126-L159) and [save point, lines 176–181](https://github.com/1298345095/wxnotepad/blob/0b972edc3a5c87d631e253c23eb11bb29ded383f/src/document/document_session.cpp#L176-L181); [Notepad4 conversion/warning, lines 1221–1247](https://github.com/zufuliu/notepad4/blob/89b8b36e0ee2c23f73be922315f17261ccfbbe1a/src/Edit.cpp#L1221-L1247) and [write/save point, lines 1251–1292](https://github.com/zufuliu/notepad4/blob/89b8b36e0ee2c23f73be922315f17261ccfbbe1a/src/Edit.cpp#L1251-L1292).

### 3. Mixed line endings are preserved without upstream's optional warning/cleanup policies

**Trigger:** Open a file containing both CRLF and LF, or save a mixed-EOL document when upstream's Fix Line Endings setting is enabled.

**wxnotepad:** Open counts EOLs to choose the insertion mode; Save serializes the existing buffer without automatically normalizing line endings. Conversion is an explicit menu operation. **Notepad4:** It also counts EOLs, but can offer a mixed-line-ending conversion warning on Open (`bWarnLineEndings`) and normalizes on Save when `bFixLineEndings` is enabled. Its save path can also strip trailing blanks when `bAutoStripBlanks` is enabled. The port's successful Open/Save does not imply those optional upstream policies ran.

Evidence: [wxnotepad EOL detection, lines 17–34](https://github.com/1298345095/wxnotepad/blob/0b972edc3a5c87d631e253c23eb11bb29ded383f/src/document/document_session.cpp#L17-L34), [buffer replacement, lines 220–234](https://github.com/1298345095/wxnotepad/blob/0b972edc3a5c87d631e253c23eb11bb29ded383f/src/document/document_session.cpp#L220-L234), [save serialization, lines 120–151](https://github.com/1298345095/wxnotepad/blob/0b972edc3a5c87d631e253c23eb11bb29ded383f/src/document/document_session.cpp#L120-L151), and [explicit conversion, lines 87–110](https://github.com/1298345095/wxnotepad/blob/0b972edc3a5c87d631e253c23eb11bb29ded383f/src/app/editor_frame.cpp#L87-L110); [Notepad4 Open warning, lines 7005–7016](https://github.com/zufuliu/notepad4/blob/89b8b36e0ee2c23f73be922315f17261ccfbbe1a/src/Notepad4.cpp#L7005-L7016), [save cleanup, lines 1162–1171](https://github.com/zufuliu/notepad4/blob/89b8b36e0ee2c23f73be922315f17261ccfbbe1a/src/Edit.cpp#L1162-L1171).

The port's CRLF tie preference matches the upstream algorithm when upstream's configured default EOL is CRLF. It is not a mismatch merely because mixed EOLs exist. Upstream retains its configured default when that mode ties for the largest count: [Notepad4 lines 793–809](https://github.com/zufuliu/notepad4/blob/89b8b36e0ee2c23f73be922315f17261ccfbbe1a/src/Edit.cpp#L793-L809), initialized from the default at [lines 1047–1049](https://github.com/zufuliu/notepad4/blob/89b8b36e0ee2c23f73be922315f17261ccfbbe1a/src/Edit.cpp#L1047-L1049); the port's fixed CRLF rule is cited above.

### 4. A short whitespace-only untitled document prompts differently

**Trigger:** Type only spaces/tabs/newlines into an untitled document, keeping it below 2,048 bytes, then choose New, Open, or Close.

**wxnotepad:** Any modified document reaches the Save/Discard/Cancel prompt. **Notepad4:** `FileSave(FileSaveFlag_Ask)` treats a short untitled buffer containing only `IsASpace` characters as an empty new file and returns before asking. This is a concrete extra prompt in the migrated basic document lifecycle; it is not a difference in explicit Save As.

Evidence: [wxnotepad Close/New/confirmation, lines 304–309](https://github.com/1298345095/wxnotepad/blob/0b972edc3a5c87d631e253c23eb11bb29ded383f/src/app/editor_frame.cpp#L304-L309), [lines 471–492](https://github.com/1298345095/wxnotepad/blob/0b972edc3a5c87d631e253c23eb11bb29ded383f/src/app/editor_frame.cpp#L471-L492), and [Open confirmation, lines 211–216](https://github.com/1298345095/wxnotepad/blob/0b972edc3a5c87d631e253c23eb11bb29ded383f/src/document/document_session.cpp#L211-L216); [Notepad4 whitespace shortcut and prompt, lines 7030–7073](https://github.com/zufuliu/notepad4/blob/89b8b36e0ee2c23f73be922315f17261ccfbbe1a/src/Notepad4.cpp#L7030-L7073), [Open/New save gate, lines 6791–6795](https://github.com/zufuliu/notepad4/blob/89b8b36e0ee2c23f73be922315f17261ccfbbe1a/src/Notepad4.cpp#L6791-L6795).

### 5. Reopening the current file loses view and manual language selection

**Trigger:** Move the caret/selection and horizontal view, manually select a language, then use Open to open that same file again.

**wxnotepad:** Successful replacement clears undo, goes to position zero, resets horizontal offset, and clears the language override so the path decides the language again. **Notepad4:** Its same-path branch records caret, anchor, line/column and scroll position, sets `keepCurrentLexer`, and restores the view after loading. This affects an existing Open workflow even without a separate Reload command in the port.

Evidence: [wxnotepad replacement, lines 220–234](https://github.com/1298345095/wxnotepad/blob/0b972edc3a5c87d631e253c23eb11bb29ded383f/src/document/document_session.cpp#L220-L234); [Notepad4 same-path capture, lines 6774–6787](https://github.com/zufuliu/notepad4/blob/89b8b36e0ee2c23f73be922315f17261ccfbbe1a/src/Notepad4.cpp#L6774-L6787), [lexer preservation, lines 6913–6932](https://github.com/zufuliu/notepad4/blob/89b8b36e0ee2c23f73be922315f17261ccfbbe1a/src/Notepad4.cpp#L6913-L6932), and [view restore, lines 6974–6984](https://github.com/zufuliu/notepad4/blob/89b8b36e0ee2c23f73be922315f17261ccfbbe1a/src/Notepad4.cpp#L6974-L6984).

### 6. Automatic language selection misses supported Python without an extension

**Trigger:** Open an extensionless script beginning `#!/usr/bin/python3`, with upstream automatic selection and CGI guessing enabled.

**wxnotepad:** `LanguageForPath` checks the extension only and falls back to Plain Text. Python can still be selected manually. **Notepad4:** For an unmatched path it sniffs the first line and recognizes a Python interpreter, selecting Python. This is a detection-policy difference for a language already supported by both editors, separate from the much larger upstream language catalog.

Evidence: [wxnotepad catalog/extension lookup, lines 20–55](https://github.com/1298345095/wxnotepad/blob/0b972edc3a5c87d631e253c23eb11bb29ded383f/src/syntax/syntax_catalog.cpp#L20-L55) and [language resolution, lines 46–48](https://github.com/1298345095/wxnotepad/blob/0b972edc3a5c87d631e253c23eb11bb29ded383f/src/document/document_session.h#L46-L48); [Notepad4 detection gate, lines 2698–2745](https://github.com/zufuliu/notepad4/blob/89b8b36e0ee2c23f73be922315f17261ccfbbe1a/src/Styles.cpp#L2698-L2745) and [Python shebang recognition, lines 1884–1926](https://github.com/zufuliu/notepad4/blob/89b8b36e0ee2c23f73be922315f17261ccfbbe1a/src/Styles.cpp#L1884-L1926).

### 7. Indentation choices carry across files instead of applying file/scheme policies

**Trigger:** Choose Spaces/2 Columns, then open another document or change language; alternatively open a consistently indented file with upstream Detect Indentation enabled.

**wxnotepad:** The indentation menu changes the current editor's Scintilla settings. Document replacement and language application do not load indentation policies, so the same editor choices carry through Open/New/language changes. **Notepad4:** Loading scans indentation when detection is enabled and no complete file tab settings are present; lexer changes load per-scheme tab/indent settings and apply the file variables unless those file settings already cover them. A Python/C++ file can therefore have a different Tab/Indent result upstream even when highlighting selects the same language.

Evidence: [wxnotepad indentation controls, lines 112–142](https://github.com/1298345095/wxnotepad/blob/0b972edc3a5c87d631e253c23eb11bb29ded383f/src/app/editor_frame.cpp#L112-L142), [replacement, lines 220–239](https://github.com/1298345095/wxnotepad/blob/0b972edc3a5c87d631e253c23eb11bb29ded383f/src/document/document_session.cpp#L220-L239), and [complete language application, lines 22–66](https://github.com/1298345095/wxnotepad/blob/0b972edc3a5c87d631e253c23eb11bb29ded383f/src/syntax/syntax_scheme.cpp#L22-L66); [Notepad4 detection gate, lines 813–827](https://github.com/zufuliu/notepad4/blob/89b8b36e0ee2c23f73be922315f17261ccfbbe1a/src/Edit.cpp#L813-L827), [detected settings, lines 943–951](https://github.com/zufuliu/notepad4/blob/89b8b36e0ee2c23f73be922315f17261ccfbbe1a/src/Edit.cpp#L943-L951), [load call, lines 1123–1133](https://github.com/zufuliu/notepad4/blob/89b8b36e0ee2c23f73be922315f17261ccfbbe1a/src/Edit.cpp#L1123-L1133), [scheme configuration, lines 667–675](https://github.com/zufuliu/notepad4/blob/89b8b36e0ee2c23f73be922315f17261ccfbbe1a/src/Styles.cpp#L667-L675), and [application, lines 1428–1438](https://github.com/zufuliu/notepad4/blob/89b8b36e0ee2c23f73be922315f17261ccfbbe1a/src/Styles.cpp#L1428-L1438).

### 8. Enter does not continue indentation as upstream can

**Trigger:** With upstream Auto Indent enabled, press Enter after `    text` or a Python `if ...:` line.

**wxnotepad:** Enter sends the core NewLine command; its host notification handler does not process character-added notifications to continue indentation. **Notepad4:** CR/LF character notifications invoke `EditAutoIndent` when `bIndentText` is enabled. That helper copies leading whitespace and can add an indentation level after a colon/open delimiter. Both editors accept Enter and have manual indentation commands, but the resulting line contents differ under this upstream setting.

Evidence: [wxnotepad Enter dispatch, lines 505–525](https://github.com/1298345095/wxnotepad/blob/0b972edc3a5c87d631e253c23eb11bb29ded383f/src/editor/editor_control.cpp#L505-L525) and [complete host notification handler, lines 351–370](https://github.com/1298345095/wxnotepad/blob/0b972edc3a5c87d631e253c23eb11bb29ded383f/src/editor/editor_control.cpp#L351-L370); [Notepad4 notification gate, lines 4730–4746](https://github.com/zufuliu/notepad4/blob/89b8b36e0ee2c23f73be922315f17261ccfbbe1a/src/Notepad4.cpp#L4730-L4746), [indent recognition/copy, lines 2215–2273](https://github.com/zufuliu/notepad4/blob/89b8b36e0ee2c23f73be922315f17261ccfbbe1a/src/EditAutoC.cpp#L2215-L2273), and [text insertion, lines 2291–2343](https://github.com/zufuliu/notepad4/blob/89b8b36e0ee2c23f73be922315f17261ccfbbe1a/src/EditAutoC.cpp#L2291-L2343).

### 9. Several existing core keyboard commands are not reached by the host

**Trigger:** Use Ctrl+Backspace/Ctrl+Delete to delete a word, Insert to toggle overtype, Shift+Enter, or the Shift+Insert/Shift+Delete clipboard shortcuts.

**wxnotepad:** Only navigation keys enter `KeyDownWithModifiers`. Other keys use a small manual dispatcher: Ctrl+Z/Y/A/C/X/V, plain Enter/Tab/Backspace/Delete, and a separate Shift+Tab operation. The combinations above are not forwarded to the original key map. **Notepad4:** Its native host forwards translated keys and modifiers; the default map binds these combinations to their editing commands. Having the same underlying core does not expose its complete keyboard behavior through the wx host.

Evidence: [wx navigation forwarding](https://github.com/1298345095/wxnotepad/blob/0b972edc3a5c87d631e253c23eb11bb29ded383f/src/editor/editor_control.cpp#L113-L145), [manual key/character dispatch](https://github.com/1298345095/wxnotepad/blob/0b972edc3a5c87d631e253c23eb11bb29ded383f/src/editor/editor_control.cpp#L484-L540); [original key forwarding](https://github.com/zufuliu/notepad4/blob/89b8b36e0ee2c23f73be922315f17261ccfbbe1a/scintilla/win32/ScintillaWin.cxx#L2075-L2076), [default editing bindings](https://github.com/zufuliu/notepad4/blob/89b8b36e0ee2c23f73be922315f17261ccfbbe1a/scintilla/src/KeyMap.cxx#L139-L175). These are source-established missing command paths; this audit did not inject the keys into a desktop.

### 10. Wrapped Home/End and paragraph navigation use different bindings

**Trigger:** Press Home/End on a later visual row of a wrapped logical line, or Ctrl+PageUp/Ctrl+PageDown to move by paragraph.

**wxnotepad:** It retains the core's default VCHome/LineEnd bindings and does not install the application's paragraph overrides. **Notepad4:** Initialization assigns VCHomeWrap/LineEndWrap (including Shift variants) and assigns Ctrl+PageUp/Down to paragraph navigation. Thus even navigation keys already forwarded by the port have different command mappings.

Evidence: [wx core initialization](https://github.com/1298345095/wxnotepad/blob/0b972edc3a5c87d631e253c23eb11bb29ded383f/src/editor/editor_control.cpp#L34-L50), [forwarding](https://github.com/1298345095/wxnotepad/blob/0b972edc3a5c87d631e253c23eb11bb29ded383f/src/editor/editor_control.cpp#L113-L145); [original application overrides](https://github.com/zufuliu/notepad4/blob/89b8b36e0ee2c23f73be922315f17261ccfbbe1a/src/Notepad4.cpp#L1746-L1753), [core defaults](https://github.com/zufuliu/notepad4/blob/89b8b36e0ee2c23f73be922315f17261ccfbbe1a/scintilla/src/KeyMap.cxx#L121-L138).

### 11. Ctrl+wheel scrolls instead of zooming

**Trigger:** Hold Ctrl while turning the vertical mouse wheel without Shift.

**wxnotepad:** Its wheel handler accumulates wheel steps and scrolls; it does not branch on Ctrl. **Notepad4:** The native wheel handler sends ZoomIn/ZoomOut for Ctrl+wheel. The port also has no exposed font/zoom controls in the current menus.

Evidence: [wx wheel handler](https://github.com/1298345095/wxnotepad/blob/0b972edc3a5c87d631e253c23eb11bb29ded383f/src/editor/editor_control.cpp#L174-L190), [current menus](https://github.com/1298345095/wxnotepad/blob/0b972edc3a5c87d631e253c23eb11bb29ded383f/src/app/editor_frame.cpp#L51-L221); [original Ctrl+wheel branch](https://github.com/zufuliu/notepad4/blob/89b8b36e0ee2c23f73be922315f17261ccfbbe1a/scintilla/win32/ScintillaWin.cxx#L2030-L2049).

### 12. Go To is restricted to a strictly parsed logical line

**Trigger:** Enter a line plus column, navigate directly to a document position, or paste a numeric line prefix such as `5410:` into Go To.

**wxnotepad:** The dialog contains one line field; the parser requires the whole trimmed input to be a positive in-range integer and moves to that line's start. **Notepad4:** The dialog supports line and column fields, a position-only path when the line is absent, and numeric-prefix extraction (its source explicitly documents `5410:`). Both can navigate to a simple line number, but the command accepts fewer inputs and targets in the port.

Evidence: [wx dialog](https://github.com/1298345095/wxnotepad/blob/0b972edc3a5c87d631e253c23eb11bb29ded383f/src/app/go_to_line_dialog.cpp#L9-L49), [strict parser/target](https://github.com/1298345095/wxnotepad/blob/0b972edc3a5c87d631e253c23eb11bb29ded383f/src/editor/line_navigation.cpp#L5-L21); [original dialog parsing/position path](https://github.com/zufuliu/notepad4/blob/89b8b36e0ee2c23f73be922315f17261ccfbbe1a/src/Edit.cpp#L5948-L6035), [column target calculation](https://github.com/zufuliu/notepad4/blob/89b8b36e0ee2c23f73be922315f17261ccfbbe1a/src/Edit.cpp#L4304-L4321).

### 13. Selection transfer has only ordinary stream-text behavior

**Trigger:** Alt-drag a rectangle, drag selected text, or paste a line/rectangle copied by an editor that publishes the Windows selection-shape clipboard formats.

**wxnotepad:** Mouse modifiers retain Shift only; text drag/drop is disabled and StartDrag is empty. Clipboard publication uses text alone, and Paste always uses `PasteShape::stream`. Empty-selection Cut/Copy are disabled. **Notepad4:** Its host preserves mouse modifiers and provides native text drag/drop; its clipboard implementation publishes/reads line and rectangular selection tags and chooses the corresponding paste shape. When its line-selection option is enabled, empty-selection Copy/Cut operates on the current line. This is a capability and integration gap, not a claim that ordinary Unicode stream copy/paste is broken.

Evidence: [wx selection modifiers/drag disable](https://github.com/1298345095/wxnotepad/blob/0b972edc3a5c87d631e253c23eb11bb29ded383f/src/editor/editor_control.cpp#L26-L48), [clipboard operations](https://github.com/1298345095/wxnotepad/blob/0b972edc3a5c87d631e253c23eb11bb29ded383f/src/editor/editor_control.cpp#L285-L349), [empty drag implementation](https://github.com/1298345095/wxnotepad/blob/0b972edc3a5c87d631e253c23eb11bb29ded383f/src/editor/editor_control.cpp#L383), [menu gating](https://github.com/1298345095/wxnotepad/blob/0b972edc3a5c87d631e253c23eb11bb29ded383f/src/app/editor_frame.cpp#L282-L301); [original native drag and modifier forwarding](https://github.com/zufuliu/notepad4/blob/89b8b36e0ee2c23f73be922315f17261ccfbbe1a/scintilla/win32/ScintillaWin.cxx#L1190-L1215), [paste-shape handling](https://github.com/zufuliu/notepad4/blob/89b8b36e0ee2c23f73be922315f17261ccfbbe1a/scintilla/win32/ScintillaWin.cxx#L3447-L3515), [publication tags](https://github.com/zufuliu/notepad4/blob/89b8b36e0ee2c23f73be922315f17261ccfbbe1a/scintilla/win32/ScintillaWin.cxx#L3959-L3991), [optional empty-selection line operations](https://github.com/zufuliu/notepad4/blob/89b8b36e0ee2c23f73be922315f17261ccfbbe1a/src/Notepad4.cpp#L3063-L3091).

### 14. Find initialization and wrap feedback differ

**Trigger:** Open Find with a selection (or the configured clipboard seed), or search past the last/first match.

**wxnotepad:** Present uses the stored session query, focuses/selects the query field, and does not import the editor selection/clipboard. When wrapping is enabled, it searches the opposite boundary directly and reports the result through the status field. **Notepad4:** Under its respective selection/clipboard options it seeds the query, and opening Replace with selected text can focus the replacement field. Next/Previous go through a suppressible OK/Cancel wrap information box. The port also lacks the original Find All, Replace in Selection, wildcard and transform-backslash controls; those are additional unported search facilities.

Evidence: [wx dialog initialization/presentation](https://github.com/1298345095/wxnotepad/blob/0b972edc3a5c87d631e253c23eb11bb29ded383f/src/app/find_replace_dialog.cpp#L13-L55), [Present](https://github.com/1298345095/wxnotepad/blob/0b972edc3a5c87d631e253c23eb11bb29ded383f/src/app/find_replace_dialog.cpp#L94-L111), [automatic wrap](https://github.com/1298345095/wxnotepad/blob/0b972edc3a5c87d631e253c23eb11bb29ded383f/src/search/search_session.cpp#L63-L80); [original seed options](https://github.com/zufuliu/notepad4/blob/89b8b36e0ee2c23f73be922315f17261ccfbbe1a/src/Edit.cpp#L4648-L4695), [dialog seed/focus](https://github.com/zufuliu/notepad4/blob/89b8b36e0ee2c23f73be922315f17261ccfbbe1a/src/Edit.cpp#L4791-L4805), [forward wrap](https://github.com/zufuliu/notepad4/blob/89b8b36e0ee2c23f73be922315f17261ccfbbe1a/src/Edit.cpp#L5315-L5325), [backward wrap](https://github.com/zufuliu/notepad4/blob/89b8b36e0ee2c23f73be922315f17261ccfbbe1a/src/Edit.cpp#L5358-L5368), [additional search controls/commands](https://github.com/zufuliu/notepad4/blob/89b8b36e0ee2c23f73be922315f17261ccfbbe1a/src/Edit.cpp#L4916-L5065).

## Intentional search adaptations to review as product choices

The port deliberately changes some original regex behavior. Backward regex Find scans forward to choose the previous match, allowing cross-line matches in both directions. The pinned original backward Boost path scans individual lines. Forward Boost matching in the port also supplies a document-start base iterator so lookbehind retains earlier context when Next resumes after a match; the pinned original passes the target start as the default base. The example recorded in the patch rationale is `aa|(?<=aa)b`: the resumed search can retain the preceding `aa` context in the port. These are implemented adaptations with existing regression coverage, rather than missing original functionality. They should remain explicit product decisions when planning compatibility work.

Evidence: [port backward scan](https://github.com/1298345095/wxnotepad/blob/0b972edc3a5c87d631e253c23eb11bb29ded383f/src/search/search_session.cpp#L43-L72), [context patch](https://github.com/1298345095/wxnotepad/blob/0b972edc3a5c87d631e253c23eb11bb29ded383f/patches/original-core.patch#L421-L432), [rationale/coverage](https://github.com/1298345095/wxnotepad/blob/0b972edc3a5c87d631e253c23eb11bb29ded383f/patches/README.md#L23-L31); [original matching directions](https://github.com/zufuliu/notepad4/blob/89b8b36e0ee2c23f73be922315f17261ccfbbe1a/scintilla/src/Document.cxx#L3576-L3616), [original Previous uses backward search](https://github.com/zufuliu/notepad4/blob/89b8b36e0ee2c23f73be922315f17261ccfbbe1a/src/Edit.cpp#L5358-L5359).

## Unmigrated policy/configuration scope

- New is fixed to UTF-8 without BOM and CRLF/Plain Text in this port. Upstream initializes from its configured default encoding/EOL/default lexer and can apply scheme defaults to an empty file. This is chiefly a settings/default-policy migration gap, not proof that fresh unconfigured installations always differ: [wxnotepad New/Replace](https://github.com/1298345095/wxnotepad/blob/0b972edc3a5c87d631e253c23eb11bb29ded383f/src/document/document_session.cpp#L220-L239), [EOL default](https://github.com/1298345095/wxnotepad/blob/0b972edc3a5c87d631e253c23eb11bb29ded383f/src/document/document_session.cpp#L17-L34), [Notepad4 New](https://github.com/zufuliu/notepad4/blob/89b8b36e0ee2c23f73be922315f17261ccfbbe1a/src/Notepad4.cpp#L6797-L6812), [default lexer](https://github.com/zufuliu/notepad4/blob/89b8b36e0ee2c23f73be922315f17261ccfbbe1a/src/Styles.cpp#L1412-L1417), [empty-file scheme defaults](https://github.com/zufuliu/notepad4/blob/89b8b36e0ee2c23f73be922315f17261ccfbbe1a/src/Notepad4.cpp#L7189-L7243).
- The current language catalog is Plain Text, C/C++, Python, and JSON. This audit does not label every absent upstream language as a defect in a migrated feature. The supported-language detection limitation above is more actionable than an unbounded catalog comparison: [port catalog](https://github.com/1298345095/wxnotepad/blob/0b972edc3a5c87d631e253c23eb11bb29ded383f/src/syntax/syntax_catalog.cpp#L20-L30), [upstream lexer selection/fallback](https://github.com/zufuliu/notepad4/blob/89b8b36e0ee2c23f73be922315f17261ccfbbe1a/src/Styles.cpp#L2691-L2804).
- The application menus currently expose the staged File/Edit/Language/View subset. Upstream also exposes printing/recent files, bookmarks, completion settings, file-change notifications, AutoSave and persisted settings. These are unmigrated features, rather than newly discovered defects in the Save As correction: [current application composition](https://github.com/1298345095/wxnotepad/blob/0b972edc3a5c87d631e253c23eb11bb29ded383f/src/app/editor_frame.cpp#L48-L221), [upstream Print/Recent](https://github.com/zufuliu/notepad4/blob/89b8b36e0ee2c23f73be922315f17261ccfbbe1a/src/Notepad4.rc#L169-L183), [bookmark margin](https://github.com/zufuliu/notepad4/blob/89b8b36e0ee2c23f73be922315f17261ccfbbe1a/src/Notepad4.rc#L462), [completion settings](https://github.com/zufuliu/notepad4/blob/89b8b36e0ee2c23f73be922315f17261ccfbbe1a/src/Notepad4.rc#L578-L581), [file monitoring/AutoSave](https://github.com/zufuliu/notepad4/blob/89b8b36e0ee2c23f73be922315f17261ccfbbe1a/src/Notepad4.rc#L663-L664), [settings persistence](https://github.com/zufuliu/notepad4/blob/89b8b36e0ee2c23f73be922315f17261ccfbbe1a/src/Notepad4.rc#L710-L711).

## Suggested order for parity work

1. Complete input command forwarding and original navigation overrides; include Ctrl+wheel zoom. These affect routine editing in every supported language.
2. Decide the lossy legacy-save buffer contract, then align automatic encoding selection and same-file reopening. Keep these decisions separate from adding more codecs.
3. Add autoindent and file/scheme indentation rules, then selection/clipboard transfer and Go To targets.
4. Align search initialization/feedback and optional save policies. Keep the documented regex adaptations explicit instead of treating every difference as a regression.

## Behavior not established by this audit

Source evidence establishes the branches above; native dialog layout, OS-specific encoding conversion details, file replacement metadata/permissions, filesystem error recovery, rendering, and cross-platform runtime equivalence require targeted observations. None were rerun here. The port writes a neighboring temporary file then replaces the destination, whereas upstream writes the destination handle directly; those mechanisms alone do not establish all observable filesystem semantics: [port write](https://github.com/1298345095/wxnotepad/blob/0b972edc3a5c87d631e253c23eb11bb29ded383f/src/document/document_session.cpp#L78-L96), [upstream open/write](https://github.com/zufuliu/notepad4/blob/89b8b36e0ee2c23f73be922315f17261ccfbbe1a/src/Edit.cpp#L1143-L1153), [write completion](https://github.com/zufuliu/notepad4/blob/89b8b36e0ee2c23f73be922315f17261ccfbbe1a/src/Edit.cpp#L1251-L1292).

The corrected Save As language rule is not reported as a mismatch: a specific selected type wins; All Files permits filename inference; the untitled Plain/default exception retains filename inference. In particular, successful Save As does **not** unconditionally infer language from the filename: [port selection resolution](https://github.com/1298345095/wxnotepad/blob/0b972edc3a5c87d631e253c23eb11bb29ded383f/src/document/document_session.cpp#L161-L180). There is no 32 MiB plain-text fallback policy in the current catalog/application paths inspected here.

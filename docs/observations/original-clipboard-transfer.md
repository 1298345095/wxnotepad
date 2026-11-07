# Original selection-shape clipboard transfer

Issue #71 retains the clipboard decisions of Notepad4 revision
`89b8b36e0ee2c23f73be922315f17261ccfbbe1a`, specifically
`scintilla/win32/ScintillaWin.cxx` (`CopyToClipboard`, `Paste`) and
`scintilla/src/Editor.cxx` (`CopySelectionRange`, `InsertPasteShape`).
The original core still owns selection extraction, rectangle insertion, line
insertion, EOL conversion, multiple selections and undo groups.

Windows publishes `CF_UNICODETEXT` together with the original tags:

| Meaning | Formats |
| --- | --- |
| Rectangle | `MSDEVColumnSelect`, `Borland IDE Block Type` with the single byte `02` |
| Line | `MSDEVLineSelect`, `VisualStudioEditorOperationsLineCutCopyClipboardTag` |

Paste accepts either line tag and the column tag or the exact Borland byte.
Rectangle meaning wins over line meaning. A line offer inserts at the current
logical line start only when the selection is empty; a selected range consumes
that offer as stream text. A line lacking its final EOL gains the document's EOL.
The core's conversion and multi-paste settings continue to control insertion.
Failed publication does not delete a Cut or LineCut range. Read-only documents
permit Copy and reject Cut/Paste. Successful edits retain the original undo group.

GTK publishes ordinary native text targets. Rectangles use ScintillaGTK's existing
UTF-8 convention: a final LF and a terminating NUL included in
the selection length. This is the convention in the locally pinned wxWidgets
3.3.3 Scintilla GTK adapter, `GetGtkSelectionText` and `GetSelection`.
The original core omits a final separator when extracting a rectangle, so the
GTK adapter supplies the final LF needed to carry that native marker. The raw
UTF-8 target is read before wx text conversion can discard the marker.
GTK line offers additionally carry the two original line tag names as native
custom targets. These targets retain line meaning between supporting peers.

The native test `original_clipboard_shapes` uses an independent clipboard peer
process, public editor commands, peer-saved UTF-8 bytes and application Save As
followed by an independent binary read. It covers both line tags, rectangle
precedence, tabs and non-ASCII text, EOL conversion enabled/disabled,
MultiPaste Once/Each, read-only behavior and single-step Undo/Redo. Windows also
covers the Borland byte and publication failure while the peer holds the clipboard.
`system_clipboard` retains stock text-widget Unicode interoperability coverage.
The shape test preserves `clipboard-shape-peer.txt` and
`clipboard-shape-editor.txt` in its CTest working directory.

The observed stock wx/GTK text widget offers ordinary Unicode without either
line tag or the rectangle marker. It therefore transfers stream text. This
observation does not establish line metadata support in other native editors.
The empty-selection current-line Copy/Cut preference belongs to issue #72;
this slice preserves the existing explicit LineCopy/LineCut command parameters.

Issue #140 retains the original bulk clipboard commands. Copy All publishes the
whole buffer as stream text while retaining selection and document state. Copy
Add appends two CRLF sequences and selected text to existing Unicode text; its
direct caret-only handler uses the whole buffer. Its native menu and Ctrl+E
remain selection-gated. Without Unicode text, it calls the underlying core Copy,
which leaves a caret-only clipboard unchanged for every current-line preference.
Only the Unicode-append branch rejects a nonempty rectangular selection.

The original SelectionText extraction replaces NUL with spaces for Copy All and
selected Copy Add. Whole-buffer Copy Add and existing clipboard text stop at the
first NUL. Successful append discards previous transfer-kind tags. Clear Clipboard
accepts non-text formats and leaves no native formats. GTK takes a zero-target
offer before releasing ownership, clearing both foreign ownership and clipboard
manager caches on the supported X11/Wayland backends.

GTK rectangle offers additionally expose `application/x-wxnotepad-plain-text`
containing original UTF-8 bytes before the LF/NUL shape carrier. Copy Add reads
that target when available, retaining both ordinary rectangles and genuine empty
final rows. Existing native text targets and Paste's shape interpretation remain
unchanged. The independent peer tests in `original_clipboard_utilities` cover
these commands, native menu/shortcut availability, original NUL boundaries,
rectangular feedback and unchanged encoded Save/Undo/Redo bytes.

Issue #141 restores Swap and Ctrl+K from `src/Notepad4.cpp::IDM_EDIT_SWAP`.
Its menu is enabled by a nonempty selection or native CanPaste. With a selection,
it reads plain native Unicode first, stops at NUL, normalizes every CR/LF/CRLF to
the document EOL mode, and uses the original Cut and ReplaceSel messages inside
one undo action. The replacement length counts UTF-8 document bytes. The original
anchor/caret direction determines the resulting selected endpoints, including
rectangles and read-only endpoint clamping. Outgoing selection extraction and
transfer-kind tags remain the delivered Cut rules.

With an empty selection, Swap uses ordinary Paste, selects the old-to-resulting
caret span, then clears every native clipboard format. This retains ordinary
line/rectangle, multiple-caret, virtual-space and paste-convert behavior. A shaped
paste can edit the document while leaving that selected span empty. Direct empty
commands clear non-text or read-only offers even when native availability disables
the menu. Document Undo/Redo does not restore the earlier clipboard offer.

The focused `original_clipboard_swap` native test uses independent clipboard
processes, actual menu/Ctrl+K, literal original Windows outcomes, and independent
UTF-8/BOM/UTF-16LE/GBK disk reads through Save/Undo/Redo. Reference source is
`89b8b36e0ee2c23f73be922315f17261ccfbbe1a`; root-coordinated Windows observations
and the corrected shape/GBK capture are retained in the external #141 evidence
directory. The existing baseline executable build log omitted its source revision.

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

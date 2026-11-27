# CSS/JS/JSON Compress and Pretty

Tools → Web Tools exposes Compress followed by Pretty, as in Notepad4
`89b8b36e0ee2c23f73be922315f17261ccfbbe1a`. Availability follows the original
scheme's `LexerAttr_CodePretty`: JSON; JavaScript and JSX; CSS, SCSS, Less and
HSS. TypeScript and TSX share the JavaScript lexer but lack that capability in
the original and remain disabled. The active scheme controls availability.

The implementation reuses `Bridge.cpp::AddStyleSeparator`, `CodePretty` and
the compression branch of `EditFormatCode`, with the delivered original lexer
styles and scheme metadata. Compress removes default style bytes and comments,
retains string/escape styles, removes non-string line continuations and adds
the original separators at style transitions. These include adjacent word
characters, CSS dimensions/colors/custom properties, adjacent plus/minus
operators and spaces around CSS math operators.

Pretty uses the original brace stack and style-sensitive spacing, indentation,
comment-line and statement rules. It preserves the original string, template,
regular-expression and comment handling. It uses the current EOL mode and
tab width/use-tabs settings. It performs no syntax validation. Compress replaces
only when the output is shorter; Pretty replaces only when the byte lengths
differ. Thus equal-length Pretty output is intentionally discarded, even when
the candidate bytes would differ.

Both commands complete styling through the main selection's range end before
reading styles. Matching selection endpoints select the whole-document path;
an empty document does nothing. Otherwise, the original main-selection range
is replaced and reselected with its previous direction. Rectangles use the
original outer range; multiple selections follow the original main-selection
path. The original Scintilla edit operation provides one Undo/Redo operation.
Read-only protection remains in the editor core. The commands preserve the
document's encoding, BOM and file identity and do not add configuration keys.

`tests/reference_capture/capture_code_format.py` runs an independent portable
copy of the original binary on a private Windows desktop using public native
scheme/command and Scintilla messages. Its executable SHA-256 is
`1425c9a4b5ab2ea1fa3ba088f38dd987b763b7b8feff23dde6243f7855e86a78`.
`tests/code_format_reference.tsv` freezes 2,604 after/Undo/Redo observations,
covering all seven capable schemes plus disabled TypeScript/TSX, CRLF/CR/LF,
Unicode, escapes, templates, regexes, CSS selectors/colors, JSON nesting,
long comments/lines, empty and equal-length
cases, forward/reversed/partial selections, rectangles, multiple selections,
empty main selections, read-only and tabs. The receipt is adjacent to the TSV.

The companion file fixture captures 46 original UTF-8/BOM, UTF-16LE/BE BOM,
GBK, Big5, Windows-1252, save/reopen and explicit equal-length no-op cases.
`original_code_format` replays both fixtures through native application menus;
`packaged_code_format_localization` checks the delivered English and two Chinese
menus and compares saved files from both commands with the same original
reference. Run these focused regressions through the standard Windows/private
X11/private Wayland entry points in `docs/ci.md`.

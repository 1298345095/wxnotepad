# Copy as RTF

Edit → Copy to Clipboard → Copy as RTF follows Copy Add. The original enables
the menu item when any selection is nonempty. The command uses the rectangle's
outer byte range or the main stream selection. If the two endpoints coincide,
the command uses the entire document; a document with no bytes leaves the
clipboard untouched. This explains why an empty main selection with a nonempty
secondary selection can copy the document from the menu. Copying preserves the
selection, dirty state and edit history.

The editor brings syntax styles current through the range end. It exports the
active fonts, colors, text styles and tab behavior using the style-table and text
rules of Notepad4 `89b8b36e0ee2c23f73be922315f17261ccfbbe1a`
`Bridge.cpp::SaveToStreamRTF` and `EditFormatCode`. The original uses its legacy
ANSI code page for the font table when editing Unicode. On Linux the RTF ANSI
header uses Windows-1252, and non-ASCII document text is emitted using RTF
Unicode escapes. On Windows a document in native ANSI mode retains its native
byte representation; explicitly opened Windows-1252 files retain the original
Unicode editor representation.

Windows publishes the registered `Rich Text Format` format with a trailing NUL.
GTK publishes the same kind of payload as `text/rtf` on X11 and Wayland. The
clipboard offer has no plain text or editor shape markers and adds no clipboard
history policy. Copy as RTF uses existing tab, font and scheme settings.

`tests/reference_capture/capture_rtf.py` launches an owned portable copy of the
pinned Windows executable, observes the real menu state, dispatches its command,
and reads bytes from the Windows clipboard. The recorded executable SHA-256 is
`1425c9a4b5ab2ea1fa3ba088f38dd987b763b7b8feff23dde6243f7855e86a78`;
the Windows capture host's ANSI code page is 936. The script writes records for
selected, whole, empty, rectangle, multiple, tab expansion, C++ styles and
legacy encoding inputs. The unchanged byte fixtures are in `tests/data/rtf/`.

Run `original_rtf_clipboard` through the relevant backend in `docs/ci.md`. A
separate native peer checks that RTF is the only content format and compares
the entire payload with the captured bytes using the counterpart host's ANSI
header. Its C++ fixture uses the same syntax and chooses the original font
faces and point sizes explicitly. Windows also performs a formatted paste into
a system RichEdit control and checks both the text and syntax color.

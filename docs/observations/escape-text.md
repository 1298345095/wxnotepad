# Original C and HTML/XML escape commands

The four Convert commands use the pinned Notepad4 command IDs 40204–40207 and
its shared `EditEscapeChars` byte scanner, reached through the existing
per-selection `CustomCaseMapping` command. The editor buffer stays UTF-8.
Plain Text, C/C++, Python and JSON all use the original non-resource-script
quote rules.

The C decoder recognizes backslash, quotes and the seven control escapes
`a b t n v f r`. It leaves `e`, hex, Unicode, octal, unknown and trailing
backslash sequences literal. HTML/XML recognizes `amp`, `quot`, `apos`, `lt`
and `gt`, with case-sensitive names and a required semicolon, in one pass.

## Reference and fixtures

Reference source: Notepad4
`89b8b36e0ee2c23f73be922315f17261ccfbbe1a`, `src/Edit.cpp`:
`EditMapTextCase` and `EditEscapeChars`; `scintilla/src/Editor.cxx`:
`ChangeCaseOfSelection`; `src/Notepad4.rc`: command names and shortcuts.

`tests/escape_text_fixtures.h` contains 40 literal byte fixtures and 28 literal
selection/history states captured from the retained original Windows executable
through its real Scintilla `CustomCaseMapping` message. Its embedded-NUL inputs
use length-aware process-memory buffers. Captures include empty, reversed,
multiple and both directions of rectangular selection, read-only input and
pending redo. Native fonts can put rectangle x positions at different byte
columns; the test restores each captured initial row's byte endpoints before
conversion to compare the same selected text.

Capture executable:
`E:/NewNotepad4/wxnotepad/build/notepad4-baseline/Notepad4.exe`.
`E:/NewNotepad4/wxnotepad/build/notepad4-baseline.log` records its successful
Notepad4 project build and output path. The original checkout's tracked source
was clean at the reference revision when capturing; the retained build log
does not record its source revision.

The capture script, raw states and red/green logs are retained under
`E:/NewNotepad4/wxnotepad-evidence/next-native-text-tools-2026-11-06/131`.
The script uses the adjacent `132/original_host.py` process-memory helper.

`original_escape_text` exercises native menu dispatch, the four exact shortcuts,
all supported languages, literal bytes and selection/history state, unchanged
redo, virtual-only rectangles, and independently reopened UTF-8/BOM, GBK and
UTF-16LE/BOM files through Save/Undo/Redo.

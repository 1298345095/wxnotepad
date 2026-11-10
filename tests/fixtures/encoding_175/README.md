# Original Windows encoding observations

These literal fixtures were captured from the retained native Notepad4 executable
using owned Windows controls and independent disk reads. Source authority is
Notepad4 `89b8b36e0ee2c23f73be922315f17261ccfbbe1a`, whose encoding catalog and
`EditSaveFile` conversion are adapted by this port under `licenses/Notepad4.txt`.
The retained executable's precise source revision is not independently recorded
in its build log. Full capture provenance and raw observations are retained at
`E:/NewNotepad4/wxnotepad-evidence/encoding-languages-2026-11-08/reference/README.md`.

`cp1252-all256` and `cp437-all256` contain every input byte, with the original
editor's observed UTF-8 output in the matching `.utf8` file. They include NUL,
control bytes, CP1252 C1/undefined mappings and CP437 box drawing. The expected
text is not produced by the codec under test.

The `save-*.bin` files are original destination saves of UTF-8 text
`A€∞Ā中😀` followed by U+E000 and CRLF. Cancel left the original destination
unchanged; accept produced these exact bytes while retaining live text and Undo.

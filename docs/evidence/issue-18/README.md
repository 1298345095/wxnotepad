# UTF-16LE file roundtrips

Scope: [#18](https://github.com/1298345095/wxnotepad/issues/18), 2026-10-30.

Normal Open detects the UTF-16LE BOM. File > Open As UTF-16LE explicitly
opens UTF-16LE with or without a BOM. The status bar identifies the encoding
and BOM. Save and Save As retain recorded encoding, BOM and actual CRLF/LF/CR
characters. New resets UTF-8 without BOM and the existing CRLF insertion mode.

The adapter uses explicit-length wxWidgets UTF-16LE/wide/UTF-8 conversions,
checking both sizing and conversion results. On Windows, UTF-16LE conversion
alone copies surrogate units; the strict UTF-8 encoder validates pairing before
any document replacement (`wxWidgets 3.3.3 src/common/strconv.cpp:1091–1103`).
On Linux, the UTF-16LE decoder also validates pairing. Embedded NUL is retained
because every conversion and editor operation receives its actual length.
The original `UTF8FromUTF16` helper stops at NUL and assumes valid surrogate
pairs, so it is unsuitable unchanged here. No UTF-16BE or heuristic guessing
was added. The existing safe writer remains the save transaction boundary.

TDD used the previously approved application file-operation/editor-state seam
with independently read disk bytes, plus real native dialog widgets. The first
red run rejected BOM UTF-16LE; the UI red run lacked the explicit menu. The
strict malformed-input cases already passed through the validated conversion
chain and needed no separate Unicode algorithm.

`utf16_roundtrip` checks BOM/no-BOM and empty samples, CJK, supplementary
characters, embedded NUL, actual mixed EOLs, unchanged Save, edited Save As to a
Unicode path, original-file isolation, failed/cancelled Save As, decoding
failure before dirty-document confirmation, retained Undo/save point, and New
format reset. Malformed cases cover odd byte count, lone high/low surrogate,
high followed by ASCII, and malformed input following NUL.

`open_new_native` additionally drives the real Windows/GTK choosers for explicit
UTF-16LE accept/cancel, shows the decoding error, and observes UTF-16LE in the
status bar of a shown frame. It uses process-local native widgets and runs
through the shared external desktop mutex.

Final validation after merging latest integration `9c2dd07` (already current):

| Backend | Release build | Focused checks |
| --- | --- | --- |
| Windows 11, MSVC 14.51 | passed | 6/6 |
| Ubuntu 24.04 WSLg, GTK3 X11, GCC 13.3 | passed | 6/6 |
| Ubuntu 24.04 WSLg, GTK3 Wayland | same Linux build | 6/6 |

All use wxWidgets 3.3.3 and the pinned original core. Linux sources/builds and
archive are under `/home/ctrls/.cache/wxnotepad-issue18/`; data uses native
temporary storage. Focused checks are `utf16_roundtrip`, `first_utf8_save`,
`open_utf8`, `open_new_native`, `save_existing`, and `save_as_native`.

Reproduce with `tools/build.py`, then CTest matching the six names above;
Windows uses `-C Release`, Linux uses explicit `GDK_BACKEND=x11` / `wayland`.
Native checks use the external `desktop_run.py` mutex wrapper. Linux IME
remains deferred.

# Character / hex conversion

The native Convert menu exposes the original Character to Hex (Ctrl+Alt+X) and
Hex to Character (Ctrl+Alt+C) commands through Scintilla's CustomCaseMapping
message. The shared original selection replacement path owns endpoint movement,
virtual space, read-only rejection, save points and grouped Undo/Redo.

The conversion boundary uses UTF-16 code units on Windows and Linux. An isolated
supplementary character receives the original additional six-digit U+ annotation;
a larger selection containing it emits its surrogate escapes. Decoding retains
the original digit limits, odd-digit truncation, scalar bounds, unpaired-surrogate
replacement and length-aware NUL handling. Unchanged decoding returns the original
bytes and preserves pending redo.

`tests/character_hex_fixtures.h` contains 18 literal byte results captured through
the retained Windows Notepad4 executable's real CustomCaseMapping message (2139).
The original checkout's tracked source was clean at reference revision
`89b8b36e0ee2c23f73be922315f17261ccfbbe1a` when capturing. The retained baseline build
log records the executable output path but does not record its source revision.

The capture helper, raw JSON, original selection-state samples, red/green commands
and native logs are retained at
`E:/NewNotepad4/wxnotepad-evidence/next-native-text-tools-2026-11-06/132/`.
The original executable and build log are retained at
`E:/NewNotepad4/wxnotepad/build/notepad4-baseline/Notepad4.exe` and
`E:/NewNotepad4/wxnotepad/build/notepad4-baseline.log`.

The `original_character_hex` test exercises actual native menu activation and
Ctrl+Alt+X/C input, all literal fixtures, empty/reversed/multiple/rectangular and
virtual selections, read-only and unchanged history, and independently reopened
UTF-8, GBK and UTF-16 disk bytes with encoding/BOM/EOL metadata. Run it through the
existing private display driver:

```sh
python3 .github/scripts/ci.py test --backend x11 --cmake-dir build --tests '^original_character_hex$' --jobs 1
python3 .github/scripts/ci.py test --backend wayland --cmake-dir build --tests '^original_character_hex$' --jobs 1
```

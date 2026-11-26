# Original CP932 and CP949 file fixtures

Captured through a separate owned Notepad4 application using
`tests/reference_capture/capture_japanese_korean.py`. The pinned source authority
is `89b8b36e0ee2c23f73be922315f17261ccfbbe1a` (EditLoadFile/EditSaveFile and
EditEncoding.cpp). The retained original executable SHA-256 is recorded in the
external `original.json` capture:
`1425c9a4b5ab2ea1fa3ba088f38dd987b763b7b8feff23dde6243f7855e86a78`. All final Unicode and Save bytes were also
independently compared with the exact Windows MultiByteToWideChar / no-best-fit
WideCharToMultiByte sequence used by that original.

`all-single-pairs.bin` contains every single byte (0–255), then every two-byte
value (0–65535, big endian), each followed by a NUL separator. The two `.utf8`
files are the actual original editor's complete Unicode buffer after explicit
CP932/CP949 Open. The `.saved.bin` files contain its actual Save output, including
malformed-sequence replacements and duplicate-character canonical bytes.
The test compares these literal files through real application Open and Save.
Its smaller fixtures separately cover boundaries, mixed EOL and best-fit loss.

The reproducible API probe additionally compared all BMP non-surrogate scalars
and strict/non-strict single/two-byte inputs against native glibc iconv. Only the
measured Windows compatibility gaps are implemented. Evidence for this round:
`F:/wxnotepad-evidence/launch-format-implementation-2026-11-26/279/reference-01`
and the sibling API observations, probe sources, comparison and run reports.

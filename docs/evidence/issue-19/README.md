# Issue 19: explicit GBK (CP936) round trips

The File menu offers **Open As GBK...** and the status bar identifies GBK.
GBK opening is explicit, strict, and length-aware, including embedded NUL.
It neither guesses GBK nor removes an apparent Unicode BOM. Save and Save As
preserve CP936 and actual CR/LF/CRLF characters without adding a BOM.

The pre-write native warning offers Cancel, Save as UTF-8, and Replace in GBK.
UTF-8 fallback is lossless without BOM and changes encoding only after disk
success. Confirmed GBK replacement changes the document to the decoded saved
bytes only after disk success, as one undoable edit followed by a savepoint.
Undo restores richer text dirty; Redo restores saved replacements clean.
Cancel/failure preserves disk, text, identity, encoding and history.

## Original conversion and compatibility data

Original source is pinned at `89b8b36e0ee2c23f73be922315f17261ccfbbe1a`.
`src/EditEncoding.cpp` defines GBK as CP936. `src/Edit.cpp:1221–1247` pivots
UTF-8 through UTF-16 and retains no-best-fit output when any substitution
occurs. The host uses existing wxWidgets conversion as a thin strict adapter
and reuses original `UniConversion.h` scalar/UTF-16-width helpers only after
complete UTF-8 validation. No new codec dependency was added.

Measured original Win32 `WC_NO_BEST_FIT_CHARS` fixtures: Euro → `80`, yen and
snowman → one `3F` each, emoji/music supplementary scalar → two `3F` each.
The whole confirmed-loss output keeps these no-best-fit mappings.

Unix iconv CP936 lacks 2,149 Windows PUA assignments. `gbk_codec.cpp` records
those exact assignments in 54 contiguous byte/codepoint ranges, bidirectionally.
Provenance: wxWidgets 3.3.3#1, Windows MSVC 19.51 vs Ubuntu WSL GCC 13;
explicit-length strict CP936 exhaustive two-byte decoding and BMP encoding.
Original probe and raw records are retained in the implementation coordination
directory `codec-probe/`; `cp936-pua-delta.json` records the measured range
compression. The table is this narrow compatibility delta, not a general
encoding catalog.

Windows also encodes U+F8F5 as `FF` despite rejecting `FF` on strict decode.
This host deliberately treats that asymmetric mapping as loss, offering UTF-8
or an explicitly confirmed valid `3F`, so saved GBK remains strictly reopenable.

Unlike original Notepad4's clean richer buffer after lossy save, this host
reflects the actual saved text and provides Undo to recover richer content.

## Validation

TDD used the preapproved application file-operation/editor-state seam and
independent literal disk bytes. The first CJK/Euro roundtrip failed before
encoding API existed; loss-choice tests failed before implementation; native
Linux PUA opening failed before adding the compact compatibility delta. Each
was followed by the minimal implementation and a passing focused check.

`gbk_roundtrip` covers CJK/Euro, mixed EOL, embedded NUL, no guessing/BOM
stripping, malformed entire streams, PUA, U+F8F5, all three loss decisions,
Save/Save As, replacement failures, source isolation, and Undo/Redo/savepoints.
`gbk_native_choices` activates actual native modal widgets owned by its own
process: explicit Open picker/menu/status, Save As cancellation, malformed
input error, all three warning buttons, and failed UTF-8/replacement writes.
The native tests run through `desktop_run.py`'s shared desktop mutex. No global
keyboard/mouse injection, Linux IME installation, or proxy change was used.

Final platform results are recorded after merging the latest integration.

# Show Hex Code and Show Character Info

Edit → Special exposes the original two commands without new shortcuts.
Show Hex Code appends the uppercase bracketed bytes and selects only that
representation. Its binary selection getter preserves embedded zero and the
original EOL separator between multiple selections. Empty selections disable the
menu; rectangles show the original rejection feedback. Read-only mode rejects
the insertion while preserving the original clamped selection movement.

These are editor bytes. Explicit GBK, Windows-1252 and UTF-16 documents use UTF-8
editor bytes in pinned Notepad4; only the native ANSI identity uses ACP bytes.
The migrated core stores UTF-8 and adapts that ANSI boundary before formatting.

Character Info reads the scalar at the selection start, including zero at EOF.
Its Unicode, UTF-16, UTF-8, HTML and optional Name/Category fields retain the
original English technical payload, independently of translated menu/dialog
labels. The native dialog displays literal ampersands, and the existing core
CopyText/native clipboard adapter publishes the same LF-separated payload.
The operation preserves document bytes, selection, read-only mode and history.

Windows loads the optional system ICU service from the system directory.
Linux builds with the available system ICU version header load the corresponding
runtime service dynamically; builds without that header, or systems without the
runtime service or its required exports, omit both optional fields. No ICU
library or character database is linked or bundled as a new prerequisite.

The independent original samples, executable hash, capture attempts and native
logs live under
`F:/wxnotepad-evidence/native-editor-controls-2026-11-17/214/`.
`original-03/samples.json` is the valid capture: startup readiness and fixture
preconditions are asserted, and the real rectangle warning is accepted on an
owned private desktop. Earlier failures and the startup-race samples are retained
separately. `tests/character_information_fixtures.h` freezes the valid payloads.

`original_character_information` drives native menus, the visible feedback and
actual clipboard, selection/history and encoded application Open/Save results.
On Linux, `original_character_information_no_metadata` uses a loadable native
service fixture lacking ICU's exports, exercising the original omission branch
through the real dynamic loader. `packaged_character_information` bundles the
actual application and checks English/Simplified/Traditional captions, native
feedback and clipboard bytes consumed through Paste and Save. Run these through
the backend-specific entry points in `docs/ci.md`.

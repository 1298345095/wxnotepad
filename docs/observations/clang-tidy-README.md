# Windows and Linux clang-tidy follow-up

Base revision: `877113b3809dd0737bf504ddf203245d6228509c`.
Inspection date: 2026-10-30, America/Los_Angeles.

## Changes

`WxSurface::PolyLine` and `Polygon` now reserve their known point count before
appending wx points. This avoids repeated vector allocation during drawing.
`DrawRGBAImage` now calculates the pixel count and RGB/RGBA offsets in `size_t`,
so multiplication happens in the x64 size type instead of overflowing `int`
before conversion to a pointer offset. Pixel order and drawing coordinates are
unchanged. No measured speedup is claimed.

## Analysis

Each platform analyzed all seven production `src/` translation units using its
native CMake compilation database and installed wxWidgets 3.3.3 dependencies.
The Linux source and build were on the native WSL filesystem. The enabled check
groups were `clang-analyzer-*`, `bugprone-*`, `performance-*` and `portability-*`,
with all other groups disabled. No automatic fixes were applied.

| Platform | clang-tidy | Unique project diagnostics before | After |
| --- | --- | ---: | ---: |
| Windows x64, MSVC compilation flags | 23.1.2 | 70 | 66 |
| Ubuntu x64, GCC 13 compilation flags | 18.1.3 | 49 | 47 |

Every analysis process exited successfully. Counts exclude dependency headers
and deduplicate shared project headers. Check availability and behavior differ
between LLVM versions, so the Windows and Linux totals are not directly comparable.
The two integer-multiplication diagnostics disappeared on both platforms; LLVM 23
also no longer reports either vector-allocation diagnostic.

## Retained diagnostics

The remaining diagnostics were inspected, not globally suppressed:

- Coordinate and count conversions occur at the integer wx drawing boundary.
  Large-coordinate/fractional-coordinate policy belongs to further rendering work.
- GBK encoding has a documented valid-UTF-8 precondition, checked by its production
  caller before conversion. LLVM 18 also warns about optional values already
  guarded by `if (converted)`.
- Ctrl+Shift is the intended combination of Scintilla modifier bits. Zero message
  values are private sentinels checked before dispatch, or notification fields
  assigned before use.
- The Linux intrinsics shim must supply the original core's `_BitScanReverse`
  and `_bittest*` names. Renaming them locally would break those calls.
- The Linux signed-char warning at `GetUnicodeKey()` does not describe the
  installed dependency: `wx/event.h` returns `wxChar`, and `wx/chartype.h`
  defines that type as `wchar_t`. No byte-sized cast was added to Unicode input.
- Allocation inside inherited `noexcept` callbacks, wx event static initialization,
  small enum representation, `#pragma once` and required interface parameter
  ordering were retained. No new ordinary-input failure was established there.

## Validation and evidence

Both native release builds succeeded. Windows `editor_input_undo` and
`display_repaint` passed **2/2**. Linux's complete existing GUI suite passed
**13/13 on X11** and **13/13 on Wayland**, inside private Xvfb/Weston displays.
The private-display runs did not use the WSLg clipboard bridge.

The original phase-1 evidence remains unchanged. This follow-up preserves raw
logs, per-file YAML diagnostics, normalized JSON results, compilation databases,
scripts, source snapshots/diff and runtime files under:

```text
E:/NewNotepad4/wxnotepad-evidence/clang-tidy-fixes-877113b
```

The earlier Windows baseline is independently retained under
`E:/NewNotepad4/wxnotepad-evidence/clang-tidy-877113b` and copied into this
follow-up's `windows/before/`. `CHECKSUMS.sha256` records the final evidence files.

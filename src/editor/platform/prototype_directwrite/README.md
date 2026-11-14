# PROTOTYPE: original DirectWrite on the wx host

Question: can the original Notepad4 Direct2D/DirectWrite backend provide matching
measurement and rendering through wxWidgets while retaining parallel layout?
This is a throwaway Windows experiment, not a production renderer switch.

Branch: `codex/prototype-directwrite`, forked at `efc1366`. The prior GDI/Pango
experiment remains on `codex/prototype-worker-measurement` with its evidence.
Linux retains the parent branch's Pango/Cairo implementation.

## Accepted scope

- Keep the original core's concurrency, thresholds, caches and editing algorithms.
  Do not serialize workers or change timeouts/assertions to make tests pass.
- Use DirectWrite for both measurements and text rendering. Validate against the
  original **DirectWrite** backend; the GDI backend has different font metrics.
- Reuse the pinned `89b8b36e0ee2c23f73be922315f17261ccfbbe1a` implementation rather
  than reimplement Unicode mapping, shaping or drawing algorithms.
- Preserve the GDI branch, correctness tests and all failed observations.
- Run Spec and Standards reviews, native clang-tidy and clang-format.
- Ask the user to close high-load applications and wait for a fresh ready
  response before any performance measurements. Functional tests do not report
  their runtimes as comparative performance results.

## Implementation

`prepare_sources.py` generates `original_directwrite.inc` in the build directory
from the prepared pinned `scintilla/win32/SurfaceD2D.cxx`. It makes only two host
seam changes: remove `final` from the native surface so wx can adapt initialization,
and omit the native Font/Surface allocation functions supplied by `wx_platform`.
All font, measurement, UTF byte mapping, painting and pixmap algorithms remain
the original source. No downloaded source or new dependency is required.

`WxDirectWriteSurface` converts wx window IDs to HWNDs and provides a persistent
`ID2D1HwndRenderTarget` for native paint events. Drawing goes directly to the
window. Targets survive paints and are released when the window is destroyed;
cached original pixmaps retain their native resource domain. Offscreen captures
bind a Direct2D DC target to wxMemoryDC. Measurements use immutable native font
formats and independent per-call DirectWrite layouts, as in the original code.

The bridge converts point sizes through the original device-height convention.
Completion controls receive the original font's LOGFONT through wx's native font
interface. Window events, input, IME and native completion controls remain wx
responsibilities. `EndDraw` target loss discards compatible core graphics and
requests a fresh paint, following ScintillaWin; physical GPU device removal has
not been induced in this experiment.

The integration is compiled only with `WXNOTEPAD_PROTOTYPE_DIRECTWRITE` by this
standalone project. The normal project and existing GDI tests retain their
current backend. There is no new rendering preference or persistence behavior.

## Build

The experiment reuses Release libraries from the existing `81515bf`/`05b7d86`
baseline. The direct object files override only the host surface and core host;
other editing/application code comes from those libraries. Use matching MSVC,
wxWidgets and compile settings. From the repository root in Git Bash:

```sh
pwsh -NoProfile -File .github/scripts/msvc.ps1 -CMakeArgumentsJson '["-S","src/editor/platform/prototype_directwrite","-B",".ci/directwrite-prototype","-G","Ninja","-DCMAKE_BUILD_TYPE=Release","-DCMAKE_TOOLCHAIN_FILE=G:/vcpkg/scripts/buildsystems/vcpkg.cmake","-DVCPKG_MANIFEST_MODE=OFF","-DPORT_BUILD=E:/codexworktrees/completion-languages/wxnotepad/.ci/native-windows","-DCORE_ROOT=E:/codexworktrees/completion-languages/wxnotepad/.ci/build/original-core","-DCMAKE_EXPORT_COMPILE_COMMANDS=ON"]'
pwsh -NoProfile -File .github/scripts/msvc.ps1 -CMakeArgumentsJson '["--build",".ci/directwrite-prototype","--parallel","6"]'
ctest --test-dir .ci/directwrite-prototype --output-on-failure
```

Correctness checks can briefly show their own windows. They do not modify the
system clipboard. `directwrite_app.exe` opens the manual application. Use
`--startup-file <path>` for a disposable sample and keep its portable settings
under this build directory.

## Verification interpretation

`directwrite_oracle` compares 324 original numeric records, including every byte
position, at em sizes corresponding to 96/144/192 DPI. These cover normal, bold
and italic fonts, 8/12/22 points, UTF-8, byte mode, embedded NUL/tab, Chinese,
surrogate pairs, combining characters, emoji, mixed scripts and ligature text.
The same shared surface is then checked with four workers × 64 iterations per
case group. Actual window DPI is independently compared with the native HWND.
The em-size matrix does not claim three physical monitor configurations.

The raster oracle compares adapted direct rendering with original direct
rendering, and adapted buffered rendering with original buffered rendering.
Both comparisons require exact RGB equality and nonempty text ink. The native
original direct and buffered paths themselves differ by 225 color channels in
the recorded fixture; the initial three-way-equality failure is retained.

The existing TOML regression keeps both 80-resize loops and its 120-second
timeout. Existing caret (including 96 DPI), insertion and completion checks
are reused without changing their assertions or timeouts. Microsoft Pinyin
preedit appearance and candidate positioning still require user verification.

Full build, numerical, raster and test evidence is retained at
`E:/NewNotepad4/wxnotepad-evidence/directwrite-prototype-2026-11-14/`.
No speedup or memory-cost conclusion is drawn before controlled measurements.

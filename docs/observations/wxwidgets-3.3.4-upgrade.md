# Pinned wxWidgets 3.3.4 overlay

The application now requires the fixed upstream `v3.3.4` release, overlay port
version 1. The root manifest, overlay manifest and exact CMake dependency agree.
The vcpkg baseline remains `d186940b2f0c4e8e5b4ff804af57d2a26fbfa091`.
The source archive is verified by the SHA-512 in the portfile. The local upstream
checkout is a reference; builds do not consume its moving `master` branch.

## Dependency changes

- Remove the old wxMSW watcher patch: release 3.3.4 already contains
  `53e793af35`, `0b744dd44c` and `06ffddba79`.
- Remove the Wayland build patch already included upstream.
- Adapt the SDL config-package patch to the release's SDL3-first detection.
  The application still builds without the optional sound feature.
- Retain the existing SDK trimming, disabled wxSTC and static application
  dependencies. Windows keeps dynamic CRT; Linux keeps its dynamic system
  runtimes and X11, Wayland and D-Bus interfaces.
- Update the actual SDK license notice and stock-catalog provenance. The
  simplified/traditional Chinese stock PO files are byte-identical to 3.3.3.

The first Linux application link failed because wxWidgets' new generated
Wayland protocol objects duplicate four global definitions in static GDK:
`xdg_popup_interface`, `xdg_positioner_interface`, `xdg_surface_interface` and
`xdg_toplevel_interface`. The Linux-only `gtk-static-wayland-symbols.patch`
prefixes those four private wx definitions and their generated references with
`wx_`. Protocol wire names are unchanged. Archive symbol inspection and a fresh
vcpkg rebuild verify the fix; GTK remains static and Wayland remains enabled.

## Wayland drag-fixture readiness

The initial focused Wayland run failed at the first `native_stream_drag`
button press. The unchanged 3.3.3 executable failed identically with the same
current driver. Failure-only scene capture after five seconds showed a normal
mapped window, but a scene captured immediately before the original press
showed Weston's startup fade surface above it, at alpha 0.517569. Native debugger
observation confirmed that Weston received the motion and button while the
application received neither pointer entry nor the press.

The stream-drag fixture now reuses `NativeKeys::AfterPresentation`, which waits
for the exact owned surface to be mapped and the startup input curtain to be
gone. It adds no replacement gesture or fixed sleep. The existing three-second
presentation check, five-second drag-phase deadline, eight-second driver wait,
90-second registered case guard and all text/selection/undo assertions remain.
The complete case passed against both SDKs after this fixture change: 3.3.3 in
13.81 seconds and 3.3.4 in 13.78 seconds. These are correctness results, not
performance measurements. Temporary driver/debugger instrumentation is excluded
from the patch.

## Local verification

Both platforms built the application and all registered test targets with the
new SDK. The final checks are:

| Check | Result |
| --- | --- |
| Windows rendering, fonts, viewport, watcher, dialogs, localization, printing, menus and dynamic languages | 18 passed |
| Windows opt-in Microsoft Pinyin cases, explicitly enabled after the initial opt-in skips | 11 passed, no skips |
| Windows complete native stream drag after the fixture change | 1 passed |
| Final Windows SDK/watcher/DirectWrite/default-and-96-DPI subset | 4 passed; already included in the 18 above |
| Linux X11 focused selection | 18 passed, no skips |
| Linux Wayland same focused selection | 18 passed, no skips |
| Windows packaged SDK/runtime contract | Passed; 23 SDK libraries, no forbidden artifacts |
| Linux packaged SDK/runtime contract | Passed; 100 SDK libraries, no forbidden artifacts |
| Canonical C++ formatting | Passed; 574 tracked files |

The Windows rows cover 30 distinct passing cases, not 34. Linux used the default
18 concurrent private sessions on each backend. The Linux package retains split
debug symbols and the relocated GIO helper. ELF inspection confirms static
wxWidgets/GTK and dynamic system runtimes/interfaces; Windows retains `/MD`.
Required hosted workflows validate the complete suites and analysis separately.

Local evidence is retained under
`F:/wxnotepad-evidence/wxwidgets-3.3.4-upgrade-2026-11-26/` and the native ext4
build at `/home/ctrls/wxnotepad-wxwidgets-3.3.4-2026-11-26/`. Initial linker and
Wayland failures remain alongside the final passing reports.

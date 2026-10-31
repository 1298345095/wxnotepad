# #20: actual-program resource results

Measured on 2026-10-30 from source **`3330c3661e9a5e670a4ac403dab80d92bd6e0688`** after the loaded-file EOL fix. The implementation was committed before any final timed row. The following results commit changes only documentation/data. All **66 independent processes passed**, including 18 complete large-file byte comparisons and saved SHA-256 checks; no final row was excluded or retried. The Windows visible batch took 30.54 seconds and released its owned windows.

Definitions and exact reproduction commands are in [resource-measurement.md](../resource-measurement.md). Raw rows, timestamps, stage counters, states, sample hashes and medians are retained in [Windows](issue20-windows.json), [Wayland](issue20-wayland.json), [X11](issue20-x11.json). [Build/machine evidence](issue20-build.json) records the 22 verified source-input hashes, compilers, dependency port records, display and calibration conditions.

## Build and conditions

Windows: Windows 11 Enterprise build 26200, i9-13900H (14 cores/20 threads), 64 GiB installed (63.6 GiB reported physical), MSVC 19.51.36260 / toolset 14.51.36231, CMake 4.2.0, x64 Release, wxWidgets 3.3.3#1. Native wxDC/GDI drawing; 2560×1600 primary display at 144 DPI (150%), 900×600 logical frame. During the single authorized batch, only each launched app was made topmost, without activation or global keyboard input. Native `WM_CHAR` went only to that PID’s editor HWND.

Linux: Ubuntu 24.04.5 WSL2, same CPU/20 logical threads, about 31.2 GiB assigned memory, GCC 13.3.0, CMake 3.30.5/Ninja 1.12.1, x64 Release. wxWidgets 3.3.3#1 and GTK3 3.24.52#2 are the **statically linked vcpkg** versions; system pkg-config also reports GTK 3.24.41 and is not the linked GTK runtime version. Source/build/executable/samples/results stayed on native Linux `/home/ctrls/.cache/wxnotepad-issue20` (ext4), rather than a Windows mount. Both actual backend types were reported by GDK in every process.

Linux displays were private Xvfb 1600×1000×24: direct X11 without a window manager, and Wayland through Weston 13.0.0’s X11 backend with **Pixman**. The application uses wxDC/GTK drawing. These differ from #7’s WSLg display/compositor conditions. Both full windows and the visible inserted `X` were assessed in untimed private screen captures; Windows native rendering was assessed in an owned PrintWindow capture. The final Windows timed rows used unoccluded owned-topmost windows. No compositor-present timestamp was measured.

Both builds use original-core revision `89b8b36e0ee2c23f73be922315f17261ccfbbe1a`; archive/patch/preparation hashes are in each raw report. vcpkg revision is `d186940b2f0c4e8e5b4ff804af57d2a26fbfa091`. Executable SHA-256:

* Windows: `f2a9e0f18727f5400eed9e6cbb1c0d5cb5c950873c42adf9b6930ae54d653dba`
* Linux (both backends): `064d44251c987d456acda92cac90c56a1de8e907634ca78f248293d9fce8d4e5`

OS caches were retained. Samples were generated and hashed before each batch and fresh scratch copies made before spawn, so even first runs may have warm caches. Untimed process checks/calibrations preceded the batches. No other agent builds, GUI tests or benchmarks overlapped. Windows data used E: local NTFS; Linux used native WSL ext4. All final batches ran sequentially, in X11 → Wayland → Windows order.

## Medians

Startup is launch → native character accepted, including observer/input overhead. Five separate processes per empty/small startup case; idle memory uses three **different pristine clean processes** per case after one quiet second. Windows memory is **working set / private bytes**; Linux memory is **RSS**, all converted to MiB (2²⁰ bytes).

| Backend | Empty startup ms | Small startup ms | Empty idle MiB | Small idle MiB |
| --- | ---: | ---: | ---: | ---: |
| Windows | 195.3 | 209.3 | 32.8 / 7.9 | 33.1 / 8.1 |
| Wayland | 99.6 | 90.2 | 37.9 | 37.7 |
| X11 | 92.0 | 101.8 | 87.8 | 90.7 |

Three independent processes per file size/backend. Open/Save dispatch the actual shown frame’s file operations; the edit inserts one native ASCII `X` at byte zero. Timings are **observed state completion upper bounds**, including 10 ms hook/observer polling and native-input overhead (including xdotool subprocesses on Linux). Synchronous Update return is observed, rather than compositor presentation. Memory follows a one-second quiet interval at each stage.

| Backend | MiB | Open ms | Edit ms | Save ms | Opened MiB | Edited MiB | Saved MiB |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Windows | 10 | 65.4 | 21.4 | 22.8 | 43.5 / 18.6 | 43.5 / 18.6 | 43.6 / 18.6 |
| Windows | 100 | 184.9 | 22.1 | 113.3 | 136.4 / 112.6 | 136.4 / 112.6 | 136.5 / 112.6 |
| Wayland | 10 | 31.2 | 15.9 | 30.8 | 48.2 | 48.2 | 58.2 |
| Wayland | 100 | 195.0 | 17.0 | 133.6 | 141.0 | 141.0 | 141.1 |
| X11 | 10 | 30.7 | 20.2 | 30.8 | 99.6 | 99.6 | 109.6 |
| X11 | 100 | 216.6 | 37.1 | 123.7 | 192.5 | 192.5 | 192.6 |

Every large output is exactly `b"X" + original`, one byte longer, with the original LF/no-BOM bytes retained. Original SHA-256 values:

| Sample | Bytes | SHA-256 |
| --- | ---: | --- |
| small | 4096 | `e607ea77fcb61bbb670bc98f5786d908518a96ebd92ed8d92af0cee5c9ba9164` |
| 10MiB | 10485760 | `0c394497bc8ccd4f2803e8753d89e071df778c76500b4e31899464d1e1b8303a` |
| 100MiB | 104857600 | `ddcefc1a31122572c470f66a28520921ab257c801ca659f54f5705d6e413beb7` |

## Comparison with frozen #7 evidence

Reference: `63d4b5b:prototype/observations/issue7-resources.json` on `codex/prototype-original-core`. Same original-core revision, same sample recipe/bytes, same machine family and wxWidgets port version. #7’s small-file startup ended at its first-idle `SetFocus(); Update()` READY marker, with memory after 100 ms. This run’s small-file Update-observed medians are Windows 171.1, Wayland 72.3, X11 71.6 ms, followed by the native acceptance endpoints above; #7 READY was 235.8 / 78.6 / 69.7 ms. Observer, native input, display and built feature surfaces differ, so these are context rather than a controlled startup speedup.

| Backend, 100 MiB | #7 hidden API Open ms | #20 shown observed Open ms | #7 opened memory MiB | #20 opened memory MiB |
| --- | ---: | ---: | ---: | ---: |
| Windows | 163.4 | 184.9 | 136.7 / 112.2 | 136.4 / 112.6 |
| Wayland | 138.6 | 195.0 | 149.6 | 141.0 |
| X11 | 141.5 | 216.6 | 143.7 | 192.5 |

#7 hid its large-file controls and directly timed read/SetText with initial undo disabled; its edit inserted seven bytes near the middle, then Undo, and it also ran find/replace. Production Open validates encoding and sets document/EOL state; here viewport drawing remains enabled and the edit stays visible. Save extracts text and writes/replaces the actual file; #7 has no Save timing. The tens-of-ms observed edit values include polling/input delivery and cannot resolve #7’s direct API edit costs.

Windows 100 MiB opened private memory remains close to the prototype (112.6 versus 112.2 MiB). Linux Wayland RSS is lower here, while X11 is higher; the private Xvfb/Weston environment and drawn full-app snapshots differ from #7’s WSLg/hidden-control setup, so the differences cannot be assigned solely to the production host. No renderer-isolated causal comparison was performed. Stage snapshots do not measure transient peak allocations.

No performance threshold was introduced. Native choosers, physical keyboard/IME latency, compositor latency, search, 1 GiB/long-line stress and Linux IME are not measured by this record. Existing feature-ticket validation remains separate.

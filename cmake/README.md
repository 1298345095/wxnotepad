# CMake layout

The top-level `CMakeLists.txt` declares the project and build options, handles the
fuzz-only entry point, and includes the build modules in dependency order.

| Module | Responsibility |
| --- | --- |
| `Dependencies.cmake` | Find the exact wxWidgets SDK and validate the prepared Notepad4 source revision. |
| `Targets.cmake` | Define `notepad4_core`, `editor_runtime`, `editor_features` and `app_shell`, including their sources, dependencies, diagnostics and generated icon. |
| `Platform.cmake` | Configure Windows/GTK sources and libraries, compiler compatibility flags and the shared `add_gui_program` helper. |
| `Localization.cmake` | Generate language data, build catalogs and define installation rules. |
| `Tests.cmake` | Define `add_editor_test` and include the ordered test groups below. |
| `Coverage.cmake` | Instrument the project targets after application and test targets have been defined. |
| `SizeOptimizations.cmake` | Apply Release data/code elimination and supported Linux RELR links; expose optional IPO. |

These files use `include()` in the top-level directory scope. Relative source
paths, generated files, executable locations and CTest working directories
therefore retain their existing base directories. Coverage also continues to
enumerate the same directory's targets. The existing `tests/fuzz` subdirectory
keeps its own targets and output directory in ordinary and fuzz-only builds.

## Test registration

`Tests.cmake` includes these contiguous groups in their original registration
order:

| Group | Tests |
| --- | --- |
| `tests/Process.cmake` | SDK probes, process runners, settings lifecycle, diagnostics and localization. |
| `tests/Application.cmake` | Application commands, document tools, window/menu behavior and file operations. |
| `tests/Editor.cmake` | Clipboard, encoding, input, text transformations, measurement and rendering. |
| `tests/SyntaxAndSearch.cmake` | Syntax, folding, completion, file workflows and search. |
| `tests/NativeRegressions.cmake` | Appended platform, renderer, allocation-failure and native-readiness regressions. |

The CI runner assigns tests to private-display shards using the CTest listing
order. Keep the include and registration order stable when moving existing
tests. Append new registrations at the end of `tests/NativeRegressions.cmake`
to preserve existing shard membership. Keep a target's setup and its test
properties together; later regressions may reuse targets from earlier groups.

# Phase 3: syntax highlighting and code folding

Validated code: `2fbf4cbad608e2074bbb4051ada2caffb0fbe026` on
`codex/implementation-syntax`. This record covers overview #30 and tickets #31–40.
The subsequent delivery commit changes documentation only.

## Delivered behavior

C/C++, Python and JSON use the pinned Notepad4 lexers, lexer-consumed keyword
groups/attributes and original default light styles. Only Null and these three
language modules are compiled. The original core remains the sole document,
history, styling and folding owner; no new runtime or GoogleTest dependency was added.

Automatic filename selection follows Open and the All Files save type. First Save
and Save As apply the selected type, including the same path or extension: a specific
type forces its language; All Files clears the override. Untitled default/current/
selected Plain Text also classifies the filename. The native dialog initially
selects the current scheme first. Ordinary Save retains the choice; successful
New/Open reset it. Failed/cancelled file operations preserve the choice. Syntax
colors inherit the existing native font, point size and DPI behavior.

Native margin clicks preserve nested fold state and keep caret/anchor endpoints
visible. The existing renderer now tiles the core's margin pattern and restores
enclosing clips. View commands collapse every nested header or reveal every line.
Find reveals all lines in a match, including hidden intermediate blocks, while
preserving unrelated folds. Replacement retains original-core matching/captures
and grouped history; literal single byte-identical replacement now preserves the
save point and existing redo, as regex single replacement and Replace All already did.

## Final verification

| Environment/check | Final result |
| --- | --- |
| Windows MSVC Release, related regression | 30/30 |
| Private Linux X11, full regression | 32/32 |
| Private Linux Wayland, full regression | 32/32 |
| clang-format 23.1.2, dry-run with Werror | Passed |
| Standards review | 0 outstanding breaches or judgments |
| Spec review | 0 outstanding findings |

Windows excludes the unchanged `system_clipboard`, `gbk_native_choices`,
`open_new_native` and `save_as_native` suites from this final shared-desktop batch;
both full Linux suites include them. Individual tickets also retain relevant
Windows native file-dialog results. Every final CTest invocation uses
`--no-tests=error`; these counts represent executed tests.

Tests operate real controls/native commands, public core state and independently
read disk bytes. Numerical paint assertions inspect only owned editor controls;
GTK uses its actual widget draw callback into an owned memory target. No image
was viewed by the model. Linux IME remains deferred.

Review found one literal-single no-op history gap and three duplication judgments.
The saved-document/retained-redo regression failed before the fix and passed
afterward, including replaying the old redo action. Shared style installation,
generator parsing/provenance and native fold test helpers retain behavior. All
three regenerated data headers are byte-identical to their pre-refactor versions.

Native Linux builds use their own filesystem and tool PATH. Updated source
archives are extracted with `tar -m` so an older commit timestamp cannot leave
newer objects stale. MSVC configuration sets both console encodings to UTF-8;
Ninja's recorded header dependencies were verified.

## Static analysis

Both native compilation databases were scanned across eight production and ten
new test translation units with clang-analyzer, bugprone, performance and
portability checks. All tool processes exited successfully.

| Tool | Production diagnostics | Including test diagnostics |
| --- | ---: | ---: |
| Windows clang-tidy 23.1.2 | 76 | 83 |
| Linux clang-tidy 18.1.3 | 47 | 48 |

These are retained diagnostics, not warning-free results. They include existing
enum/conversion/static-initialization findings, native callback conversions and
the repository's pragma-once convention. The new Windows `SetClip` allocation
advisory follows the original `Surface::SetClip(PRectangle) noexcept` contract:
wxDCClipper storage can allocate while exceptions cannot escape that interface.
Its native folding/painting path and diagnostic are retained in the #36 evidence.
No blanket suppression or speculative exception-recovery path was introduced.

## Resource observations

48 independent native Linux processes passed, including eight complete 10/100 MiB
saved-byte comparisons and SHA-256 checks. The before executable is the preserved
phase-2 code `04537629dae25d2a1d739e42751823959fbe91cf`, verified by its hash; after
is the validated phase-3 code above. Both use the same private Xvfb/Weston setup.
All builds/tests/scans had finished before these sequential observations.

The existing resource observer was reused with a 128-byte C++ sample line and
`.cpp` sample/scratch filenames. Process control, native character input, timing,
memory collection and independent save verification are unchanged. Before treats
these files as Plain Text; after activates C/C++ highlighting and folding.

Three processes per empty/small startup case; two separate pristine processes per
idle case; one process per 10/100 MiB operation case. Startup ends at native
character acceptance and includes input/observer overhead. RSS follows one quiet
second. OS caches are retained, and samples/copies are prepared outside timing.
These are observations without a new performance threshold; operation rows are
single samples, not precision performance claims or transient peak-memory measurements.

| Backend/version | Empty startup ms | Small C++ startup ms | Empty RSS MiB | Small C++ RSS MiB |
| --- | ---: | ---: | ---: | ---: |
| X11 before | 92.4 | 101.2 | 88.8 | 91.3 |
| X11 after | 91.5 | 112.2 | 87.9 | 91.2 |
| Wayland before | 78.4 | 77.5 | 38.4 | 38.3 |
| Wayland after | 77.9 | 109.3 | 38.2 | 38.8 |

| Backend/size | Open ms before → after | Edit ms before → after | Save ms before → after | Opened RSS MiB before → after |
| --- | ---: | ---: | ---: | ---: |
| X11, 10 MiB | 30.7 → 41.0 | 21.3 → 26.6 | 20.8 → 20.5 | 100.7 → 110.9 |
| X11, 100 MiB | 226.1 → 246.0 | 20.1 → 31.8 | 112.6 → 156.5 | 192.8 → 299.4 |
| Wayland, 10 MiB | 30.9 → 30.8 | 15.6 → 16.6 | 30.8 → 20.5 | 48.5 → 59.6 |
| Wayland, 100 MiB | 225.8 → 235.4 | 16.0 → 29.4 | 112.4 → 153.6 | 141.7 → 248.1 |

The 100 MiB styled sample adds about 106 MiB of RSS over the Plain Text baseline.
The pinned core keeps a `SplitVector<char>` style buffer and expands it to the
document byte length when styling is enabled (`CellBuffer.h:98`,
`CellBuffer::EnsureStyleBuffer`, `CellBuffer.cxx:574–584`). This explains a major
part of the observed linear memory cost; the batch does not isolate every other
allocation. A future large-file highlighting policy should account for this cost.
Plain Text is selectable explicitly; no automatic file-size cutoff was added.

| Executable | Before bytes | After bytes | Increase |
| --- | ---: | ---: | ---: |
| Windows | 857,088 | 936,960 | 79,872 bytes (78 KiB) |
| Linux | 36,328,416 | 36,479,144 | 150,728 bytes |

These sizes cover the executables, with existing dependency choices unchanged.
The Windows runtime uses copied DLLs; Linux's vcpkg GUI dependencies are statically
linked. Release packaging remains later work. No Windows resource timing batch
was run in this phase.

## Evidence and reproduction

Full local evidence is retained at
`E:/NewNotepad4/wxnotepad-evidence/syntax-phase3`: per-ticket red/green logs,
source snapshots, both review reports, final native compilation databases and
analysis records, CTest outputs, resource rows/scripts/hashes and delivery archives.

- `windows-final/`, `linux-final/`: final whole-phase build/test/analysis records.
- `review-fixes/`: failing literal no-op case, three-platform 18/18 verification,
  generator byte-identity proof and helper-refactor evidence.
- `resources/{x11,wayland}-{before,after}.json`: raw process rows and definitions.
- `final-validation.json`, `resource-summary.json`: aggregate measured results.
- `windows-final.ps1`, `linux-final.sh`, `linux-resources.sh`: exact local invocations.

The standard build entry point and all behavioral test programs remain in the
repository. Earlier [C/C++](syntax-cpp-README.md), [Python](syntax-python-README.md)
and [JSON](syntax-json-README.md) records describe their individual delivery slices.

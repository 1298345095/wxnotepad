# Headless fuzz testing

The optional Linux Clang 18 build uses libFuzzer, AddressSanitizer and
UndefinedBehaviorSanitizer. It instruments the production encoding and
file-variable implementations and the pinned `UniConversion.cxx`; the existing
wxWidgets binary dependency is reused without sanitizer instrumentation. The
application and its runtime dependencies are unchanged.

Three targets cover:

| Target | Inputs and checks |
| --- | --- |
| `fuzz_codecs` | Arbitrary bytes through UTF-8 validation, UTF-16LE/BE, GBK, GB18030, Big5, Windows-1252 and DOS-437 decoding; valid UTF-8 through encoders; Unicode equality when an encoder reports no loss |
| `fuzz_detection` | Six control bytes select original detection options, default encoding, strong/weak choices, file extension and weak BOM/native ANSI/OEM identity; remaining bytes are file content; checks explicit-choice precedence, deterministic detection and settings normalization |
| `fuzz_file_variables` | Arbitrary bytes through original file-variable and encoding-tag parsing with each disable-option combination; checks original bounds and suppression rules |

Legacy encoders require valid UTF-8. The harness respects that caller contract;
malformed bytes go through decoders and the validator. Lossy legacy conversions,
UTF-16BE replacement/truncation, and duplicate legacy mappings do not imply a
byte-for-byte roundtrip. Existing independently captured Notepad4 fixtures remain
the behavioral oracle; fuzz assertions do not replace them or change tolerance
rules. This suite targets input handling, while native GUI tests still cover
focus, clipboard ownership, IME and window lifetime.

## Local build

Use the existing wxWidgets 3.3.3 dependency and prepared original source. On
Ubuntu 24.04 the test tools are `clang-18`, `llvm-18` and `libclang-rt-18-dev`.
Prepare the source with the existing `tools/build.py --prepare-only` entry point,
then configure a separate build directory:

```sh
cmake -S . -B .ci/build/fuzz -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_CXX_COMPILER=clang++-18 \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DWXNOTEPAD_FUZZ_ONLY=ON \
  -DNOTEPAD4_SOURCE="$PWD/.ci/build/original-core" \
  -DCMAKE_TOOLCHAIN_FILE="$HOME/vcpkg/scripts/buildsystems/vcpkg.cmake" \
  -DVCPKG_MANIFEST_MODE=OFF
cmake --build .ci/build/fuzz --parallel 4
ctest --test-dir .ci/build/fuzz --output-on-failure --parallel 3
python3 tests/fuzz/run.py fuzz --bin-dir .ci/build/fuzz/tests/fuzz \
  --corpus .ci/fuzz/corpus --output .ci/reports/fuzz --seconds 60
```

`WXNOTEPAD_FUZZ_ONLY` builds only these headless tests and requires Linux/Clang.
Ordinary Windows/MSVC and Linux/GCC test builds compile the same harnesses with
a small replay driver and register three fixed-corpus CTest tests. No display,
clipboard, Google Test or fuzz runtime is required for ordinary replay. This
also keeps harnesses in the normal clang-tidy compilation database.

## Corpus, deadlines and failures

`tests/fuzz/corpus.json` stores named seed bytes as UTF-8 text, hexadecimal or
references to existing fixtures. Optional `prefix` and space `padding` fields
construct detection headers and the original 512-byte scanning boundaries.
The runner materializes binary inputs outside the tracked source tree and
preserves accumulated libFuzzer inputs when refreshing checked-in seeds.

Each PR/main CI session mutates each target for 60 seconds. Manual workflow
runs offer 60, 300 or 900 seconds per target. Each input has a 10-second limit,
64 KiB mutation length limit and 2048 MiB RSS limit. The runner adds a 30-second
process grace period and propagates sanitizer, invariant and timeout failures.
These are fuzz budgets, not application file-size limits.

The Fuzz workflow reuses dependency caches, skips the GUI/compositor build,
restores the accumulated corpus and saves new corpus snapshots on successful
main runs. Its evidence artifact retains logs, exact commands, statuses,
crash/timeout inputs, corpus, compilation database and matching debug binaries.
Raw original-source archives and preparation records are retained too.

To reproduce a saved input, use the matching executable:

```sh
ASAN_SYMBOLIZER_PATH="$(command -v llvm-symbolizer-18)" \
  .ci/build/fuzz/tests/fuzz/fuzz_codecs path/to/crash-input
```

Use libFuzzer's `-minimize_crash=1 -exact_artifact_path=...` mode to reduce a
confirmed failure, keeping the original input and logs. After fixing a defect,
add its reduced input to `corpus.json` so ordinary Windows/Linux CI replays it.

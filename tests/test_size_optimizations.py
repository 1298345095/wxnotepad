"""Build and run small native targets through the production size policy."""

import importlib.util
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import textwrap
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("ci_size", ROOT / ".github/scripts/ci.py")
CI = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(CI)
APPLICATION_TARGETS = ("notepad4_core", "editor_runtime", "editor_features", "app_shell", "wxnotepad")
NATIVE_TOOLS = ("cmake", "ninja", "pwsh") if sys.platform == "win32" else ("cmake", "ninja", "c++")


def marker(kind, target):
    return f"unreferenced-{kind}-{target}-size-policy-fixture".encode("ascii")


def unused_definitions(target):
    return (
        'extern "C" {\n'
        f'volatile char unused_data_{target}[8192] = "{marker("data", target).decode()}";\n'
        f'const char* unused_function_{target}() {{ return "{marker("function", target).decode()}"; }}\n'
        "}\n"
    )


@unittest.skipUnless(
    sys.platform in ("linux", "win32") and all(shutil.which(tool) for tool in NATIVE_TOOLS),
    "Requires native CMake, Ninja and the platform C++ toolchain",
)
class NativeSizeOptimizations(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="wxnotepad size policy ")
        self.addCleanup(temporary.cleanup)
        self.work = Path(temporary.name).resolve()
        self.source = self.work / "source files"
        self.source.mkdir()
        (self.source / "CMakeLists.txt").write_text(
            textwrap.dedent("""\
            cmake_minimum_required(VERSION 3.25)
            project(SizePolicyFixture LANGUAGES CXX)
            set(CMAKE_CXX_STANDARD 17)
            if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
                include(CheckPIESupported)
                check_pie_supported()
                set(CMAKE_POSITION_INDEPENDENT_CODE ON)
            endif()
            add_library(notepad4_core OBJECT core.cpp)
            add_library(editor_runtime STATIC runtime.cpp)
            target_link_libraries(editor_runtime PUBLIC notepad4_core)
            add_library(editor_features STATIC features.cpp)
            target_link_libraries(editor_features PUBLIC editor_runtime)
            add_library(app_shell STATIC shell.cpp)
            target_link_libraries(app_shell PUBLIC editor_features)
            add_executable(wxnotepad application.cpp)
            target_link_libraries(wxnotepad PRIVATE app_shell)
            add_executable(native_regression regression.cpp)
            target_link_libraries(native_regression PRIVATE app_shell)
            include("${SIZE_MODULE}")
            if(WXNOTEPAD_ENABLE_COVERAGE AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
                include("${COVERAGE_MODULE}")
            endif()
            file(WRITE "${CMAKE_BINARY_DIR}/fixture.txt" "compiler=${CMAKE_CXX_COMPILER_ID}\\n")
            string(TOUPPER "${CMAKE_BUILD_TYPE}" configuration)
            foreach(target notepad4_core editor_runtime editor_features app_shell wxnotepad)
                get_property(ipo TARGET ${target} PROPERTY INTERPROCEDURAL_OPTIMIZATION_${configuration})
                file(APPEND "${CMAKE_BINARY_DIR}/fixture.txt" "ipo.${target}=${ipo}\\n")
            endforeach()
            """),
            encoding="utf-8",
        )
        sources = {
            "core.cpp": (
                "notepad4_core",
                "volatile int retained_value = 17;\n"
                'const char* volatile retained_text = "retained relative relocation";\n'
                "int core_value() { return retained_value + retained_text[0] - 'r'; }\n",
            ),
            "runtime.cpp": ("editor_runtime", "int core_value();\nint runtime_value() { return core_value() + 7; }\n"),
            "features.cpp": (
                "editor_features",
                "int runtime_value();\nint features_value() { return runtime_value() + 11; }\n",
            ),
            "shell.cpp": ("app_shell", "int features_value();\nint shell_value() { return features_value() + 7; }\n"),
            "application.cpp": (
                "wxnotepad",
                "int shell_value();\nint main() { return shell_value() == 42 ? 0 : 1; }\n",
            ),
        }
        for filename, (target, source) in sources.items():
            (self.source / filename).write_text(unused_definitions(target) + source, encoding="utf-8")
        (self.source / "regression.cpp").write_text(sources["application.cpp"][1], encoding="utf-8")

    def cmake(self, arguments):
        return subprocess.run(
            CI.cmake_command(arguments), capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=60
        )

    def configure(self, name, configuration="Release", options=()):
        directory = self.work / name
        result = self.cmake(
            [
                "-S",
                self.source,
                "-B",
                directory,
                "-G",
                "Ninja",
                f"-DCMAKE_BUILD_TYPE={configuration}",
                f"-DSIZE_MODULE={ROOT / 'cmake/SizeOptimizations.cmake'}",
                f"-DCOVERAGE_MODULE={ROOT / 'cmake/Coverage.cmake'}",
                *options,
            ]
        )
        return directory, result

    def build(self, name, configuration="Release", options=(), *, compiler=None, optional_ipo=False):
        directory, result = self.configure(name, configuration, options)
        output = result.stdout + result.stderr
        if result.returncode and optional_ipo and "IPO is not supported by this toolchain" in output:
            self.skipTest(output)
        self.assertEqual(result.returncode, 0, output)
        settings = dict(
            line.split("=", 1) for line in (directory / "fixture.txt").read_text(encoding="utf-8").splitlines()
        )
        if settings["compiler"] not in ("GNU", "Clang", "MSVC"):
            self.skipTest("The size policy does not target compiler " + settings["compiler"])
        if compiler and settings["compiler"] != compiler:
            self.skipTest("This check requires " + compiler)
        result = self.cmake(["--build", directory, "--config", configuration, "--parallel", "2"])
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        binaries = {}
        for target in ("wxnotepad", "native_regression"):
            executable = directory / (target + (".exe" if sys.platform == "win32" else ""))
            result = subprocess.run([executable], capture_output=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            binaries[target] = executable
        return directory, settings, binaries

    def check_markers(self, binaries, kind, present):
        for executable_target, executable in binaries.items():
            content = executable.read_bytes()
            targets = APPLICATION_TARGETS if executable_target == "wxnotepad" else APPLICATION_TARGETS[:-1]
            for target in targets:
                with self.subTest(executable=executable_target, source=target, kind=kind):
                    self.assertEqual(
                        marker(kind, target) in content,
                        present,
                        f"{kind} from {target} in {executable}: expected present={present}",
                    )

    def check_relr(self, binaries, present):
        if sys.platform != "linux" or not shutil.which("readelf"):
            return
        for executable in binaries.values():
            dynamic = subprocess.check_output(["readelf", "--dynamic", executable], text=True)
            self.assertEqual("(RELR)" in dynamic, present, dynamic)

    def test_release_discards_unreferenced_sections_and_opt_out_preserves_baseline(self):
        _, _, baseline_binaries = self.build("release baseline", options=["-DWXNOTEPAD_ENABLE_SIZE_OPTIMIZATIONS=OFF"])
        self.check_markers(baseline_binaries, "data", True)
        if sys.platform == "linux":
            self.check_markers(baseline_binaries, "function", True)
        self.check_relr(baseline_binaries, False)
        directory, settings, binaries = self.build("release optimized")
        cache = CI.read_cmake_cache(directory)
        if sys.platform == "linux" and cache.get("WXNOTEPAD_LINKER_GC_SECTIONS") != "1":
            self.skipTest("This native linker does not support section garbage collection")
        self.check_markers(binaries, "data", False)
        self.check_markers(binaries, "function", False)
        for target in binaries:
            self.assertLess(binaries[target].stat().st_size, baseline_binaries[target].stat().st_size)
        self.assertTrue(all(settings["ipo." + target] != "TRUE" for target in APPLICATION_TARGETS))
        if sys.platform == "linux" and cache.get("WXNOTEPAD_LINKER_RELR") == "1":
            self.check_relr(binaries, True)

    def test_debug_retains_unreferenced_code_data_and_debug_information(self):
        _, settings, binaries = self.build("debug build", "Debug")
        self.check_markers(binaries, "data", True)
        self.check_markers(binaries, "function", True)
        self.check_relr(binaries, False)
        self.assertTrue(all(settings["ipo." + target] != "TRUE" for target in APPLICATION_TARGETS))
        for executable in binaries.values():
            if sys.platform == "win32":
                self.assertTrue(executable.with_suffix(".pdb").is_file())
            elif shutil.which("readelf"):
                sections = subprocess.check_output(["readelf", "--sections", executable], text=True)
                self.assertIn(".debug_info", sections)

    @unittest.skipUnless(sys.platform == "linux", "Coverage is supported by the Linux GCC profile")
    def test_coverage_runs_instrumented_code_without_release_policy_and_rejects_ipo(self):
        directory, settings, binaries = self.build(
            "coverage build", options=["-DWXNOTEPAD_ENABLE_COVERAGE=ON"], compiler="GNU"
        )
        self.check_markers(binaries, "data", True)
        self.check_markers(binaries, "function", True)
        self.check_relr(binaries, False)
        self.assertTrue(list(directory.rglob("*.gcda")), "Executed instrumented targets must write coverage data")
        self.assertTrue(all(settings["ipo." + target] != "TRUE" for target in APPLICATION_TARGETS))
        _, result = self.configure(
            "invalid coverage ipo", options=["-DWXNOTEPAD_ENABLE_COVERAGE=ON", "-DWXNOTEPAD_ENABLE_IPO=ON"]
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("IPO and coverage", result.stdout + result.stderr)

    def test_opt_in_ipo_compiles_and_runs_application_and_archive_consuming_regression(self):
        _, settings, binaries = self.build("ipo build", options=["-DWXNOTEPAD_ENABLE_IPO=ON"], optional_ipo=True)
        for target in APPLICATION_TARGETS:
            self.assertEqual(settings["ipo." + target], "TRUE")
        self.check_markers(binaries, "data", False)
        self.check_markers(binaries, "function", False)


if __name__ == "__main__":
    unittest.main()

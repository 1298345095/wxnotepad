"""Check SDK inventory and distribution failures using configured installation layouts."""

import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("sdk_contract", ROOT / ".github/scripts/check_sdk.py")
SDK = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(SDK)


class SdkContract(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="wxnotepad SDK contract ")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name).resolve()
        self.build = self.root / "build"
        self.build.mkdir()

    def configure(self, triplet="x64-windows-ci"):
        prefix = self.root / "installed" / triplet
        configuration = prefix / "share/wxwidgets"
        configuration.mkdir(parents=True)
        header = prefix / ("include/wx/version.h" if "windows" in triplet else "include/wx-3.3/wx/version.h")
        header.parent.mkdir(parents=True)
        header.write_text("fixture SDK header\n", encoding="utf-8")
        libraries = prefix / "lib"
        libraries.mkdir()
        names = (
            ("wxmsw333u_core_vc_x64_custom.lib", "wxbase333u_vc_x64_custom.lib")
            if "windows" in triplet
            else ("libwx_gtk3u_core-3.3.a", "libwx_baseu-3.3.a")
        )
        for name in names:
            (libraries / name).touch()
        (self.build / "CMakeCache.txt").write_text(
            f"VCPKG_INSTALLED_DIR:PATH={prefix.parent.as_posix()}\n"
            f"VCPKG_TARGET_TRIPLET:STRING={triplet}\n"
            f"wxWidgets_DIR:PATH={configuration.as_posix()}\n",
            encoding="utf-8",
        )
        return prefix

    def test_clean_sdk_and_runtime_are_checked(self):
        prefix = self.configure()
        artifact = self.root / "artifact"
        artifact.mkdir()
        (artifact / "wxnotepad.exe").touch()
        (artifact / "vcruntime140.dll").touch()
        (artifact / "msvcp140.dll").touch()
        (artifact / "ucrtbase.dll").touch()
        report = SDK.inspect(self.build, artifact)
        self.assertTrue(report["success"], report)
        self.assertEqual(report["sdk_root"], str(prefix.resolve()))
        self.assertEqual(report["sdk_library_count"], 2)
        self.assertEqual(report["runtime_file_count"], 4)

    def test_wrong_or_empty_sdk_is_not_an_empty_success(self):
        prefix = self.configure()
        (prefix / "include/wx/version.h").unlink()
        self.assertFalse(SDK.inspect(self.build)["success"])
        (prefix / "include/wx/version.h").touch()
        for library in (prefix / "lib").iterdir():
            library.unlink()
        self.assertFalse(SDK.inspect(self.build)["success"])

    def test_windows_distribution_rejects_shared_application_dependencies(self):
        self.configure()
        artifact = self.root / "artifact"
        artifact.mkdir()
        (artifact / "wxnotepad.exe").touch()
        for name in (
            "wxbase333u_vc_x64_custom.dll",
            "wxmsw333u_core_vc_x64_custom.dll",
            "libpng16.dll",
            "z.dll",
            "application_dependency.dll",
        ):
            with self.subTest(name=name):
                shared = artifact / name
                shared.touch()
                report = SDK.inspect(self.build, artifact)
                self.assertFalse(report["success"])
                self.assertEqual(report["forbidden_runtime_artifacts"], [str(shared)])
                shared.unlink()

    def test_missing_cache_or_selected_wx_configuration_fails(self):
        self.assertFalse(SDK.inspect(self.build)["success"])
        self.configure()
        with (self.build / "CMakeCache.txt").open("a", encoding="utf-8") as output:
            output.write(f"wxWidgets_DIR:PATH={(self.root / 'other SDK').as_posix()}\n")
        self.assertFalse(SDK.inspect(self.build)["success"])

    def test_stc_and_standalone_core_artifacts_in_release_or_debug_fail(self):
        prefix = self.configure()
        names = (
            "debug/bin/wxmsw333ud_stc_vc_x64_custom.pdb",
            "lib/Scintilla.lib",
            "debug/lib/Lexilla.lib",
            "lib/libscintilla.a",
            "lib/liblexilla.a",
            "bin/Scintilla.dll",
            "lib/liblexilla.so.5",
        )
        for name in names:
            with self.subTest(name=name):
                forbidden = prefix / name
                forbidden.parent.mkdir(parents=True, exist_ok=True)
                forbidden.touch()
                report = SDK.inspect(self.build)
                self.assertFalse(report["success"])
                self.assertEqual(report["forbidden_sdk_artifacts"], [str(forbidden)])
                forbidden.unlink()

    def test_linux_helper_archives_fail_but_gtk_codec_dependencies_are_allowed(self):
        prefix = self.configure("x64-linux")
        artifact = self.root / "artifact"
        libraries = artifact / "lib"
        libraries.mkdir(parents=True)
        (artifact / "wxnotepad").touch()
        (prefix / "lib/libwebp.a").touch()
        (libraries / "libdbus-1.so.3").touch()
        self.assertTrue(SDK.inspect(self.build, artifact)["success"])
        shared_codec = libraries / "libwebp.so.7"
        shared_codec.touch()
        report = SDK.inspect(self.build, artifact)
        self.assertFalse(report["success"])
        self.assertEqual(report["forbidden_runtime_artifacts"], [str(shared_codec)])
        shared_codec.unlink()
        forbidden = prefix / "lib/libwxlexilla-3.3.a"
        forbidden.touch()
        report = SDK.inspect(self.build, artifact)
        self.assertFalse(report["success"])
        self.assertEqual(report["forbidden_sdk_artifacts"], [str(forbidden)])

    def test_windows_unused_runtime_codec_or_regex_library_fails(self):
        self.configure()
        artifact = self.root / "artifact"
        artifact.mkdir()
        (artifact / "wxnotepad.exe").touch()
        for name in ("libwebp.dll", "jpeg62.dll", "tiff.dll", "pcre2-16.dll"):
            with self.subTest(name=name):
                forbidden = artifact / name
                forbidden.touch()
                report = SDK.inspect(self.build, artifact)
                self.assertFalse(report["success"])
                self.assertEqual(report["forbidden_runtime_artifacts"], [str(forbidden)])
                forbidden.unlink()

    def test_missing_distribution_is_not_an_empty_success(self):
        self.configure()
        self.assertFalse(SDK.inspect(self.build, self.root / "missing artifact")["success"])

    def test_cli_retains_success_and_failure_reports(self):
        prefix = self.configure()
        for attempt in ("clean", "stale"):
            if attempt == "stale":
                (prefix / "lib/wxmsw333u_stc.lib").touch()
            output = self.root / "reports" / f"{attempt}.json"
            command = [
                sys.executable,
                "-X",
                "utf8",
                ROOT / ".github/scripts/check_sdk.py",
                "--cmake-dir",
                self.build,
                "--output",
                output,
            ]
            process = subprocess.run(command, capture_output=True, text=True, encoding="utf-8")
            self.assertEqual(process.returncode, int(attempt == "stale"), repr(process))
            report = json.loads(output.read_text(encoding="utf-8"))
            self.assertEqual(report["success"], attempt == "clean")


if __name__ == "__main__":
    unittest.main()

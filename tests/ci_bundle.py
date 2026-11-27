"""Exercise Linux packaging with real ELF binaries and matching GDB symbols."""

import hashlib
import importlib.util
import json
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("ci_bundle", ROOT / ".github/scripts/ci.py")
CI = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(CI)


@unittest.skipUnless(sys.platform == "linux", "ELF packaging requires Linux")
class LinuxBundle(unittest.TestCase):
    def test_distribution_runs_and_gdb_finds_separate_symbols(self):
        for executable in ("gcc", "objcopy", "strip", "readelf", "gdb"):
            self.assertIsNotNone(shutil.which(executable), executable)
        with tempfile.TemporaryDirectory(prefix="wxnotepad bundle ") as folder:
            root = Path(folder).resolve()
            work = root / ".ci"
            cmake = root / "configured build"
            cmake.mkdir(parents=True)
            runtime = root / "runtime output/Release"
            runtime.mkdir(parents=True)
            installed = root / "sdk packages"
            libraries = installed / "x64-linux-ci/lib"
            libraries.mkdir(parents=True)
            original_core = root / "prepared original"
            original_core.mkdir()
            (original_core / "wxnotepad-source.json").write_text('{"revision":"fixture"}\n', encoding="utf-8")
            (cmake / "CMakeCache.txt").write_text(
                f"CMAKE_HOME_DIRECTORY:INTERNAL={root}\n"
                f"NOTEPAD4_SOURCE:PATH={original_core}\n"
                f"VCPKG_INSTALLED_DIR:PATH={installed}\n"
                "VCPKG_TARGET_TRIPLET:STRING=x64-linux-ci\n",
                encoding="utf-8",
            )
            licenses = root / "licenses"
            licenses.mkdir()
            (licenses / "fixture.txt").write_text("Fixture license\n", encoding="utf-8")
            resources = root / "resources"
            resources.mkdir()
            profile_files = ("Notepad4 DarkTheme.ini-default", "Notepad4 DarkTheme.NOTICE.txt")
            for name in profile_files:
                shutil.copy2(ROOT / "resources" / name, resources / name)
            notice = installed / "x64-linux-ci/share/probe/copyright"
            notice.parent.mkdir(parents=True)
            notice.write_text("Library license\n", encoding="utf-8")
            (root / "po").mkdir()
            shutil.copy2(ROOT / "po/languages.json", root / "po/languages.json")
            gettext_bin = os.environ.get("WXNOTEPAD_GETTEXT_BIN")
            msgfmt = str(Path(gettext_bin) / "msgfmt") if gettext_bin else shutil.which("msgfmt")
            self.assertIsNotNone(msgfmt, "Real host msgfmt is required for the bundle check")
            for code in ("zh_CN", "zh_TW"):
                for domain in ("wxnotepad", "wxstd"):
                    catalog = runtime / "locale" / code / "LC_MESSAGES" / (domain + ".mo")
                    catalog.parent.mkdir(parents=True, exist_ok=True)
                    subprocess.run([msgfmt, "--check", "-o", catalog, ROOT / "po/wxstd" / (code + ".po")], check=True)
            subprocess.run(["git", "init", "-q", root], check=True)
            subprocess.run(
                [
                    "git",
                    "-C",
                    root,
                    "-c",
                    "user.name=Bundle Test",
                    "-c",
                    "user.email=bundle@example.invalid",
                    "commit",
                    "-q",
                    "--allow-empty",
                    "-m",
                    "Create packaging fixture",
                ],
                check=True,
            )
            library_source = root / "library-probe.c"
            library_source.write_text("int library_answer(void) { return 42; }\n", encoding="utf-8")
            library = libraries / "libbundle_probe.so"
            subprocess.run(
                ["gcc", "-g", "-fPIC", "-shared", library_source, "-Wl,-soname,libbundle_probe.so", "-o", library],
                check=True,
            )
            source = root / "bundle-probe.c"
            source.write_text(
                "extern int library_answer(void);\nint main(void) { return library_answer() == 42 ? 0 : 1; }\n",
                encoding="utf-8",
            )
            executable = runtime / "wxnotepad"
            subprocess.run(
                ["gcc", "-g", source, f"-L{libraries}", "-lbundle_probe", f"-Wl,-rpath,{libraries}", "-o", executable],
                check=True,
            )
            helper = installed / "x64-linux-ci/libexec/gio-launch-desktop"
            helper.parent.mkdir()
            shutil.copy2(executable, helper)
            original_bytes = executable.read_bytes()
            original_library_bytes = library.read_bytes()

            distributed = CI.bundle(cmake, executable, work)
            artifact = work / "artifact"
            self.assertEqual(distributed, artifact / "wxnotepad")
            for name in profile_files:
                self.assertEqual((artifact / name).read_bytes(), (ROOT / "resources" / name).read_bytes())
            self.assertTrue((work / "reports/runtime-libraries.txt").is_file())
            self.assertEqual(executable.read_bytes(), original_bytes)
            self.assertEqual(library.read_bytes(), original_library_bytes)
            self.assertLess(distributed.stat().st_size, len(original_bytes))
            self.assertLess((artifact / "lib/libbundle_probe.so").stat().st_size, len(original_library_bytes))
            for relative in (Path("wxnotepad"), Path("lib/libbundle_probe.so"), Path("libexec/gio-launch-desktop")):
                sections = subprocess.check_output(["readelf", "-SW", artifact / relative], text=True)
                self.assertNotIn(".debug_info", sections)
                self.assertIn(".gnu_debuglink", sections)
                debug_directory = relative.parent / ".debug"
                shutil.copytree(work / "symbols" / debug_directory, artifact / debug_directory, dirs_exist_ok=True)
            subprocess.run([distributed], env=dict(os.environ, LD_LIBRARY_PATH=str(artifact / "lib")), check=True)
            subprocess.run(
                [artifact / "libexec/gio-launch-desktop"],
                env=dict(os.environ, LD_LIBRARY_PATH=str(artifact / "lib")),
                check=True,
            )
            debugger = subprocess.run(
                ["gdb", "-q", "-nx", "-batch", "-ex", f'file "{distributed}"', "-ex", "info line main"],
                capture_output=True,
                text=True,
                check=True,
            )
            self.assertIn("bundle-probe.c", debugger.stdout)
            metadata = json.loads((artifact / "build.json").read_text(encoding="utf-8"))
            self.assertEqual(metadata["executable_sha256"], hashlib.sha256(distributed.read_bytes()).hexdigest())
            self.assertEqual(metadata["unstripped_executable_sha256"], hashlib.sha256(original_bytes).hexdigest())
            self.assertEqual((work / "symbols/build.json").read_bytes(), (artifact / "build.json").read_bytes())
            self.assertEqual((artifact / "licenses/vcpkg/probe/copyright").read_text(), "Library license\n")
            import gettext

            for code in ("zh_CN", "zh_TW"):
                for domain in ("wxnotepad", "wxstd"):
                    relative = Path("locale") / code / "LC_MESSAGES" / (domain + ".mo")
                    self.assertEqual((artifact / relative).read_bytes(), (runtime / relative).read_bytes())
                    with (artifact / relative).open("rb") as stream:
                        self.assertTrue(gettext.GNUTranslations(stream).info()["language"].startswith("zh"))


if __name__ == "__main__":
    unittest.main()

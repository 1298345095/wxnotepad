"""Exercise localization maintenance through its public CLI and real gettext tools."""

import json
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TOOL = ROOT / "tools/localization.py"


class LocalizationCLIBase:
    def run_tool(self, *arguments, success=True):
        result = subprocess.run(
            [sys.executable, "-X", "utf8", TOOL, *map(str, arguments)], capture_output=True, text=True, encoding="utf-8"
        )
        if success:
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        else:
            self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
        return result

    def run_cmake(self, *arguments):
        result = subprocess.run(["cmake", *map(str, arguments)], capture_output=True, text=True, encoding="utf-8")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        return result


class LocalizationCLI(LocalizationCLIBase, unittest.TestCase):
    def test_release_language_codes_use_ascii_identifiers(self):
        with tempfile.TemporaryDirectory(prefix="wxnotepad language codes ") as folder:
            root = Path(folder)
            (root / "po").mkdir()
            data = {"domain": "wxnotepad", "source_language": "en", "languages": []}
            for code, valid in (
                ("en", True),
                ("zh_CN", True),
                ("en_001", True),
                ("ené", False),
                ("en中文", False),
                ("en１２", False),
            ):
                with self.subTest(code=code):
                    data["source_language"] = code
                    data["languages"] = [{"code": code, "lang_id": 1033, "name": "Fixture"}]
                    (root / "po/languages.json").write_text(json.dumps(data), encoding="utf-8")
                    result = self.run_tool("--root", root, "header", "--output", root / "languages.h", success=valid)
                    if not valid:
                        self.assertIn("Invalid or duplicate language code", result.stderr)

    def test_release_metadata_generates_runtime_definitions(self):
        with tempfile.TemporaryDirectory(prefix="wxnotepad language header ") as folder:
            header = Path(folder) / "ui_language_data.h"
            self.run_tool("header", "--output", header)
            text = header.read_text(encoding="utf-8")
            for expected in (
                "namespace AppLocalization",
                "uint16_t langId",
                "std::string_view nativeName",
                'CatalogDomain = "wxnotepad"',
                '{1033, "en", "English", false}',
                '{2052, "zh_CN", "中文(简体)", true}',
                '{1028, "zh_TW", "中文(繁體)", true}',
            ):
                self.assertIn(expected, text)
            previous = header.stat().st_mtime_ns
            self.run_tool("header", "--output", header)
            self.assertEqual(header.stat().st_mtime_ns, previous)


GETTEXT = os.environ.get("WXNOTEPAD_GETTEXT_BIN")


@unittest.skipUnless(GETTEXT or shutil.which("xgettext"), "Requires real GNU gettext host tools")
class GettextCLI(LocalizationCLIBase, unittest.TestCase):
    def test_extract_update_check_and_compile_standard_catalogs(self):
        with tempfile.TemporaryDirectory(prefix="wxnotepad gettext CLI ") as folder:
            root = Path(folder)
            (root / "po").mkdir()
            shutil.copy2(ROOT / "po/languages.json", root / "po/languages.json")
            (root / "src").mkdir()
            source = root / "src/example.cpp"
            source.write_text(
                "void messages() {\n"
                "// TRANSLATORS: complete count sentence.\n"
                'wxString::Format(wxPLURAL("%d item", "%d items", n), n);\n'
                'wxGETTEXT_IN_CONTEXT("menu", "Open");\n'
                'wxTRANSLATE("Exit");\n}\n',
                encoding="utf-8",
            )
            common = ["--root", root]
            if GETTEXT:
                common += ["--gettext-bin", GETTEXT]
            self.run_tool(*common, "extract")
            pot = root / "po/wxnotepad.pot"
            extracted = pot.read_text(encoding="utf-8")
            self.assertIn('msgctxt "menu"', extracted)
            self.assertIn('msgid_plural "%d items"', extracted)
            self.assertIn("#, c-format", extracted)
            self.assertIn("TRANSLATORS: complete count sentence.", extracted)
            self.assertNotIn("POT-Creation-Date:", extracted)
            self.run_tool(*common, "extract")
            self.assertEqual(pot.read_text(encoding="utf-8"), extracted)
            for code in ("zh_CN", "zh_TW"):
                # Test-only translations use literal English to verify the GNU pipeline.
                app = (
                    'msgid ""\nmsgstr ""\n'
                    '"Project-Id-Version: CLI fixture 1\\n"\n'
                    '"PO-Revision-Date: 2026-11-15 00:00+0000\\n"\n'
                    '"Last-Translator: Test Fixture\\n"\n'
                    '"Language-Team: Test Fixture\\n"\n'
                    f'"Language: {code}\\n"\n'
                    '"MIME-Version: 1.0\\n"\n'
                    '"Content-Type: text/plain; charset=UTF-8\\n"\n'
                    '"Content-Transfer-Encoding: 8bit\\n"\n'
                    '"Plural-Forms: nplurals=1; plural=0;\\n"\n\n'
                    '#, c-format\nmsgid "%d item"\nmsgid_plural "%d items"\nmsgstr[0] "%d item"\n\n'
                    'msgctxt "menu"\nmsgid "Open"\nmsgstr "Open"\n\n'
                    'msgid "Exit"\nmsgstr "Exit"\n'
                )
                (root / f"po/{code}.po").write_text(app, encoding="utf-8")
                (root / "po/wxstd").mkdir(exist_ok=True)
                (root / f"po/wxstd/{code}.po").write_text(app, encoding="utf-8")
            self.run_tool(*common, "check")
            before = {p: p.read_bytes() for p in (root / "po").rglob("*") if p.is_file()}
            output = root / "output"
            self.run_tool(*common, "compile", "--output", output)
            import gettext

            with (output / "zh_CN/LC_MESSAGES/wxnotepad.mo").open("rb") as stream:
                translations = gettext.GNUTranslations(stream)
            self.assertEqual(translations.pgettext("menu", "Open"), "Open")
            self.assertEqual(translations.ngettext("%d item", "%d items", 2), "%d item")
            self.assertTrue((output / "zh_TW/LC_MESSAGES/wxstd.mo").is_file())
            self.assertFalse((output / "en").exists())
            for path, content in before.items():
                self.assertEqual(path.read_bytes(), content, "Compilation changed tracked text")
            if sys.platform == "linux":
                # Exercise the same included CMake catalog pipeline with a tiny
                # executable: no replacement translation implementation is used.
                shutil.copytree(ROOT / "tools", root / "tools")
                shutil.copytree(ROOT / "cmake", root / "cmake")
                shutil.copytree(ROOT / "licenses", root / "licenses")
                (root / "stub.cpp").write_text("int main() { return 0; }" + chr(10), encoding="utf-8")
                (root / "CMakeLists.txt").write_text(
                    chr(10).join(
                        (
                            "cmake_minimum_required(VERSION 3.25)",
                            "project(LocalizationBuild VERSION 1.0 LANGUAGES CXX)",
                            "add_library(app_shell STATIC stub.cpp)",
                            "add_executable(wxnotepad stub.cpp)",
                            "include(cmake/Localization.cmake)",
                            "",
                        )
                    ),
                    encoding="utf-8",
                )
                build, install = root / "build", root / "install"
                self.run_cmake(
                    "-S",
                    root,
                    "-B",
                    build,
                    "-G",
                    "Ninja",
                    f"-DCMAKE_INSTALL_PREFIX={install}",
                    f"-DWXNOTEPAD_GETTEXT_BIN={GETTEXT or Path(shutil.which('msgfmt')).parent}",
                )
                self.run_cmake("--build", build)
                executable_time = (build / "wxnotepad").stat().st_mtime_ns
                catalog = root / "po/zh_CN.po"
                catalog.write_text(
                    catalog.read_text(encoding="utf-8").replace('msgstr "Exit"', 'msgstr "Leave"'), encoding="utf-8"
                )
                self.run_cmake("--build", build, "--target", "wxnotepad")
                self.assertEqual((build / "wxnotepad").stat().st_mtime_ns, executable_time)
                with (build / "locale/zh_CN/LC_MESSAGES/wxnotepad.mo").open("rb") as stream:
                    self.assertEqual(gettext.GNUTranslations(stream).gettext("Exit"), "Leave")
                self.run_cmake("--install", build)
                for code in ("zh_CN", "zh_TW"):
                    for domain in ("wxnotepad", "wxstd"):
                        relative = Path(code) / "LC_MESSAGES" / (domain + ".mo")
                        self.assertEqual(
                            (install / "share/locale" / relative).read_bytes(),
                            (build / "locale" / relative).read_bytes(),
                        )
                for notice in (ROOT / "licenses").iterdir():
                    if notice.is_file():
                        self.assertEqual(
                            (install / "share/licenses/wxnotepad" / notice.name).read_bytes(), notice.read_bytes()
                        )
                custom_locale = root / "custom locale"
                self.run_cmake("-S", root, "-B", build, f"-DWXNOTEPAD_LOCALE_DIR={custom_locale}")
                self.run_cmake("--build", build)
                self.run_cmake("--install", build)
                for code in ("zh_CN", "zh_TW"):
                    for domain in ("wxnotepad", "wxstd"):
                        relative = Path(code) / "LC_MESSAGES" / (domain + ".mo")
                        self.assertEqual(
                            (custom_locale / relative).read_bytes(), (build / "locale" / relative).read_bytes()
                        )
            source.write_text(source.read_text(encoding="utf-8") + '\n_("New command");\n', encoding="utf-8")
            self.run_tool(*common, "check", success=False)
            self.run_tool(*common, "update")
            self.assertIn('msgid "New command"', (root / "po/zh_CN.po").read_text(encoding="utf-8"))
            self.run_tool(*common, "check", success=False)
            broken = root / "po/zh_CN.po"
            broken.write_text(
                broken.read_text(encoding="utf-8").replace('msgstr[0] "%d item"', 'msgstr[0] "%s item"'),
                encoding="utf-8",
            )
            self.run_tool(*common, "compile", "--output", output, success=False)


if __name__ == "__main__":
    unittest.main()

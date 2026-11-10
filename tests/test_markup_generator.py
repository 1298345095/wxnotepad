"""Worked source examples for reproducible grouped and offset markup style data."""
import importlib.util
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
SPEC = importlib.util.spec_from_file_location("markup_generator", ROOT / "tools/update_markup_schemes.py")
GENERATOR = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(GENERATOR)


class MarkupGenerator(unittest.TestCase):
    def test_grouped_styles_preserve_traits_and_offsets(self):
        text = '''static const EDITSTYLE Styles_JS[] = {
        { MULTI_STYLE(SCE_JS_WORD, SCE_JS_WORD2, 0, 0), 0, "Keyword", L"bold; italic; fore:#123456; back:#ABCDEF; eolfilled" },
        { SCE_JS_DEFAULT, 0, "Default", L"" },
        };'''
        self.assertEqual(GENERATOR.styles(text, "JS", "js_style"), [
            "    {js_style(SCE_JS_WORD), 0x123456, 0xABCDEF, true, true, true},",
            "    {js_style(SCE_JS_WORD2), 0x123456, 0xABCDEF, true, true, true},",
            "    {js_style(SCE_JS_DEFAULT), 0x000000, 0xFFFFFF, false, false, false},"])

    def test_generation_keeps_consumed_lists_and_original_extension_sets(self):
        def source(_source, name):
            group = {"stlHTML.cpp": "HTML", "stlXML.cpp": "XML", "stlJavaScript.cpp": "JS", "stlCSS.cpp": "CSS"}[Path(name).name]
            return (f'static const char * const Keywords_{group}[] = {{\n"tag", nullptr, nullptr, nullptr, nullptr, nullptr, "DOCTYPE", // 7 attributes\n"completion", // 8 values\n"unused"\n}};\n'
                    f'static const EDITSTYLE Styles_{group}[] = {{\n{{ SCE_{group}_DEFAULT, 0, "Default", L"fore:#123456" }},\n}};\n'
                    'L"html; htm"\n').encode()
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "src/syntax/generated").mkdir(parents=True)
            with patch.object(GENERATOR, "ROOT", root), patch.object(GENERATOR, "read_pinned", side_effect=source), \
                    patch.object(sys, "argv", ["update_markup_schemes.py", "--source", temporary]):
                GENERATOR.main()
            text = (root / "src/syntax/generated/markup_scheme_data.h").read_text(encoding="utf-8")
            self.assertIn('"html", "htm"', text)
            self.assertIn('{htmlKeywordText[5] ? htmlKeywordText[5] : "", 3U << 8U}', text)
            self.assertIn('js_style(SCE_JS_DEFAULT)', text)
            self.assertIn('css_style(SCE_CSS_DEFAULT)', text)
            self.assertIn('licenses/Notepad4.txt', text)
            self.assertNotIn('"unused"', text)

    def test_cli_help(self):
        subprocess.run([sys.executable, str(ROOT / "tools/update_markup_schemes.py"), "--help"],
                       check=True, capture_output=True)


if __name__ == "__main__":
    unittest.main()

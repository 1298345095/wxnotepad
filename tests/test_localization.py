"""Exercise localization maintenance through its public CLI and real gettext tools."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
TOOL = ROOT / 'tools/localization.py'


class LocalizationCLI(unittest.TestCase):
    def run_tool(self, *arguments, success=True):
        result = subprocess.run([sys.executable, '-X', 'utf8', TOOL, *map(str, arguments)],
                                capture_output=True, text=True, encoding='utf-8')
        if success:
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        else:
            self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
        return result

    def test_release_metadata_generates_runtime_definitions(self):
        with tempfile.TemporaryDirectory(prefix='wxnotepad language header ') as folder:
            header = Path(folder) / 'ui_language_data.h'
            self.run_tool('header', '--output', header)
            text = header.read_text(encoding='utf-8')
            for expected in ('namespace AppLocalization', 'uint16_t langId', 'std::string_view nativeName',
                             'CatalogDomain = "wxnotepad"', '{1033, "en", "English", false}',
                             '{2052, "zh_CN", "中文(简体)", true}', '{1028, "zh_TW", "中文(繁體)", true}'):
                self.assertIn(expected, text)
            previous = header.stat().st_mtime_ns
            self.run_tool('header', '--output', header)
            self.assertEqual(header.stat().st_mtime_ns, previous)


if __name__ == '__main__':
    unittest.main()

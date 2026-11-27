"""Preserve offline capture exports and explicitly selected developer paths."""

import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from capture_number_radix import write_reference_header

ROOT = Path(__file__).resolve().parents[1]


class OfflineCaptureExports(unittest.TestCase):
    def setUp(self):
        temporary = self.enterContext(tempfile.TemporaryDirectory(prefix="wxnotepad capture 测试 "))
        self.directory = Path(temporary)
        self.working = self.directory / "working directory"
        self.working.mkdir()

    def convert_csv(self, capture, output):
        return subprocess.run(
            [sys.executable, "-X", "utf8", ROOT / "tools/csv_reference.py", capture, output],
            cwd=self.working,
            capture_output=True,
            text=True,
            encoding="utf-8",
        )

    def test_csv_preserves_states_options_and_external_unicode_paths(self):
        capture = self.directory / "原始 capture.json"
        output = self.directory / "independent reference.tsv"
        rows = [
            {
                "name": "custom delimiter",
                "position": 3,
                "options": [44, 34, True, False, "中"],
                "states": [
                    {
                        "stage": "before",
                        "text": "612c620a632c64",
                        "styles": "01020103010401",
                        "lines": [1, 2],
                        "folds": [1024, 1025],
                    },
                    {
                        "stage": "undo",
                        "text": "612c620a632c64",
                        "styles": "01020103010401",
                        "lines": [1, 2],
                        "folds": [1024, 1025],
                    },
                ],
            },
            {
                "name": "automatic",
                "position": 0,
                "states": [{"stage": "before", "text": "", "styles": "", "lines": [0], "folds": [1024]}],
            },
        ]
        capture.write_text(json.dumps(rows, ensure_ascii=False), encoding="utf-8")
        result = self.convert_csv(Path("..") / capture.name, Path("..") / output.name)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(
            output.read_bytes(),
            b"# Pinned Notepad4 89b8b36e0ee2c23f73be922315f17261ccfbbe1a; capture_csv.py; BSD-3-Clause.\n"
            b"custom delimiter\t3\tbefore\t612c620a632c64\t01020103010401\t1,2\t1024,1025\t44\t34\t1\t0\te4b8ad\n"
            b"custom delimiter\t3\tundo\t612c620a632c64\t01020103010401\t1,2\t1024,1025\t44\t34\t1\t0\te4b8ad\n"
            b"automatic\t0\tbefore\t\t\t0\t1024\n",
        )
        self.assertEqual(json.loads(capture.read_text(encoding="utf-8")), rows)
        self.assertEqual(list(self.working.iterdir()), [])

    def test_csv_invalid_capture_preserves_existing_output(self):
        capture = self.directory / "invalid.json"
        output = self.directory / "existing.tsv"
        capture.write_text("{invalid", encoding="utf-8")
        output.write_bytes(b"previous independent reference\n")
        result = self.convert_csv(capture, output)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("JSONDecodeError", result.stderr)
        self.assertEqual(output.read_bytes(), b"previous independent reference\n")

    def test_radix_header_preserves_utf8_lengths_nul_and_forward_results(self):
        output = self.directory / "数值 reference.h"
        results = [
            {"command": "To Hexadecimal", "before": '中"\\\0\n', "reverse": False, "after": {"text": "😀0"}},
            {"command": "reverse-only", "before": "12", "reverse": True, "after": {"text": "ignored"}},
            {"case": "selection-only", "after": {"text": "ignored"}},
            {"command": "To Decimal", "before": "", "reverse": False, "after": {"text": "0"}},
        ]
        write_reference_header(output, results)
        header = output.read_text(encoding="utf-8")
        self.assertIn("inline constexpr std::array<Fixture, 2> fixtures = {{\n", header)
        self.assertIn('    {"To Hexadecimal", {"中\\"\\\\\\0\\n", 7}, {"😀0", 5}},\n', header)
        self.assertIn('    {"To Decimal", {"", 0}, {"0", 1}},\n', header)
        self.assertNotIn("reverse-only", header)
        self.assertNotIn("selection-only", header)
        self.assertNotIn("ignored", header)
        self.assertNotIn("\0", header)
        self.assertTrue(header.endswith("}};\n}\n"))

    def test_radix_header_handles_no_forward_conversions(self):
        output = self.directory / "empty.h"
        write_reference_header(output, [{"case": "empty", "after": {"text": ""}}])
        header = output.read_text(encoding="utf-8")
        self.assertIn("inline constexpr std::array<Fixture, 0> fixtures = {{\n}};\n", header)


if __name__ == "__main__":
    unittest.main()

"""Exercise the default analyzer through its real native diagnostics and reports."""

import importlib.util
import json
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("native_checks", ROOT / ".github/scripts/checks.py")
CHECKS = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(CHECKS)


@unittest.skipUnless(sys.platform == "linux", "The canonical native analyzer runs on Linux")
class NativeChecks(unittest.TestCase):
    def test_default_analyzer_reports_success_and_propagates_real_errors(self):
        self.assertIsNotNone(shutil.which("clang-tidy-21"), "Requires the Ubuntu 26.04 native analyzer")
        for source_text, succeeds in (
            ("int probe() { return 42; }\n", True),
            ("int probe() { int* pointer = nullptr; return *pointer; }\n", False),
        ):
            with (
                self.subTest(succeeds=succeeds),
                tempfile.TemporaryDirectory(prefix="wxnotepad native check ") as folder,
            ):
                directory = Path(folder).resolve()
                source = directory / "src/probe.cpp"
                source.parent.mkdir()
                source.write_text(source_text, encoding="utf-8")
                build = directory / "build"
                build.mkdir()
                (build / "compile_commands.json").write_text(
                    json.dumps(
                        [
                            {
                                "directory": str(directory),
                                "file": str(source),
                                "arguments": ["c++", "-std=c++23", "-c", str(source)],
                            }
                        ]
                    ),
                    encoding="utf-8",
                )
                reports = directory / ".ci/reports"
                with (
                    mock.patch.multiple(CHECKS, ROOT=directory, REPORTS=reports),
                    mock.patch.object(sys, "argv", ["checks.py", "tidy", "--build-dir", str(build), "--jobs", "1"]),
                    mock.patch.object(CHECKS.subprocess, "run", wraps=subprocess.run) as analyzer,
                ):
                    if succeeds:
                        CHECKS.main()
                    else:
                        with self.assertRaisesRegex(SystemExit, "clang-tidy compiler/analyzer errors"):
                            CHECKS.main()
                self.assertEqual(analyzer.call_count, 1)
                self.assertEqual(analyzer.call_args.args[0][0], "clang-tidy-21")
                summary = json.loads((reports / "tidy-summary.json").read_text(encoding="utf-8"))
                self.assertEqual([record["file"] for record in summary], ["src/probe.cpp"])
                self.assertEqual(summary[0]["exit_code"] == 0, succeeds)
                if not succeeds:
                    self.assertIn(
                        "clang-analyzer-core.NullDereference",
                        (directory / summary[0]["log"]).read_text(encoding="utf-8"),
                    )


if __name__ == "__main__":
    unittest.main()

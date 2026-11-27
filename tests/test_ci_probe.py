"""Exercise the local probe runner with real child processes and durable evidence."""

import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("ci_probe", ROOT / ".github/scripts/ci.py")
CI = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(CI)


class NativeProbe(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="wxnotepad probe 测试 ")
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name)
        self.reports = self.directory / "evidence"

    def summary(self):
        return json.loads((self.reports / "summary.json").read_text(encoding="utf-8"))

    def test_waits_for_every_process_and_preserves_literal_arguments(self):
        marker = self.directory / "finished"
        arguments = ["space value", 'literal $value & "quotes"', "中文"]
        script = (
            "import json,sys,time; from pathlib import Path; time.sleep(.05); "
            'f=Path(sys.argv[1]).open("a"); f.write("done\\n"); f.close(); '
            "print(json.dumps(sys.argv[2:]))"
        )
        command = [sys.executable, "-X", "utf8", "-c", script, str(marker), *arguments]
        self.assertEqual(CI.probe(command, self.reports, 10, 2, self.directory), 0)
        self.assertEqual(marker.read_text(encoding="utf-8"), "done\ndone\n")
        summary = self.summary()
        self.assertTrue(summary["success"])
        self.assertEqual(summary["requested_runs"], 2)
        self.assertEqual(summary["completed_runs"], 2)
        self.assertEqual([item["exit_code"] for item in summary["results"]], [0, 0])
        for item in summary["results"]:
            self.assertEqual(item["command"], command)
            self.assertEqual(json.loads(Path(item["log"]).read_text(encoding="utf-8")), arguments)

    def test_nonzero_exit_stops_without_retry_and_retains_output(self):
        command = [sys.executable, "-c", "print('failure evidence'); raise SystemExit(7)"]
        self.assertEqual(CI.probe(command, self.reports, 10, 3, self.directory), 1)
        summary = self.summary()
        self.assertFalse(summary["success"])
        self.assertEqual(summary["requested_runs"], 3)
        self.assertEqual(summary["completed_runs"], 1)
        self.assertEqual(len(summary["results"]), 1)
        self.assertEqual(summary["results"][0]["exit_code"], 7)
        self.assertIn("failure evidence", Path(summary["results"][0]["log"]).read_text(encoding="utf-8"))

    def test_timeout_is_not_a_completed_success(self):
        command = [sys.executable, "-c", "import time; time.sleep(30)"]
        self.assertEqual(CI.probe(command, self.reports, 0.1, 2, self.directory), 1)
        summary = self.summary()
        self.assertFalse(summary["success"])
        self.assertEqual(summary["completed_runs"], 0)
        self.assertEqual(summary["results"][0]["status"], "timeout")
        self.assertIsNone(summary["results"][0]["exit_code"])
        self.assertIn("deadline", Path(summary["results"][0]["log"]).read_text(encoding="utf-8"))

    def test_launch_failure_retains_evidence_without_an_exit_code(self):
        self.assertEqual(CI.probe([str(self.directory / "missing-program")], self.reports, 10, 1, self.directory), 1)
        summary = self.summary()
        self.assertFalse(summary["success"])
        self.assertEqual(summary["completed_runs"], 0)
        self.assertEqual(summary["results"][0]["status"], "launch_failed")
        self.assertIsNone(summary["results"][0]["exit_code"])
        self.assertIn("missing-program", summary["results"][0]["command"][0])
        self.assertIn("Could not start probe", Path(summary["results"][0]["log"]).read_text(encoding="utf-8"))

    def test_existing_evidence_is_not_overwritten(self):
        self.reports.mkdir()
        previous = self.reports / "summary.json"
        previous.write_bytes(b"previous evidence")
        with self.assertRaisesRegex(SystemExit, "already exists"):
            CI.probe([sys.executable, "-c", "pass"], self.reports, 10, 1, self.directory)
        self.assertEqual(previous.read_bytes(), b"previous evidence")

    def test_cli_returns_failure_for_an_incomplete_run(self):
        command = [
            sys.executable,
            "-X",
            "utf8",
            ROOT / ".github/scripts/ci.py",
            "probe",
            "--report-dir",
            self.reports,
            "--timeout",
            "10",
            "--runs",
            "2",
            "--",
            sys.executable,
            "-c",
            "raise SystemExit(9)",
        ]
        result = subprocess.run(command, capture_output=True, text=True, encoding="utf-8")
        self.assertEqual(result.returncode, 1, repr(result))
        self.assertFalse(self.summary()["success"])
        self.assertEqual(self.summary()["results"][0]["exit_code"], 9)

    def test_cli_rejects_empty_commands_and_nonpositive_limits(self):
        base = [sys.executable, "-X", "utf8", ROOT / ".github/scripts/ci.py", "probe", "--report-dir", self.reports]
        for arguments in (
            ["--timeout", "1"],
            ["--timeout", "0", "--", "unused"],
            ["--timeout", "1", "--runs", "0", "--", "unused"],
        ):
            with self.subTest(arguments=arguments):
                result = subprocess.run(base + arguments, capture_output=True, text=True, encoding="utf-8")
                self.assertEqual(result.returncode, 2, repr(result))
        self.assertFalse(self.reports.exists())

    @unittest.skipUnless(os.environ.get("WXNOTEPAD_GUI_PROBE"), "Native GUI executable supplied by CTest")
    def test_real_gui_processes_have_completed_numeric_exit_codes(self):
        binary = Path(os.environ["WXNOTEPAD_GUI_PROBE"])
        evidence = Path(os.environ["WXNOTEPAD_NATIVE_PROBE_EVIDENCE"])
        evidence.mkdir(parents=True, exist_ok=True)
        self.reports = Path(tempfile.mkdtemp(prefix="attempt-", dir=evidence)) / "reports"
        self.assertEqual(CI.probe([str(binary)], self.reports, 15, 2, self.directory), 0)
        summary = self.summary()
        self.assertTrue(summary["success"])
        self.assertEqual(summary["completed_runs"], 2)
        for item in summary["results"]:
            self.assertEqual(item["exit_code"], 0)
            self.assertIn("PASS: retained SDK capabilities", Path(item["log"]).read_text(encoding="utf-8"))


if __name__ == "__main__":
    unittest.main()

"""Check generated source preservation and bounded diagnostic dispatch, without native runs."""
import importlib.util
import json
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / ".github/scripts"))
SPEC = importlib.util.spec_from_file_location("periodic_backup_diagnostic", ROOT / ".github/scripts/periodic_backup_diagnostic.py")
DIAGNOSTIC = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(DIAGNOSTIC)


class PeriodicBackupDiagnostic(unittest.TestCase):
    def test_patch_applies_to_real_sources_without_changing_fixture_or_originals(self):
        parent = ROOT / ".ci"
        parent.mkdir(exist_ok=True)
        originals = {name: DIAGNOSTIC.digest(ROOT / name) for name in DIAGNOSTIC.SOURCES}
        with tempfile.TemporaryDirectory(prefix="periodic-diagnostic-source-", dir=parent) as folder:
            output = Path(folder) / "output"
            DIAGNOSTIC.prepare(output)
            proof = json.loads((output / "source-proof.json").read_text(encoding="utf-8"))
            self.assertTrue(proof["fixture_tokens_unchanged"])
            self.assertTrue(proof["native_file_timer_call_counts_unchanged"])
            self.assertTrue((output / "periodic_backup.diff").exists())
        self.assertEqual(originals, {name: DIAGNOSTIC.digest(ROOT / name) for name in DIAGNOSTIC.SOURCES})

    def run_fake_cases(self, outcomes):
        calls = []
        with tempfile.TemporaryDirectory() as folder:
            output = Path(folder)

            def probe(command, report, timeout, runs, working):
                index = len(calls)
                failed = outcomes[index]
                calls.append((timeout, runs, working))
                report.mkdir()
                trace = Path(os.environ["WXNOTEPAD_PERIODIC_TRACE_DIR"]) / "periodic-trace.tsv"
                trace.parent.mkdir()
                trace.write_text("outcome=" + ("fail" if failed else "pass") + " count=1 dropped=0 capacity=2048\n",
                                 encoding="utf-8")
                (report / "run-001.log").write_text(DIAGNOSTIC.CONTINUOUS_FAILURE if failed else "PASS\n", encoding="utf-8")
                (report / "summary.json").write_text(json.dumps({"results": [
                    {"status": "completed", "exit_code": int(failed)}]}), encoding="utf-8")
                return int(failed)

            with patch.object(DIAGNOSTIC.ci, "probe", probe), patch.dict(os.environ, {"WXNOTEPAD_PERIODIC_TRACE_DIR": "prior"}):
                result = DIAGNOSTIC.run_observed(Path(folder) / "native-build", output)
                self.assertEqual(os.environ["WXNOTEPAD_PERIODIC_TRACE_DIR"], "prior")
            summary = json.loads((output / "diagnostic-summary.json").read_text(encoding="utf-8"))
            self.assertTrue(all(timeout == 90 and runs == 1 for timeout, runs, _ in calls))
            return result, summary, calls

    def test_first_original_failure_stops_serial_dispatch_and_stays_red(self):
        result, summary, calls = self.run_fake_cases([False, True])
        self.assertEqual(result, 1)
        self.assertEqual(len(calls), 2)
        self.assertEqual(summary["classification"], "continuous_edit_assertion")
        self.assertFalse(summary["defect_resolved"])

    def test_ten_passing_controls_mean_not_reproduced_not_fixed(self):
        result, summary, calls = self.run_fake_cases([False] * 10)
        self.assertEqual(result, 0)
        self.assertEqual(len(calls), 10)
        self.assertEqual(summary["classification"], "not_reproduced")
        self.assertFalse(summary["defect_resolved"])


if __name__ == "__main__":
    unittest.main()

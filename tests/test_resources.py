"""Check resource observations, complete-byte verification and retained reports."""

import hashlib
import importlib.util
import itertools
import json
import os
import sys
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import Mock, patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
SPEC = importlib.util.spec_from_file_location("resources", ROOT / "tools/resources.py")
RESOURCES = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(RESOURCES)


class SavedDocumentVerification(unittest.TestCase):
    def test_complete_saved_bytes_and_failures(self):
        # Cross the comparison's read-block boundary and retain embedded NULs.
        content = b"\0line\r\n" * 150000
        with tempfile.TemporaryDirectory(prefix="wxnotepad resource check ") as folder:
            source = Path(folder) / "source.txt"
            saved = Path(folder) / "saved.txt"
            source.write_bytes(content)
            saved.write_bytes(b"X" + content)
            RESOURCES.verify_saved_bytes(saved, source)
            for invalid in (content, b"Y" + content, b"X" + content[:-1], b"X" + content + b"extra"):
                with self.subTest(length=len(invalid), prefix=invalid[:1]):
                    saved.write_bytes(invalid)
                    with self.assertRaises(ValueError):
                        RESOURCES.verify_saved_bytes(saved, source)

    def test_digest_includes_every_block_and_optional_native_edit(self):
        content = b"\0line\r\n" * 150000
        with tempfile.TemporaryDirectory() as folder:
            source = Path(folder) / "source.txt"
            source.write_bytes(content)
            self.assertEqual(RESOURCES.digest(source), hashlib.sha256(content).hexdigest())
            self.assertEqual(RESOURCES.digest(source, b"X"), hashlib.sha256(b"X" + content).hexdigest())

    def test_prepared_samples_have_exact_sizes_and_searchable_block_tails(self):
        with tempfile.TemporaryDirectory() as folder:
            samples = RESOURCES.prepare(Path(folder) / "samples")
            line = (b"plain text 0123456789 abcdefghijklmnopqrstuvwxyz " * 3)[:127] + b"\n"
            self.assertEqual(samples["small"].read_bytes(), line * 32)
            for name, size in (("10MiB", 10 * 1024 * 1024), ("100MiB", 100 * 1024 * 1024)):
                with self.subTest(sample=name):
                    self.assertEqual(samples[name].stat().st_size, size)
                    with samples[name].open("rb") as source:
                        self.assertEqual(source.read(len(line)), line)
                        for offset in (1024 * 1024 - 128, size - 128):
                            source.seek(offset)
                            self.assertEqual(source.read(6), b"needle")
                            self.assertEqual(source.read(122), line[6:])


class ResourceObservations(unittest.TestCase):
    def setUp(self):
        self.directory = Path(self.enterContext(tempfile.TemporaryDirectory(prefix="resource observations ")))
        self.sample = self.directory / "sample.txt"
        self.content = b"original\0text\r\n"
        self.sample.write_bytes(self.content)
        self.process = Mock(pid=42)
        self.launch = self.enterContext(patch.object(RESOURCES.subprocess, "Popen", return_value=self.process))
        self.enterContext(patch.object(RESOURCES.time, "sleep"))
        self.enterContext(patch.object(RESOURCES.time, "perf_counter", side_effect=itertools.count(1, 0.25)))
        self.enterContext(patch.object(RESOURCES, "memory", return_value={"rss_bytes": 4096}))
        self.edit = self.enterContext(patch.object(RESOURCES, "native_edit"))
        self.topmost = self.enterContext(patch.object(RESOURCES, "show_owned_topmost"))

    def observe(self, kind, sample, *, corrupt_save=False, initial_failure=None):
        initial = {
            "sequence": "0",
            "shown": "1",
            "dirty": "0",
            "length": str(len(self.content) if sample and kind != "operations" else 0),
            "backend": "GdkWaylandDisplay",
            "active": "1",
        }
        states = [initial]
        if kind == "operations":
            states.append({**initial, "sequence": "1", "result": "ok", "length": str(len(self.content))})
        if kind != "idle":
            states.append({**states[-1], "dirty": "1", "length": str((len(self.content) if sample else 0) + 1)})
        if kind == "operations":
            states.append({**states[-1], "sequence": "2", "dirty": "0"})
        snapshots = iter(states)

        def state(folder, process, predicate, timeout, clock):
            if initial_failure:
                raise initial_failure
            snapshot = next(snapshots)
            if snapshot["sequence"] == "1" and snapshot["dirty"] == "0":
                self.assertEqual(
                    (folder / "command").read_text(encoding="utf-8"), f"1\nopen\n{folder / 'document.txt'}\n"
                )
            elif snapshot["sequence"] == "2":
                self.assertEqual((folder / "command").read_text(encoding="utf-8"), "2\nsave\n\n")
                (folder / "document.txt").write_bytes(b"X" + self.content + (b"extra" if corrupt_save else b""))
            self.assertTrue(predicate(snapshot), snapshot)
            return snapshot

        with patch.object(RESOURCES, "observe_state", side_effect=state):
            return RESOURCES.observe(Path("wxnotepad"), self.directory, kind, sample, 1, True)

    def test_startup_and_idle_report_distinct_measurements_for_empty_and_loaded_documents(self):
        for kind in ("startup", "idle"):
            for sample in (None, self.sample):
                with self.subTest(kind=kind, sample=sample):
                    self.edit.reset_mock()
                    row = self.observe(kind, sample)
                    self.assertEqual(row["status"], "ok")
                    self.assertEqual(row["case"], "sample" if sample else "empty")
                    self.assertEqual(row["pid"], 42)
                    self.assertGreater(row["launch_to_update_observed_ms"], 0)
                    if kind == "startup":
                        self.edit.assert_called_once()
                        self.assertEqual(row["final_state"]["dirty"], "1")
                        self.assertGreater(row["launch_to_native_edit_observed_ms"], row["edit_observed_ms"])
                    else:
                        self.edit.assert_not_called()
                        self.assertEqual(row["memory"], {"idle": {"rss_bytes": 4096}})
                        self.assertNotIn("edit_observed_ms", row)
                    command = self.launch.call_args.args[0]
                    self.assertEqual("--startup-file" in command, sample is not None)
                    if sample:
                        self.assertEqual(Path(command[-1]).read_bytes(), self.content)
        self.assertEqual(self.process.terminate.call_count, 4)
        self.assertEqual(self.process.wait.call_count, 4)
        self.assertEqual(self.topmost.call_count, 4)

    def test_operations_verify_saved_content_and_report_all_measurement_stages(self):
        row = self.observe("operations", self.sample)
        self.assertEqual(row["status"], "ok", row)
        self.assertTrue(row["content_verified"])
        self.assertEqual(row["output_bytes"], len(self.content) + 1)
        self.assertEqual(row["output_sha256"], hashlib.sha256(b"X" + self.content).hexdigest())
        self.assertEqual(row["memory"], dict.fromkeys(("opened", "edited", "saved"), {"rss_bytes": 4096}))
        self.assertEqual(row["final_state"]["sequence"], "2")
        self.assertEqual(row["final_state"]["dirty"], "0")
        for field in ("open_observed_ms", "edit_observed_ms", "save_observed_ms"):
            self.assertGreater(row[field], 0)
        stamps = list(row["timestamps"].values())
        self.assertEqual(stamps, sorted(stamps))
        self.assertNotIn("--startup-file", self.launch.call_args.args[0])
        self.process.terminate.assert_called_once()
        self.process.wait.assert_called_once_with(timeout=10)

    def test_corrupt_saved_content_marks_observation_failed_and_reaps_process(self):
        row = self.observe("operations", self.sample, corrupt_save=True)
        self.assertEqual(row["status"], "failed")
        self.assertIn("unexpected trailing bytes", row["error"])
        self.assertNotIn("content_verified", row)
        self.process.terminate.assert_called_once()
        self.process.wait.assert_called_once_with(timeout=10)

    def test_exited_app_retains_failure_and_reaps_process(self):
        row = self.observe("startup", None, initial_failure=RESOURCES.AppExited(7))
        self.assertEqual(row["status"], "failed")
        self.assertIn("App exited 7", row["error"])
        self.assertNotIn("final_state", row)
        self.edit.assert_not_called()
        self.process.terminate.assert_called_once()
        self.process.wait.assert_called_once_with(timeout=10)


class ResourceReports(unittest.TestCase):
    def test_summary_groups_samples_and_uses_medians_of_successful_observations(self):
        rows = [
            {
                "kind": "idle",
                "case": "small",
                "status": "ok",
                "launch_to_update_observed_ms": value,
                "memory": {"idle": {"rss_bytes": value * 1024}},
            }
            for value in (9, 1, 5)
        ]
        rows.append({"kind": "idle", "case": "small", "status": "failed", "error": "failed before launch"})
        rows.append({**rows[0], "case": "empty"})
        self.assertEqual(
            RESOURCES.summarize(rows),
            {
                "idle/small": {
                    "successful_processes": 3,
                    "launch_to_update_observed_ms": 5,
                    "memory": {"idle": {"rss_bytes": 5120}},
                },
                "idle/empty": {
                    "successful_processes": 1,
                    "launch_to_update_observed_ms": 9,
                    "memory": {"idle": {"rss_bytes": 9216}},
                },
            },
        )

    def test_batch_retains_completed_rows_and_failed_row_before_stopping(self):
        success = {"kind": "startup", "case": "empty", "run": 1, "status": "ok", "memory": {}}
        failure = {"kind": "startup", "case": "empty", "run": 2, "status": "failed", "error": "app exited"}
        args = SimpleNamespace(startup_count=3, idle_count=1, operation_count=1, owned_topmost=False)
        report = {"rows": []}
        samples = {name: Path(f"{name}.txt") for name in ("small", "10MiB", "100MiB")}
        with (
            tempfile.TemporaryDirectory() as folder,
            patch.object(RESOURCES, "observe", side_effect=[success, failure]) as observe,
            patch("builtins.print"),
        ):
            with self.assertRaisesRegex(SystemExit, "raw row retained"):
                RESOURCES.observe_batch(Path("wxnotepad"), Path(folder), args, samples, report)
            retained = json.loads((Path(folder) / "results.json").read_text(encoding="utf-8"))
        self.assertEqual(observe.call_count, 2)
        self.assertEqual(retained["rows"], [success, failure])
        self.assertEqual(retained["summary"], {"startup/empty": {"successful_processes": 1, "memory": {}}})
        self.assertEqual(report, retained)

    def test_cli_records_executable_sample_and_core_provenance(self):
        with tempfile.TemporaryDirectory(prefix="resource provenance ") as name:
            root = Path(name)
            executable = root / "wxnotepad"
            executable.write_bytes(b"fixture executable")
            core = root / "original-core"
            core.mkdir()
            (core / "wxnotepad-source.json").write_text('{"commit":"pinned-core"}', encoding="utf-8")
            cmake = root / "cmake"
            cmake.mkdir()
            (cmake / "CMakeCache.txt").write_text(
                "CMAKE_BUILD_TYPE:STRING=Release\nUNRELATED:STRING=value\nwxWidgets_DIR:PATH=/sdk/wx\n",
                encoding="utf-8",
            )
            samples = {key: root / f"{key}.txt" for key in ("small", "10MiB", "100MiB")}
            for sample in samples.values():
                sample.write_bytes(b"original\0sample\n")
            arguments = [
                "resources.py",
                str(executable),
                str(root / "output"),
                "--source-commit",
                "measured-commit",
                "--startup-count",
                "2",
                "--idle-count",
                "1",
                "--operation-count",
                "1",
            ]
            with (
                patch.object(sys, "argv", arguments),
                patch.object(RESOURCES, "prepare", return_value=samples),
                patch.object(RESOURCES, "observe_batch") as observe_batch,
            ):
                with self.assertRaises(SystemExit) as exit_code:
                    RESOURCES.main()
            self.assertEqual(exit_code.exception.code, 0)
            report = observe_batch.call_args.args[-1]
            self.assertEqual(report["source_commit"], "measured-commit")
            self.assertEqual(report["exe_sha256"], hashlib.sha256(b"fixture executable").hexdigest())
            self.assertEqual(report["core_build_record"], {"commit": "pinned-core"})
            self.assertEqual(report["cmake_build"], ["CMAKE_BUILD_TYPE:STRING=Release", "wxWidgets_DIR:PATH=/sdk/wx"])
            self.assertEqual(
                report["planned_counts"], {"startup_per_case": 2, "idle_per_case": 1, "operations_per_size": 1}
            )
            self.assertEqual(report["rows"], [])
            for sample in report["samples"].values():
                self.assertEqual(sample["bytes"], len(b"original\0sample\n"))
                self.assertEqual(sample["sha256"], hashlib.sha256(b"original\0sample\n").hexdigest())
                self.assertEqual(sample["expected_saved_sha256"], hashlib.sha256(b"Xoriginal\0sample\n").hexdigest())

    @unittest.skipUnless(sys.platform == "linux", "Uses the Linux process status interface")
    def test_memory_reads_positive_resident_set_for_current_process(self):
        self.assertGreater(RESOURCES.memory(os.getpid())["rss_bytes"], 0)


if __name__ == "__main__":
    unittest.main()

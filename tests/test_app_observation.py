"""Exercise publication races and the two observation callers' failure contracts."""
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import Mock, patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import app_observation as observation
import resource_process
import resources


class AppObservation(unittest.TestCase):
    def test_state_keeps_tabs_in_values_and_empty_fields(self):
        with tempfile.TemporaryDirectory() as name:
            folder = Path(name)
            (folder / "state").write_text("sequence\t2\npath\t中文\tfile\nempty\t\n", encoding="utf-8")
            self.assertEqual(observation.read_state(folder), {
                "sequence": "2", "path": "中文\tfile", "empty": ""})

    def test_wait_retries_publication_races_and_unmatched_snapshots(self):
        process = Mock()
        process.poll.return_value = None
        snapshots = [FileNotFoundError(), PermissionError(), {"sequence": "0"}, {"sequence": "1"}]
        clock = Mock(side_effect=[0, 0, .01, .02, .03])
        with patch.object(observation, "read_state", side_effect=snapshots), \
                patch.object(observation.time, "sleep") as sleep:
            state = observation.wait_state(Path("unused"), process, lambda s: s["sequence"] == "1", 1, clock)
        self.assertEqual(state, {"sequence": "1"})
        self.assertEqual([call.args for call in sleep.call_args_list], [(.01,)] * 3)

    def test_exited_process_is_reported_before_reading_state(self):
        process = Mock(returncode=7)
        process.poll.return_value = 7
        for module, error_type, message, clock_name in (
                (resources, RuntimeError, "App exited 7", "perf_counter"),
                (resource_process, AssertionError, "App exited: 7", "monotonic")):
            with self.subTest(caller=module.__name__), \
                    patch.object(module.time, clock_name, side_effect=[0, 0]), \
                    patch.object(observation, "read_state") as read:
                with self.assertRaises(error_type) as caught:
                    module.wait_state(Path("unused"), process, lambda _: True)
                self.assertEqual(str(caught.exception), message)
                read.assert_not_called()

    def test_callers_keep_default_and_explicit_deadlines(self):
        for module, timeout, kwargs, error_type, message, clock_name in (
                (resources, 30, {}, RuntimeError, "App state observation timed out", "perf_counter"),
                (resource_process, 15, {}, AssertionError, "Actual app observation timed out", "monotonic"),
                (resource_process, .2, {"timeout": .2}, AssertionError,
                 "Actual app observation timed out", "monotonic")):
            process = Mock()
            with self.subTest(caller=module.__name__, timeout=timeout), \
                    patch.object(module.time, clock_name, side_effect=[0, timeout]):
                with self.assertRaises(error_type) as caught:
                    module.wait_state(Path("unused"), process, lambda _: True, **kwargs)
                self.assertEqual(str(caught.exception), message)
                process.poll.assert_not_called()

    def test_invalid_snapshot_and_predicate_errors_are_not_retried(self):
        process = Mock()
        process.poll.return_value = None
        with tempfile.TemporaryDirectory() as name:
            folder = Path(name)
            (folder / "state").write_text("not a key/value record\n", encoding="utf-8")
            with self.assertRaises(ValueError):
                observation.wait_state(folder, process, lambda _: True, 1)
            (folder / "state").write_text("sequence\t0\n", encoding="utf-8")
            with self.assertRaises(KeyError):
                observation.wait_state(folder, process, lambda state: state["missing"], 1)

    def test_both_command_formats_replace_only_after_complete_write(self):
        replace = Path.replace
        with tempfile.TemporaryDirectory() as name:
            folder = Path(name)
            target = folder / "command"
            for publish, expected in (
                    (lambda: resource_process.command(folder, "1\n"), "1\n"),
                    (lambda: resources.command(folder, 2, "open", "中文.txt"), "2\nopen\n中文.txt\n")):
                target.write_text("previous", encoding="utf-8")

                def complete(source, destination):
                    self.assertEqual(target.read_text(encoding="utf-8"), "previous")
                    self.assertEqual(source.read_text(encoding="utf-8"), expected)
                    return replace(source, destination)

                with patch.object(Path, "replace", complete):
                    publish()
                self.assertEqual(target.read_text(encoding="utf-8"), expected)
                self.assertFalse((folder / "pending").exists())


if __name__ == "__main__":
    unittest.main()

"""Check the fuzz runner's failure propagation and durable corpus/evidence behavior."""
import importlib.util
from pathlib import Path
import sys
import tempfile
import unittest

SPEC = importlib.util.spec_from_file_location('fuzz_runner', Path(__file__).parent / 'fuzz/run.py')
RUNNER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(RUNNER)


class FuzzRunnerTests(unittest.TestCase):
    def test_failed_process_retains_diagnostics_and_exit_status(self):
        with tempfile.TemporaryDirectory() as temporary:
            log = Path(temporary) / 'failure.log'
            result = RUNNER.execute([sys.executable, '-c', "print('failure evidence'); raise SystemExit(7)"], log, 10)
            self.assertEqual(result['exit_code'], 7)
            self.assertIn('failure evidence', log.read_text(encoding='utf-8'))

    def test_timeout_is_a_failure_with_retained_log(self):
        with tempfile.TemporaryDirectory() as temporary:
            log = Path(temporary) / 'timeout.log'
            result = RUNNER.execute([sys.executable, '-c', 'import time; time.sleep(30)'], log, 0.2)
            self.assertEqual(result['exit_code'], 124)
            self.assertIn('exceeded', log.read_text(encoding='utf-8'))

    def test_seed_refresh_preserves_mutations_and_exact_binary_bytes(self):
        with tempfile.TemporaryDirectory() as temporary:
            corpus = Path(temporary)
            mutation = corpus / 'retained-mutation'
            mutation.write_bytes(b'\xff\x00\x81')
            RUNNER.materialize('codecs', corpus)
            RUNNER.materialize('codecs', corpus)
            self.assertEqual(mutation.read_bytes(), b'\xff\x00\x81')
            self.assertEqual((corpus / 'single-byte-repertoire.bin').read_bytes(), bytes(range(256)))
            self.assertEqual((corpus / 'empty.bin').read_bytes(), b'')


if __name__ == '__main__':
    unittest.main()

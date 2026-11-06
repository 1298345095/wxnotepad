"""Exercise crash evidence collection with an actual native process and core."""
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class NativeCoreEvidence(unittest.TestCase):
    def test_crashed_process_keeps_its_matching_binary_core_and_source_backtrace(self):
        with tempfile.TemporaryDirectory(prefix='wxnotepad core evidence ') as folder:
            directory = Path(folder)
            source = directory / 'probe.cpp'
            executable = directory / 'crash-probe'
            source.write_text('#include <csignal>\n'
                              'extern "C" void owned_ci_crash() { std::raise(SIGSEGV); }\n'
                              'int main() { owned_ci_crash(); }\n', encoding='utf-8')
            subprocess.run(['g++', '-g1', '-O0', source, '-o', executable],
                           check=True, capture_output=True, text=True, encoding='utf-8')
            cores = directory / 'cores'
            cores.mkdir()
            core = cores / ('core.123.' + str(executable).replace('/', '!'))
            capture = subprocess.run(['gdb', '--batch', '--nx', '-ex', 'run',
                                      '-ex', f'generate-core-file {core}', '--args', executable],
                                     capture_output=True, text=True, encoding='utf-8')
            self.assertEqual(capture.returncode, 0, capture.stdout + capture.stderr)
            self.assertTrue(core.is_file(), capture.stdout + capture.stderr)
            original_core = core.read_bytes()

            result = subprocess.run([sys.executable, ROOT / '.github/scripts/collect_cores.py',
                                     '--core-dir', cores], capture_output=True, text=True, encoding='utf-8')
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            report = (cores / 'backtrace-123.txt').read_text(encoding='utf-8')
            self.assertIn('owned_ci_crash', report)
            self.assertIn('probe.cpp:2', report)
            self.assertEqual((cores / 'executable-123-crash-probe').read_bytes(), executable.read_bytes())
            self.assertEqual(core.read_bytes(), original_core)


if __name__ == '__main__':
    unittest.main()

"""Exercise CI's Windows CMake invocation through the actual developer shell."""
import importlib.util
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('ci', ROOT / '.github/scripts/ci.py')
CI = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(CI)


@unittest.skipUnless(sys.platform == 'win32', 'The developer-shell wrapper is Windows-only')
class WindowsCMakeCommands(unittest.TestCase):
    def test_paths_and_literal_arguments_reach_cmake(self):
        with tempfile.TemporaryDirectory(prefix='wxnotepad cmake ') as folder:
            directory = Path(folder)
            script = directory / 'probe.cmake'
            output = directory / 'received.txt'
            script.write_text('file(WRITE "${OUTPUT}" "${WINDOWS_PATH}\n${SPACED_PATH}\n${LITERAL}")\n',
                              encoding='utf-8')
            values = [r'D:\a\wxnotepad\wxnotepad\.ci\vcpkg',
                      'C:/Program Files/示例 SDK', 'literal $value & "quotes"']
            command = CI.cmake_command([f'-DOUTPUT={output}', f'-DWINDOWS_PATH={values[0]}',
                                        f'-DSPACED_PATH={values[1]}', f'-DLITERAL={values[2]}',
                                        '-P', script])
            result = subprocess.run(command, capture_output=True, text=True, encoding='utf-8', errors='replace')
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertEqual(output.read_text(encoding='utf-8'), '\n'.join(values))

    def test_cmake_failure_reaches_the_caller(self):
        with tempfile.TemporaryDirectory(prefix='wxnotepad cmake ') as folder:
            script = Path(folder) / 'failure.cmake'
            script.write_text('message(FATAL_ERROR "owned CI invocation failure")\n', encoding='utf-8')
            result = subprocess.run(CI.cmake_command(['-P', script]), capture_output=True,
                                    text=True, encoding='utf-8', errors='replace')
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('owned CI invocation failure', result.stdout + result.stderr)


if __name__ == '__main__':
    unittest.main()

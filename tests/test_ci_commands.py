"""Exercise CI's Windows CMake invocation through the actual developer shell."""
import importlib.util
import json
import os
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
    def test_header_changes_rebuild_ninja_consumers_with_mixed_console_codepages(self):
        with tempfile.TemporaryDirectory(prefix='wxnotepad header dependency ') as folder:
            directory = Path(folder)
            source = directory / 'source'
            source.mkdir()
            (source / 'CMakeLists.txt').write_text(
                'cmake_minimum_required(VERSION 3.25)\nproject(HeaderDependency LANGUAGES CXX)\n'
                'add_executable(probe main.cpp)\n'
                'target_compile_options(probe PRIVATE /utf-8)\n', encoding='utf-8')
            (source / 'main.cpp').write_text(
                '#include "value.h"\nint main() { return VALUE; }\n', encoding='utf-8')
            header = source / 'value.h'
            header.write_text('#define VALUE 11\n', encoding='utf-8')
            build = directory / 'build'
            environment = dict(os.environ, VSLANG='2052')

            def cmake(*arguments):
                # A private console reproduces the input/output encoding mismatch
                # without changing the developer's console or showing a window.
                driver = ('import ctypes,json,subprocess,sys; '
                          'ctypes.windll.kernel32.SetConsoleCP(936); '
                          'ctypes.windll.kernel32.SetConsoleOutputCP(65001); '
                          'sys.exit(subprocess.run(json.loads(sys.argv[1])).returncode)')
                command = [sys.executable, '-X', 'utf8', '-c', driver,
                           json.dumps([str(item) for item in CI.cmake_command(arguments)])]
                result = subprocess.run(command, env=environment,
                                        creationflags=subprocess.CREATE_NO_WINDOW,
                                        capture_output=True, text=True, encoding='utf-8', errors='replace')
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

            cmake('-S', source, '-B', build, '-G', 'Ninja')
            cmake('--build', build)
            self.assertEqual(subprocess.run([build / 'probe.exe']).returncode, 11)
            header.write_text('#define VALUE 22\n', encoding='utf-8')
            cmake('--build', build)
            self.assertEqual(subprocess.run([build / 'probe.exe']).returncode, 22)

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

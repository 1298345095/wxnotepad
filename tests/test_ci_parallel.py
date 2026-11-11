"""Exercise parallel CI partitions with real CTest processes and working directories."""
from concurrent.futures import ThreadPoolExecutor
import importlib.util
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('ci_parallel', ROOT / '.github/scripts/ci.py')
CI = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(CI)


class ParallelCTest(unittest.TestCase):
    def setUp(self):
        self.folder = tempfile.TemporaryDirectory(prefix='wxnotepad parallel ]=] 测试 ')
        self.addCleanup(self.folder.cleanup)
        self.directory = Path(self.folder.name)
        self.source = self.directory / 'source'
        self.build = self.directory / 'build'
        self.results = self.directory / 'results'
        self.source.mkdir()
        self.results.mkdir()
        custom = self.source / 'custom working directory'
        custom.mkdir()
        probe = self.source / 'probe.py'
        probe.write_text(
            'from pathlib import Path\nimport sys,time\n'
            'root,index,partner,working=sys.argv[1:]\nroot=Path(root)\n'
            'assert Path.cwd().resolve()==Path(working).resolve(), str(Path.cwd())\n'
            '(root/(index+".started")).write_text("ready",encoding="utf-8")\n'
            'deadline=time.monotonic()+5\n'
            'while partner and not (root/(partner+".started")).exists():\n'
            '    assert time.monotonic()<deadline, "paired tests did not run concurrently"\n'
            '    time.sleep(.01)\n'
            'with (root/(index+".done")).open("x",encoding="utf-8") as output: output.write(index)\n',
            encoding='utf-8')
        definitions = ['cmake_minimum_required(VERSION 3.25)', 'project(ParallelProbe NONE)', 'enable_testing()']
        self.names = ['owned.[first]', 'owned+(second)', 'owned-custom']
        for index, name in enumerate(self.names):
            working = custom if index == 2 else self.build
            values = [sys.executable, probe.as_posix(), self.results.as_posix(), str(index),
                      str(1 - index) if index < 2 else '', working.as_posix()]
            arguments = ' '.join(f'[==[{value}]==]' for value in values)
            definitions.append(f'add_test(NAME [==[{name}]==] COMMAND {arguments})')
            if index == 2:
                definitions.append(f'set_tests_properties([==[{name}]==] PROPERTIES '
                                   f'WORKING_DIRECTORY [==[{custom.as_posix()}]==])')
        (self.source / 'CMakeLists.txt').write_text('\n'.join(definitions) + '\n', encoding='utf-8')
        subprocess.run(['cmake', '-S', self.source, '-B', self.build],
                       check=True, capture_output=True, text=True, encoding='utf-8')

    def execute(self, shard):
        workspace, names = shard
        expression = '^(' + '|'.join(re.escape(name) for name in names) + ')$'
        result = subprocess.run(['ctest', '--test-dir', workspace, '-C', 'Release', '--parallel', '1',
                                 '-R', expression, '--no-tests=error', '--output-on-failure',
                                 '--output-junit', workspace / 'tests.xml'],
                                capture_output=True, text=True, encoding='utf-8')
        return result

    def test_shards_execute_every_test_once_with_separate_logs_and_original_working_directories(self):
        shards = CI.test_shards(self.build, None, 2, self.directory / 'reports')
        self.assertEqual(len(shards), 2)
        original_log = self.build / 'Testing/Temporary/LastTest.log'
        listing_log = original_log.read_bytes()
        with ThreadPoolExecutor(max_workers=2) as workers:
            results = list(workers.map(self.execute, shards))
        for result in results:
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(sorted(path.read_text(encoding='utf-8') for path in self.results.glob('*.done')),
                         ['0', '1', '2'])
        for workspace, _ in shards:
            self.assertTrue((workspace / 'tests.xml').is_file())
            self.assertTrue((workspace / 'Testing/Temporary/LastTest.log').is_file())
        self.assertEqual(original_log.read_bytes(), listing_log)

    def test_filter_and_worker_limit_preserve_the_exact_selected_test(self):
        shards = CI.test_shards(self.build, '^owned-custom$', 8, self.directory / 'reports')
        self.assertEqual([names for _, names in shards], [['owned-custom']])
        result = self.execute(shards[0])
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual([path.name for path in self.results.glob('*.done')], ['2.done'])

    def test_nested_registered_test_executes_with_its_original_directory_and_environment(self):
        nested = self.source / 'nested tests' / 'deep'
        nested.mkdir(parents=True)
        probe = nested / 'child.py'
        probe.write_text(
            'from pathlib import Path\nimport os,sys\n'
            'assert Path.cwd().resolve()==Path(sys.argv[1]).resolve(), str(Path.cwd())\n'
            'assert os.environ["WXNOTEPAD_CHILD_VALUE"]=="registered"\n'
            'Path(sys.argv[2]).write_text("nested",encoding="utf-8")\n', encoding='utf-8')
        (nested.parent / 'CMakeLists.txt').write_text('add_subdirectory(deep)\n', encoding='utf-8')
        arguments = ' '.join(f'[==[{value}]==]' for value in
                             [sys.executable, probe.as_posix(), (self.build / 'nested tests/deep').as_posix(),
                              (self.results / 'nested.done').as_posix()])
        (nested / 'CMakeLists.txt').write_text(
            f'add_test(NAME owned-child COMMAND {arguments})\n'
            'set_tests_properties(owned-child PROPERTIES ENVIRONMENT "WXNOTEPAD_CHILD_VALUE=registered")\n',
            encoding='utf-8')
        with (self.source / 'CMakeLists.txt').open('a', encoding='utf-8') as output:
            output.write('add_subdirectory([==[nested tests]==])\n')
        subprocess.run(['cmake', '-S', self.source, '-B', self.build],
                       check=True, capture_output=True, text=True, encoding='utf-8')
        shards = CI.test_shards(self.build, '^owned-child$', 2, self.directory / 'nested reports')
        result = self.execute(shards[0])
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual((self.results / 'nested.done').read_text(encoding='utf-8'), 'nested')
        self.assertEqual([path.name for path in self.results.glob('*.done')], ['nested.done'])

    def test_empty_selection_is_a_failure(self):
        with self.assertRaisesRegex(SystemExit, 'No CTest tests match'):
            CI.test_shards(self.build, '^missing-test$', 4, self.directory / 'reports')

    def test_windows_run_retains_reports_and_refuses_to_overwrite_a_previous_attempt(self):
        reports = self.directory / 'windows evidence'
        with mock.patch.object(CI, 'WINDOWS', True), mock.patch.object(
                CI, 'windows_desktop_names', return_value={'thread': 'Default', 'input': 'Default'}):
            CI.test('windows', self.build, '^owned-custom$', 1, report_directory=reports)
            junit = (reports / 'windows-tests.xml').read_bytes()
            log = (reports / 'windows-ctest.log').read_bytes()
            with self.assertRaisesRegex(SystemExit, 'Report directory already exists'):
                CI.test('windows', self.build, '^owned-custom$', 1, report_directory=reports)
        self.assertIn(b'100% tests passed', log)
        self.assertEqual((reports / 'windows-tests.xml').read_bytes(), junit)
        self.assertEqual((reports / 'windows-ctest.log').read_bytes(), log)
        self.assertTrue((reports / 'windows-desktop.json').is_file())
        self.assertTrue((reports / 'windows-ctest/LastTest.log').is_file())

    def test_windows_private_desktop_fails_before_starting_any_test(self):
        reports = self.directory / 'rejected desktop'
        with mock.patch.object(CI, 'WINDOWS', True), mock.patch.object(
                CI, 'windows_desktop_names', return_value={'thread': 'owned-private', 'input': 'Default'}):
            with self.assertRaisesRegex(SystemExit, 'Default input desktop'):
                CI.test('windows', self.build, '^owned-custom$', 1, report_directory=reports)
        self.assertEqual(list(self.results.glob('*.started')), [])
        self.assertTrue((reports / 'windows-desktop.json').is_file())
        self.assertFalse((reports / 'windows-tests.xml').exists())

    def test_windows_child_failure_keeps_its_nonzero_status_and_junit(self):
        reports = self.directory / 'failed windows evidence'
        (self.results / '2.done').write_text('earlier attempt', encoding='utf-8')
        with mock.patch.object(CI, 'WINDOWS', True), mock.patch.object(
                CI, 'windows_desktop_names', return_value={'thread': 'Default', 'input': 'Default'}):
            with self.assertRaises(SystemExit) as failure:
                CI.test('windows', self.build, '^owned-custom$', 1, report_directory=reports)
        self.assertNotEqual(failure.exception.code, 0)
        self.assertTrue((reports / 'windows-tests.xml').is_file())
        self.assertIn('FileExistsError', (reports / 'windows-ctest.log').read_text(encoding='utf-8'))
        self.assertIn(b'FileExistsError', (reports / 'windows-ctest/LastTest.log').read_bytes())

    def test_child_failures_return_to_workers_and_default_commands_still_fail(self):
        previous_reports = CI.REPORTS
        CI.REPORTS = self.directory / 'command reports'
        self.addCleanup(setattr, CI, 'REPORTS', previous_reports)

        def child(code):
            return CI.run([sys.executable, '-c', f'import sys; print("owned exit {code}"); sys.exit({code})'],
                          f'child-{code}.log', check=False)

        with ThreadPoolExecutor(max_workers=2) as workers:
            self.assertEqual(list(workers.map(child, [9, 0])), [9, 0])
        self.assertIn('owned exit 9', (CI.REPORTS / 'child-9.log').read_text(encoding='utf-8'))
        self.assertIn('owned exit 0', (CI.REPORTS / 'child-0.log').read_text(encoding='utf-8'))
        with self.assertRaises(SystemExit) as failure:
            CI.run([sys.executable, '-c', 'import sys; sys.exit(9)'], 'default-failure.log')
        self.assertEqual(failure.exception.code, 9)


if __name__ == '__main__':
    unittest.main()

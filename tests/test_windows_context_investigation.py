"""Keep focused investigations cold-buildable, scoped and fail-preserving."""
import json
import os
from contextlib import chdir
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest import mock
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / '.github/scripts'))
import windows_context_investigation as investigation
import windows_foreground


class WindowsInvestigation(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='wxnotepad investigation ')
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name)
        self.build = self.directory / 'build'
        self.events = self.directory / 'events.txt'

    def configure(self, *, unbuilt=False, context=True, swap=True, fail=False):
        script = self.directory / 'event.py'
        script.write_text(
            'import os,sys; from pathlib import Path\n'
            'with Path(sys.argv[1]).open("a") as output: output.write(sys.argv[2] + "\\n")\n'
            'if sys.argv[2] == "original_editor_context_menu":\n'
            ' assert os.environ["WXNOTEPAD_INVESTIGATION_CONTRACT"] == "retained"\n'
            ' if sys.argv[3] == "fail": raise SystemExit(7)\n', encoding='utf-8')
        cases = ['before']
        if context:
            cases.append(investigation.CONTEXT_CASE)
        cases += ['native_windows_clipboard_peer_lifecycle', 'after']
        if swap:
            cases += [investigation.SWAP_CASE, 'after-swap']
        source = 'cmake_minimum_required(VERSION 3.25)\nproject(Investigation NONE)\nenable_testing()\n'
        for name in cases:
            if unbuilt and name == investigation.CONTEXT_CASE:
                command = f'"{(self.build / "context_menu.exe").as_posix()}"'
            else:
                command = (f'"{Path(sys.executable).as_posix()}" "{script.as_posix()}" '
                           f'"{self.events.as_posix()}" "{name}" "{"fail" if fail else "pass"}"')
            source += f'add_test(NAME {name} COMMAND {command})\n'
        if context:
            source += ('set_tests_properties(original_editor_context_menu PROPERTIES TIMEOUT 60 '
                       'ENVIRONMENT "WXNOTEPAD_INVESTIGATION_CONTRACT=retained")\n')
        if swap:
            source += 'set_tests_properties(original_clipboard_swap PROPERTIES TIMEOUT 180)\n'
            for target, event, dependency in (('clipboard_peer', 'peer-built', ''),
                                               ('clipboard_smoke', 'swap-built', 'DEPENDS clipboard_peer')):
                source += (f'add_custom_target({target} {dependency} COMMAND "{Path(sys.executable).as_posix()}" '
                           f'"{script.as_posix()}" "{self.events.as_posix()}" "{event}" "pass")\n')
        (self.directory / 'CMakeLists.txt').write_text(source, encoding='utf-8')
        subprocess.run(['cmake', '-S', self.directory, '-B', self.build, '-G', 'Ninja'],
                       check=True, capture_output=True)

    def execute(self, reports, runs):
        return subprocess.run([sys.executable, '-X', 'utf8',
                               ROOT / '.github/scripts/windows_context_investigation.py', 'run',
                               '--cmake-dir', self.build, '--report-dir', reports, '--runs', str(runs)],
                              cwd=self.directory, capture_output=True, text=True, encoding='utf-8')

    def run_with_test_desktop(self, reports, runs):
        arguments = ['windows_context_investigation.py', 'run', '--cmake-dir', str(self.build),
                     '--report-dir', str(reports), '--runs', str(runs)]
        # The process/CTest contract is portable; replace only the Windows host
        # and input-desktop boundary. Native CLI checks below still use the OS.
        with chdir(self.directory), mock.patch.object(sys, 'argv', arguments), \
                mock.patch.object(investigation.ci, 'WINDOWS', True), \
                mock.patch.object(investigation.ci, 'windows_desktop_names',
                                  return_value={'thread': 'Default', 'input': 'Default'}):
            investigation.main()

    def test_registered_ctest_repetitions_keep_reports_and_original_environment(self):
        self.configure()
        reports = self.directory / 'portable-runs'
        self.run_with_test_desktop(reports, 2)
        self.assertEqual(self.events.read_text(), ('before\n' + investigation.CONTEXT_CASE + '\n') * 2)
        for attempt in ('run-001', 'run-002'):
            suite = ET.parse(reports / attempt / 'windows-tests.xml').getroot()
            self.assertEqual((suite.get('tests'), suite.get('failures')), ('2', '0'))

    def test_registered_ctest_failure_stops_before_a_second_attempt(self):
        self.configure(fail=True)
        reports = self.directory / 'portable-failure'
        with self.assertRaises(SystemExit) as failure:
            self.run_with_test_desktop(reports, 3)
        self.assertEqual(failure.exception.code, 8)
        self.assertEqual(self.events.read_text(), 'before\n' + investigation.CONTEXT_CASE + '\n')
        self.assertFalse((reports / 'run-002').exists())
        suite = ET.parse(reports / 'run-001/windows-tests.xml').getroot()
        self.assertEqual(suite.get('failures'), '1')

    def test_selected_cold_target_build_keeps_its_real_peer_dependency(self):
        self.configure()
        reports = self.directory / 'cold-build'
        reports.mkdir()
        investigation.build(investigation.plan(self.build, 'swap'), reports, 2)
        self.assertEqual(self.events.read_text(), 'peer-built\nswap-built\n')

    def test_cold_prefix_preserves_registration_and_stops_at_context_case(self):
        self.configure(unbuilt=True)
        registration = self.build / 'CTestTestfile.cmake'
        before = registration.read_bytes()
        selected = investigation.plan(self.build, 'prefix')
        self.assertEqual(selected['tests'], ['before', investigation.CONTEXT_CASE])
        self.assertEqual(registration.read_bytes(), before)
        listing = json.loads(subprocess.check_output(
            ['ctest', '--test-dir', str(self.build), '--show-only=json-v1']))
        context = next(case for case in listing['tests'] if case['name'] == investigation.CONTEXT_CASE)
        self.assertIn({'name': 'TIMEOUT', 'value': 60.0}, context['properties'])

    def test_clipboard_repeats_original_case_without_interleaving_other_tests(self):
        self.configure()
        self.assertEqual(investigation.plan(self.build, 'clipboard')['tests'],
                         [investigation.CONTEXT_CASE])

    def test_missing_context_cannot_silently_shrink_investigation(self):
        self.configure(context=False)
        for scenario in ('prefix', 'clipboard'):
            with self.subTest(scenario=scenario), self.assertRaisesRegex(SystemExit, investigation.CONTEXT_CASE):
                investigation.plan(self.build, scenario)

    def test_swap_selects_its_original_case_target_and_timeout(self):
        self.configure()
        selected = investigation.plan(self.build, 'swap')
        self.assertEqual(selected['tests'], [investigation.SWAP_CASE])
        self.assertEqual(selected['targets'], ['clipboard_smoke'])
        listing = json.loads(subprocess.check_output(
            ['ctest', '--test-dir', str(self.build), '--show-only=json-v1']))
        case = next(case for case in listing['tests'] if case['name'] == investigation.SWAP_CASE)
        self.assertIn({'name': 'TIMEOUT', 'value': 180.0}, case['properties'])

    def test_missing_swap_cannot_silently_shrink_investigation(self):
        self.configure(swap=False)
        for scenario in ('swap', 'swap-prefix'):
            with self.subTest(scenario=scenario), self.assertRaisesRegex(SystemExit, investigation.SWAP_CASE):
                investigation.plan(self.build, scenario)

    def test_swap_prefix_keeps_all_registered_predecessors_in_order(self):
        self.configure()
        selected = investigation.plan(self.build, 'swap-prefix')
        self.assertEqual(selected['tests'], ['before', investigation.CONTEXT_CASE,
                                             'native_windows_clipboard_peer_lifecycle', 'after', investigation.SWAP_CASE])
        self.assertIn('clipboard_smoke', selected['targets'])
        self.assertIn('context_menu', selected['targets'])

    def test_zero_attempts_cannot_report_an_empty_success(self):
        reports = self.directory / 'reports'
        result = self.execute(reports, 0)
        self.assertEqual(result.returncode, 2)
        self.assertIn('--runs and --jobs must be positive', result.stderr)
        self.assertFalse(reports.exists())

    def test_cli_report_paths_cannot_escape_the_invocation_directory(self):
        work = self.directory / 'work'
        work.mkdir()
        outside = self.directory / 'outside'
        for candidate in (Path('../outside'), outside):
            with self.subTest(candidate=candidate):
                result = subprocess.run([sys.executable, '-X', 'utf8',
                                         ROOT / '.github/scripts/windows_context_investigation.py', 'plan',
                                         '--cmake-dir', self.build, '--report-dir', candidate],
                                        cwd=work, capture_output=True, text=True, encoding='utf-8')
                self.assertEqual(result.returncode, 2)
                self.assertIn('Report path must stay within the working directory', result.stderr)
                self.assertFalse(outside.exists())

    def test_foreground_cli_rejects_escape_before_touching_external_state(self):
        work = self.directory / 'work'
        work.mkdir()
        outside = self.directory / 'outside.json'
        before = b'{"original_milliseconds": 1234, "set_succeeded": true}\n'
        outside.write_bytes(before)
        environment = {key: value for key, value in os.environ.items() if key != 'GITHUB_ACTIONS'}
        for action in ('prepare', 'restore'):
            for candidate in (Path('../outside.json'), outside):
                with self.subTest(action=action, candidate=candidate):
                    result = subprocess.run([sys.executable, '-X', 'utf8',
                                             ROOT / '.github/scripts/windows_foreground.py', action,
                                             '--state-file', candidate], cwd=work, env=environment,
                                            capture_output=True, text=True, encoding='utf-8')
                    self.assertEqual(result.returncode, 2)
                    self.assertIn('Report path must stay within the working directory', result.stderr)
                    self.assertEqual(outside.read_bytes(), before)

    def test_planning_cannot_hide_terminal_windows(self):
        reports = self.directory / 'reports'
        result = subprocess.run([sys.executable, '-X', 'utf8',
                                 ROOT / '.github/scripts/windows_context_investigation.py', 'plan',
                                 '--hide-terminal', '--report-dir', reports],
                                capture_output=True, text=True, encoding='utf-8')
        self.assertEqual(result.returncode, 2)
        self.assertIn('--hide-terminal is only valid for run', result.stderr)
        self.assertFalse(reports.exists())

    def test_foreground_policy_cannot_change_during_planning_or_on_local_host(self):
        for action in ('plan', 'run'):
            with self.subTest(action=action):
                reports = self.directory / action
                result = subprocess.run([sys.executable, '-X', 'utf8',
                                         ROOT / '.github/scripts/windows_context_investigation.py', action,
                                         '--zero-foreground-timeout', '--report-dir', reports],
                                        env={key: value for key, value in os.environ.items() if key != 'GITHUB_ACTIONS'},
                                        cwd=self.directory, capture_output=True, text=True, encoding='utf-8')
                self.assertEqual(result.returncode, 2)
                self.assertIn('--zero-foreground-timeout is only valid for run on GitHub Actions', result.stderr)
                self.assertFalse(reports.exists())

    def test_shared_foreground_setup_and_restore_reject_local_host(self):
        state = self.directory / 'foreground.json'
        for action in ('prepare', 'restore'):
            with self.subTest(action=action):
                result = subprocess.run([sys.executable, '-X', 'utf8',
                                         ROOT / '.github/scripts/windows_foreground.py', action,
                                         '--state-file', state],
                                        env={key: value for key, value in os.environ.items() if key != 'GITHUB_ACTIONS'},
                                        cwd=self.directory, capture_output=True, text=True, encoding='utf-8')
                self.assertNotEqual(result.returncode, 0)
                self.assertIn('only supported on hosted Windows CI', result.stderr)
                self.assertFalse(state.exists())

    @unittest.skipUnless(sys.platform == 'win32' and os.environ.get('GITHUB_ACTIONS') == 'true',
                         'Only change the isolated hosted Windows desktop')
    def test_native_foreground_policy_restores_after_failed_operation(self):
        state = self.directory / 'foreground.json'
        observed = []

        def failed_operation():
            with windows_foreground.temporary(state):
                observed.append(json.loads(state.read_text(encoding='utf-8')))
                raise RuntimeError('owned failed operation')

        with self.assertRaisesRegex(RuntimeError, 'owned failed operation'):
            failed_operation()
        self.assertTrue(observed[0]['set_succeeded'])
        self.assertEqual(observed[0]['effective_milliseconds'], 0)
        restored = json.loads(state.read_text(encoding='utf-8'))
        self.assertTrue(restored['restore_succeeded'])
        self.assertEqual(restored['restored_milliseconds'], restored['original_milliseconds'])

    @unittest.skipUnless(sys.platform == 'win32', 'Canonical Windows desktop runner')
    def test_repetitions_use_real_ctest_and_reject_existing_evidence(self):
        self.configure()
        reports = self.directory / 'reports'
        result = self.execute(reports, 2)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        expected = ('before\n' + investigation.CONTEXT_CASE + '\n') * 2
        self.assertEqual(self.events.read_text(), expected)
        for attempt in ('run-001', 'run-002'):
            suite = ET.parse(reports / attempt / 'windows-tests.xml').getroot()
            self.assertEqual((suite.get('tests'), suite.get('failures')), ('2', '0'))
        result = self.execute(reports, 2)
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(self.events.read_text(), expected)

    @unittest.skipUnless(sys.platform == 'win32', 'Canonical Windows desktop runner')
    def test_failed_attempt_preserves_junit_and_stops_without_retry(self):
        self.configure(fail=True)
        reports = self.directory / 'reports'
        result = self.execute(reports, 3)
        self.assertEqual(result.returncode, 8, result.stdout + result.stderr)
        self.assertEqual(self.events.read_text(), 'before\n' + investigation.CONTEXT_CASE + '\n')
        self.assertFalse((reports / 'run-002').exists())
        suite = ET.parse(reports / 'run-001/windows-tests.xml').getroot()
        self.assertEqual(suite.get('failures'), '1')


if __name__ == '__main__':
    unittest.main()

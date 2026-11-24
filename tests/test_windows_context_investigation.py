"""Keep focused investigations cold-buildable, scoped and fail-preserving."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / '.github/scripts'))
import windows_context_investigation as investigation


class WindowsInvestigation(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='wxnotepad investigation ')
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name)
        self.build = self.directory / 'build'
        self.events = self.directory / 'events.txt'

    def configure(self, *, unbuilt=False, peer=True, fail=False):
        script = self.directory / 'event.py'
        script.write_text(
            'import os,sys; from pathlib import Path\n'
            'with Path(sys.argv[1]).open("a") as output: output.write(sys.argv[2] + "\\n")\n'
            'if sys.argv[2] == "original_editor_context_menu":\n'
            ' assert os.environ["WXNOTEPAD_INVESTIGATION_CONTRACT"] == "retained"\n'
            ' if sys.argv[3] == "fail": raise SystemExit(7)\n', encoding='utf-8')
        cases = ['before', investigation.CONTEXT_CASE]
        if peer:
            cases.append(investigation.PEER_CASE)
        cases.append('after')
        source = 'cmake_minimum_required(VERSION 3.25)\nproject(Investigation NONE)\nenable_testing()\n'
        for name in cases:
            if unbuilt and name == investigation.CONTEXT_CASE:
                command = f'"{(self.build / "context_menu.exe").as_posix()}"'
            else:
                command = (f'"{Path(sys.executable).as_posix()}" "{script.as_posix()}" '
                           f'"{self.events.as_posix()}" "{name}" "{"fail" if fail else "pass"}"')
            source += f'add_test(NAME {name} COMMAND {command})\n'
        source += ('set_tests_properties(original_editor_context_menu PROPERTIES TIMEOUT 60 '
                   'ENVIRONMENT "WXNOTEPAD_INVESTIGATION_CONTRACT=retained")\n')
        (self.directory / 'CMakeLists.txt').write_text(source, encoding='utf-8')
        subprocess.run(['cmake', '-S', self.directory, '-B', self.build, '-G', 'Ninja'],
                       check=True, capture_output=True)

    def execute(self, reports, runs):
        return subprocess.run([sys.executable, '-X', 'utf8',
                               ROOT / '.github/scripts/windows_context_investigation.py', 'run',
                               '--cmake-dir', self.build, '--report-dir', reports, '--runs', str(runs)],
                              capture_output=True, text=True, encoding='utf-8')

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

    def test_clipboard_selects_both_real_registered_cases_only(self):
        self.configure()
        self.assertEqual(investigation.plan(self.build, 'clipboard')['tests'],
                         [investigation.CONTEXT_CASE, investigation.PEER_CASE])

    def test_missing_peer_cannot_silently_shrink_clipboard_investigation(self):
        self.configure(peer=False)
        with self.assertRaisesRegex(SystemExit, investigation.PEER_CASE):
            investigation.plan(self.build, 'clipboard')

    def test_zero_attempts_cannot_report_an_empty_success(self):
        reports = self.directory / 'reports'
        result = self.execute(reports, 0)
        self.assertEqual(result.returncode, 2)
        self.assertIn('--runs and --jobs must be positive', result.stderr)
        self.assertFalse(reports.exists())

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

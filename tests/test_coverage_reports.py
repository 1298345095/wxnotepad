"""Check real GCC counters, unexecuted code, shared headers and collection failures."""
import importlib.util
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('collect_coverage', ROOT / '.github/scripts/collect_coverage.py')
COLLECTOR = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(COLLECTOR)


class MissingCoverage(unittest.TestCase):
    def test_cli_rejects_executable_override(self):
        result = subprocess.run([sys.executable, str(ROOT / '.github/scripts/collect_coverage.py'),
                                 '--gcov', str(Path(sys.executable))],
                                capture_output=True, text=True, encoding='utf-8')
        self.assertEqual(result.returncode, 2)
        self.assertIn('unrecognized arguments: --gcov', result.stderr)

    def test_missing_notes_fail_without_creating_a_successful_report(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            with self.assertRaisesRegex(SystemExit, 'No coverage notes'):
                COLLECTOR.collect(directory / 'build', directory / 'reports', directory)
            self.assertFalse((directory / 'reports').exists())


@unittest.skipUnless(sys.platform == 'linux' and shutil.which('g++-13') and shutil.which('gcov-13'),
                     'The coverage toolchain is Linux GCC 13')
class GCCReports(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='wxnotepad coverage 测试 ; $literal ')
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.build = self.root / 'build'
        self.build.mkdir()
        for name in ('src', 'tests'):
            (self.root / name).mkdir()
        (self.root / 'src/choice.h').write_text(
            '#pragma once\nstatic inline int Choose(bool value) {\n'
            '    if (value) return 1;\n    return 2;\n}\n', encoding='utf-8')
        (self.root / 'src/repeated.h').write_text(
            '#pragma once\nstatic inline int Repeated(bool value) {\n'
            '    if (value) return 1;\n    return 2;\n}\n', encoding='utf-8')
        for name, value, expected in (('first', 'true', 1), ('second', 'false', 2)):
            source = self.root / 'tests' / f'{name}.cpp'
            source.write_text('#include "../src/choice.h"\n#include "../src/repeated.h"\n'
                              f'int main() {{ return Choose({value}) != {expected} || Repeated(true) != 1; }}\n',
                              encoding='utf-8')
            self.compile(source, '-o', self.build / name)
        for name in ('first', 'second', 'first'):
            subprocess.run([self.build / name], check=True)
        source = self.root / 'src/never.cpp'
        source.write_text('int NeverRun() { return 42; }\n', encoding='utf-8')
        self.compile(source, '-c', '-o', self.build / 'never.o')

    def compile(self, source, *arguments):
        subprocess.run(['g++-13', '--coverage', '-O0', '-fprofile-abs-path', source, *arguments],
                       check=True, capture_output=True, text=True, encoding='utf-8')

    def test_reports_keep_independent_header_branches_and_notes_only_zero_hits(self):
        output = self.root / 'reports'
        COLLECTOR.collect(self.build, output, self.root)
        reports = []
        for path in (output / 'gcov').glob('*.gcov'):
            text = path.read_text(encoding='utf-8')
            source = Path(text.splitlines()[0].split('Source:', 1)[1].strip()).resolve()
            reports.append((source, text))
        headers = [text for source, text in reports if source == self.root / 'src/choice.h']
        self.assertEqual(len(headers), 2)
        self.assertTrue(any('taken 2' in text and 'taken 0' in text for text in headers))
        self.assertTrue(any('taken 1' in text and 'taken 0' in text for text in headers))
        untouched = [text for source, text in reports if source == self.root / 'src/never.cpp']
        self.assertEqual(len(untouched), 1)
        self.assertIn('#####', untouched[0])
        self.assertTrue(all(source.is_relative_to(self.root / 'src') for source, _ in reports))
        self.assertIn('gcov', (output / 'gcov.log').read_text(encoding='utf-8'))
        with self.assertRaises(FileExistsError):
            COLLECTOR.collect(self.build, output, self.root)

    def test_corrupt_counter_is_a_failure_not_an_uncovered_success(self):
        counter = next(self.build.glob('*first*.gcda'))
        counter.write_bytes(b'corrupt owned gcov counter')
        output = self.root / 'corrupt-report'
        with self.assertRaisesRegex(SystemExit, 'gcov failed'):
            COLLECTOR.collect(self.build, output, self.root)
        self.assertFalse((output / 'summary.json').exists())

    @unittest.skipUnless(importlib.util.find_spec('gcovr'), 'Install the pinned coverage requirements')
    def test_sonar_report_merges_headers_and_preserves_source_conditions(self):
        exception_source = self.root / 'src/exception_paths.cpp'
        exception_source.write_text(
            'int MayThrow(bool value) {\n'
            '    if (!value) throw 7;\n'
            '    return 1;\n}\n'
            'int Handle(bool value) {\n'
            '    try {\n'
            '        return MayThrow(value);\n'
            '    } catch (int) {\n'
            '        return 2;\n    }\n}\n', encoding='utf-8')
        driver = self.root / 'tests/exception_main.cpp'
        driver.write_text('int Handle(bool);\nint main() { return Handle(true) != 1; }\n', encoding='utf-8')
        executable = self.build / 'exception_driver'
        self.compile(exception_source, driver, '-o', executable)
        subprocess.run([executable], check=True)
        output = self.root / 'reports'
        collector = self.root / '.github/scripts/collect_coverage.py'
        collector.parent.mkdir(parents=True)
        shutil.copy2(ROOT / '.github/scripts/collect_coverage.py', collector)
        subprocess.run([sys.executable, str(collector), '--build-dir', str(self.build),
                        '--output', str(output)], check=True, capture_output=True,
                       text=True, encoding='utf-8')
        files = {item.attrib['path']: item for item in ET.parse(output / 'sonar.xml').getroot()}
        choice = files['src/choice.h'].find("lineToCover[@lineNumber='3']")
        self.assertEqual(choice.attrib['branchesToCover'], '2')
        self.assertEqual(choice.attrib['coveredBranches'], '2')
        repeated = files['src/repeated.h'].find("lineToCover[@lineNumber='3']")
        self.assertEqual(repeated.attrib['branchesToCover'], '2')
        self.assertEqual(repeated.attrib['coveredBranches'], '1')
        untouched = files['src/never.cpp'].find("lineToCover[@lineNumber='1']")
        self.assertEqual(untouched.attrib['covered'], 'false')
        exceptions = files['src/exception_paths.cpp']
        condition = exceptions.find("lineToCover[@lineNumber='2']")
        self.assertEqual(condition.attrib['branchesToCover'], '2')
        self.assertEqual(condition.attrib['coveredBranches'], '1')
        call = exceptions.find("lineToCover[@lineNumber='7']")
        self.assertEqual(call.attrib['branchesToCover'], '1')
        self.assertEqual(call.attrib['coveredBranches'], '1')
        handler = exceptions.find("lineToCover[@lineNumber='9']")
        self.assertEqual(handler.attrib['covered'], 'false')
        self.assertTrue(all(name.startswith('src/') for name in files))
        self.assertTrue(list((output / 'gcov').glob('*.gcov')))


if __name__ == '__main__':
    unittest.main()

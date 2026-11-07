"""Check real GCC counters, unexecuted code, shared headers and collection failures."""
import importlib.util
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('collect_coverage', ROOT / '.github/scripts/collect_coverage.py')
COLLECTOR = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(COLLECTOR)


class MissingCoverage(unittest.TestCase):
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
        self.temporary = tempfile.TemporaryDirectory(prefix='wxnotepad coverage 测试 ')
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.build = self.root / 'build'
        self.build.mkdir()
        for name in ('src', 'tests'):
            (self.root / name).mkdir()
        (self.root / 'src/choice.h').write_text(
            '#pragma once\nstatic inline int Choose(bool value) {\n'
            '    if (value) return 1;\n    return 2;\n}\n', encoding='utf-8')
        for name, value, expected in (('first', 'true', 1), ('second', 'false', 2)):
            source = self.root / 'tests' / f'{name}.cpp'
            source.write_text('#include "../src/choice.h"\n'
                              f'int main() {{ return Choose({value}) != {expected}; }}\n', encoding='utf-8')
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


if __name__ == '__main__':
    unittest.main()

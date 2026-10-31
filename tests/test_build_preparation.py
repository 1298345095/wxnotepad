"""Exercise pinned-source preparation against real Git archives and local config."""
import hashlib
import importlib.util
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('core_build', ROOT / 'tools/build.py')
BUILD = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(BUILD)


class PreparationTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='wxnotepad-prepare-test-')
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.source = self.root / 'source'
        self.source.mkdir()
        self.git('init')
        for key, value in [('core.autocrlf', 'false'), ('core.eol', 'lf'),
                           ('user.name', 'Preparation fixture'), ('user.email', 'fixture@example.invalid')]:
            self.git('config', key, value)
        (self.source / '.gitattributes').write_bytes(b'* text=auto\n')
        (self.source / 'scintilla').mkdir()
        (self.source / 'scintilla/fixture.txt').write_bytes(b'before\n')
        (self.source / 'License.txt').write_bytes(b'Fixture license\n')
        self.git('add', '.')
        tree = self.git('write-tree').strip()
        self.revision = self.git('commit-tree', '--no-gpg-sign', tree, '-m', 'Create preparation fixture').strip()
        self.reference = self.root / 'reference.tar'
        self.git('archive', '--format=tar', f'--output={self.reference}', self.revision,
                 'scintilla', 'License.txt')
        patches = self.root / 'patches'
        patches.mkdir()
        (patches / 'source.sha256').write_text(
            hashlib.sha256(self.reference.read_bytes()).hexdigest() + '\n', encoding='utf-8')
        (patches / 'original-core.patch').write_text(
            '--- a/scintilla/fixture.txt\n+++ b/scintilla/fixture.txt\n'
            '@@ -1 +1 @@\n-before\n+after\n', encoding='utf-8', newline='\n')
        self.enterContext(patch.object(BUILD, 'ROOT', self.root))
        self.enterContext(patch.object(BUILD, 'REVISION', self.revision))

    def git(self, *arguments):
        result = subprocess.run(['git', '-C', str(self.source), *arguments],
                                capture_output=True, text=True, check=True)
        return result.stdout

    def test_local_line_ending_settings_preserve_verified_archive(self):
        cases = [('true', 'lf'), ('false', 'crlf'), ('input', 'native')]
        for index, (autocrlf, eol) in enumerate(cases):
            with self.subTest(autocrlf=autocrlf, eol=eol):
                self.git('config', 'core.autocrlf', autocrlf)
                self.git('config', 'core.eol', eol)
                destination = self.root / f'build-{index}'
                destination.mkdir()
                staged = BUILD.prepare(destination, self.source, None)
                self.assertEqual((destination / 'notepad4-source.tar').read_bytes(), self.reference.read_bytes())
                self.assertEqual((staged / 'scintilla/fixture.txt').read_bytes(), b'after\n')
                self.assertEqual(self.git('config', 'core.autocrlf').strip(), autocrlf)
                self.assertEqual(self.git('config', 'core.eol').strip(), eol)

    def test_corrupt_supplied_archive_is_rejected(self):
        corrupt = self.root / 'corrupt.tar'
        corrupt.write_bytes(self.reference.read_bytes() + b'corrupted')
        with self.assertRaisesRegex(RuntimeError, 'SHA-256 does not match'):
            BUILD.prepare(self.root / 'bad-build', None, corrupt)


if __name__ == '__main__':
    unittest.main()

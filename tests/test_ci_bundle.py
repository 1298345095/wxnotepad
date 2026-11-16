"""Check distribution contents and native symbol separation through CI's bundler."""
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('ci_bundle', ROOT / '.github/scripts/ci.py')
CI = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(CI)


class DistributionBundle(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix='wxnotepad bundle ')
        self.addCleanup(temporary.cleanup)
        self.work = Path(temporary.name)
        self.build = self.work / 'build'
        self.directory = self.build / 'cmake'
        self.directory.mkdir(parents=True)
        self.reports = self.work / 'reports'
        self.reports.mkdir()
        self.vcpkg = self.work / 'vcpkg'
        source = self.build / 'original-core/wxnotepad-source.json'
        source.parent.mkdir()
        source.write_text('{"revision": "bundle-fixture"}', encoding='utf-8')
        self.catalogs = {}
        languages = json.loads((ROOT / 'po/languages.json').read_text(encoding='utf-8'))
        for language in languages['languages']:
            if language['code'] != languages['source_language']:
                for domain in (languages['domain'], 'wxstd'):
                    relative = Path(language['code']) / 'LC_MESSAGES' / (domain + '.mo')
                    content = f'catalog fixture: {language["code"]}/{domain}'.encode('utf-8')
                    self.catalogs[relative] = content
                    target = self.directory / 'locale' / relative
                    target.parent.mkdir(parents=True, exist_ok=True)
                    target.write_bytes(content)
        paths = mock.patch.multiple(CI, WORK=self.work, BUILD=self.build, REPORTS=self.reports,
                                    VCPKG=self.vcpkg)
        self.addCleanup(paths.stop)
        paths.start()

    def check_contents(self, executable, triplet):
        destination = self.work / 'artifact'
        for relative, content in self.catalogs.items():
            self.assertEqual((destination / 'locale' / relative).read_bytes(), content)
        self.assertFalse((destination / 'locale/en').exists())
        for notice in (ROOT / 'licenses').iterdir():
            if notice.is_file():
                self.assertEqual((destination / 'licenses' / notice.name).read_bytes(), notice.read_bytes())
        record = json.loads((destination / 'build.json').read_text(encoding='utf-8'))
        self.assertEqual(record['revision'], 'bundle-fixture')
        self.assertEqual(record['triplet'], triplet)
        self.assertEqual(record['application_commit'], subprocess.check_output(
            ['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip())
        self.assertEqual(record['executable_sha256'], hashlib.sha256(
            (destination / executable).read_bytes()).hexdigest())
        self.assertTrue((destination / 'README.txt').is_file())
        return record

    def test_windows_bundle_retains_executable_dlls_and_catalogs(self):
        (self.directory / 'wxnotepad.exe').write_bytes(b'owned executable fixture')
        (self.directory / 'retained.dll').write_bytes(b'owned runtime fixture')
        notice = self.vcpkg / 'installed/x64-windows-ci/share/retained/copyright'
        notice.parent.mkdir(parents=True)
        notice.write_bytes(b'owned dependency notice')
        with mock.patch.multiple(CI, WINDOWS=True, TRIPLET='x64-windows-ci'):
            CI.bundle()
        destination = self.work / 'artifact'
        self.assertEqual((destination / 'wxnotepad.exe').read_bytes(), b'owned executable fixture')
        self.assertEqual((destination / 'retained.dll').read_bytes(), b'owned runtime fixture')
        self.assertEqual((destination / 'licenses/vcpkg/retained/copyright').read_bytes(), notice.read_bytes())
        record = self.check_contents('wxnotepad.exe', 'x64-windows-ci')
        self.assertNotIn('unstripped_executable_sha256', record)

    @unittest.skipUnless(sys.platform == 'linux' and all(shutil.which(tool) for tool in
                         ('g++', 'objcopy', 'strip', 'readelf', 'ldd')), 'Requires the Linux native binary tools')
    def test_linux_bundle_preserves_originals_and_matching_debug_symbols(self):
        library = self.vcpkg / 'installed/x64-linux-ci/lib/libbundle_fixture.so'
        library.parent.mkdir(parents=True)
        library_source = self.work / 'library.cpp'
        library_source.write_text('extern "C" int bundle_fixture() { return 0; }\n', encoding='utf-8')
        source = self.work / 'main.cpp'
        source.write_text('extern "C" int bundle_fixture();\n'
                          'int main() { return bundle_fixture(); }\n', encoding='utf-8')
        executable = self.directory / 'wxnotepad'
        subprocess.run(['g++', '-g', '-fPIC', '-shared', library_source, '-o', library], check=True)
        subprocess.run(['g++', '-g', source, '-L' + str(library.parent), '-lbundle_fixture',
                        '-Wl,-rpath,' + str(library.parent), '-o', executable], check=True)
        originals = {path: path.read_bytes() for path in (executable, library)}
        with mock.patch.multiple(CI, WINDOWS=False, TRIPLET='x64-linux-ci'):
            CI.bundle()
        destination = self.work / 'artifact'
        self.assertEqual({path.name for path in (destination / 'lib').iterdir()}, {library.name})
        for original, relative in ((executable, Path('wxnotepad')), (library, Path('lib') / library.name)):
            self.assertEqual(original.read_bytes(), originals[original])
            binary = destination / relative
            debug = self.work / 'symbols' / relative.parent / '.debug' / (relative.name + '.debug')
            sections = subprocess.check_output(['readelf', '--sections', binary], text=True)
            self.assertNotIn('.debug_info', sections)
            self.assertIn('.gnu_debuglink', sections)
            self.assertIn('.debug_info', subprocess.check_output(
                ['readelf', '--sections', debug], text=True, stderr=subprocess.PIPE))
        record = self.check_contents('wxnotepad', 'x64-linux-ci')
        self.assertEqual(record['unstripped_executable_sha256'], hashlib.sha256(originals[executable]).hexdigest())
        self.assertEqual((destination / 'build.json').read_bytes(), (self.work / 'symbols/build.json').read_bytes())
        with mock.patch.dict(os.environ, LD_LIBRARY_PATH=str(destination / 'lib')):
            self.assertEqual(CI.probe([destination / 'wxnotepad'], self.work / 'probe', 10, 1, destination), 0)


if __name__ == '__main__':
    unittest.main()

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
        self.work = Path(temporary.name).resolve()
        self.cmake = self.work / 'configured tree'
        self.cmake.mkdir()
        self.directory = self.work / 'runtime files/Release'
        self.directory.mkdir(parents=True)
        self.reports = self.work / 'bundle reports'
        self.installed = self.work / 'sdk packages'
        self.installed.mkdir()
        self.triplet = 'fixture-sdk'
        source = self.work / 'prepared original/wxnotepad-source.json'
        source.parent.mkdir()
        source.write_text('{"revision": "bundle-fixture"}', encoding='utf-8')
        (self.cmake / 'CMakeCache.txt').write_text(
            f'CMAKE_HOME_DIRECTORY:INTERNAL={ROOT}\n'
            f'NOTEPAD4_SOURCE:PATH={source.parent}\n'
            f'VCPKG_INSTALLED_DIR:PATH={self.installed}\n'
            f'VCPKG_TARGET_TRIPLET:STRING={self.triplet}\n', encoding='utf-8')
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
        executable = self.directory / 'wxnotepad.exe'
        executable.write_bytes(b'owned executable fixture')
        (self.directory / 'retained.dll').write_bytes(b'owned runtime fixture')
        notice = self.installed / self.triplet / 'share/retained/copyright'
        notice.parent.mkdir(parents=True)
        notice.write_bytes(b'owned dependency notice')
        packaged = CI.bundle(self.cmake, executable, self.work)
        destination = self.work / 'artifact'
        self.assertEqual(packaged, destination / 'wxnotepad.exe')
        self.assertEqual((destination / 'wxnotepad.exe').read_bytes(), b'owned executable fixture')
        self.assertEqual((destination / 'retained.dll').read_bytes(), b'owned runtime fixture')
        self.assertEqual((destination / 'licenses/vcpkg/retained/copyright').read_bytes(), notice.read_bytes())
        record = self.check_contents('wxnotepad.exe', self.triplet)
        self.assertNotIn('unstripped_executable_sha256', record)

    @unittest.skipUnless(sys.platform == 'linux' and all(shutil.which(tool) for tool in
                         ('g++', 'objcopy', 'strip', 'readelf', 'ldd')), 'Requires the Linux native binary tools')
    def test_linux_bundle_preserves_originals_and_matching_debug_symbols(self):
        library = self.installed / self.triplet / 'lib/libbundle_fixture.so'
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
        helper = self.installed / self.triplet / 'libexec/gio-launch-desktop'
        helper.parent.mkdir()
        helper_library = library.with_name('libhelper_fixture.so')
        subprocess.run(['g++', '-g', '-fPIC', '-shared', library_source, '-o', helper_library], check=True)
        subprocess.run(['g++', '-g', source, '-L' + str(library.parent), '-lhelper_fixture',
                        '-Wl,-rpath,' + str(library.parent), '-o', helper], check=True)
        originals = {path: path.read_bytes() for path in (executable, library, helper, helper_library)}
        packaged = CI.bundle(self.cmake, executable, self.work, report_dir=self.reports)
        destination = self.work / 'artifact'
        self.assertEqual(packaged, destination / 'wxnotepad')
        self.assertTrue((self.reports / 'runtime-libraries.txt').is_file())
        self.assertEqual({path.name for path in (destination / 'lib').iterdir()}, {library.name, helper_library.name})
        for original, relative in ((executable, Path('wxnotepad')), (library, Path('lib') / library.name),
                                   (helper_library, Path('lib') / helper_library.name),
                                   (helper, Path('libexec/gio-launch-desktop'))):
            self.assertEqual(original.read_bytes(), originals[original])
            binary = destination / relative
            debug = self.work / 'symbols' / relative.parent / '.debug' / (relative.name + '.debug')
            sections = subprocess.check_output(['readelf', '--sections', binary], text=True)
            self.assertNotIn('.debug_info', sections)
            self.assertIn('.gnu_debuglink', sections)
            self.assertIn('.debug_info', subprocess.check_output(
                ['readelf', '--sections', debug], text=True, stderr=subprocess.PIPE))
        record = self.check_contents('wxnotepad', self.triplet)
        self.assertEqual(record['unstripped_executable_sha256'], hashlib.sha256(originals[executable]).hexdigest())
        self.assertEqual((destination / 'build.json').read_bytes(), (self.work / 'symbols/build.json').read_bytes())
        with mock.patch.dict(os.environ, LD_LIBRARY_PATH=str(destination / 'lib')):
            self.assertEqual(CI.probe([destination / 'wxnotepad'], self.work / 'probe', 10, 1, destination), 0)
            self.assertEqual(subprocess.run([destination / 'libexec/gio-launch-desktop']).returncode, 0)
        self.assertTrue((self.reports / 'gio-launch-desktop-libraries.txt').is_file())

    @unittest.skipUnless(sys.platform == 'linux' and all(shutil.which(tool) for tool in
                         ('g++', 'objcopy', 'strip', 'ldd')), 'Requires the Linux native binary tools')
    def test_cli_accepts_spaced_input_paths_and_separate_reports(self):
        source = self.work / 'cli main.cpp'
        source.write_text('int main() { return 0; }\n', encoding='utf-8')
        executable = self.directory / 'wxnotepad'
        subprocess.run(['g++', '-g', source, '-o', executable], check=True)
        helper = self.installed / self.triplet / 'libexec/gio-launch-desktop'
        helper.parent.mkdir(parents=True)
        shutil.copy2(executable, helper)
        original = executable.read_bytes()
        output = self.work / 'cli bundle output'
        reports = self.work / 'retained cli reports'
        working = self.work / 'unrelated working directory'
        working.mkdir()
        result = subprocess.run(
            [sys.executable, '-X', 'utf8', ROOT / '.github/scripts/ci.py', 'bundle',
             '--cmake-dir', self.cmake, '--executable', executable,
             '--output-dir', output, '--report-dir', reports],
            cwd=working, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertTrue((output / 'artifact/wxnotepad').is_file())
        self.assertTrue((output / 'symbols/.debug/wxnotepad.debug').is_file())
        self.assertIn('libc.so', (reports / 'runtime-libraries.txt').read_text(encoding='utf-8'))
        self.assertFalse((output / 'reports').exists())
        self.assertEqual(executable.read_bytes(), original)

    def test_missing_required_catalog_failure_reaches_caller_and_cli(self):
        executable = self.directory / 'wxnotepad.exe'
        executable.write_bytes(b'owned executable fixture')
        missing = self.directory / 'locale' / next(iter(self.catalogs))
        missing.unlink()
        with self.assertRaises(FileNotFoundError):
            CI.bundle(self.cmake, executable, self.work)
        self.assertFalse((self.work / 'artifact/build.json').exists())
        output = self.work / 'failed cli bundle'
        result = subprocess.run(
            [sys.executable, '-X', 'utf8', ROOT / '.github/scripts/ci.py', 'bundle',
             '--cmake-dir', self.cmake, '--executable', executable,
             '--output-dir', output],
            capture_output=True, text=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('FileNotFoundError', result.stderr)
        self.assertFalse((output / 'artifact/build.json').exists())


if __name__ == '__main__':
    unittest.main()

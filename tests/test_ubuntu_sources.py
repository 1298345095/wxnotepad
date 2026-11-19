"""Run CI's Ubuntu source normalization against owned APT source files."""
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
NORMALIZE = ROOT / '.github/scripts/normalize-ubuntu-sources.sh'


@unittest.skipUnless(sys.platform == 'linux' and shutil.which('bash'), 'Linux APT provisioning')
class UbuntuSources(unittest.TestCase):
    @unittest.skipUnless(shutil.which('apt-get'), 'APT request planning requires apt-get')
    def test_apt_plans_only_official_https_requests_for_runner_sources(self):
        with tempfile.TemporaryDirectory(prefix='wxnotepad apt requests ') as folder:
            directory = Path(folder)
            sources = directory / 'ubuntu.sources'
            sources.write_text(
                'Types: deb\nURIs: mirror+file:/etc/apt/apt-mirrors.txt\n'
                'Suites: noble noble-updates noble-backports noble-security\n'
                'Components: main universe restricted multiverse\nArchitectures: amd64\n'
                'Signed-By: /usr/share/keyrings/ubuntu-archive-keyring.gpg\n', encoding='utf-8')
            subprocess.run(['bash', NORMALIZE.as_posix(), sources.as_posix()], check=True)
            lists = directory / 'lists'
            (lists / 'partial').mkdir(parents=True)
            result = subprocess.run(
                ['apt-get', '--print-uris', '-o', f'Dir::Etc::sourcelist={sources}',
                 '-o', 'Dir::Etc::sourceparts=-', '-o', f'Dir::State::lists={lists}', 'update'],
                check=True, capture_output=True, text=True, encoding='utf-8')
            uris = {shlex.split(line)[0] for line in result.stdout.splitlines() if line.startswith("'")}
            self.assertTrue(uris, result.stdout + result.stderr)
            self.assertTrue(all(uri.startswith('https://archive.ubuntu.com/ubuntu/') for uri in uris), uris)
            self.assertTrue({f'https://archive.ubuntu.com/ubuntu/dists/{suite}/InRelease'
                             for suite in ('noble', 'noble-updates', 'noble-backports', 'noble-security')}
                            .issubset(uris), uris)

    def test_runner_mirror_file_uses_official_https_in_both_source_formats(self):
        with tempfile.TemporaryDirectory(prefix='wxnotepad apt sources ') as folder:
            directory = Path(folder)
            legacy = directory / 'sources.list'
            deb822 = directory / 'ubuntu.sources'
            mirror = 'mirror+file:/etc/apt/apt-mirrors.txt'
            legacy.write_text(f'deb {mirror} noble main universe\n', encoding='utf-8')
            fields = ('Types: deb\nURIs: {uri}\n'
                      'Suites: noble noble-updates noble-backports noble-security\n'
                      'Components: main universe restricted multiverse\n'
                      'Signed-By: /usr/share/keyrings/ubuntu-archive-keyring.gpg\n')
            deb822.write_text(fields.format(uri=mirror), encoding='utf-8')

            subprocess.run(['bash', NORMALIZE.as_posix(), legacy.as_posix(), deb822.as_posix()], check=True)

            official = 'https://archive.ubuntu.com/ubuntu'
            for source, expected in ((legacy, f'deb {official} noble main universe\n'),
                                     (deb822, fields.format(uri=official))):
                with self.subTest(format=source.name):
                    self.assertEqual(source.read_text(encoding='utf-8'), expected)

    def test_azure_sources_preserve_options_and_unrelated_repositories(self):
        with tempfile.TemporaryDirectory(prefix='wxnotepad apt sources ') as folder:
            directory = Path(folder)
            legacy = directory / 'sources.list'
            deb822 = directory / 'ubuntu.sources'
            legacy.write_text(
                'deb [arch=amd64 signed-by=/usr/share/keyrings/ubuntu-archive-keyring.gpg] '
                'http://azure.archive.ubuntu.com/ubuntu noble main restricted\n'
                'deb https://security.ubuntu.com/ubuntu noble-security main restricted\n'
                'deb [signed-by=/etc/apt/keyrings/vendor.gpg] https://vendor.example/ubuntu noble main\n',
                encoding='utf-8')
            deb822.write_text(
                'Types: deb deb-src\nURIs: https://azure.archive.ubuntu.com/ubuntu/\n'
                'Suites: noble noble-updates\nComponents: main universe\nArchitectures: amd64\n'
                'Signed-By: /usr/share/keyrings/ubuntu-archive-keyring.gpg\n\n'
                'Types: deb\nURIs: mirror+file:/etc/apt/vendor-mirrors.txt\n'
                'Suites: stable\nComponents: main\nSigned-By: /etc/apt/keyrings/vendor.gpg\n', encoding='utf-8')
            expected_legacy = (
                'deb [arch=amd64 signed-by=/usr/share/keyrings/ubuntu-archive-keyring.gpg] '
                'https://archive.ubuntu.com/ubuntu noble main restricted\n'
                'deb https://security.ubuntu.com/ubuntu noble-security main restricted\n'
                'deb [signed-by=/etc/apt/keyrings/vendor.gpg] https://vendor.example/ubuntu noble main\n')
            expected_deb822 = (
                'Types: deb deb-src\nURIs: https://archive.ubuntu.com/ubuntu/\n'
                'Suites: noble noble-updates\nComponents: main universe\nArchitectures: amd64\n'
                'Signed-By: /usr/share/keyrings/ubuntu-archive-keyring.gpg\n\n'
                'Types: deb\nURIs: mirror+file:/etc/apt/vendor-mirrors.txt\n'
                'Suites: stable\nComponents: main\nSigned-By: /etc/apt/keyrings/vendor.gpg\n')

            command = ['bash', NORMALIZE.as_posix(), legacy.as_posix(), deb822.as_posix(),
                       (directory / 'missing.sources').as_posix()]
            for attempt in range(2):
                subprocess.run(command, check=True)
                with self.subTest(attempt=attempt):
                    self.assertEqual(legacy.read_text(encoding='utf-8'), expected_legacy)
                    self.assertEqual(deb822.read_text(encoding='utf-8'), expected_deb822)


if __name__ == '__main__':
    unittest.main()

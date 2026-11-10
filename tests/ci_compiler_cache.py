"""Exercise real compiler-cache hits, header invalidation and explicit analysis bypass."""
import importlib.util
import json
import os
from pathlib import Path
import shutil
import socket
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('ci', ROOT / '.github/scripts/ci.py')
CI = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(CI)


class CompilerCache(unittest.TestCase):
    def test_clean_rebuild_hits_header_change_misses_and_analysis_bypasses(self):
        executable = shutil.which('sccache')
        self.assertIsNotNone(executable, 'Install the pinned sccache tool before running this integration test')
        reports = ROOT / '.ci/reports/compiler-cache-probe'
        reports.mkdir(parents=True, exist_ok=True)
        directory = Path(tempfile.mkdtemp(prefix='native cache ', dir=reports))
        source = directory / 'source'
        build = directory / 'build'
        source.mkdir()
        (source / 'CMakeLists.txt').write_text(
            'cmake_minimum_required(VERSION 3.25)\nproject(CacheProbe LANGUAGES CXX)\n'
            'add_executable(probe main.cpp)\n'
            'if(MSVC)\n  target_compile_options(probe PRIVATE /utf-8)\nendif()\n', encoding='utf-8')
        (source / 'main.cpp').write_text('#include "value.h"\nint main() { return VALUE; }\n', encoding='utf-8')
        header = source / 'value.h'
        header.write_text('#define VALUE 11\n', encoding='utf-8')
        configuration = directory / 'sccache.toml'
        configuration.write_text('', encoding='utf-8')
        with socket.socket() as listener:
            listener.bind(('127.0.0.1', 0))
            port = listener.getsockname()[1]
        environment = dict(os.environ, SCCACHE_DIR=str(directory / 'cache'), SCCACHE_CONF=str(configuration),
                           SCCACHE_SERVER_PORT=str(port), SCCACHE_GHA_ENABLED='off')
        # sccache 0.19 enables GHA whenever VERSION exists, overriding ENABLED=off.
        # Keep this cold-cache probe local even after earlier CI runs warmed GHA.
        environment.pop('SCCACHE_GHA_VERSION', None)
        environment.pop('SCCACHE_DISABLE', None)

        def command(arguments, label, *, check=True):
            result = subprocess.run([str(item) for item in arguments], env=environment, capture_output=True,
                                    text=True, encoding='utf-8', errors='replace')
            (directory / f'{label}.log').write_text(result.stdout + result.stderr, encoding='utf-8')
            if check:
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            return result

        def cmake(arguments, label):
            return command(CI.cmake_command(arguments), label)

        def stats(label):
            result = command([executable, '--show-stats', '--stats-format=json'], label)
            return json.loads(result.stdout)['stats']

        def count(statistics, name):
            return sum(statistics[name]['counts'].values())

        def configure(enabled, label):
            result = command(CI.cmake_command(
                ['-S', source, '-B', build, '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release',
                 *CI.compiler_launcher_arguments(enabled)]), label, check=False)
            if CI.WINDOWS and enabled and 'sccache requires English MSVC diagnostic resources' in result.stderr:
                self.skipTest('Cached compilation correctly rejected: English MSVC resources missing (sccache#2864)')
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

        binary = build / ('probe.exe' if CI.WINDOWS else 'probe')
        command([executable, '--start-server'], 'start-server')
        try:
            configure(True, 'configure-cached')
            cmake(['--build', build], 'cold-build')
            self.assertEqual(subprocess.run([binary]).returncode, 11)
            cold = stats('cold-stats')
            self.assertGreater(count(cold, 'cache_misses'), 0)
            cmake(['--build', build, '--target', 'clean'], 'clean')
            cmake(['--build', build], 'warm-build')
            self.assertEqual(subprocess.run([binary]).returncode, 11)
            warm = stats('warm-stats')
            self.assertGreater(count(warm, 'cache_hits'), count(cold, 'cache_hits'))
            header.write_text('#define VALUE 22\n', encoding='utf-8')
            cmake(['--build', build], 'header-build')
            self.assertEqual(subprocess.run([binary]).returncode, 22)
            changed = stats('header-stats')
            self.assertGreater(count(changed, 'cache_misses'), count(warm, 'cache_misses'))
            # Clear both a previously cached launcher and an inherited launcher default.
            environment['CMAKE_CXX_COMPILER_LAUNCHER'] = 'invalid-inherited-launcher'
            configure(False, 'configure-analysis')
            cmake(['--build', build, '--target', 'clean'], 'clean-analysis')
            cmake(['--build', build], 'analysis-build')
            self.assertEqual(subprocess.run([binary]).returncode, 22)
            uncached = stats('analysis-stats')
            self.assertEqual(uncached['compile_requests'], changed['compile_requests'])
            (source / 'main.cpp').write_text('#error deliberate compiler failure\n', encoding='utf-8')
            configure(True, 'configure-failure')
            result = command(CI.cmake_command(['--build', build]), 'failed-build', check=False)
            self.assertNotEqual(result.returncode, 0, 'cache must not hide a new compiler error')
            print(f'Compiler-cache integration evidence: {directory}', flush=True)
        finally:
            command([executable, '--stop-server'], 'stop-server', check=False)


if __name__ == '__main__':
    unittest.main()

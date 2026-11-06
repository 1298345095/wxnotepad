"""Small CI glue around the existing pinned-core preparation and CMake entry points."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
WORK = ROOT / '.ci'
REPORTS = WORK / 'reports'
VCPKG = WORK / 'vcpkg'
BUILD = WORK / 'build'
WINDOWS = sys.platform == 'win32'
TRIPLET = 'x64-windows-ci' if WINDOWS else 'x64-linux-ci'


def run(command, log, env=None):
    REPORTS.mkdir(parents=True, exist_ok=True)
    print('+ ' + ' '.join(str(item) for item in command), flush=True)
    with (REPORTS / log).open('w', encoding='utf-8') as output:
        process = subprocess.Popen([str(item) for item in command], cwd=ROOT, env=env,
                                   stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                   text=True, encoding='utf-8', errors='replace')
        for line in process.stdout:
            output.write(line)
            print(line, end='', flush=True)
        result = process.wait()
    if result:
        raise SystemExit(result)


def metadata():
    WORK.mkdir(exist_ok=True)
    manifest = json.loads((ROOT / 'vcpkg.json').read_text(encoding='utf-8'))
    image = os.environ.get('ImageVersion', 'local')
    output = f'baseline={manifest["builtin-baseline"]}\nimage={image}\n'
    with open(os.environ['GITHUB_OUTPUT'], 'a', encoding='utf-8') as stream:
        stream.write(output)
    print(output, end='')


def dependencies():
    environment = dict(os.environ)
    environment['VCPKG_ROOT'] = str(VCPKG)
    for name in ('downloads', 'binaries'):
        (WORK / 'cache' / name).mkdir(parents=True, exist_ok=True)
    environment['VCPKG_DOWNLOADS'] = str(WORK / 'cache/downloads')
    environment['VCPKG_BINARY_SOURCES'] = f'clear;files,{WORK / "cache/binaries"},readwrite'
    executable = VCPKG / ('vcpkg.exe' if WINDOWS else 'vcpkg')
    run([executable, 'install', '--triplet', TRIPLET, f'--overlay-triplets={ROOT / ".github/triplets"}',
         f'--x-install-root={VCPKG / "installed"}', '--clean-after-build'],
        'dependencies.log', environment)


def configure(app_only):
    if not WINDOWS:
        header = VCPKG / 'installed' / TRIPLET / 'include/gtk-3.0/gdk/gdkconfig.h'
        configuration = header.read_text(encoding='utf-8')
        backends = {name.lower(): bool(re.search(r'^\s*#\s*define\s+GDK_WINDOWING_' + name + r'\b',
                                                configuration, re.MULTILINE))
                    for name in ('X11', 'WAYLAND')}
        REPORTS.mkdir(parents=True, exist_ok=True)
        (REPORTS / 'gtk-backends.json').write_text(json.dumps(backends, indent=2) + '\n', encoding='utf-8')
        if not all(backends.values()):
            raise SystemExit('The CI GTK3 dependency must support X11 and Wayland; enable gtk3[wayland]')
    run([sys.executable, '-X', 'utf8', ROOT / 'tools/build.py', '--prepare-only',
         '--build-dir', BUILD], 'prepare.log')
    command = cmake_command(['-S', ROOT, '-B', BUILD / 'cmake',
               f'-DNOTEPAD4_SOURCE={BUILD / "original-core"}',
               f'-DCMAKE_TOOLCHAIN_FILE={VCPKG / "scripts/buildsystems/vcpkg.cmake"}',
               f'-DVCPKG_INSTALLED_DIR={VCPKG / "installed"}',
               f'-DVCPKG_TARGET_TRIPLET={TRIPLET}', '-DVCPKG_MANIFEST_MODE=OFF',
               f'-DVCPKG_OVERLAY_TRIPLETS={ROOT / ".github/triplets"}',
               '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON', '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release',
               '-DBUILD_TESTING=' + ('OFF' if app_only else 'ON')])
    run(command, 'configure.log')


def cmake_command(arguments):
    arguments = [str(item) for item in arguments]
    if WINDOWS:
        # pwsh -File splits named arguments such as -DKEY=D:/path at the colon.
        # Pass the array as one value and splat it only inside the script.
        return ['pwsh', '-NoProfile', '-File', ROOT / '.github/scripts/msvc.ps1',
                '-CMakeArgumentsJson', json.dumps(arguments)]
    return ['cmake', *arguments]


def build(app_only, jobs):
    print(f'Build workers: {jobs}', flush=True)
    command = ['--build', BUILD / 'cmake', '--config', 'Release', '--parallel', str(jobs)]
    if app_only:
        command += ['--target', 'wxnotepad']
    run(cmake_command(command), 'build.log')


def test_shards(cmake_dir, expression, jobs, directory):
    listing = ['ctest', '--test-dir', str(cmake_dir), '-C', 'Release', '--show-only=json-v1']
    if expression:
        listing += ['-R', expression]
    tests = json.loads(subprocess.check_output(listing, text=True, encoding='utf-8'))['tests']
    if not tests:
        raise SystemExit('No CTest tests match the requested selection')

    def literal(value):
        value = str(value)
        delimiter = '='
        while f']{delimiter}]' in value:
            delimiter += '='
        return f'[{delimiter}[{value}]{delimiter}]'

    # Separate CTest workspaces keep cost data, LastTest logs and JUnit output
    # independent, while the registered commands and working directories stay intact.
    shards = []
    for index in range(min(jobs, len(tests))):
        selected = tests[index::min(jobs, len(tests))]
        workspace = directory / f'shard-{index + 1}'
        workspace.mkdir(parents=True, exist_ok=True)
        registration = f'include({literal((cmake_dir / "CTestTestfile.cmake").as_posix())})\n'
        for item in selected:
            properties = {entry['name']: entry['value'] for entry in item.get('properties', [])}
            working = properties.get('WORKING_DIRECTORY', str(cmake_dir))
            registration += (f'set_tests_properties({literal(item["name"])} PROPERTIES '
                             f'WORKING_DIRECTORY {literal(working)})\n')
        (workspace / 'CTestTestfile.cmake').write_text(registration, encoding='utf-8')
        shards.append((workspace, [item['name'] for item in selected]))
    return shards


def test(backend, cmake_dir, expression, jobs):
    if (backend == 'windows') != WINDOWS:
        raise SystemExit('The requested test backend does not match this host')
    REPORTS.mkdir(parents=True, exist_ok=True)
    command = ['ctest', '--test-dir', cmake_dir, '-C', 'Release', '--parallel', '1',
               '--output-on-failure', '--no-tests=error',
               '--output-junit', REPORTS / f'{backend}-tests.xml']
    if expression:
        command += ['-R', expression]
    if WINDOWS:
        if jobs != 1:
            raise SystemExit('Windows native tests share one desktop and clipboard; use --jobs 1')
        run(command, f'{backend}-ctest.log')
        return

    shards = test_shards(cmake_dir.resolve(), expression, jobs, REPORTS / backend)
    print(f'Private {backend}: {sum(len(names) for _, names in shards)} tests across '
          f'{len(shards)} concurrent sessions', flush=True)
    plan = [dict(shard=index, tests=names) for index, (_, names) in enumerate(shards, 1)]
    (REPORTS / f'{backend}-test-plan.json').write_text(json.dumps(plan, indent=2) + '\n', encoding='utf-8')

    def run_shard(item):
        index, (workspace, names) = item
        selection = '^(' + '|'.join(re.escape(name) for name in names) + ')$'
        selected = ['ctest', '--test-dir', workspace, '-C', 'Release', '--parallel', '1',
                    '--output-on-failure', '--no-tests=error', '-R', selection,
                    '--output-junit', workspace / 'tests.xml']
        environment = dict(os.environ, WXNOTEPAD_DISPLAY_LOG_DIR=str(workspace / 'display'))
        isolated = ['xvfb-run', '-a', '-s', '-screen 0 1600x1000x24 -nolisten tcp',
                    'dbus-run-session', '--', sys.executable, ROOT / 'tools/clipboard_isolated.py',
                    backend, *selected]
        try:
            run(isolated, f'{backend}-shard-{index}-ctest.log', environment)
        except SystemExit as failure:
            return failure.code
        return 0

    with ThreadPoolExecutor(max_workers=len(shards)) as workers:
        results = list(workers.map(run_shard, enumerate(shards, 1)))
    if any(results):
        raise SystemExit(f'Private {backend} test shards failed; inspect the retained per-shard logs')


def bundle():
    directory = BUILD / 'cmake'
    executable = directory / ('wxnotepad.exe' if WINDOWS else 'wxnotepad')
    destination = WORK / 'artifact'
    destination.mkdir(parents=True, exist_ok=True)
    shutil.copy2(executable, destination / executable.name)
    if WINDOWS:
        for library in directory.glob('*.dll'):
            shutil.copy2(library, destination / library.name)
    else:
        dependencies = subprocess.check_output(['ldd', str(executable)], text=True)
        (REPORTS / 'runtime-libraries.txt').write_text(dependencies, encoding='utf-8')
        libraries = destination / 'lib'
        libraries.mkdir(exist_ok=True)
        for line in dependencies.splitlines():
            if '=> ' not in line:
                continue
            name = Path(line.split('=> ', 1)[1].split(' (', 1)[0].strip())
            if name.is_absolute() and name.is_relative_to(VCPKG / 'installed'):
                shutil.copy2(name, libraries / name.name)
    shutil.copytree(ROOT / 'licenses', destination / 'licenses', dirs_exist_ok=True)
    for notice in (VCPKG / 'installed' / TRIPLET / 'share').glob('*/copyright'):
        target = destination / 'licenses/vcpkg' / notice.parent.name
        target.mkdir(parents=True, exist_ok=True)
        shutil.copy2(notice, target / 'copyright')
    source = json.loads((BUILD / 'original-core/wxnotepad-source.json').read_text(encoding='utf-8'))
    source['application_commit'] = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()
    source['triplet'] = TRIPLET
    source['executable_sha256'] = hashlib.sha256(executable.read_bytes()).hexdigest()
    (destination / 'build.json').write_text(json.dumps(source, indent=2) + '\n', encoding='utf-8')
    (destination / 'README.txt').write_text(
        'Development build output, not a release/install package.\n'
        'See build.json for the tested source and executable checksum.\n'
        'Windows: keep the bundled DLLs beside wxnotepad.exe.\n'
        'Linux: Ubuntu 24.04 x64 desktop libraries are required; run from this directory with\n'
        'LD_LIBRARY_PATH="$PWD/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" ./wxnotepad\n',
        encoding='utf-8')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='command', required=True)
    for name in ('metadata', 'dependencies', 'bundle'):
        commands.add_parser(name)
    for name in ('configure', 'build'):
        command = commands.add_parser(name)
        command.add_argument('--app-only', action='store_true')
        if name == 'build':
            command.add_argument('--jobs', type=int, default=os.cpu_count() or 1)
    tests = commands.add_parser('test')
    tests.add_argument('--backend', choices=('windows', 'x11', 'wayland'), required=True)
    tests.add_argument('--cmake-dir', type=Path, default=BUILD / 'cmake')
    tests.add_argument('--tests', help='Optional CTest regex for local verification; CI runs the complete suite')
    tests.add_argument('--jobs', type=int, default=1 if WINDOWS else os.cpu_count() or 1,
                       help='Concurrent private Linux display sessions; Windows uses one desktop')
    arguments = parser.parse_args()
    if getattr(arguments, 'jobs', 1) < 1:
        parser.error('--jobs must be positive')
    if arguments.command == 'configure':
        configure(arguments.app_only)
    elif arguments.command == 'build':
        build(arguments.app_only, arguments.jobs)
    elif arguments.command == 'test':
        test(arguments.backend, arguments.cmake_dir.resolve(), arguments.tests, arguments.jobs)
    else:
        globals()[arguments.command]()


if __name__ == '__main__':
    main()

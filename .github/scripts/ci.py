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
import time

ROOT = Path(__file__).resolve().parents[2]
WORK = ROOT / '.ci'
REPORTS = WORK / 'reports'
VCPKG = WORK / 'vcpkg'
BUILD = WORK / 'build'
WINDOWS = sys.platform == 'win32'
TRIPLET = 'x64-windows-ci' if WINDOWS else 'x64-linux-ci'


def run(command, log, env=None, check=True):
    REPORTS.mkdir(parents=True, exist_ok=True)
    print('+ ' + ' '.join(str(item) for item in command), flush=True)
    started = time.monotonic()
    with (REPORTS / log).open('w', encoding='utf-8') as output:
        process = subprocess.Popen([str(item) for item in command], cwd=ROOT, env=env,
                                   stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                   text=True, encoding='utf-8', errors='replace')
        for line in process.stdout:
            output.write(line)
            print(line, end='', flush=True)
        result = process.wait()
        summary = f'\nCommand finished: exit {result}, elapsed {time.monotonic() - started:.3f}s\n'
        output.write(summary)
        print(summary, end='', flush=True)
    if result and check:
        raise SystemExit(result)
    return result


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
         f'--overlay-ports={ROOT / ".github/ports"}',
         f'--x-install-root={VCPKG / "installed"}', '--clean-after-build'],
        'dependencies.log', environment)


def compiler_launcher_arguments(enabled):
    arguments = ['-DCMAKE_CXX_COMPILER_LAUNCHER=' + ('sccache' if enabled else '')]
    if enabled and WINDOWS:
        # CMake >=3.25/CMP0141: embedded debug information avoids a shared compiler PDB.
        arguments.append('-DCMAKE_MSVC_DEBUG_INFORMATION_FORMAT=$<$<CONFIG:Debug,RelWithDebInfo>:Embedded>')
    return arguments


def configure(app_only, *, coverage=False, compiler_cache=False):
    directory = BUILD / ('coverage-cmake' if coverage else 'cmake')
    prefix = 'coverage-' if coverage else ''
    if coverage and sys.platform != 'linux':
        raise SystemExit('Coverage requires Linux and GCC')
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
    command = cmake_command(['-S', ROOT, '-B', directory,
               f'-DNOTEPAD4_SOURCE={BUILD / "original-core"}',
               f'-DCMAKE_TOOLCHAIN_FILE={VCPKG / "scripts/buildsystems/vcpkg.cmake"}',
               f'-DVCPKG_INSTALLED_DIR={VCPKG / "installed"}',
               f'-DVCPKG_TARGET_TRIPLET={TRIPLET}', '-DVCPKG_MANIFEST_MODE=OFF',
               f'-DVCPKG_OVERLAY_TRIPLETS={ROOT / ".github/triplets"}',
               '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON', '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release',
               '-DWXNOTEPAD_ENABLE_DIAGNOSTICS=OFF',
               '-DWXNOTEPAD_ENABLE_COVERAGE=' + ('ON' if coverage else 'OFF'),
               '-DBUILD_TESTING=' + ('OFF' if app_only else 'ON'),
               *compiler_launcher_arguments(compiler_cache and not coverage)])
    if coverage:
        command.append(f'-DPython3_EXECUTABLE={sys.executable}')
    run(command, prefix + 'configure.log')


def cmake_command(arguments):
    arguments = [str(item) for item in arguments]
    if WINDOWS:
        # pwsh -File splits named arguments such as -DKEY=D:/path at the colon.
        # Pass the array as one value and splat it only inside the script.
        return ['pwsh', '-NoProfile', '-File', ROOT / '.github/scripts/msvc.ps1',
                '-CMakeArgumentsJson', json.dumps(arguments)]
    return ['cmake', *arguments]


def build(app_only, jobs, *, coverage=False):
    print(f'Build workers: {jobs}', flush=True)
    directory = BUILD / ('coverage-cmake' if coverage else 'cmake')
    command = ['--build', directory, '--config', 'Release', '--parallel', str(jobs)]
    if app_only:
        command += ['--target', 'wxnotepad']
    run(cmake_command(command), ('coverage-' if coverage else '') + 'build.log')


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
    # CTest resolves subdirs() against its current workspace, not the included file.
    child_files = [path for path in cmake_dir.rglob('CTestTestfile.cmake') if path.parent != cmake_dir]
    shards = []
    for index in range(min(jobs, len(tests))):
        selected = tests[index::min(jobs, len(tests))]
        workspace = directory / f'shard-{index + 1}'
        workspace.mkdir(parents=True, exist_ok=True)
        for source in child_files:
            target = workspace / source.relative_to(cmake_dir)
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text(f'include({literal(source.as_posix())})\n', encoding='utf-8')
        registration = f'include({literal((cmake_dir / "CTestTestfile.cmake").as_posix())})\n'
        for item in selected:
            properties = {entry['name']: entry['value'] for entry in item.get('properties', [])}
            working = properties.get('WORKING_DIRECTORY', str(cmake_dir))
            registration += (f'set_tests_properties({literal(item["name"])} PROPERTIES '
                             f'WORKING_DIRECTORY {literal(working)})\n')
        (workspace / 'CTestTestfile.cmake').write_text(registration, encoding='utf-8')
        shards.append((workspace, [item['name'] for item in selected]))
    return shards


def windows_desktop_names():
    import ctypes
    from ctypes import wintypes

    desktop_read_objects = 0x0001
    user_object_name = 2
    desktop_name_characters = 256
    user = ctypes.WinDLL('user32', use_last_error=True)
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    kernel.GetCurrentThreadId.restype = wintypes.DWORD
    user.GetThreadDesktop.argtypes = [wintypes.DWORD]
    user.GetThreadDesktop.restype = wintypes.HANDLE
    user.OpenInputDesktop.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
    user.OpenInputDesktop.restype = wintypes.HANDLE
    user.GetUserObjectInformationW.argtypes = [wintypes.HANDLE, ctypes.c_int, ctypes.c_void_p,
                                             wintypes.DWORD, ctypes.POINTER(wintypes.DWORD)]
    user.CloseDesktop.argtypes = [wintypes.HANDLE]

    def name(handle):
        value = ctypes.create_unicode_buffer(desktop_name_characters)
        needed = wintypes.DWORD()
        if not user.GetUserObjectInformationW(handle, user_object_name, value,
                                              ctypes.sizeof(value), ctypes.byref(needed)):
            raise ctypes.WinError(ctypes.get_last_error())
        return value.value

    input_desktop = user.OpenInputDesktop(0, False, desktop_read_objects)
    if not input_desktop:
        raise SystemExit('Windows input desktop is unavailable: ' + str(ctypes.WinError(ctypes.get_last_error())))
    try:
        return {'thread': name(user.GetThreadDesktop(kernel.GetCurrentThreadId())), 'input': name(input_desktop)}
    finally:
        user.CloseDesktop(input_desktop)


def test(backend, cmake_dir, expression, jobs, report_prefix='', report_directory=None):
    if (backend == 'windows') != WINDOWS:
        raise SystemExit('The requested test backend does not match this host')
    reports = report_directory.resolve() if report_directory is not None else REPORTS
    try:
        reports.mkdir(parents=True, exist_ok=report_directory is None)
    except FileExistsError:
        raise SystemExit('Report directory already exists; choose a fresh directory to retain earlier evidence')
    if WINDOWS:
        if jobs != 1:
            raise SystemExit('Windows native tests share one desktop and clipboard; use --jobs 1')
        desktop = windows_desktop_names()
        (reports / 'windows-desktop.json').write_text(json.dumps(desktop, indent=2) + '\n', encoding='utf-8')
        if any(value.casefold() != 'default' for value in desktop.values()):
            raise SystemExit('Windows native tests require the Default input desktop; '
                             f'thread={desktop["thread"]}, input={desktop["input"]}. '
                             'Run ci.py test directly from a terminal on the normal desktop.')
    report = report_prefix + backend
    command = ['ctest', '--test-dir', cmake_dir, '-C', 'Release', '--parallel', '1',
               '--output-on-failure', '--no-tests=error',
               '--output-junit', reports / f'{report}-tests.xml']
    if expression:
        command += ['-R', expression]
    if WINDOWS:
        try:
            run(command, reports / f'{report}-ctest.log')
        finally:
            temporary = cmake_dir / 'Testing/Temporary'
            if report_directory is not None and temporary.exists():
                shutil.copytree(temporary, reports / f'{report}-ctest', dirs_exist_ok=True)
        return

    shards = test_shards(cmake_dir.resolve(), expression, jobs, reports / report)
    print(f'Private {backend}: {sum(len(names) for _, names in shards)} tests across '
          f'{len(shards)} concurrent sessions', flush=True)
    plan = [{'shard': index, 'tests': names} for index, (_, names) in enumerate(shards, 1)]
    (reports / f'{report}-test-plan.json').write_text(json.dumps(plan, indent=2) + '\n', encoding='utf-8')

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
        return run(isolated, reports / f'{report}-shard-{index}-ctest.log', environment, check=False)

    with ThreadPoolExecutor(max_workers=len(shards)) as workers:
        results = list(workers.map(run_shard, enumerate(shards, 1)))
    if any(results):
        raise SystemExit(f'Private {backend} test shards failed; inspect the retained per-shard logs')


def diagnostic_build(jobs, *, compiler_cache=False):
    directory = BUILD / 'diagnostic-cmake'
    run(cmake_command(['-S', ROOT, '-B', directory, '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release',
                       f'-DNOTEPAD4_SOURCE={BUILD / "original-core"}',
                       f'-DCMAKE_TOOLCHAIN_FILE={VCPKG / "scripts/buildsystems/vcpkg.cmake"}',
                       f'-DVCPKG_INSTALLED_DIR={VCPKG / "installed"}',
                       f'-DVCPKG_TARGET_TRIPLET={TRIPLET}', '-DVCPKG_MANIFEST_MODE=OFF',
                       f'-DVCPKG_OVERLAY_TRIPLETS={ROOT / ".github/triplets"}',
                       '-DBUILD_TESTING=ON', '-DWXNOTEPAD_ENABLE_DIAGNOSTICS=ON',
                       *compiler_launcher_arguments(compiler_cache)]), 'diagnostic-configure.log')
    run(cmake_command(['--build', directory, '--config', 'Release', '--parallel', str(jobs),
                       '--target', 'wxnotepad', 'editing_keys', 'close_smoke']), 'diagnostic-build.log')


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


def argument_parser():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='command', required=True)
    for name in ('metadata', 'dependencies', 'bundle'):
        commands.add_parser(name)
    for name in ('configure', 'build'):
        command = commands.add_parser(name)
        command.add_argument('--app-only', action='store_true')
        if name == 'build':
            command.add_argument('--jobs', type=int, default=os.cpu_count() or 1)
        else:
            command.add_argument('--compiler-cache', action='store_true', help='Use sccache for ordinary compilation')
    tests = commands.add_parser('test')
    tests.add_argument('--backend', choices=('windows', 'x11', 'wayland'), required=True)
    tests.add_argument('--cmake-dir', type=Path, default=BUILD / 'cmake')
    tests.add_argument('--tests', help='Optional CTest regex for local verification; CI runs the complete suite')
    tests.add_argument('--report-dir', type=Path, help='Fresh report directory for a local run; never overwrites it')
    tests.add_argument('--jobs', type=int, default=1 if WINDOWS else os.cpu_count() or 1,
                       help='Concurrent private Linux display sessions; Windows uses one desktop')
    diagnostic = commands.add_parser('diagnostic-build')
    diagnostic.add_argument('--jobs', type=int, default=os.cpu_count() or 1)
    diagnostic.add_argument('--compiler-cache', action='store_true')
    coverage = commands.add_parser('coverage-build')
    coverage.add_argument('--jobs', type=int, default=os.cpu_count() or 1)
    diagnostic = commands.add_parser('diagnostic-test')
    diagnostic.add_argument('--backend', choices=('windows', 'x11', 'wayland'), required=True)
    return parser


def main():
    parser = argument_parser()
    arguments = parser.parse_args()
    if getattr(arguments, 'jobs', 1) < 1:
        parser.error('--jobs must be positive')
    if arguments.command == 'configure':
        configure(arguments.app_only, compiler_cache=arguments.compiler_cache)
    elif arguments.command == 'build':
        build(arguments.app_only, arguments.jobs)
    elif arguments.command == 'test':
        test(arguments.backend, arguments.cmake_dir.resolve(), arguments.tests, arguments.jobs,
             report_directory=arguments.report_dir)
    elif arguments.command == 'diagnostic-build':
        diagnostic_build(arguments.jobs, compiler_cache=arguments.compiler_cache)
    elif arguments.command == 'coverage-build':
        configure(False, coverage=True)
        build(False, arguments.jobs, coverage=True)
    elif arguments.command == 'diagnostic-test':
        test(arguments.backend, (BUILD / 'diagnostic-cmake').resolve(), '^native_diagnostic_', 1, 'diagnostic-')
    else:
        globals()[arguments.command]()


if __name__ == '__main__':
    main()

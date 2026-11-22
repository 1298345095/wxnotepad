"""Small CI glue around the existing pinned-core preparation and CMake entry points."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
import math
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
from cmake_cache import read_cmake_cache

WORK = ROOT / '.ci'
REPORTS = WORK / 'reports'
VCPKG = WORK / 'vcpkg'
BUILD = WORK / 'build'
BUILD_RECORD = 'build.json'
GIO_LAUNCH_HELPER = Path('libexec/gio-launch-desktop')
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


def probe(command, report_directory, timeout, runs, working_directory):
    reports = report_directory.resolve()
    try:
        reports.mkdir(parents=True, exist_ok=False)
    except FileExistsError:
        raise SystemExit('Report directory already exists; choose a fresh directory to retain earlier evidence')
    summary = {'command': [str(item) for item in command], 'cwd': str(working_directory.resolve()),
               'timeout_seconds': timeout, 'requested_runs': runs, 'completed_runs': 0, 'success': False, 'results': []}

    def publish():
        summary['completed_runs'] = sum(item['status'] == 'completed' for item in summary['results'])
        (reports / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n', encoding='utf-8')

    publish()
    for index in range(1, runs + 1):
        log = reports / f'run-{index:03}.log'
        record = {'command': summary['command'], 'status': 'running', 'exit_code': None, 'log': str(log)}
        summary['results'].append(record)
        publish()
        started = time.monotonic()
        print(f'Probe {index}/{runs}: {command[0]} (log: {log})', flush=True)
        try:
            with log.open('wb') as output:
                try:
                    result = subprocess.run(summary['command'], cwd=working_directory,
                                            stdout=output, stderr=subprocess.STDOUT, timeout=timeout, check=False)
                    record.update(status='completed', exit_code=result.returncode)
                except subprocess.TimeoutExpired:
                    record['status'] = 'timeout'
                    output.write(f'\nProbe exceeded its {timeout}s deadline.\n'.encode('utf-8'))
                except OSError as error:
                    record['status'] = 'launch_failed'
                    output.write(f'\nCould not start probe: {error}\n'.encode('utf-8'))
        finally:
            record['seconds'] = round(time.monotonic() - started, 3)
            publish()
        print(f'Probe {index}/{runs}: {record["status"]}, exit={record["exit_code"]}', flush=True)
        if record['status'] != 'completed' or record['exit_code'] != 0:
            return 1
    summary['success'] = True
    publish()
    return 0


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
    run([executable, 'install', '--triplet', TRIPLET, '--host-triplet', TRIPLET, f'--overlay-triplets={ROOT / ".github/triplets"}',
         f'--overlay-ports={ROOT / ".github/ports"}',
         f'--x-install-root={VCPKG / "installed"}', '--clean-after-build'],
        'dependencies.log', environment)
    gettext = VCPKG / 'installed' / TRIPLET / 'tools/gettext/bin'
    for name in ('xgettext', 'msgmerge', 'msgfmt', 'msgcmp', 'msgattrib'):
        tool = gettext / (name + ('.exe' if WINDOWS else ''))
        version = subprocess.check_output([tool, '--version'], text=True, encoding='utf-8').splitlines()[0]
        if not version.endswith(' 0.22.5'):
            raise SystemExit('Expected pinned GNU gettext 0.22.5: ' + version)
        print(version, flush=True)
    if 'GITHUB_ENV' in os.environ:
        with open(os.environ['GITHUB_ENV'], 'a', encoding='utf-8') as stream:
            stream.write(f'WXNOTEPAD_GETTEXT_BIN={gettext}\n')
    run([sys.executable, '-X', 'utf8', ROOT / 'tools/localization.py',
         '--gettext-bin', gettext, 'check'], 'localization-check.log')
    run([sys.executable, '-X', 'utf8', '-m', 'unittest', 'discover', '-s', 'tests',
         '-p', 'test_localization.py', '-v'], 'localization-cli-tests.log',
        dict(environment, WXNOTEPAD_GETTEXT_BIN=str(gettext)))


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
               f'-DVCPKG_TARGET_TRIPLET={TRIPLET}', f'-DVCPKG_HOST_TRIPLET={TRIPLET}',
               f'-DWXNOTEPAD_GETTEXT_BIN={VCPKG / "installed" / TRIPLET / "tools/gettext/bin"}',
               '-DVCPKG_MANIFEST_MODE=OFF',
               f'-DVCPKG_OVERLAY_TRIPLETS={ROOT / ".github/triplets"}',
               '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON', '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release',
               '-DWXNOTEPAD_ENABLE_DIAGNOSTICS=OFF',
               '-DWXNOTEPAD_VERIFY_TRIMMED_SDK=ON',
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


def require_windows_test_desktop(jobs, reports):
    if jobs != 1:
        raise SystemExit('Windows native tests share one desktop and clipboard; use --jobs 1')
    desktop = windows_desktop_names()
    (reports / 'windows-desktop.json').write_text(json.dumps(desktop, indent=2) + '\n', encoding='utf-8')
    if any(value.casefold() != 'default' for value in desktop.values()):
        raise SystemExit('Windows native tests require the Default input desktop; '
                         f'thread={desktop["thread"]}, input={desktop["input"]}. '
                         'Run ci.py test directly from a terminal on the normal desktop.')


def test(backend, cmake_dir, expression, jobs, report_prefix='', report_directory=None):
    if (backend == 'windows') != WINDOWS:
        raise SystemExit('The requested test backend does not match this host')
    reports = report_directory.resolve() if report_directory is not None else REPORTS
    try:
        reports.mkdir(parents=True, exist_ok=report_directory is None)
    except FileExistsError:
        raise SystemExit('Report directory already exists; choose a fresh directory to retain earlier evidence')
    if WINDOWS:
        require_windows_test_desktop(jobs, reports)
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

    environment = dict(os.environ)
    # CTest-only display probes have no application SDK. A cache selecting the
    # application or either SDK field must still validate its complete helper path.
    if (cmake_dir / 'CMakeCache.txt').exists():
        cache = read_cmake_cache(cmake_dir)
        if (cache.get('CMAKE_PROJECT_NAME') == 'wxnotepad' or
                'VCPKG_INSTALLED_DIR' in cache or 'VCPKG_TARGET_TRIPLET' in cache):
            environment['GIO_LAUNCH_DESKTOP'] = str(linux_gio_helper(cache))

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
        child_environment = dict(environment, WXNOTEPAD_DISPLAY_LOG_DIR=str(workspace / 'display'))
        isolated = [sys.executable, ROOT / 'tools/xvfb_session.py',
                    'dbus-run-session', '--', sys.executable, ROOT / 'tools/clipboard_isolated.py',
                    backend, *selected]
        return run(isolated, reports / f'{report}-shard-{index}-ctest.log', child_environment, check=False)

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
                       f'-DVCPKG_TARGET_TRIPLET={TRIPLET}', f'-DVCPKG_HOST_TRIPLET={TRIPLET}',
                       f'-DWXNOTEPAD_GETTEXT_BIN={VCPKG / "installed" / TRIPLET / "tools/gettext/bin"}',
                       '-DVCPKG_MANIFEST_MODE=OFF',
                       f'-DVCPKG_OVERLAY_TRIPLETS={ROOT / ".github/triplets"}',
                       '-DBUILD_TESTING=ON', '-DWXNOTEPAD_ENABLE_DIAGNOSTICS=ON',
                       '-DWXNOTEPAD_VERIFY_TRIMMED_SDK=ON',
                       *compiler_launcher_arguments(compiler_cache)]), 'diagnostic-configure.log')
    targets = ['wxnotepad', 'editing_keys', 'close_smoke']
    if WINDOWS:
        targets.append('open_targets')
    run(cmake_command(['--build', directory, '--config', 'Release', '--parallel', str(jobs),
                       '--target', *targets]), 'diagnostic-build.log')


def diagnostic_test(backend):
    expression = '^native_diagnostic_'
    if backend == 'windows':
        expression = '(^native_diagnostic_|^original_open_targets$)'
    test(backend, (BUILD / 'diagnostic-cmake').resolve(), expression, 1, 'diagnostic-')


def split_linux_symbols(binary, symbols):
    """Keep matching symbols separately and strip only the distribution copy."""
    debug_directory = symbols / '.debug'
    debug_directory.mkdir(parents=True, exist_ok=True)
    debug_file = debug_directory / (binary.name + '.debug')
    subprocess.run(['objcopy', '--only-keep-debug', binary, debug_file], check=True)
    subprocess.run(['strip', '--strip-unneeded', binary], check=True)
    subprocess.run(['objcopy', f'--add-gnu-debuglink={debug_file}', binary], check=True)


def bundle_linux_runtime(executable, destination, symbols, installed, reports):
    libraries = destination / 'lib'
    libraries.mkdir(exist_ok=True)
    for relative, report in ((Path(executable.name), 'runtime-libraries.txt'),
                             (GIO_LAUNCH_HELPER, 'gio-launch-desktop-libraries.txt')):
        binary = destination / relative
        split_linux_symbols(binary, symbols / relative.parent)
        dependencies = subprocess.check_output(['ldd', str(binary)], text=True)
        (reports / report).write_text(dependencies, encoding='utf-8')
        for line in dependencies.splitlines():
            if '=> ' not in line:
                continue
            name = Path(line.split('=> ', 1)[1].split(' (', 1)[0].strip())
            target = libraries / name.name
            if name.is_absolute() and name.resolve().is_relative_to(installed) and not target.exists():
                shutil.copy2(name, target)
                split_linux_symbols(target, symbols / 'lib')


def linux_gio_helper(cache):
    # GLib's compiled libexec prefix points into vcpkg's disposable package
    # staging tree. Select the helper from this build's actual installed SDK.
    helper = Path(cache['VCPKG_INSTALLED_DIR']) / cache['VCPKG_TARGET_TRIPLET'] / GIO_LAUNCH_HELPER
    if not helper.is_file() or not os.access(helper, os.X_OK):
        raise FileNotFoundError(f'Required executable GLib desktop launch helper: {helper}')
    return helper.resolve()


def bundle(cmake_dir, executable, output_root, *, report_dir=None):
    """Collect a configured wxnotepad build and return its distributed executable."""
    cache = read_cmake_cache(cmake_dir)
    source_root = Path(cache['CMAKE_HOME_DIRECTORY'])
    installed = Path(cache['VCPKG_INSTALLED_DIR']).resolve()
    triplet = cache['VCPKG_TARGET_TRIPLET']
    source_record = Path(cache['NOTEPAD4_SOURCE']) / 'wxnotepad-source.json'
    executable = executable.absolute()
    output_root = output_root.resolve()
    directory = executable.parent
    windows = executable.suffix.lower() == '.exe'
    reports = report_dir.resolve() if report_dir is not None else output_root / 'reports'
    reports.mkdir(parents=True, exist_ok=True)
    destination = output_root / 'artifact'
    destination.mkdir(parents=True, exist_ok=True)
    shutil.copy2(executable, destination / executable.name)
    symbols = output_root / 'symbols'
    if windows:
        for library in directory.glob('*.dll'):
            shutil.copy2(library, destination / library.name)
    else:
        helper = destination / GIO_LAUNCH_HELPER
        helper.parent.mkdir(exist_ok=True)
        shutil.copy2(linux_gio_helper(cache), helper)
        bundle_linux_runtime(executable, destination, symbols, installed, reports)
    languages = json.loads((source_root / 'po/languages.json').read_text(encoding='utf-8'))
    for language in languages['languages']:
        if language['code'] == languages['source_language']:
            continue
        for domain in (languages['domain'], 'wxstd'):
            relative = Path(language['code']) / 'LC_MESSAGES' / (domain + '.mo')
            target = destination / 'locale' / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(directory / 'locale' / relative, target)
    shutil.copytree(source_root / 'licenses', destination / 'licenses', dirs_exist_ok=True)
    for notice in (installed / triplet / 'share').glob('*/copyright'):
        target = destination / 'licenses/vcpkg' / notice.parent.name
        target.mkdir(parents=True, exist_ok=True)
        shutil.copy2(notice, target / 'copyright')
    source = json.loads(source_record.read_text(encoding='utf-8'))
    source['application_commit'] = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=source_root, text=True).strip()
    source['triplet'] = triplet
    source['executable_sha256'] = hashlib.sha256((destination / executable.name).read_bytes()).hexdigest()
    if not windows:
        source['unstripped_executable_sha256'] = hashlib.sha256(executable.read_bytes()).hexdigest()
    (destination / BUILD_RECORD).write_text(json.dumps(source, indent=2) + '\n', encoding='utf-8')
    if not windows:
        shutil.copy2(destination / BUILD_RECORD, symbols / BUILD_RECORD)
    (destination / 'README.txt').write_text(
        'Development build output, not a release/install package.\n'
        'See build.json for the tested source and executable checksum.\n'
        'Windows: keep the bundled DLLs beside wxnotepad.exe.\n'
        'Linux: Ubuntu 24.04 x64 desktop libraries are required; run from this directory with\n'
        'GIO_LAUNCH_DESKTOP="$PWD/libexec/gio-launch-desktop" \\\n'
        'LD_LIBRARY_PATH="$PWD/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" ./wxnotepad\n'
        'The matching Linux symbols/ directory is retained with the build evidence.\n'
        'Copy its .debug/, lib/.debug/ and libexec/.debug/ directories here for GDB.\n',
        encoding='utf-8')
    return destination / executable.name


def argument_parser():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='command', required=True)
    for name in ('metadata', 'dependencies'):
        commands.add_parser(name)
    packaging = commands.add_parser('bundle', help='Collect development output from a configured wxnotepad build')
    packaging.add_argument('--cmake-dir', type=Path, default=BUILD / 'cmake')
    packaging.add_argument('--executable', type=Path,
                           help='Actual built target; defaults to wxnotepad[.exe] in --cmake-dir')
    packaging.add_argument('--output-dir', type=Path, default=WORK,
                           help='Root for artifact/ and matching Linux symbols/')
    packaging.add_argument('--report-dir', type=Path, help='Report directory; defaults to <output-dir>/reports')
    for name in ('configure', 'build'):
        command = commands.add_parser(name)
        command.add_argument('--app-only', action='store_true')
        if name == 'build':
            command.add_argument('--jobs', type=int, default=os.cpu_count() or 1)
        else:
            command.add_argument('--coverage', action='store_true',
                                 help='Configure the Linux GCC coverage tree without building')
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
    native_probe = commands.add_parser('probe', help='Run standalone owned processes synchronously and retain evidence')
    native_probe.add_argument('--report-dir', type=Path, required=True)
    native_probe.add_argument('--timeout', type=float, required=True, help='Deadline in seconds for each process')
    native_probe.add_argument('--runs', type=int, default=1, help='Serial runs; stop on the first failure')
    native_probe.add_argument('--cwd', type=Path, default=ROOT)
    native_probe.add_argument('program', nargs=argparse.REMAINDER, help='Command and literal arguments after --')
    return parser


def _bundle_command(arguments):
    filename = 'wxnotepad.exe' if WINDOWS else 'wxnotepad'
    executable = arguments.executable or arguments.cmake_dir / filename
    bundle(arguments.cmake_dir, executable, arguments.output_dir, report_dir=arguments.report_dir)


def main():
    parser = argument_parser()
    arguments = parser.parse_args()
    if getattr(arguments, 'jobs', 1) < 1:
        parser.error('--jobs must be positive')
    if arguments.command == 'probe':
        program = arguments.program[1:] if arguments.program[:1] == ['--'] else arguments.program
        if not program or arguments.runs < 1 or not math.isfinite(arguments.timeout) or arguments.timeout <= 0:
            parser.error('probe requires a command, positive --runs and a finite positive --timeout')
        raise SystemExit(probe(program, arguments.report_dir, arguments.timeout, arguments.runs, arguments.cwd))
    elif arguments.command == 'configure':
        configure(arguments.app_only, coverage=arguments.coverage, compiler_cache=arguments.compiler_cache)
    elif arguments.command == 'build':
        build(arguments.app_only, arguments.jobs)
    elif arguments.command == 'test':
        test(arguments.backend, arguments.cmake_dir.resolve(), arguments.tests, arguments.jobs,
             report_directory=arguments.report_dir)
    elif arguments.command == 'bundle':
        _bundle_command(arguments)
    elif arguments.command == 'diagnostic-build':
        diagnostic_build(arguments.jobs, compiler_cache=arguments.compiler_cache)
    elif arguments.command == 'coverage-build':
        configure(False, coverage=True)
        build(False, arguments.jobs, coverage=True)
    elif arguments.command == 'diagnostic-test':
        diagnostic_test(arguments.backend)
    else:
        globals()[arguments.command]()


if __name__ == '__main__':
    main()

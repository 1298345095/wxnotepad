"""Small CI glue around the existing pinned-core preparation and CMake entry points."""
import argparse
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
    command = cmake_command() + ['-S', ROOT, '-B', BUILD / 'cmake',
               f'-DNOTEPAD4_SOURCE={BUILD / "original-core"}',
               f'-DCMAKE_TOOLCHAIN_FILE={VCPKG / "scripts/buildsystems/vcpkg.cmake"}',
               f'-DVCPKG_INSTALLED_DIR={VCPKG / "installed"}',
               f'-DVCPKG_TARGET_TRIPLET={TRIPLET}', '-DVCPKG_MANIFEST_MODE=OFF',
               f'-DVCPKG_OVERLAY_TRIPLETS={ROOT / ".github/triplets"}',
               '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON', '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release',
               '-DBUILD_TESTING=' + ('OFF' if app_only else 'ON')]
    run(command, 'configure.log')


def cmake_command():
    if WINDOWS:
        return ['pwsh', '-NoProfile', '-File', ROOT / '.github/scripts/msvc.ps1']
    return ['cmake']


def build(app_only):
    command = cmake_command() + ['--build', BUILD / 'cmake', '--config', 'Release', '--parallel', '2']
    if app_only:
        command += ['--target', 'wxnotepad']
    run(command, 'build.log')


def test(backend, cmake_dir, expression):
    if (backend == 'windows') != WINDOWS:
        raise SystemExit('The requested test backend does not match this host')
    REPORTS.mkdir(parents=True, exist_ok=True)
    command = ['ctest', '--test-dir', cmake_dir, '-C', 'Release', '--parallel', '1',
               '--output-on-failure', '--no-tests=error',
               '--output-junit', REPORTS / f'{backend}-tests.xml']
    if expression:
        command += ['-R', expression]
    environment = dict(os.environ)
    if not WINDOWS:
        environment['WXNOTEPAD_DISPLAY_LOG_DIR'] = str(REPORTS / 'display')
        command = ['xvfb-run', '-a', '-s', '-screen 0 1600x1000x24 -nolisten tcp',
                   'dbus-run-session', '--', sys.executable, ROOT / 'tools/clipboard_isolated.py',
                   backend, *command]
    run(command, f'{backend}-ctest.log', environment)


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
        commands.add_parser(name).add_argument('--app-only', action='store_true')
    tests = commands.add_parser('test')
    tests.add_argument('--backend', choices=('windows', 'x11', 'wayland'), required=True)
    tests.add_argument('--cmake-dir', type=Path, default=BUILD / 'cmake')
    tests.add_argument('--tests', help='Optional CTest regex for local verification; CI runs the complete suite')
    arguments = parser.parse_args()
    if arguments.command in ('configure', 'build'):
        globals()[arguments.command](arguments.app_only)
    elif arguments.command == 'test':
        test(arguments.backend, arguments.cmake_dir.resolve(), arguments.tests)
    else:
        globals()[arguments.command]()


if __name__ == '__main__':
    main()

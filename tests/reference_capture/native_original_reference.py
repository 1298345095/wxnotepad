"""Build/run an independent untouched pinned Win32 control for hosted diagnosis."""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile
import tempfile
import time

ROOT = Path(__file__).resolve().parents[2]


def pinned_helpers(upstream, build):
    spec = importlib.util.spec_from_file_location('original_build', ROOT / 'tools/build.py')
    original_build = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(original_build)
    contents = subprocess.check_output(['git', '-C', str(upstream), 'show',
                                        f'{original_build.REVISION}:src/Helpers.cpp'])
    path = build / 'pinned-Helpers.cpp'
    path.write_bytes(contents)
    begin = contents.index(b'void OpenContainingFolder(HWND hwnd,')
    end = contents.index(b'//=============================================================================', begin)
    helper = contents[begin:end]
    helper_path = build / 'pinned-open-folder.inc'
    helper_path.write_bytes(helper)
    (build / 'pinned-Helpers.json').write_text(json.dumps({
        'revision': original_build.REVISION, 'path': 'src/Helpers.cpp',
        'sha256': hashlib.sha256(contents).hexdigest(),
        'helper_sha256': hashlib.sha256(helper).hexdigest()}, indent=2) + '\n', encoding='utf-8')
    return helper_path


def open_target_control(executable, mode, output):
    """Observe one independent request; never retry it or set Explorer selection."""
    output.mkdir(parents=True, exist_ok=True)
    prefix = 'wxnotepad-shell-目标, space-' if mode == 'explorer-startup' else 'wxnotepad-shell-control-'
    with tempfile.TemporaryDirectory(prefix=prefix) as name:
        root = Path(name).resolve(strict=True)
        owned_executable = root / executable.name
        shutil.copy2(executable, owned_executable)
        directory = root / 'documents'
        directory.mkdir()
        (directory / 'doc-only.txt').write_bytes(b'independent non-target\n')
        (directory / 'parent.txt').write_bytes(b'independent target\n')
        log_path = output / 'observation.log'
        with log_path.open('wb') as log:
            observer = subprocess.Popen([
                'pwsh', '-NoProfile', '-File', ROOT / 'tests/open_target_explorer.ps1',
                '-Root', root, '-Evidence', output, '-Requests', '1'], stdout=log, stderr=subprocess.STDOUT)
            caller = None
            try:
                deadline = time.monotonic() + 10
                while not (root / 'explorer-ready').exists() and time.monotonic() < deadline:
                    if observer.poll() is not None:
                        raise RuntimeError('Independent Explorer observer exited before its baseline')
                    time.sleep(0.01)
                if not (root / 'explorer-ready').exists():
                    raise RuntimeError('Independent Explorer observer baseline timed out')
                caller = subprocess.Popen([owned_executable, root, mode], stdout=log, stderr=subprocess.STDOUT)
                caller_result = caller.wait(timeout=40 if mode == 'single-item-ready' else 30)
                observer_result = observer.wait(timeout=30 if mode == 'single-item-ready' else 10)
                result = {'mode': mode, 'caller_exit': caller_result, 'observer_exit': observer_result,
                          'selected': caller_result == 0 and observer_result == 0}
            finally:
                for process in (caller, observer):
                    if process and process.poll() is None:
                        process.terminate()
                        process.wait(timeout=10)
                for marker in root.glob('folder-*'):
                    shutil.copy2(marker, output / marker.name)
        print(log_path.read_text(encoding='utf-8', errors='replace'), end='', flush=True)
        return result


def open_target_controls(executable, output):
    results = []
    for mode in ('original', 'single-item', 'single-item-ready', 'explorer-startup'):
        try:
            results.append(open_target_control(executable, mode, output / mode))
        except (OSError, RuntimeError, subprocess.TimeoutExpired) as error:
            results.append({'mode': mode, 'selected': False, 'error': str(error)})
            print(f'Independent {mode} control failed: {error}', flush=True)
    (output / 'verdicts.json').write_text(json.dumps(results, indent=2) + '\n', encoding='utf-8')
    print('Independent Explorer verdicts: ' + json.dumps(results), flush=True)
    return all(result['selected'] for result in results)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--archive', type=Path, default=ROOT / '.ci/build/notepad4-source.tar')
    parser.add_argument('--build-dir', type=Path, default=ROOT / '.ci/original-reference')
    parser.add_argument('--compiler-cache', action='store_true', help='Use the existing CI sccache launcher')
    parser.add_argument('--output', type=Path, default=ROOT / '.ci/reports/original-gdi-viewport.tsv')
    parser.add_argument('--word-wrap-output-dir', type=Path)
    parser.add_argument('--open-targets-output-dir', type=Path,
                        help='Instead run the pinned-original and single-item independent shell controls')
    parser.add_argument('--upstream', type=Path, default=ROOT / '.ci/build/upstream',
                        help='Existing pinned Git checkout, used only by independent shell controls')
    args = parser.parse_args()
    if sys.platform != 'win32':
        raise SystemExit('The original reference requires native Windows')
    expected = (ROOT / 'patches/source.sha256').read_text(encoding='utf-8').strip()
    if hashlib.sha256(args.archive.read_bytes()).hexdigest() != expected:
        raise SystemExit('Original reference archive does not match the pinned source hash')
    build = args.build_dir.resolve()
    source = build / 'original-source'
    stamp = source / 'archive.sha256'
    if not stamp.exists():
        source.mkdir(parents=True, exist_ok=True)
        with tarfile.open(args.archive) as archive:
            archive.extractall(source, filter='data')
        stamp.write_text(expected, encoding='utf-8')
    elif stamp.read_text(encoding='utf-8') != expected:
        raise SystemExit('Original reference source stamp differs from the pinned archive')
    spec = importlib.util.spec_from_file_location('native_ci', ROOT / '.github/scripts/ci.py')
    ci = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(ci)
    configure = ['-S', ROOT / 'tests/native_original_reference', '-B', build / 'cmake', '-G', 'Ninja',
                 '-DCMAKE_BUILD_TYPE=Release', f'-DNOTEPAD4_SOURCE={source.as_posix()}', '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON', *ci.compiler_launcher_arguments(args.compiler_cache)]
    if args.open_targets_output_dir:
        helpers = pinned_helpers(args.upstream, build)
        configure.append(f'-DNOTEPAD4_OPEN_FOLDER={helpers.as_posix()}')
    subprocess.run(ci.cmake_command(configure), check=True)
    target = 'native_original_open_target' if args.open_targets_output_dir else 'native_original_viewport'
    subprocess.run(ci.cmake_command(['--build', build / 'cmake', '--target', target,
                                    '--parallel', '8']), check=True)
    if args.open_targets_output_dir:
        output = args.open_targets_output_dir.resolve()
        output.mkdir(parents=True, exist_ok=True)
        for filename in ('pinned-Helpers.json', 'pinned-open-folder.inc'):
            shutil.copy2(build / filename, output / Path(filename).name)
        selected = open_target_controls(build / 'cmake/native_original_open_target.exe',
                                        output)
        raise SystemExit(0 if selected else 1)
    output = args.output.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run([build / 'cmake/native_original_viewport.exe', output], check=True, timeout=120)
    print(output.read_text(encoding='utf-8'), end='')
    if args.word_wrap_output_dir:
        for variant in ('current', 'dpi96'):
            environment = dict(os.environ)
            if variant == 'dpi96':
                environment['__COMPAT_LAYER'] = 'DPIUNAWARE'
            subprocess.run([build / 'cmake/native_original_viewport.exe', '--word-wrap',
                            ROOT / 'tests/data', args.word_wrap_output_dir.resolve() / variant],
                           env=environment, check=True, timeout=90)


if __name__ == '__main__':
    main()

"""Build/run an independent untouched pinned Win32 control for hosted diagnosis."""
import argparse
import hashlib
import importlib.util
from pathlib import Path
import subprocess
import sys
import tarfile

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--archive', type=Path, default=ROOT / '.ci/build/notepad4-source.tar')
    parser.add_argument('--build-dir', type=Path, default=ROOT / '.ci/original-reference')
    parser.add_argument('--compiler-cache', action='store_true', help='Use the existing CI sccache launcher')
    parser.add_argument('--output', type=Path, default=ROOT / '.ci/reports/original-gdi-viewport.tsv')
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
    subprocess.run(ci.cmake_command(configure), check=True)
    subprocess.run(ci.cmake_command(['--build', build / 'cmake', '--target', 'native_original_viewport',
                                    '--parallel', '8']), check=True)
    output = args.output.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run([build / 'cmake/native_original_viewport.exe', output], check=True, timeout=120)
    print(output.read_text(encoding='utf-8'), end='')


if __name__ == '__main__':
    main()

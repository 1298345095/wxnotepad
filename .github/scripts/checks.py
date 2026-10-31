"""Run the repository's canonical formatter or native maintained-source analyzer."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import json
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
REPORTS = ROOT / '.ci/reports'
CHECKS = '-*,clang-analyzer-*,bugprone-*,performance-*,portability-*'
BLOCKING_CHECKS = 'clang-analyzer-core.*,clang-analyzer-unix.*'


def format_sources(executable):
    version = subprocess.check_output([executable, '--version'], text=True)
    if 'version 23.1.2' not in version:
        raise SystemExit('Use canonical clang-format 23.1.2: ' + version.strip())
    names = subprocess.check_output(['git', 'ls-files', '-z', '--', 'src', 'tests'], cwd=ROOT).split(b'\0')
    files = [name.decode('utf-8') for name in names if Path(name.decode('utf-8')).suffix in
             ('.c', '.cc', '.cpp', '.cxx', '.h', '.hpp')]
    if not files:
        raise SystemExit('No tracked C/C++ files were selected')
    subprocess.run([executable, '--dry-run', '--Werror', *files], cwd=ROOT, check=True)
    print(f'Canonical format passed: {len(files)} tracked files')


def tidy_sources(executable, build):
    REPORTS.mkdir(parents=True, exist_ok=True)
    records = json.loads((build / 'compile_commands.json').read_text(encoding='utf-8'))
    files = sorted({Path(item['file']).resolve() for item in records
                    if Path(item['file']).suffix in ('.c', '.cc', '.cpp', '.cxx') and
                    (Path(item['file']).resolve().is_relative_to(ROOT / 'src') or
                     Path(item['file']).resolve().is_relative_to(ROOT / 'tests'))})
    if not files:
        raise SystemExit('No maintained translation units were selected')
    directory = REPORTS / 'tidy'
    directory.mkdir(exist_ok=True)
    header_filter = re.escape(str(ROOT / 'src')) + '|' + re.escape(str(ROOT / 'tests'))

    def analyze(path):
        relative = path.relative_to(ROOT).as_posix()
        log = directory / (relative.replace('/', '__') + '.log')
        fixes = log.with_suffix('.yaml')
        command = [executable, '-p', str(build), f'--checks={CHECKS}',
                   f'--warnings-as-errors={BLOCKING_CHECKS}', f'--header-filter={header_filter}',
                   f'--export-fixes={fixes}', str(path)]
        with log.open('w', encoding='utf-8') as output:
            result = subprocess.run(command, cwd=ROOT, stdout=output, stderr=subprocess.STDOUT)
        print(f'{relative}: exit {result.returncode}', flush=True)
        return dict(file=relative, exit_code=result.returncode, log=log.relative_to(ROOT).as_posix())

    with ThreadPoolExecutor(max_workers=2) as workers:
        results = list(workers.map(analyze, files))
    (REPORTS / 'tidy-summary.json').write_text(json.dumps(results, indent=2) + '\n', encoding='utf-8')
    print(f'Analyzed {len(results)} maintained translation units; full diagnostics and fix records retained.')
    if any(item['exit_code'] for item in results):
        raise SystemExit('clang-tidy compiler/analyzer errors; inspect the retained logs')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='command', required=True)
    commands.add_parser('format').add_argument('--executable', default='clang-format')
    tidy = commands.add_parser('tidy')
    tidy.add_argument('--executable', default='clang-tidy-18')
    tidy.add_argument('--build-dir', type=Path, default=ROOT / '.ci/build/cmake')
    arguments = parser.parse_args()
    if arguments.command == 'format':
        format_sources(arguments.executable)
    else:
        tidy_sources(arguments.executable, arguments.build_dir.resolve())


if __name__ == '__main__':
    main()

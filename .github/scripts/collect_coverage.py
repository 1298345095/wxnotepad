"""Collect GCC coverage or merge backend reports for maintained C++ sources."""
import argparse
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def collect(build, output, root=ROOT):
    build, output, root = build.resolve(), output.resolve(), root.resolve()
    notes = sorted(build.rglob('*.gcno'))
    if not notes:
        raise SystemExit(f'No coverage notes found under {build}')
    reports = output / 'gcov'
    reports.mkdir(parents=True, exist_ok=False)
    kept = 0
    with (output / 'gcov.log').open('w', encoding='utf-8') as log:
        version = subprocess.check_output(['gcov-13', '--version'], text=True, encoding='utf-8')
        log.write(version)
        for index, note in enumerate(notes):
            with tempfile.TemporaryDirectory(prefix='wxnotepad-gcov-') as temporary:
                command = ['gcov-13', '-b', '-c', '-p', '-l', '--hash-filenames', '--', str(note)]
                result = subprocess.run(command, cwd=temporary, stdout=subprocess.PIPE,
                                        stderr=subprocess.STDOUT, text=True, encoding='utf-8')
                log.write('+ ' + ' '.join(command) + '\n' + result.stdout)
                if result.returncode or 'stamp mismatch' in result.stdout:
                    raise SystemExit(f'gcov failed for {note}; inspect {output / "gcov.log"}')
                for report in sorted(Path(temporary).glob('*.gcov')):
                    with report.open(encoding='utf-8') as stream:
                        header = stream.readline()
                    source = Path(header.split('Source:', 1)[1].strip())
                    if source.is_absolute() and source.resolve().is_relative_to(root / 'src'):
                        shutil.copy2(report, reports / f'{index}-{report.name}')
                        kept += 1
    if not kept:
        raise SystemExit('No maintained src/ coverage reports were generated')
    summary = {'notes': len(notes), 'reports': kept, 'build': str(build), 'source': str(root)}
    (output / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n', encoding='utf-8')
    print(f'Collected {kept} maintained-source reports from {len(notes)} translation units')


def write_sonar_report(output, root=ROOT):
    output, root = output.resolve(), root.resolve()
    command = [sys.executable, '-m', 'gcovr', '--root', str(root),
               '--filter', re.escape((root / 'src').as_posix() + '/'),
               '--gcov-use-existing-files', '--gcov-keep', '--exclude-throw-branches',
               '--sonarqube', str(output / 'sonar.xml'), '--sonarqube-metric', 'branch',
               '--json', str(output / 'coverage.json'),
               str(output / 'gcov')]
    with (output / 'gcovr.log').open('w', encoding='utf-8') as log:
        subprocess.run(command, check=True, stdout=log, stderr=subprocess.STDOUT)


def merge_reports(reports, output, root=ROOT):
    output, root = output.resolve(), root.resolve()
    output.mkdir(parents=True, exist_ok=True)
    command = [sys.executable, '-m', 'gcovr', '--root', str(root),
               '--filter', re.escape((root / 'src').as_posix() + '/'),
               '--sonarqube', str(output / 'sonar.xml'), '--sonarqube-metric', 'branch',
               '--json', str(output / 'coverage.json')]
    # Each backend has already applied the source and throw-branch policy while
    # reading its own matched GCC notes/counters. Merge by source/branch identity.
    for report in reports:
        command += ['--json-add-tracefile', str(report.resolve())]
    with (output / 'gcovr-merge.log').open('w', encoding='utf-8') as log:
        subprocess.run(command, check=True, stdout=log, stderr=subprocess.STDOUT)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, default=ROOT / '.ci/build/coverage-cmake')
    parser.add_argument('--output', type=Path, default=ROOT / '.ci/reports/coverage')
    parser.add_argument('--merge', type=Path, nargs='+', metavar='JSON',
                        help='Merge full gcovr JSON reports instead of collecting GCC counters')
    arguments = parser.parse_args()
    if arguments.merge:
        merge_reports(arguments.merge, arguments.output)
    else:
        collect(arguments.build_dir, arguments.output)
        write_sonar_report(arguments.output)


if __name__ == '__main__':
    main()

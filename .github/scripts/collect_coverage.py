"""Collect GCC coverage or merge backend reports for maintained C++ sources."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
GCOV_SHARDS_DIR = 'gcov-shards'


def shard_directories(build):
    root = build / GCOV_SHARDS_DIR
    if not root.is_dir():
        return []
    return sorted(path for path in root.iterdir() if path.is_dir())


def note_tasks(build, notes, shards):
    tasks = []
    for index, note in enumerate(notes):
        relative_counter = note.relative_to(build).with_suffix('.gcda')
        matched = []
        direct = note.with_suffix('.gcda')
        if direct.is_file():
            matched.append(('direct', direct))
        for shard in shards:
            counter = shard / relative_counter
            if counter.is_file():
                matched.append((shard.name, counter))
        if not matched:
            matched.append(('none', None))
        for tag, counter in matched:
            tasks.append((index, note, tag, counter))
    return tasks


def run_gcov(task, reports, root):
    index, note, tag, counter = task
    with tempfile.TemporaryDirectory(prefix='wxnotepad-gcov-') as temporary:
        temp_dir = Path(temporary)
        if counter is not None and counter.parent != note.parent:
            target = temp_dir / note.name
            shutil.copy2(note, target)
            shutil.copy2(counter, temp_dir / counter.name)
        else:
            target = note
        command = ['gcov-15', '-b', '-c', '-p', '-l', '--hash-filenames', '--', str(target)]
        result = subprocess.run(command, cwd=temp_dir, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, text=True, encoding='utf-8')
        entry = '+ ' + ' '.join(command) + '\n' + result.stdout
        if result.returncode or 'stamp mismatch' in result.stdout:
            return entry, 0, note
        kept = 0
        for report in sorted(temp_dir.glob('*.gcov')):
            with report.open(encoding='utf-8') as stream:
                header = stream.readline()
            source = Path(header.split('Source:', 1)[1].strip())
            if source.is_absolute() and source.resolve().is_relative_to(root / 'src'):
                shutil.copy2(report, reports / f'{index}-{tag}-{report.name}')
                kept += 1
        return entry, kept, None


def collect(build, output, root=ROOT):
    build, output, root = build.resolve(), output.resolve(), root.resolve()
    notes = sorted(build.rglob('*.gcno'))
    if not notes:
        raise SystemExit(f'No coverage notes found under {build}')
    shards = shard_directories(build)
    tasks = note_tasks(build, notes, shards)
    reports = output / 'gcov'
    reports.mkdir(parents=True, exist_ok=False)
    kept = 0
    failed = None
    with (output / 'gcov.log').open('w', encoding='utf-8') as log:
        version = subprocess.check_output(['gcov-15', '--version'], text=True, encoding='utf-8')
        log.write(version)
        workers = min(os.cpu_count() or 1, len(tasks))
        with ThreadPoolExecutor(max_workers=workers) as pool:
            for entry, count, error_note in pool.map(lambda item: run_gcov(item, reports, root), tasks):
                log.write(entry)
                kept += count
                if error_note is not None and failed is None:
                    failed = error_note
    if failed is not None:
        raise SystemExit(f'gcov failed for {failed}; inspect {output / "gcov.log"}')
    if not kept:
        raise SystemExit('No maintained src/ coverage reports were generated')
    summary = {'notes': len(notes), 'shards': [path.name for path in shards],
               'reports': kept, 'build': str(build), 'source': str(root)}
    (output / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n', encoding='utf-8')
    print(f'Collected {kept} maintained-source reports from {len(notes)} translation units')


def write_sonar_report(output, root=ROOT):
    output, root = output.resolve(), root.resolve()
    command = [sys.executable, '-m', 'gcovr', '--root', str(root),
               '--filter', re.escape((root / 'src').as_posix() + '/'),
               '--gcov-use-existing-files', '--gcov-keep', '--exclude-throw-branches',
               '--merge-lines', '--sonarqube', str(output / 'sonar.xml'), '--sonarqube-metric', 'branch',
               '--json', str(output / 'coverage.json'),
               str(output / 'gcov')]
    with (output / 'gcovr.log').open('w', encoding='utf-8') as log:
        subprocess.run(command, check=True, stdout=log, stderr=subprocess.STDOUT)


def merge_reports(reports, output, root=ROOT):
    output, root = output.resolve(), root.resolve()
    output.mkdir(parents=True, exist_ok=True)
    command = [sys.executable, '-m', 'gcovr', '--root', str(root),
               '--filter', re.escape((root / 'src').as_posix() + '/'),
               '--merge-lines', '--sonarqube', str(output / 'sonar.xml'), '--sonarqube-metric', 'branch',
               '--json', str(output / 'coverage.json')]
    # Each backend has already applied the source and throw-branch policy while
    # reading its own matched GCC notes/counters. Merge by source/branch identity.
    # Generic XML needs one entry per source line, including template instances.
    # --merge-lines combines their counters; JSON retains the raw instances.
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

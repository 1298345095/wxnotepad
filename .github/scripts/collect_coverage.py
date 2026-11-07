"""Collect unmodified GCC reports for maintained C++ sources, including zero-hit code."""
import argparse
import json
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def collect(build, output, root=ROOT, gcov='gcov-13'):
    build, output, root = build.resolve(), output.resolve(), root.resolve()
    notes = sorted(build.rglob('*.gcno'))
    if not notes:
        raise SystemExit(f'No coverage notes found under {build}')
    reports = output / 'gcov'
    reports.mkdir(parents=True, exist_ok=False)
    kept = 0
    with (output / 'gcov.log').open('w', encoding='utf-8') as log:
        version = subprocess.check_output([gcov, '--version'], text=True, encoding='utf-8')
        log.write(version)
        for index, note in enumerate(notes):
            with tempfile.TemporaryDirectory(prefix='wxnotepad-gcov-') as temporary:
                command = [gcov, '-b', '-c', '-p', '-l', '--hash-filenames', str(note)]
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


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, default=ROOT / '.ci/build/coverage-cmake')
    parser.add_argument('--output', type=Path, default=ROOT / '.ci/reports/coverage')
    parser.add_argument('--gcov', default='gcov-13')
    arguments = parser.parse_args()
    collect(arguments.build_dir, arguments.output, gcov=arguments.gcov)


if __name__ == '__main__':
    main()

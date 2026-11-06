"""Retain matching Linux crash executables and print their native backtraces."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--core-dir', type=Path, default=ROOT / '.ci/reports/cores')
    directory = parser.parse_args().core_dir
    failed = False
    for core in sorted(directory.glob('core.*')):
        # Linux core_pattern is core.%p.%E; %E escapes executable path slashes as !.
        _, pid, encoded = core.name.split('.', 2)
        executable = Path(encoded.replace('!', '/'))
        retained = directory / f'executable-{pid}-{executable.name}'
        shutil.copy2(executable, retained)
        report = directory / f'backtrace-{pid}.txt'
        with report.open('w', encoding='utf-8') as output:
            output.write(f'Core: {core}\nExecutable: {executable}\nRetained: {retained}\n\n')
            output.flush()
            result = subprocess.run(
                ['gdb', '--batch', '--nx', '-ex', 'set pagination off',
                 '-ex', 'info files', '-ex', 'info sharedlibrary',
                 '-ex', 'thread apply all bt full', retained, core],
                env=dict(os.environ, DEBUGINFOD_URLS=''), stdout=output, stderr=subprocess.STDOUT)
        print(report.read_text(encoding='utf-8'), flush=True)
        failed |= result.returncode != 0
    raise SystemExit(1 if failed else 0)


if __name__ == '__main__':
    main()

"""Verify selection menu preferences through independent frames and saved INI bytes."""
import os
import argparse
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


def exercise(source, executable_folder, configuration, environment):
    executable = executable_folder / source.name
    shutil.copy2(source, executable)
    configuration.parent.mkdir(parents=True, exist_ok=True)
    seed = '[Settings]\n; preserved\nCustom=retained\nSelectOption=31\nAutoIndent=0\n'
    configuration.write_text(seed, encoding='utf-16')

    def run(expected, action):
        result = subprocess.run([str(executable), str(expected), action], env=environment,
                                capture_output=True, text=True, encoding='utf-8', timeout=20)
        print(result.stdout, end='')
        if result.returncode:
            raise RuntimeError(result.stdout + result.stderr)
        saved = configuration.read_text(encoding='utf-16')
        assert 'Custom=retained' in saved and '; preserved' in saved and 'AutoIndent=0' in saved, saved
        return saved

    saved = run(31, 'toggle')
    assert 'SelectOption=22' in saved
    run(22, 'keep')
    saved = run(22, 'disable-exit')
    assert 'SelectOption=22' in saved and 'SaveSettings=0' in saved
    saved = run(22, 'toggle')
    assert 'SelectOption=22' in saved
    saved = run(22, 'explicit')
    assert 'SelectOption=31' in saved and 'SaveSettings=0' in saved
    run(31, 'keep')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('executable', type=Path)
    parser.add_argument('--installed-directory', type=Path,
                        help='Owned writable Linux installation prefix for isolated XDG mode')
    args = parser.parse_args()
    source = args.executable.resolve()
    environment = dict(os.environ, PATH=str(source.parent) + os.pathsep + os.environ['PATH'])
    with tempfile.TemporaryDirectory(prefix='wxnotepad-selection-lifecycle-') as name:
        folder = Path(name)
        portable = folder / 'portable'
        portable.mkdir()
        exercise(source, portable, portable / 'Notepad4.ini', environment)
        if args.installed_directory:
            with tempfile.TemporaryDirectory(prefix='wxnotepad-selection-', dir=args.installed_directory) as installed_name:
                native_config = folder / 'config'
                environment['XDG_CONFIG_HOME'] = str(native_config)
                exercise(source, Path(installed_name), native_config / 'Notepad4' / 'Notepad4.ini', environment)
    print('PASS: selection menu explicit/exit saves, disabled exit, reopen and unrelated bits/settings')


if __name__ == '__main__':
    main()

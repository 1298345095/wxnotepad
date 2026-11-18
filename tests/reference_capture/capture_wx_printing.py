"""Capture owned wxnotepad fixture jobs through the native Windows printer dialog."""

import argparse
import json
import os
from pathlib import Path
import subprocess

from capture_printing import Native, save_pdf


def capture(executable, output, action, selection=False, direct_write=False, wrapped=False):
    output.mkdir(parents=True)
    trigger = output / 'print.trigger'
    env = os.environ.copy()
    env['WXNOTEPAD_PRINT_CAPTURE_TRIGGER'] = str(trigger)
    if selection:
        env['WXNOTEPAD_PRINT_CAPTURE_SELECTION'] = '1'
    if direct_write:
        env['WXNOTEPAD_PRINT_CAPTURE_DIRECTWRITE'] = '1'
    if wrapped:
        env['WXNOTEPAD_PRINT_CAPTURE_WRAPPED'] = '1'
    rows = {}
    with (output / 'application.log').open('wb') as log:
        process = subprocess.Popen([executable], env=env, cwd=output, stdout=log, stderr=subprocess.STDOUT)
        native = Native(process)
        try:
            frame = native.wait(lambda: next((window for window in native.windows() if native.user.GetMenu(window)),
                                             None), 'owned wxnotepad fixture frame')
            rows['menu'] = native.menu(frame)
            rows['native_dialog'] = native.automate_print(frame, action, output, trigger.touch)
            if action != 'Cancel':
                save_pdf(native, output / 'output.pdf', rows)
            if process.wait(timeout=8) != 0:
                raise RuntimeError('Windows printing fixture failed its document-state assertions')
        finally:
            if process.poll() is None:
                process.kill()
                process.wait()
            (output / 'capture.json').write_text(json.dumps(rows, ensure_ascii=False, indent=2) + '\n',
                                                 encoding='utf-8')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--executable', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--action', choices=('Cancel', 'Print', 'PrintRange'), required=True)
    parser.add_argument('--selection', action='store_true')
    parser.add_argument('--direct-write', action='store_true')
    parser.add_argument('--wrapped', action='store_true')
    args = parser.parse_args()
    capture(args.executable.resolve(), args.output.resolve(), args.action, args.selection, args.direct_write, args.wrapped)


if __name__ == '__main__':
    main()

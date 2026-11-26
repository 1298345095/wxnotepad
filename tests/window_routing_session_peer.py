"""An independently displayed packaged editor in an actual second desktop session."""
import json
import os
from pathlib import Path
import subprocess
import sys
import time

from localization_package import environment
from startup_options_process import start


def main():
    executable, document, control = map(Path, sys.argv[1:4])
    code = sys.argv[4]
    control.mkdir(parents=True, exist_ok=True)
    env = environment(executable, control / 'children', code)
    app = start(executable, control / 'app', env, arguments=('-rs', str(document)))
    try:
        # Within this second session, an ordinary SFI launch must find its own
        # receiver. It must never see the editor on the outer test's display.
        subprocess.run([str(executable), '-ns', str(document)], env=env, timeout=5, check=True)
        record = {'pid': app.process.pid, 'backend': app.state['backend'],
                  'display': os.environ.get('DISPLAY'), 'wayland': os.environ.get('WAYLAND_DISPLAY'),
                  'runtime': os.environ.get('XDG_RUNTIME_DIR'), 'document_path': app.state['document_path']}
        pending = control / 'ready.tmp'
        pending.write_text(json.dumps(record), encoding='utf-8')
        pending.replace(control / 'ready.json')
        deadline = time.monotonic() + 30
        while not (control / 'done').exists():
            assert time.monotonic() < deadline, 'Outer session did not release its owned peer'
            time.sleep(.02)
        app.close()
    finally:
        app.stop()


if __name__ == '__main__':
    main()

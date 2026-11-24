"""A pending native paste must not prevent the owned peer from shutting down."""
import argparse
from pathlib import Path
import subprocess
import tempfile
import time

RESPONSE_TIMEOUT_SECONDS = 5


def check(executable, evidence):
    evidence.mkdir(parents=True, exist_ok=True)
    directory = Path(tempfile.mkdtemp(prefix='attempt-', dir=evidence)).resolve()
    print(f'Clipboard peer lifecycle evidence: {directory}', flush=True)
    with (directory / 'peer.log').open('wb') as log:
        process = subprocess.Popen([str(executable.resolve()), str(directory)],
                                   stdout=log, stderr=subprocess.STDOUT)
        try:
            def wait_file(name, expected):
                deadline = time.monotonic() + RESPONSE_TIMEOUT_SECONDS
                while time.monotonic() < deadline:
                    path = directory / name
                    if path.exists() and path.read_text(encoding='utf-8') == expected:
                        return
                    assert process.poll() is None, ('Peer exited before response', process.returncode, name)
                    time.sleep(.01)
                stage = directory / 'peer-stage'
                raise AssertionError((name, expected, stage.read_text(encoding='utf-8')))

            def request(command):
                (directory / 'response').unlink(missing_ok=True)
                temporary = directory / 'request.tmp'
                temporary.write_text(command, encoding='utf-8')
                temporary.replace(directory / 'request')

            request('ready')
            wait_file('response', 'ready')
            request('utility-empty')
            wait_file('response', 'ready')
            request('paste')
            wait_file('peer-stage', 'native-paste-returned')
            assert not (directory / 'response').exists(), 'Empty native paste must still be pending'
            request('quit')
            wait_file('response', 'bye')
            assert process.wait(timeout=RESPONSE_TIMEOUT_SECONDS) == 0, 'Owned peer must exit after acknowledging quit'
            print('PASS: native empty paste remains pending, quit responds and owned peer exits', flush=True)
        finally:
            if process.poll() is None:
                process.kill()
                process.wait(timeout=RESPONSE_TIMEOUT_SECONDS)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('executable', type=Path)
    parser.add_argument('--evidence', type=Path, required=True)
    arguments = parser.parse_args()
    check(arguments.executable, arguments.evidence)


if __name__ == '__main__':
    main()

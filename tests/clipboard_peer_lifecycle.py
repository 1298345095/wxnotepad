"""A pending native paste must not prevent the owned peer from shutting down."""
import argparse
import ctypes
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
                expected_values = (expected,) if isinstance(expected, str) else expected
                deadline = time.monotonic() + RESPONSE_TIMEOUT_SECONDS
                while time.monotonic() < deadline:
                    path = directory / name
                    try:
                        received = path.read_text(encoding='utf-8') if path.exists() else None
                    except PermissionError:
                        # A published response can briefly be held by another Windows reader.
                        received = None
                    if received in expected_values:
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
            sample = 'x' * (64 * 1024) + '\n中文'
            (directory / 'offer.txt').write_text(sample, encoding='utf-8')
            request('utility-offer')
            wait_file('response', 'ready')
            user = ctypes.WinDLL('user32', use_last_error=True)
            user.OpenClipboard.argtypes = [ctypes.c_void_p]
            user.OpenClipboard.restype = ctypes.c_int
            user.CloseClipboard.argtypes = []
            user.CloseClipboard.restype = ctypes.c_int
            access_deadline = time.monotonic() + RESPONSE_TIMEOUT_SECONDS
            while not user.OpenClipboard(None):
                assert time.monotonic() < access_deadline, 'Parent must acquire the clipboard for native contention'
                time.sleep(.01)
            try:
                request('paste')
                wait_file('peer-stage', ('pasting-native-text', 'native-paste-returned'))
                time.sleep(.25)
                assert not (directory / 'response').exists(), 'Paste must not report text while another process holds it'
            finally:
                assert user.CloseClipboard(), 'Parent releases its owned clipboard lock'
            wait_file('response', sample)
            print('PASS: one stock native Paste waits for foreign clipboard release and transfers exact text', flush=True)
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

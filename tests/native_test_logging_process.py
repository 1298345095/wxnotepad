"""A busy clipboard must fail the native test through stderr, without a log dialog."""
import argparse
from pathlib import Path
import subprocess
import tempfile
import time

PEER_TIMEOUT_SECONDS = 5
TEST_TIMEOUT_SECONDS = 10


def request(directory, peer, command):
    response = directory / 'response'
    response.unlink(missing_ok=True)
    temporary = directory / 'request.tmp'
    temporary.write_text(command, encoding='utf-8')
    temporary.replace(directory / 'request')
    deadline = time.monotonic() + PEER_TIMEOUT_SECONDS
    while not response.exists():
        if peer.poll() is not None or time.monotonic() >= deadline:
            raise RuntimeError(f'Owned clipboard peer did not answer {command}')
        time.sleep(0.01)
    return response.read_text(encoding='utf-8')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('search_seeding', type=Path)
    parser.add_argument('clipboard_peer', type=Path)
    parser.add_argument('--evidence', required=True, type=Path)
    args = parser.parse_args()
    args.evidence.mkdir(parents=True, exist_ok=True)
    directory = Path(tempfile.mkdtemp(prefix='busy-clipboard-', dir=args.evidence)).resolve()
    print(f'Native failure evidence: {directory}', flush=True)
    with (directory / 'peer.log').open('wb') as peer_log:
        peer = subprocess.Popen([args.clipboard_peer.resolve(), directory],
                                stdout=peer_log, stderr=subprocess.STDOUT)
        try:
            if request(directory, peer, 'hold') != 'held':
                raise AssertionError('The independent peer must hold the clipboard')
            with (directory / 'stdout.log').open('wb') as stdout, (directory / 'stderr.log').open('wb') as stderr:
                result = subprocess.run([args.search_seeding.resolve()], stdout=stdout, stderr=stderr,
                                        timeout=TEST_TIMEOUT_SECONDS, check=False)
            error = (directory / 'stderr.log').read_text(encoding='utf-8', errors='replace')
            if result.returncode == 0 or 'FAIL: test must publish its clipboard reset fixture' not in error:
                raise AssertionError('The busy clipboard must produce the original assertion failure on stderr')
        finally:
            try:
                if peer.poll() is None:
                    request(directory, peer, 'release')
                    request(directory, peer, 'quit')
                    peer.wait(timeout=PEER_TIMEOUT_SECONDS)
            finally:
                if peer.poll() is None:
                    peer.kill()
                    peer.wait()
    print('PASS: busy clipboard retains a nonzero test exit and stderr without a modal log dialog')


if __name__ == '__main__':
    main()

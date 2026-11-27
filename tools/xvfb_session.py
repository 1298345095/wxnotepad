"""Run a command on an authenticated Xvfb display allocated by the X server."""

from contextlib import contextmanager, ExitStack, nullcontext
import os
from pathlib import Path
import secrets
import subprocess
import sys
import tempfile

SCREEN_GEOMETRY = "1600x1000x24"
PRIMARY_DISPLAY = 0


def authorize(path, display, cookie):
    subprocess.run(["xauth", "-f", path, "source", "-"], input=f"add :{display} . {cookie}\n", text=True, check=True)


@contextmanager
def display_server(authority, log):
    read_fd, write_fd = os.pipe()
    server = None
    try:
        with os.fdopen(read_fd, encoding="ascii") as ready:
            try:
                server = subprocess.Popen(
                    [
                        "Xvfb",
                        "-displayfd",
                        str(write_fd),
                        "-screen",
                        "0",
                        SCREEN_GEOMETRY,
                        # Sequential GUI processes can connect while a last-client
                        # reset is rebuilding the server, losing their initial connection.
                        "-noreset",
                        # Linux abstract UNIX sockets also work with WSLg's read-only socket mount.
                        "-nolisten",
                        "tcp",
                        "-nolisten",
                        "unix",
                        "-listen",
                        "local",
                        "-auth",
                        str(authority),
                    ],
                    pass_fds=(write_fd,),
                    stdout=log,
                    stderr=log,
                )
            finally:
                os.close(write_fd)
            display = ready.readline().strip()
        if not display:
            raise RuntimeError("Xvfb exited before reporting a ready display")
        yield int(display)
    finally:
        if server is not None:
            if server.poll() is None:
                server.terminate()
            try:
                server.wait(timeout=5)
            except subprocess.TimeoutExpired:
                server.kill()
                server.wait()


def main():
    command = sys.argv[1:]
    if not command:
        raise SystemExit("Usage: xvfb_session.py COMMAND [ARG ...]")
    with tempfile.TemporaryDirectory(prefix="wxnotepad-xvfb-") as temporary:
        authority = Path(temporary) / "Xauthority"
        authority.touch(mode=0o600)
        cookie = secrets.token_hex(16)
        # Xvfb loads the cookie before choosing its display; clients receive a
        # matching entry for the allocated number after the readiness message.
        authorize(authority, 0, cookie)
        reports = os.environ.get("WXNOTEPAD_DISPLAY_LOG_DIR")
        if reports:
            directory = Path(reports)
            directory.mkdir(parents=True, exist_ok=True)
            output = (directory / f"xvfb-{os.getpid()}.log").open("wb")
        else:
            output = nullcontext(sys.stderr)
        with output as log, ExitStack() as servers:
            display = servers.enter_context(display_server(authority, log))
            if display == PRIMARY_DISPLAY:
                # Keep our first server alive while allocating another number;
                # native-input guards deliberately reject the primary desktop.
                display = servers.enter_context(display_server(authority, log))
            authorize(authority, display, cookie)
            environment = dict(os.environ, DISPLAY=f":{display}", XAUTHORITY=str(authority))
            result = subprocess.run(command, env=environment)
            return result.returncode if result.returncode >= 0 else 128 - result.returncode


if __name__ == "__main__":
    sys.exit(main())

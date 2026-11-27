"""Read published app state and send commands through the observation protocol."""

import time

POLL = 0.01


class AppExited(Exception):
    def __init__(self, returncode):
        self.returncode = returncode
        super().__init__(returncode)


class StateTimeout(Exception):
    pass


def read_state(folder):
    return {
        key: value
        for key, value in (line.split("\t", 1) for line in (folder / "state").read_text(encoding="utf-8").splitlines())
    }


def wait_state(folder, process, predicate, timeout, clock=time.monotonic):
    deadline = clock() + timeout
    while clock() < deadline:
        if process.poll() is not None:
            raise AppExited(process.returncode)
        try:
            state = read_state(folder)
            if predicate(state):
                return state
        except FileNotFoundError, PermissionError:
            pass
        time.sleep(POLL)
    raise StateTimeout()


def publish_command(folder, text):
    pending = folder / "pending"
    pending.write_text(text, encoding="utf-8")
    pending.replace(folder / "command")

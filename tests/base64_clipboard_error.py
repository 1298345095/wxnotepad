"""Require an actual Base64 Character Info clipboard error to exit without a modal log dialog."""

import subprocess
import sys


expected = b"FAIL: Character Info copies the same native-byte interpretation shown in its feedback"
try:
    result = subprocess.run(
        [sys.argv[1], "--clipboard-contention"],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        timeout=10,
        check=False,
    )
except subprocess.TimeoutExpired as error:
    sys.stdout.buffer.write(error.stdout or b"")
    raise AssertionError("Clipboard failure must exit instead of waiting in an undriven error dialog") from None
sys.stdout.buffer.write(result.stdout)
assert result.returncode == 1, f"Original assertion failure must retain exit 1, received {result.returncode}"
assert expected in result.stdout, "The actual Character Info clipboard-result assertion must fail"
assert b"Failed to get data from the clipboard" in result.stdout, "Retain the native clipboard error in stderr"
print(
    "PASS: native clipboard contention retains the assertion, error output and failing child exit without a modal hang"
)

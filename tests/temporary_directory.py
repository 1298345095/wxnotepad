"""Temporary directories with bounded Windows sharing-violation cleanup."""

import os
import tempfile
import time


class TemporaryDirectory(tempfile.TemporaryDirectory):
    def cleanup(self):
        deadline = time.monotonic() + 5.0
        while True:
            try:
                super().cleanup()
                return
            except PermissionError as error:
                if os.name != 'nt' or getattr(error, 'winerror', None) != 32:
                    raise
                remaining = deadline - time.monotonic()
                if remaining > 0:
                    time.sleep(min(0.05, remaining))
                    if time.monotonic() < deadline:
                        continue
                error.add_note('Temporary directory cleanup exceeded the 5-second WinError 32 retry deadline.')
                raise

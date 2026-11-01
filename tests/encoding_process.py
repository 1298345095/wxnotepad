"""Original encoding options reach actual Open and Save across fresh processes."""
import os
from pathlib import Path
import shutil
import sys
import tempfile

from resource_process import command, wait_state
from settings_process import Application


def run(executable):
    source = executable.resolve()
    with tempfile.TemporaryDirectory(prefix="wxnotepad-encoding-settings-") as name:
        folder = Path(name)
        application = folder / source.name
        shutil.copy2(source, application)
        environment = dict(os.environ, PATH=str(source.parent) + os.pathsep + os.environ["PATH"])
        config = folder / "Notepad4.ini"
        fixture = folder / "fallback.txt"
        cases = [
            ("DefaultEncoding=936", b"\xd6\xd0\xce\xc4\r\n", "CP936", 0, 8, None),
            ("DefaultEncoding=936\nLoadASCIIasUTF8=0", b"plain", "CP936", 0, 5, None),
            ("DefaultEncoding=936\nLoadASCIIasUTF8=1", b"plain", "UTF-8", 0, 5, None),
            ("DefaultEncoding=4\nLoadASCIIasUTF8=0", b"plain", "UTF-8", 0, 5, None),
            ("DefaultEncoding=4\nLoadASCIIasUTF8=0", b"", "UTF-8", 1, 0, b"\xef\xbb\xbf"),
            ("DefaultEncoding=1\nLoadASCIIasUTF8=0", b"", "UTF-16LE", 1, 0, b"\xff\xfe"),
            ("DefaultEncoding=6\nLoadASCIIasUTF8=0", b"", "UTF-16LE", 0, 0, None),
            ("DefaultEncoding=936\nNoEncodingTags=1", b"# coding: windows1252\nplain", "UTF-8", 0, 27, None),
            ("DefaultEncoding=3", b"plain", "UTF-8", 0, 5, None),
            ("DefaultEncoding=-4294967296\nLoadASCIIasUTF8=0", b"plain", "UTF-8", 0, 5, None),
        ]
        for seed, data, encoding, bom, length, saved_bytes in cases:
            config.write_text("[Settings]\n" + seed + "\n[Unrelated]\nKeep=1\n", encoding="utf-16")
            fixture.write_bytes(data)
            app = Application(application, folder, environment)
            try:
                command(folder, f"1\nopen\n{fixture}\n")
                state = wait_state(folder, app.process, lambda s: s["sequence"] == "1")
                assert state["result"] == "ok" and state["length"] == str(length), (seed, state)
                assert state["encoding"] == encoding and state["bom"] == str(bom), (seed, state)
                command(folder, "2\nsave\n")
                state = wait_state(folder, app.process, lambda s: s["sequence"] == "2")
                assert state["result"] == "ok" and fixture.read_bytes() == (data if saved_bytes is None else saved_bytes), (seed, state)
                app.sequence = 2
                app.close()
                assert "Keep=1" in config.read_text(encoding="utf-16")
                if seed == "DefaultEncoding=3" or seed.startswith("DefaultEncoding=-4294967296"):
                    assert "DefaultEncoding=" not in config.read_text(encoding="utf-16")
                print(f"PASS: fresh-process {seed!r} -> {encoding}, BOM={bom}, exact Save")
            finally:
                app.stop()


if __name__ == "__main__":
    run(Path(sys.argv[1]))

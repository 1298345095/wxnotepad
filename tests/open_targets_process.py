"""Run native target actions with an owned source and independent editor children."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import shutil


def main():
    source = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix="wxnotepad-open-targets-") as name:
        folder = Path(name)
        executable = folder / source.name
        shutil.copy2(source, executable)
        (folder / "parent.txt").write_bytes(b"parent fixture\n")
        environment = dict(os.environ, WXNOTEPAD_OPEN_TARGET_ROOT=str(folder),
                           PATH=str(source.parent) + os.pathsep + os.environ["PATH"])
        if sys.platform != "win32":
            applications = folder / "data" / "applications"
            applications.mkdir(parents=True)
            config = folder / "config"
            config.mkdir()
            handler = Path(__file__).with_name("open_target_handler.py")
            (applications / "wxnotepad-open-target.desktop").write_text(
                "[Desktop Entry]\nType=Application\nName=Owned target recorder\n"
                f'Exec="{sys.executable}" "{handler}" %u\n'
                "MimeType=x-scheme-handler/wx242;x-scheme-handler/http;x-scheme-handler/https;"
                "x-scheme-handler/mailto;inode/directory;text/plain;application/octet-stream;\n",
                encoding="utf-8")
            (config / "mimeapps.list").write_text("[Default Applications]\n" + "".join(
                f"{kind}=wxnotepad-open-target.desktop;\n" for kind in (
                    "x-scheme-handler/wx242", "x-scheme-handler/http", "x-scheme-handler/https",
                    "x-scheme-handler/mailto", "inode/directory", "text/plain", "application/octet-stream")),
                encoding="utf-8")
            environment.update(XDG_DATA_HOME=str(applications.parent), XDG_CONFIG_HOME=str(config))
        manager = None
        try:
            if sys.platform == "win32":
                manager = subprocess.Popen(["pwsh", "-NoProfile", "-File", str(Path(__file__).with_name("open_target_explorer.ps1")),
                                            "-Root", str(folder)], env=environment)
                deadline = time.monotonic() + 10
                while not (folder / "explorer-ready").exists() and time.monotonic() < deadline:
                    assert manager.poll() is None, "owned Explorer observer must stay running"
                    time.sleep(0.01)
                assert (folder / "explorer-ready").exists(), "owned Explorer observer must record its baseline"
            else:
                manager = subprocess.Popen([str(executable), "--file-manager"], env=environment)
                deadline = time.monotonic() + 10
                while not (folder / "filemanager-ready").exists() and time.monotonic() < deadline:
                    assert manager.poll() is None, "owned file-manager service must stay running"
                    time.sleep(0.01)
                assert (folder / "filemanager-ready").exists(), "owned file-manager service must acquire its name"
            subprocess.run([str(executable)], cwd=folder, env=environment, check=True, timeout=45)
            if sys.platform == "win32":
                assert manager.wait(timeout=10) == 0, "actual Explorer folder/item observations must succeed"
        finally:
            if manager and manager.poll() is None:
                manager.terminate()
                manager.wait(timeout=10)
        assert (folder / "parent.txt").read_bytes() == b"parent fixture\n"
    print("PASS: native target actions preserve independently read source file")


if __name__ == "__main__":
    main()

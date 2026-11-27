"""Run native selection actions with private Linux desktop associations."""

import os
import subprocess
import sys
import tempfile
from pathlib import Path


def main():
    executable = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix="wxnotepad-selection-actions-") as temporary:
        root = Path(temporary).resolve()
        environment = dict(os.environ)
        if sys.platform != "win32":
            applications = root / "data/applications"
            applications.mkdir(parents=True)
            config = root / "config"
            config.mkdir()
            (applications / "wxnotepad-selection-actions.desktop").write_text(
                "[Desktop Entry]\nType=Application\nName=Owned selection URI recorder\n"
                f'Exec="{executable}" --desktop-target %u "{root}"\n'
                "MimeType=x-scheme-handler/http;x-scheme-handler/https;\n",
                encoding="utf-8",
            )
            (config / "mimeapps.list").write_text(
                "[Default Applications]\nx-scheme-handler/http=wxnotepad-selection-actions.desktop;\n"
                "x-scheme-handler/https=wxnotepad-selection-actions.desktop;\n",
                encoding="utf-8",
            )
            environment.update(XDG_DATA_HOME=str(applications.parent), XDG_CONFIG_HOME=str(config))
        subprocess.run([str(executable)], env=environment, check=True)
        if sys.platform != "win32":
            for language in (1033, 2052, 1028):
                subprocess.run(
                    [str(executable), "--provider-defaults", str(root), str(language)], env=environment, check=True
                )
            for receipt in sorted(root.glob("received-*.txt")):
                print("ACTUAL native URI argv: " + repr(receipt.read_bytes()), flush=True)


if __name__ == "__main__":
    main()

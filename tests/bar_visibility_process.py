"""Check original visibility defaults, save policies and independent native relaunches."""
from contextlib import ExitStack
from pathlib import Path
import os
import shutil
import subprocess
import sys
import tempfile

source = Path(sys.argv[1]).resolve()
with ExitStack() as stack:
    folder = Path(stack.enter_context(tempfile.TemporaryDirectory(prefix="wxnotepad-bars-")))
    executable_folder = folder
    config_folder = folder
    installed = os.environ.get("WXNOTEPAD_TEST_INSTALLED_DIRECTORY")
    if installed:
        executable_folder = Path(stack.enter_context(tempfile.TemporaryDirectory(dir=installed)))
        config_folder = folder / "native-config" / "Notepad4"
        config_folder.mkdir(parents=True)
    executable = executable_folder / source.name
    shutil.copy2(source, executable)
    environment = dict(os.environ, PATH=str(source.parent) + os.pathsep + os.environ["PATH"])
    if installed:
        environment["XDG_CONFIG_HOME"] = str(folder / "native-config")
    ini = config_folder / "Notepad4.ini"

    def run(menu, status, action="keep", policy="exit"):
        result = subprocess.run([str(executable), str(int(menu)), str(int(status)), action, policy],
                                env=environment, capture_output=True, text=True, timeout=20)
        assert result.returncode == 0, result.stdout + result.stderr

    for menu, status in [(True, True), (True, False), (False, True), (False, False)]:
        for save_on_exit, explicit in [(True, False), (False, False), (False, True)]:
            ini.write_text("[Settings]\n; retained\nCustom=保留\n" +
                           f"ShowMenu={int(menu)}\nShowStatusbar={int(status)}\n" +
                           ("" if save_on_exit else "SaveSettings=0\n") +
                           "[Other]\nShowMenu=unrelated\n", encoding="utf-16")
            run(menu, status, "toggle", "explicit" if explicit else "exit")
            saved = ini.read_text(encoding="utf-16").replace("\r\n", "\n")
            assert "; retained" in saved and "Custom=保留" in saved
            assert "[Other]\nShowMenu=unrelated\n" in saved
            expected = (not menu, not status) if save_on_exit or explicit else (menu, status)
            section = saved.split("[Other]", 1)[0]
            for key, visible in zip(("ShowMenu", "ShowStatusbar"), expected):
                entries = [line for line in section.splitlines() if line.startswith(key + "=")]
                expected_entries = [] if visible and (save_on_exit or explicit) else [f"{key}={int(visible)}"]
                assert entries == expected_entries, saved
            run(*expected)
    ini.write_text("[Settings]\n", encoding="utf-16")
    run(True, True)
print("PASS: original visibility values/defaults, disabled/explicit exit-save and native restore after relaunch")

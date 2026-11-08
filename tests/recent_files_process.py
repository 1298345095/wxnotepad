"""Verify recent history through independent application processes and saved INI bytes."""
import os
from pathlib import Path
import shutil
import sys
import tempfile
import subprocess
import configparser

from settings_process import Application


def main():
    source = Path(sys.argv[1]).resolve()
    probe_source = Path(sys.argv[2]).resolve()
    with tempfile.TemporaryDirectory(prefix="wxnotepad-recent-process-", dir=source.parent) as name:
        folder = Path(name)
        executable = folder / source.name
        shutil.copy2(source, executable)
        probe = folder / probe_source.name
        shutil.copy2(probe_source, probe)
        environment = dict(os.environ, PATH=str(source.parent) + os.pathsep + os.environ["PATH"])
        seed = folder / "Notepad4.ini-default"
        seed.write_text("[Settings]\nSaveRecentFiles=65\nSaveSettings=0\nUnrelatedSetting=keep\n"
                        "[Styles]\nUnrelatedStyle=keep\n", encoding="utf-16")
        first = folder / "first.txt"
        first.write_bytes(b"first")
        app = Application(executable, folder, environment, first)
        try:
            app.close()
            saved = (folder / "Notepad4.ini").read_text(encoding="utf-16")
            assert "[Recent Files]" in saved and "first.txt" in saved, saved
            assert "SaveRecentFiles=65" in saved and "UnrelatedSetting=keep" in saved and "UnrelatedStyle=keep" in saved, saved
            if os.name != "nt" or source.drive.lower() != Path(os.environ["WINDIR"]).drive.lower():
                assert any(line in saved for line in ("01=first.txt", "01=.\\first.txt", "01=./first.txt")), saved
        finally:
            app.stop()
        def history():
            document = configparser.ConfigParser(interpolation=None)
            document.read(folder / "Notepad4.ini", encoding="utf-16")
            return [Path(value.replace("\\", os.sep)).name for value in document["Recent Files"].values()]

        def restart(paths):
            result = subprocess.run([str(probe), "--history", *(str(path) for path in paths)], env=environment,
                                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
            assert result.returncode == 0, result.stdout

        restart([first])
        second, third = folder / "second.txt", folder / "third.txt"
        second.write_bytes(b"second")
        third.write_bytes(b"third")
        observe_a, observe_b = folder / "a", folder / "b"
        observe_a.mkdir()
        observe_b.mkdir()
        first_process = Application(executable, observe_a, environment, second)
        second_process = Application(executable, observe_b, environment, third)
        try:
            first_process.close()
            second_process.close()
            assert history() == ["third.txt", "first.txt", "second.txt"], history()
            restart([third, first, second])
        finally:
            first_process.stop()
            second_process.stop()
        print("PASS: restart and sequential closes preserve each process's additions with original MergeSave order")
        ini = folder / "Notepad4.ini"
        ini.write_text(f"[Settings]\nSaveSettings=0\nUnrelatedSetting=keep\n[Recent Files]\n01={first}\n", encoding="utf-16")
        restart([])
        app = Application(executable, folder, environment, third)
        try:
            app.close()
            assert history() == ["first.txt"], history()
            restart([])
        finally:
            app.stop()
        print("PASS: remember defaults Off, ignores stored history and retains previous persisted section")
        fixtures = [folder / f"capacity-{index:02}.txt" for index in range(45)]
        for path in fixtures:
            path.write_bytes(b"capacity")
        entries = "".join(f"{index + 1:02}={path}\n" for index, path in enumerate(fixtures))
        ini.write_text("[Settings]\nSaveRecentFiles=81\n[Settings2]\nPortableMyDocs=0\n"
                       "[Recent Files]\n" + entries, encoding="utf-16")
        restart(fixtures[:40])
        app = Application(executable, folder, environment, fixtures[40])
        try:
            app.close()
            expected = [fixtures[40], *fixtures[:39]]
            assert history() == [path.name for path in expected], history()
            restart(expected)
            assert "SaveRecentFiles=81" in ini.read_text(encoding="utf-16")
        finally:
            app.stop()
        app = Application(executable, folder, environment)
        try:
            app.named_menu("Settings", "Remember Recent Files")
            app.close()
            assert "SaveRecentFiles=80" in ini.read_text(encoding="utf-16")
            assert history() == [path.name for path in expected], history()
            restart([])
        finally:
            app.stop()
        app = Application(executable, folder, environment)
        try:
            app.named_menu("Settings", "Remember Recent Files")
            app.close()
            assert "SaveRecentFiles=81" in ini.read_text(encoding="utf-16")
            restart(expected)
        finally:
            app.stop()
        print("PASS: native Remember Recent Files menu changes only encoded remember bit and retains capacity/history")
        ini.write_text("[Settings]\nSaveRecentFiles=3\n[Recent Files]\n" + entries, encoding="utf-16")
        restart(fixtures[:32])
        print("PASS: encoded capacity40 loads/evicts forty entries; encoded3 uses original minimum32")
        upper = folder / "FIRST.TXT"
        if os.name != "nt":
            upper.write_bytes(b"distinct case")
        ini.write_text("[Settings]\nSaveRecentFiles=65\n[Settings2]\nRelativeFileMRU=0\n"
                       f"[Recent Files]\n01={upper}\n", encoding="utf-16")
        app = Application(executable, folder, environment, first)
        try:
            app.close()
            restart([upper] if os.name == "nt" else [first, upper])
        finally:
            app.stop()
        print("PASS: Windows ordinal case dedup retains stored spelling; case-sensitive hosts retain distinct paths")
        if os.name == "nt":
            import ctypes
            from ctypes import wintypes
            import uuid
            shell = ctypes.WinDLL("shell32")
            ole = ctypes.WinDLL("ole32")
            token = (ctypes.c_ubyte * 16).from_buffer_copy(uuid.UUID("fdd39ad0-238f-46af-adb4-6c85480369c7").bytes_le)
            documents = ctypes.c_void_p()
            shell.SHGetKnownFolderPath.argtypes = [ctypes.c_void_p, wintypes.DWORD, wintypes.HANDLE,
                                                   ctypes.POINTER(ctypes.c_void_p)]
            ole.CoTaskMemFree.argtypes = [ctypes.c_void_p]
            assert shell.SHGetKnownFolderPath(ctypes.byref(token), 0, None, ctypes.byref(documents)) == 0
            try:
                documents_path = Path(ctypes.wstring_at(documents))
            finally:
                ole.CoTaskMemFree(documents)
            with tempfile.TemporaryDirectory(prefix="wxnotepad-recent-", dir=documents_path) as documents_name:
                document = Path(documents_name) / "portable.txt"
                document.write_bytes(b"portable Documents")
                ini.write_text("[Settings]\nSaveRecentFiles=65\n", encoding="utf-16")
                app = Application(executable, folder, environment, document)
                try:
                    app.close()
                    assert "%CSIDL:MYDOCUMENTS%" in ini.read_text(encoding="utf-16")
                    restart([document])
                finally:
                    app.stop()
            environment["WXNOTEPAD_RECENT_FIXTURE"] = str(folder)
            ini.write_text("[Settings]\nSaveRecentFiles=65\n[Recent Files]\n"
                           "01=%WXNOTEPAD_RECENT_FIXTURE%\\first.txt\n", encoding="utf-16")
            restart([first])
            print("PASS: portable Documents token and environment expansion resolve through native restart chooser")
    print("PASS: remembered recent history saves independently of SaveSettings")


if __name__ == "__main__":
    main()

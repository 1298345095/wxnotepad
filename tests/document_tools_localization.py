"""Review document-tool captions and the directory chooser in packaged native apps."""

import argparse
import ctypes
import sys
import tempfile
from pathlib import Path

from localization_layout import capture_and_check, owned_topmost
from localization_native import plain
from localization_package import menus, read_settings
from localization_process import PREFERENCE, Acceptance

CAPTIONS = {
    "en": (
        "Execute &Document\tCtrl+L",
        "O&pen Document With...",
        "&Save Before Running Tools",
        "Open Document With",
        "Browse Directory...",
    ),
    "zh_CN": ("执行文档(&D)\tCtrl+L", "打开文档方式(&P)...", "运行工具前保存(&S)", "打开文档方式", "浏览目录(B)..."),
    "zh_TW": ("執行文件(&D)\tCtrl+L", "開啟文件方式(&P)...", "執行工具前儲存(&S)", "開啟文件方式", "瀏覽目錄(B)..."),
}


def capture(fixture, app, window, path):
    with owned_topmost(fixture.dialogs, app.process, window):
        if sys.platform == "win32":
            ctypes.WinDLL("dwmapi").DwmFlush()
        capture_and_check(fixture.dialogs, app, window, path)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    parser.add_argument("--cmake-dir", type=Path, required=True)
    parser.add_argument("--evidence", type=Path, required=True)
    args = parser.parse_args()
    args.evidence.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="document-tools-localization-") as directory:
        fixture = Acceptance(args, Path(directory))
        programs = Path(directory) / "available programs 中文"
        programs.mkdir()
        changed = Path(directory) / "changed programs 中文"
        changed.mkdir()
        (programs / ("sample.lnk" if sys.platform == "win32" else "sample.desktop")).write_text("owned program entry")
        for code in ("en", "zh_CN", "zh_TW"):
            execute, open_with, save_tools, title, browse = CAPTIONS[code]
            app = fixture.start(PREFERENCE[code], preferred="en", extra=f"OpenWithDir={programs}\n", save=True)
            try:
                entries = menus(app.state)
                ids = {
                    caption: next(identity for identity, item in entries.items() if item["label"] == caption)
                    for caption in (execute, open_with, save_tools)
                }
                assert entries[ids[execute]]["enabled"] == "0"
                assert entries[ids[open_with]]["enabled"] == "1"
                assert entries[ids[save_tools]]["checked"] == "0"
                app.choose(ids[save_tools])
                assert menus(app.state)[ids[save_tools]]["checked"] == "1"
                app.begin("menu", str(ids[open_with]))
                window = fixture.dialogs.wait(app.process, title)
                assert browse in [plain(text) for text in fixture.dialogs.texts(window)]
                capture(fixture, app, window, fixture.logs / f"open-with-{code}.png")
                fixture.dialogs.click(window, "Cancel" if sys.platform != "win32" or code == "en" else "取消")
                app.finish()
                app.close()
                config = read_settings(fixture.config)
                assert config["Settings"]["SaveBeforeRunningTools"] == "1"
                assert config["Settings2"]["UILanguage"] == str(PREFERENCE[code])
                assert "available programs" in config["Settings"]["OpenWithDir"]
            finally:
                app.stop()
    print("PASS: three native UI languages, chooser cancellation and shared preference round trips")


if __name__ == "__main__":
    main()

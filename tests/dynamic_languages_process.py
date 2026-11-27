"""Original script identities/settings/Restart through actual packaged processes."""

import argparse
import json
import tempfile
from pathlib import Path

from localization_package import Process, bundle_application, environment, menu_id, menus, settings, wait_child

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("executable", type=Path)
parser.add_argument("--gdi", action="store_true")
parser.add_argument("--cmake-dir", required=True, type=Path)
parser.add_argument("--evidence", required=True, type=Path)
args = parser.parse_args()
args.evidence.mkdir(parents=True, exist_ok=True)
# Pinned SciLexer.h identities and the original stlRuby/Perl/Tcl comment styles.
PINNED_LEXERS = {"rb": "22", "pl": "6", "tcl": "25"}
PINNED_COMMENT_STYLES = {"rb": "1", "pl": "1", "tcl": "2"}
ORIGINAL_COMMENT_FOREGROUND_BGR = str(0x608060)
with tempfile.TemporaryDirectory(prefix="dynamic-languages-") as name:
    root = Path(name)
    executable = bundle_application(
        args.executable.resolve(), args.cmake_dir.resolve(), root / "bundle", args.evidence / root.name / "bundle"
    )
    config = executable.parent / "Notepad4.ini"
    children = root / "children"
    children.mkdir()
    restart = automatic = None
    captions = {
        "en": ["Ruby Script", "Perl Script", "Tcl Script"],
        "zh_CN": ["Ruby 脚本", "Perl 脚本", "Tcl 脚本"],
        "zh_TW": ["Ruby 指令碼", "Perl 指令碼", "Tcl 指令碼"],
    }
    for code, preference in [("en", 1033), ("zh_CN", 2052), ("zh_TW", 1028)]:
        for index, (suffix, section, identity) in enumerate(
            [("rb", "Ruby Script", 33), ("pl", "Perl Script", 31), ("tcl", "Tcl Script", 35)]
        ):
            extra = (
                "DetectIndentation=0\nFileWatchingMode=0\n"
                f"[Styles]\nDefaultScheme={identity}\nFavoriteSchemes=33,31,35,2\n"
                f"[{section}]\nUseGlobalTabSettings=0\nTabWidth=6\nIndentWidth=2\nTabsAsSpaces=1\n"
            )
            if args.gdi:
                extra = "RenderingTechnology=0" + chr(10) + extra
            settings(config, preference, extra)
            path = root / f"{code}-document-界.{suffix}"
            original = b"\xef\xbb\xbf" + "# preserved 界\r\n".encode()
            path.write_bytes(original)
            app = Process(executable, root / f"app-{code}-{suffix}", environment(executable, children, code), path)
            child = None
            try:
                if code == "en":
                    restart = menu_id(app.state, "Restart")
                    automatic = menu_id(app.state, "Automatic")
                caption = captions[code][index]
                chosen = menu_id(app.state, caption)
                assert caption in app.state["document_status"], app.state
                assert app.state["tab_width"] == "6" and app.state["indent_width"] == "2", app.state
                app.choose(chosen)
                before = dict(app.state)
                assert before["lexer"] == PINNED_LEXERS[suffix], before
                assert before["style_at_caret"] == PINNED_COMMENT_STYLES[suffix], before
                assert before["style_foreground_at_caret"] == ORIGINAL_COMMENT_FOREGROUND_BGR, before
                previous = {folder.name for folder in children.iterdir()}
                app.begin("menu", str(restart))
                assert app.process.wait(timeout=10) == 0
                child = wait_child(children, previous)
                for field in (
                    "document_path",
                    "length",
                    "encoding",
                    "bom",
                    "eol_mode",
                    "tab_width",
                    "indent_width",
                    "lexer",
                    "style_at_caret",
                    "style_foreground_at_caret",
                    "style_background_at_caret",
                    "can_undo",
                    "can_redo",
                ):
                    assert child.state[field] == before[field], (field, before, child.state)
                assert child.process.pid != app.process.pid
                assert menus(child.state)[menu_id(child.state, caption)]["checked"] == "1", child.state
                assert caption in child.state["document_status"], child.state
                child.send("save")
                assert path.read_bytes() == original
                child.choose(automatic)
                assert menus(child.state)[automatic]["checked"] == "1"
                child.close()
                child = None
                assert f"DefaultScheme={identity}" in config.read_text(encoding="utf-16")
                assert "FavoriteSchemes=33,31,35,2" in config.read_text(encoding="utf-16")
                reopened = Process(
                    executable, root / f"reopen-{code}-{suffix}", environment(executable, children, code), path
                )
                try:
                    assert caption in reopened.state["document_status"], reopened.state
                    assert reopened.state["lexer"] == PINNED_LEXERS[suffix], reopened.state
                    assert reopened.state["style_at_caret"] == PINNED_COMMENT_STYLES[suffix], reopened.state
                    assert reopened.state["can_undo"] == "0" and reopened.state["can_redo"] == "0", reopened.state
                    reopened.send("save")
                    assert path.read_bytes() == original
                    reopened.close()
                finally:
                    reopened.stop()
                # Unknown-file fallback exercises each stored original identity independently.
                unknown = root / "unknown.extension"
                unknown.write_bytes(original)
                fallback = Process(
                    executable, root / f"default-{code}-{suffix}", environment(executable, children, code), unknown
                )
                try:
                    assert caption in fallback.state["document_status"], fallback.state
                    fallback.close()
                finally:
                    fallback.stop()
                (args.evidence / root.name / f"{code}-{suffix}.json").write_text(
                    json.dumps(
                        {"parent": before, "reopened": reopened.state, "default": fallback.state},
                        ensure_ascii=False,
                        indent=2,
                    ),
                    encoding="utf-8",
                )
            finally:
                app.stop()
                if child:
                    child.stop()
            print(f"PASS: packaged {code} {section} menu/settings/default identity and independent Restart", flush=True)

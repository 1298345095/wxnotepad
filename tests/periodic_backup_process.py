"""Exercise periodic backup and per-process retention in an actual development bundle."""
from english_settings import english_settings
import argparse
import sys
import os
from pathlib import Path
import tempfile
import time

from localization_package import Process, bundle_application, menu_id
from localization_native import Dialogs, plain
from localization_layout import capture_and_check, owned_topmost
from resource_process import native_edit, wait_state
from settings_process import Application


def wait_for(check, message):
    deadline = time.monotonic() + 5
    while time.monotonic() < deadline:
        if check():
            return
        time.sleep(.01)
    raise AssertionError(message)


def localized_dialogs(executable, root, environment):
    dialogs = Dialogs(root / 'native-scenes')
    screenshots = Path.cwd() / 'Testing' / 'Temporary' / 'periodic-backup-localization'
    for code, language, title, caption, period, periodic, retained, folder in (
            ('en', 1033, 'AutoSave Settings', 'AutoSave Settings...', 'Backup period in seconds',
             'Enable periodic backup.', 'Manually delete backups.', 'Open Folder'),
            ('zh_CN', 2052, '自动保存设置', '自动保存设置...', '备份间隔（秒）',
             '启用定期备份(P)。', '手动删除备份(M)。', '打开文件夹(F)'),
            ('zh_TW', 1028, '自動儲存設定', '自動儲存設定...', '備份間隔（秒）',
             '啟用定期備份(P)。', '手動刪除備份(M)。', '開啟資料夾(F)')):
        ini = executable.parent / 'Notepad4.ini'
        ini.write_text('[Settings]\nSaveSettings=0\nAutoSaveOption=22\nAutoSavePeriod=125\n'
                       f'[Settings2]\nUILanguage={language}\n', encoding='utf-16')
        app = Process(executable, root / ('localized-' + code), environment)
        try:
            identity = menu_id(app.state, caption)
            app.begin('menu', str(identity))
            window = dialogs.wait(app.process, title)
            labels = [plain(text) for text in dialogs.texts(window)]
            assert all(label in labels for label in (periodic, retained, folder)), labels
            dialogs.set_input(window, period, '0.333')
            if code == 'en' and sys.platform != 'win32':
                dialogs.click(window, folder)
                record = root / 'folder-argv'
                wait_for(record.exists, 'Open Folder launches the platform file-manager boundary')
                expected = root / 'xdg' / 'Notepad4' / 'AutoSave'
                assert record.read_text() == str(expected) and expected.is_dir()
            with owned_topmost(dialogs, app.process, window):
                capture_and_check(dialogs, app, window, screenshots / (code + '.png'))
            labels = [plain(text) for text in dialogs.texts(window)]
            dialogs.click(window, next(label for label in labels if label in ("Cancel", "取消") or
                                       label.startswith(("Cancel(", "取消("))))
            app.finish()
            app.begin('menu', str(identity))
            window = dialogs.wait(app.process, title)
            dialogs.set_input(window, period, '0.333')
            dialogs.click(window, periodic)
            ok = 'OK' if sys.platform != 'win32' or code == 'en' else ('确定' if code == 'zh_CN' else '確認')
            labels = [plain(text) for text in dialogs.texts(window)]
            dialogs.click(window, next(label for label in labels if label == ok or label.startswith(ok + '(')))
            app.finish()
            save = menu_id(app.state, {'en': 'Save Settings Now', 'zh_CN': '立即保存设置(N)',
                                      'zh_TW': '立即儲存設定(N)'}[code])
            app.choose(save)
            saved = ini.read_text(encoding='utf-16')
            assert 'AutoSaveOption=23' in saved and 'AutoSavePeriod=333' in saved, saved
            app.close()
        finally:
            app.stop()
    print('PASS: packaged English/Simplified Chinese/Traditional Chinese native dialog controls, cancellation, settings and screenshots')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('executable', type=Path)
    parser.add_argument('--cmake-dir', type=Path, required=True)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='wxnotepad-periodic-package-') as name:
        root = Path(name)
        executable = bundle_application(args.executable.resolve(), args.cmake_dir.resolve(), root / 'bundle')
        ini = executable.parent / 'Notepad4.ini'
        ini.write_text(english_settings('[Settings]\nAutoSaveOption=23\nAutoSavePeriod=150\n'
                                       'FileWatchingMode=0\nWarnLineEndings=0\n'), encoding='utf-16')
        source = root / 'source.txt'
        source.write_bytes(b'packaged\n')
        environment = dict(os.environ)
        if sys.platform != 'win32':
            manager = root / 'bin'
            manager.mkdir()
            opener = manager / 'xdg-open'
            opener.write_text('#!/bin/sh' + chr(10) + 'printf %s \"$1\" > \"$WXNOTEPAD_FOLDER_ARGV\"' + chr(10))
            opener.chmod(0o755)
            environment.update(PATH=str(manager) + os.pathsep + environment['PATH'],
                               XDG_DATA_HOME=str(root / 'xdg'), WXNOTEPAD_FOLDER_ARGV=str(root / 'folder-argv'))
        owned = root / 'observer'
        owned.mkdir()
        retained = None
        for launch in range(2):
            app = Application(executable, owned, environment, source)
            try:
                state = wait_state(owned, app.process, lambda s: s['backend'] != 'GdkWaylandDisplay' or s['active'] == '1')
                native_edit(app.process, state)
                wait_state(owned, app.process, lambda s: s['dirty'] == '1')
                backups = lambda: list(root.glob(f'source.txt *{app.process.pid}.txt'))
                wait_for(lambda: len(backups()) == 1, 'packaged native edit must create a real periodic backup')
                assert backups()[0].read_bytes() == (b'Xpackaged\n' if not launch else b'XXpackaged\n')
                assert source.read_bytes() == (b'packaged\n' if not launch else b'Xpackaged\n')
                app.named_menu('File', 'Save')
                assert not backups(), 'native ordinary Save cleans this process managed entries'
                assert retained is None or retained.is_file(), 'Save never cleans a previous process retained file'
                if not launch:
                    native_edit(app.process, app.state)
                    wait_for(lambda: len(backups()) == 1, 'later edit restarts timer after Save')
                    retained = backups()[0]
                    app.named_menu('Edit', 'Undo')
                    assert app.state['dirty'] == '0', app.state
                app.close()
                assert retained.is_file(), 'clean close retains tracked lifecycle backups'
                saved = ini.read_text(encoding='utf-16')
                assert 'AutoSaveOption=23' in saved and 'AutoSavePeriod=150' in saved, saved
            finally:
                app.stop()
        assert retained.read_bytes() == b'XXpackaged\n'
        localized_dialogs(executable, root, environment)
    print('PASS: actual packaged process native edits, real periodic timers, Save/close and restart ownership')


if __name__ == '__main__':
    main()

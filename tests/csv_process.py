"""Actual packaged CSV/TSV options, localized native layout, edit/save and restart."""
import argparse
import difflib
import os
from pathlib import Path
import sys
import tempfile

from localization_package import Process, bundle_application, menu_id, menus
from localization_native import Dialogs, plain
from localization_layout import capture_and_check, owned_topmost
from resource_process import native_edit, wait_state

LANGUAGES = [
    ('en', 1033, 'CSV File', 'CSV Options', 'Comma (,)', 'Tab', 'Double quote', 'Other:', 'Custom delimiter'),
    ('zh_CN', 2052, 'CSV 文件', 'CSV 选项', '逗号 (,)', '制表符', '双引号', '其他:', '自定义分隔符'),
    ('zh_TW', 1028, 'CSV 檔案', 'CSV 選項', '逗號 (,)', '定位字元', '雙引號', '其他:', '自訂分隔符號'),
]


def checked(dialogs, window, caption):
    for control in dialogs.children(window):
        if sys.platform == 'win32':
            if plain(dialogs.title(control)) == caption:
                return dialogs.user.SendMessageW(control, 0xF0, 0, 0) == 1  # BM_GETCHECK
        elif (plain(control.get_name()) == caption and
              control.get_role() in (dialogs.atspi.Role.RADIO_BUTTON, dialogs.atspi.Role.CHECK_BOX)):
            return control.get_state_set().contains(dialogs.atspi.StateType.CHECKED)
    raise AssertionError(f'Native CSV option absent: {caption}')


def accept(dialogs, window):
    labels = [plain(text) for text in dialogs.texts(window)]
    caption = next(text for text in labels if any(text == value or text.startswith(value + '(')
                                                  for value in ('OK', '确定', '確定', '確認')))
    dialogs.click(window, caption)


def select_radio(dialogs, window, caption):
    if sys.platform == 'win32':
        dialogs.click(window, caption)
        return
    control = next(control for control in dialogs.children(window)
                   if control.get_role() == dialogs.atspi.Role.RADIO_BUTTON and plain(control.get_name()) == caption)
    assert control.get_action_iface().do_action(0), caption


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('executable', type=Path)
    parser.add_argument('--cmake-dir', required=True, type=Path)
    parser.add_argument('--gdi', action='store_true')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='wxnotepad-csv-process-') as directory:
        root = Path(directory)
        executable = bundle_application(args.executable.resolve(), args.cmake_dir.resolve(), root / 'bundle')
        environment = dict(os.environ, PATH=str(executable.parent) + os.pathsep + os.environ['PATH'])
        dialogs = Dialogs(root / 'native-scenes')
        configuration = executable.parent / 'Notepad4.ini'
        for code, language, scheme, title, comma, tab, qualifier, other, custom in LANGUAGES:
            configuration.write_text('[Settings]\nSaveSettings=0\nWordWrap=0\nWarnLineEndings=0\n'
                                     f'RenderingTechnology={0 if args.gdi else 1}\n'
                                     f'[Settings2]\nUILanguage={language}\n'
                                     '[Styles]\nFavoriteSchemes=1 80 50\nDefaultScheme=80\nAutoSelect=1\n'
                                     '[Unrelated]\nKeep=保留\n', encoding='utf-16', newline='\r\n')
            ini_bytes = configuration.read_bytes()
            for extension, delimiter in [('csv', ','), ('tsv', '\t')]:
                path = root / ('data.' + extension)
                original = f'一{delimiter}二{delimiter}三\r\na{delimiter}b{delimiter}c\r\n'.encode('utf-8')
                path.write_bytes(original)
                for restart in range(2):
                    app = Process(executable, root / f'{code}-{extension}-{restart}', environment, path)
                    try:
                        assert scheme in app.state['document_status'], app.state
                        if os.name == 'nt':
                            assert menus(app.state)[menu_id(app.state, 'Direct2D')]['checked'] == str(int(not args.gdi))
                        identity = menu_id(app.state, scheme + '...')
                        app.begin('menu', str(identity))
                        window = dialogs.wait(app.process, title)
                        assert checked(dialogs, window, comma if extension == 'csv' else tab)
                        assert checked(dialogs, window, qualifier)
                        labels = [plain(text) for text in dialogs.texts(window)]
                        assert other in labels and ('Space' if code == 'en' else '空格') in labels, labels
                        if restart == 0:
                            select_radio(dialogs, window, other)
                            dialogs.set_input(window, custom, ':more')
                            if extension == 'csv':
                                screenshots = Path.cwd() / 'Testing/Temporary/csv-localization' / app.state['backend']
                                screenshots.mkdir(parents=True, exist_ok=True)
                                with owned_topmost(dialogs, app.process, window):
                                    capture_and_check(dialogs, app, window, screenshots / (code + ('-gdi' if args.gdi else '') + '.png'))
                        accept(dialogs, window)
                        app.finish()
                        assert path.read_bytes() == (b'X' + original if restart else original)
                        assert app.state['dirty'] == '0'
                        if restart == 0:
                            native_edit(app.process, app.state)
                            app.state = wait_state(app.folder, app.process, lambda state: state['dirty'] == '1')
                            app.send('save')
                            assert path.read_bytes() == b'X' + original
                        app.close()
                    finally:
                        app.stop()
                difference = ''.join(difflib.unified_diff(ini_bytes.decode('utf-16').splitlines(True),
                                                         configuration.read_text(encoding='utf-16').splitlines(True)))
                assert configuration.read_bytes() == ini_bytes, difference
    print('PASS: packaged CSV/TSV original options, three localized native layouts, exact save and fresh-process defaults')


if __name__ == '__main__':
    main()

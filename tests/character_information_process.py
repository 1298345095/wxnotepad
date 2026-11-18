"""Exercise the packaged application menus, native feedback and system clipboard."""
import argparse
from pathlib import Path
import sys
import tempfile

from localization_native import Dialogs
from localization_package import Process, bundle_application, environment, menus, settings
from resource_process import wait_state

TEXT = {
    'en': (1033, '&Show Hex Code', 'Show Character &Info', 'Character Info'),
    'zh_CN': (2052, '显示十六进制代码(&S)', '显示字符信息(&I)', '字符信息'),
    'zh_TW': (1028, '顯示十六進位碼(&S)', '顯示字元資訊(&I)', '字元資訊'),
}
# Literal raw payload captured independently from pinned Notepad4's real command.
PAYLOAD = ('Unicode: U+0041\nUTF-16: 0041\nUTF-8: 41\nHTML: &#65;\n'
           'Name: LATIN CAPITAL LETTER A\nCategory: Lu Uppercase_Letter')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('executable', type=Path)
    parser.add_argument('--cmake-dir', type=Path, required=True)
    parser.add_argument('--evidence', type=Path, required=True)
    parser.add_argument('--languages', nargs='+', choices=tuple(TEXT), default=tuple(TEXT))
    args = parser.parse_args()
    args.evidence.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='character-information-package-', dir=args.evidence) as name:
        directory = Path(name).resolve()
        executable = bundle_application(args.executable.resolve(), args.cmake_dir.resolve(), directory / 'bundle',
                                        directory / 'bundle-reports')
        original = directory / 'original.txt'
        original.write_bytes(b'A')
        dialogs = Dialogs(directory)
        for code in args.languages:
            language, hex_caption, info_caption, title = TEXT[code]
            settings(executable.parent / 'Notepad4.ini', language, 'DefaultEOLMode=1\n')
            app = Process(executable, directory / code, environment(executable, directory / 'children', code),
                          startup=original, cwd=directory)
            try:
                items = menus(app.state)
                hex_id = next(identity for identity, item in items.items() if item['label'] == hex_caption)
                info_id = next(identity for identity, item in items.items() if item['label'] == info_caption)
                assert items[hex_id]['path'].rsplit('/', 1)[0] == items[info_id]['path'].rsplit('/', 1)[0]
                assert items[hex_id]['enabled'] == '0' and items[info_id]['enabled'] == '1'
                assert '\t' not in items[hex_id]['label'] and '\t' not in items[info_id]['label']
                # Existing native F10 route establishes a real Wayland key serial.
                dialogs.key(app, 'F10')
                app.state = wait_state(app.folder, app.process, lambda state: state['read_only'] == '1')
                dialogs.key(app, 'F10')
                app.state = wait_state(app.folder, app.process, lambda state: state['read_only'] == '0')
                copy_id = next(identity for identity, item in items.items() if item['label'].endswith('\tCtrl+C'))
                app.choose(copy_id)
                before = app.state
                app.begin('menu', str(info_id))
                window = dialogs.wait(app.process, title)
                assert PAYLOAD in dialogs.texts(window), dialogs.texts(window)
                okay = {'en': 'OK', 'zh_CN': '确定', 'zh_TW': '確認'}[code] if sys.platform == 'win32' else 'OK'
                dialogs.click(window, okay)
                app.finish()
                assert all(app.state[key] == before[key] for key in ('length', 'dirty', 'caret_position', 'document_path'))
                assert original.read_bytes() == b'A'
                # Consume the real native clipboard through the delivered editor's
                # Paste command and observe exact bytes through its Save entrypoint.
                app.choose(5002)  # wxID_NEW
                app.choose(5033)  # wxID_PASTE
                output = directory / (code + '-clipboard.txt')
                app.send('save', str(output))
                assert output.read_bytes() == PAYLOAD.encode(), output.read_bytes()
                app.close()
            finally:
                app.stop()
            print(f'PASS: packaged {code} Show Hex/Character Info labels, native feedback, unchanged document and clipboard bytes', flush=True)


if __name__ == '__main__':
    main()

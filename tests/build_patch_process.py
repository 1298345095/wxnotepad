"""Shown-app INI/Make/Diff localization, original defaults, exact save and restart."""
import argparse
import os
from pathlib import Path
import shutil
import tempfile

from localization_package import Process, menu_id, menus
from resource_process import native_edit, wait_state

LANGUAGES = {
    'en': (1033, 'Automatic', ['Ini Config File', 'Makefile', 'Diff File']),
    'zh_CN': (2052, '自动(A)', ['Ini 配置文件', 'Makefile', 'Diff 文件']),
    'zh_TW': (1028, '自動(A)', ['Ini 設定檔案', 'Makefile', 'Diff 檔案']),
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('executable', type=Path)
    parser.add_argument('--language', choices=LANGUAGES, default='en')
    parser.add_argument('--gdi', action='store_true')
    args = parser.parse_args()
    source = args.executable.resolve()
    language, automatic, titles = LANGUAGES[args.language]
    with tempfile.TemporaryDirectory(prefix='wxnotepad-build-patch-') as directory:
        folder = Path(directory)
        executable = folder / source.name
        shutil.copy2(source, executable)
        shutil.copytree(source.parent / 'locale', folder / 'locale')
        environment = dict(os.environ, PATH=str(source.parent) + os.pathsep + os.environ['PATH'])
        config = folder / 'Notepad4.ini'
        for extension, identity, title, text in zip(
                ['ini', 'mak', 'diff'], [48, 45, 50], titles,
                ['; 界\r\n[section]\nkey=value\n', 'all:\r\n\tcommand 界\n',
                 '--- a/界\r\n+++ b/界\n@@ -1 +1 @@\n-old\n+new\n']):
            config.write_text('[Settings]\nSaveSettings=0\nWordWrap=0\nDetectIndentation=0\nWarnLineEndings=0\n'
                              'TabWidth=7\nIndentWidth=3\nTabsAsSpaces=1\n'
                              f'RenderingTechnology={0 if args.gdi else 1}\n'
                              f'[Settings2]\nUILanguage={language}\n[Styles]\nDefaultScheme={identity}\n'
                              'FavoriteSchemes=50 48 45\nAutoSelect=1\n[Unrelated]\nRetained=保留\n', encoding='utf-16')
            path = folder / ('case.' + extension)
            original = text.encode('utf-8')
            path.write_bytes(original)
            for restart in range(2):
                app = Process(executable, folder / f'{extension}-{restart}', environment, path)
                try:
                    assert int(app.state['length']) == len(original) + restart, app.state
                    if os.name == 'nt':
                        assert menus(app.state)[menu_id(app.state, 'Direct2D')]['checked'] == str(int(not args.gdi))
                    assert (app.state['tab_width'], app.state['use_tabs']) == (
                        ('4', '1') if extension == 'mak' else ('7', '0')), app.state
                    identity_menu = menu_id(app.state, title)
                    assert menus(app.state)[identity_menu]['enabled'] == '1'
                    app.choose(identity_menu)
                    assert menus(app.state)[identity_menu]['checked'] == '1'
                    assert app.state['dirty'] == '0' and app.state['encoding'] == 'UTF-8'
                    app.choose(menu_id(app.state, automatic))
                    assert menus(app.state)[identity_menu]['checked'] == '0'
                    if restart == 0:
                        native_edit(app.process, app.state)
                        app.state = wait_state(app.folder, app.process, lambda state: state['dirty'] == '1')
                        app.send('save')
                    assert path.read_bytes() == b'X' + original
                    app.close()
                finally:
                    app.stop()
            # Original DefaultScheme identities also survive an unknown extension
            # with automatic classification disabled, in a fresh actual process.
            config.write_text(config.read_text(encoding='utf-16').replace('AutoSelect=1', 'AutoSelect=0'), encoding='utf-16')
            unknown = folder / 'case.unknown'
            unknown.write_bytes(original)
            app = Process(executable, folder / f'{extension}-default', environment, unknown)
            try:
                assert title in app.state['document_status'], app.state
                assert config.read_text(encoding='utf-16').find(f'DefaultScheme={identity}') >= 0
                app.close()
            finally:
                app.stop()
            saved = config.read_text(encoding='utf-16')
            assert '[Unrelated]\nRetained=保留\n' in saved and 'FavoriteSchemes=50 48 45' in saved, saved
    print(f'PASS: {args.language} INI/Make/Diff actual-app menus, original identities/defaults/tabs, save and restart')


if __name__ == '__main__':
    main()

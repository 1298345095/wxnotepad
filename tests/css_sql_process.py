"""Actual shown-app CSS/SQL startup, localized menus, native edit, save and restart."""
import argparse
import os
from pathlib import Path
import shutil
import tempfile

from localization_package import Process, menu_id, menus
from resource_process import native_edit, wait_state

LANGUAGES = {
    'en': (1033, 'Automatic', 'CSS Style Sheet'),
    'zh_CN': (2052, '自动(A)', 'CSS 样式表(H)'),
    'zh_TW': (1028, '自動(A)', 'CSS 樣式表(H)'),
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('executable', type=Path)
    parser.add_argument('--language', choices=LANGUAGES, default='en')
    args = parser.parse_args()
    source = args.executable.resolve()
    language, automatic, family = LANGUAGES[args.language]
    with tempfile.TemporaryDirectory(prefix='wxnotepad-css-sql-') as directory:
        folder = Path(directory)
        executable = folder / source.name
        shutil.copy2(source, executable)
        shutil.copytree(source.parent / 'locale', folder / 'locale')
        environment = dict(os.environ, PATH=str(source.parent) + os.pathsep + os.environ['PATH'])
        config = folder / 'Notepad4.ini'
        config.write_text('[Settings]\nSaveSettings=0\nWordWrap=0\nDetectIndentation=0\nWarnLineEndings=0\n'
                          f'[Settings2]\nUILanguage={language}\n[Unrelated]\nRetained=保留\n', encoding='utf-16')
        for extension, name in [('css', 'CSS'), ('scss', 'SCSS'), ('less', 'Less'), ('hss', 'HSS'), ('sql', 'SQL')]:
            path = folder / ('case.' + extension)
            original = ('/* 界 */\r\na { color: red; }\n' if extension != 'sql'
                        else '-- 界\r\nSELECT COUNT(*) FROM sample;\n').encode('utf-8')
            path.write_bytes(original)
            for restart in range(2):
                app = Process(executable, folder / f'{extension}-{restart}', environment, path)
                try:
                    assert int(app.state['length']) == len(original) + restart, app.state
                    choices = menus(app.state)
                    identity = menu_id(app.state, name)
                    assert choices[identity]['enabled'] == '1' and choices[identity]['checked'] == '0', choices[identity]
                    family_id = menu_id(app.state, family)
                    family_path = choices[family_id]['path']
                    children = [item['label'].replace('&', '') for item in choices.values()
                                if item['path'].rsplit('/', 1)[0] == family_path]
                    assert children == ['CSS', 'SCSS', 'Less', 'HSS'], children
                    app.choose(identity)
                    assert menus(app.state)[identity]['checked'] == '1'
                    assert app.state['dirty'] == '0' and app.state['encoding'] == 'UTF-8' and app.state['bom'] == '0'
                    app.choose(menu_id(app.state, automatic))
                    assert menus(app.state)[identity]['checked'] == '0'
                    if restart == 0:
                        native_edit(app.process, app.state)
                        app.state = wait_state(app.folder, app.process, lambda state: state['dirty'] == '1')
                        app.send('save')
                        assert path.read_bytes() == b'X' + original, path.read_bytes()
                    else:
                        assert path.read_bytes() == b'X' + original
                    app.close()
                finally:
                    app.stop()
        saved_config = config.read_text(encoding='utf-16')
        assert '[Unrelated]\nRetained=保留\n' in saved_config and 'SaveSettings=0' in saved_config, saved_config
    print(f'PASS: {args.language} actual-app CSS dialect/SQL startup, menus, Automatic, native edit/save and restart')


if __name__ == '__main__':
    main()

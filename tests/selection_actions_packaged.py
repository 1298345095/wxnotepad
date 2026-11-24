"""Check selection-action commands through the real owned packaged application."""
import argparse
import json
from pathlib import Path
import shutil
import tempfile

from localization_package import Process, environment, menus, settings

# Independently reviewed captions follow the pinned original resource identities.
CAPTIONS = {
    1033: ('en', ('Search with &Google', 'Search with &Bing', 'Search on &Wikipedia',
                  'Custom Action &1\tCtrl+Shift+1', 'Custom Action &2\tCtrl+Shift+2')),
    2052: ('zh_CN', ('使用 Google 搜索(&G)', '使用 Bing 搜索(&B)', '在维基百科中搜索(&W)',
                    '自定义动作 &1\tCtrl+Shift+1', '自定义动作 &2\tCtrl+Shift+2')),
    1028: ('zh_TW', ('使用 &Google 搜尋', '使用 &Bing 搜尋', '在維基百科中搜尋(&W)',
                    '自訂動作 &1\tCtrl+Shift+1', '自訂動作 &2\tCtrl+Shift+2')),
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('executable', type=Path)
    parser.add_argument('--evidence', type=Path, required=True)
    args = parser.parse_args()
    args.evidence.mkdir(parents=True, exist_ok=False)
    source = args.executable.resolve()
    with tempfile.TemporaryDirectory(prefix='wxnotepad-selection-actions-package-') as temporary:
        root = Path(temporary).resolve()
        package = root / 'package'
        package.mkdir()
        executable = package / source.name
        shutil.copy2(source, executable)
        for dependency in source.parent.glob('*.dll'):
            shutil.copy2(dependency, package / dependency.name)
        for directory in ('locale', 'lib', 'libexec'):
            if (source.parent / directory).exists():
                shutil.copytree(source.parent / directory, package / directory)
        for preference, (code, expected) in CAPTIONS.items():
            settings(package / 'Notepad4.ini', preference)
            app = Process(executable, root / code, environment(executable, root / 'children', code),
                          cwd=root, log_path=args.evidence / (code + '.log'))
            try:
                observed = list(menus(app.state).values())
                (args.evidence / (code + '-menus.json')).write_text(
                    json.dumps(observed, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
                selection = [item for item in observed if item['label'] in expected]
                assert [item['label'] for item in selection] == list(expected), (
                    'All five original Action on Selection commands in native menu order', code, selection)
                paths = [item['path'].rsplit('/', 1)[0] for item in selection]
                assert len(set(paths)) == 1, ('Five commands share Action on Selection submenu', paths)
                assert all(item['enabled'] == '0' for item in selection), (
                    'Empty selection disables providers and unconfigured custom actions', code, selection)
                app.close()
            finally:
                app.stop()
    print('PASS: packaged English/Simplified/Traditional selection-action captions, grouping and availability')


if __name__ == '__main__':
    main()

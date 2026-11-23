"""Review the delivered Extra Esc menus and ordinary close prompts in all UI languages."""
import argparse
from pathlib import Path
import tempfile
import sys
import time
from localization_process import Acceptance, PREFERENCE
from localization_package import menus, read_settings
from localization_native import plain
from localization_layout import capture_and_check, owned_topmost


CAPTIONS = {
    'en': ('Extra Esc &Key Function', '&None', '&Minimize wxnotepad', 'E&xit wxnotepad'),
    'zh_CN': ('Esc 键特别功能(&K)', '无(&N)', '最小化 wxnotepad(&M)', '退出 wxnotepad(&X)'),
    'zh_TW': ('額外 Esc 鍵功能(&K)', '無(&N)', '最小化 wxnotepad(&M)', '離開 wxnotepad(&X)'),
}
PROMPTS = {
    'en': ('Save changes?', 'Save changes to Untitled before closing?', ('Save', 'Discard', 'Cancel')),
    'zh_CN': ('保存修改？', '关闭前保存对 未命名 的修改？', ('保存', '放弃', '取消')),
    'zh_TW': ('儲存變更？', '關閉前是否儲存對 未命名 的變更？', ('儲存', '捨棄', '取消')),
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('executable', type=Path)
    parser.add_argument('--cmake-dir', type=Path, required=True)
    parser.add_argument('--evidence', type=Path, required=True)
    args = parser.parse_args()
    args.evidence.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='escape-localization-') as directory:
        fixture = Acceptance(args, Path(directory))
        for code in ('en', 'zh_CN', 'zh_TW'):
            app = fixture.start(PREFERENCE[code], preferred='en', save=True)
            try:
                entries = menus(app.state)
                captions = CAPTIONS[code]
                parent_id = next(identity for identity, item in entries.items() if item['label'] == captions[0])
                parent = entries[parent_id]['path']
                ids = [parent_id] + [next(identity for identity, item in entries.items()
                                         if item['label'] == caption and item['path'].startswith(parent + '/'))
                                     for caption in captions[1:]]
                assert all(entries[identity]['path'].startswith(parent + '/') for identity in ids[1:])
                assert [entries[identity]['checked'] for identity in ids[1:]] == ['1', '0', '0']
                for chosen in ids[2:]:
                    app.choose(chosen)
                    current = menus(app.state)
                    assert [current[identity]['checked'] for identity in ids[1:]] == [
                        '1' if identity == chosen else '0' for identity in ids[1:]]
                fixture.dirty(app)
                app.begin('close')
                title, message, buttons = PROMPTS[code]
                # GTK's AT-SPI alert name is its Question type; the actual
                # translated application message independently identifies it.
                dialog = fixture.dialogs.wait(app.process, title, message=message)
                text = [plain(item).split('(', 1)[0] for item in fixture.dialogs.texts(dialog)]
                assert all(button in text for button in buttons), text
                with owned_topmost(fixture.dialogs, app.process, dialog):
                    capture_and_check(fixture.dialogs, app, dialog, fixture.logs / f'escape-close-{code}.png')
                cancel_deadline = time.monotonic() + 10
                fixture.dialogs.click(dialog, buttons[2] if code == 'en' else buttons[2] + '(C)',
                                      deadline=cancel_deadline)
                if sys.platform == 'win32':
                    # A dismissed task dialog can remain visible after the
                    # observer acknowledges Cancel.
                    # Confirm this exact prompt is gone before another same-title
                    # lookup, sharing the cancellation's existing readiness budget.
                    while fixture.dialogs.user.IsWindowVisible(dialog) and time.monotonic() < cancel_deadline:
                        time.sleep(.03)
                    assert not fixture.dialogs.user.IsWindowVisible(dialog), 'Cancelled native prompt did not close'
                app.finish()
                assert app.state['dirty'] == '1' and app.process.poll() is None
                app.begin('close')
                dialog = fixture.dialogs.wait(app.process, title, message=message)
                fixture.dialogs.click(dialog, buttons[1] if code == 'en' else buttons[1] + '(D)')
                assert app.process.wait(timeout=10) == 0
                config = read_settings(fixture.config)
                assert config['Settings']['EscFunction'] == '2'
                assert config['Settings2']['UILanguage'] == str(PREFERENCE[code])
            finally:
                app.stop()
    print('PASS: three delivered Extra Esc menu languages, checks, native close prompts, Cancel/Discard and saving')


if __name__ == '__main__':
    main()

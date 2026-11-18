"""Real artifact bundling and the existing independent shown-app observer seam."""
import configparser
import importlib.util
import os
from pathlib import Path
import signal
import subprocess
import sys
import time

from resource_process import command, wait_state
from settings_process import Application

ROOT = Path(__file__).resolve().parents[1]


def cache_values(directory):
    result = {}
    for line in (directory / 'CMakeCache.txt').read_text(encoding='utf-8').splitlines():
        if '=' in line and ':' in line.split('=', 1)[0] and not line.startswith('//'):
            key, value = line.split('=', 1)
            result[key.split(':', 1)[0]] = value
    return result


def bundle_application(source, cmake, folder, reports=None):
    """Feed actual build outputs through the distribution's real artifact bundler."""
    spec = importlib.util.spec_from_file_location('localization_ci', ROOT / '.github/scripts/ci.py')
    ci = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(ci)
    executable = ci.bundle(cmake, source, folder, report_dir=reports)
    package = executable.parent
    for code in ('zh_CN', 'zh_TW'):
        for domain in ('wxnotepad', 'wxstd'):
            relative = Path('locale') / code / 'LC_MESSAGES' / (domain + '.mo')
            assert (package / relative).read_bytes() == (source.parent / relative).read_bytes(), relative
    assert not (package / 'locale/en').exists(), 'Source English needs no MO'
    return executable


def settings(path, language, extra='', save=False):
    language_line = '' if language is None else f'UILanguage={language}\n'
    text = (f'[Settings]\nSaveSettings={int(save)}\nWarnLineEndings=0\nUnrelatedSetting=keep\n'
            + extra + '\n[Settings2]\n' + language_line + 'UnrelatedLanguage=preserved\n')
    path.write_text(text, encoding='utf-16', newline='\r\n')
    return path.read_bytes()


def read_settings(path):
    config = configparser.ConfigParser(interpolation=None)
    config.read(path, encoding='utf-16')
    return config


def menus(state):
    result = {}
    for key, value in state.items():
        if key.startswith('menu.'):
            _, identity, field = key.split('.', 2)
            result.setdefault(int(identity), {})[field] = value
    return result


def menu_id(state, caption):
    found = [identity for identity, item in menus(state).items()
             if item['label'].split('\t', 1)[0].replace('&', '') == caption]
    assert len(found) == 1, (caption, found)
    return found[0]


def language_id(state, preference):
    if preference != 1024:
        return menu_id(state, {1033: 'English', 2052: '中文(简体)', 1028: '中文(繁體)'}[preference])
    english = menus(state)[menu_id(state, 'English')]['path']
    system_path = english.rsplit('/', 1)[0] + '/0'
    return next(identity for identity, item in menus(state).items() if item['path'] == system_path)


class Process(Application):
    def __init__(self, executable, folder, environment, startup=None, arguments=(), cwd=None, log_path=None):
        self.folder = folder
        self.folder.mkdir(parents=True, exist_ok=True)
        self.sequence = 0
        (folder / 'state').unlink(missing_ok=True)
        args = [str(executable), '--observe', str(folder), *arguments]
        if startup is not None:
            args += ['--startup-file', str(startup)]
        log_path = log_path if log_path is not None else folder / 'stderr.log'
        log_path.parent.mkdir(parents=True, exist_ok=True)
        self.log = log_path.open('wb')
        self.process = subprocess.Popen(args, env=environment, cwd=cwd, stdout=self.log, stderr=self.log)
        self.state = wait_state(folder, self.process, lambda state: state['sequence'] == '0' and
                                (state['backend'] != 'GdkWaylandDisplay' or state['active'] == '1'))
        assert self.state['shown'] == '1', self.state

    def begin(self, operation, source=''):
        self.sequence += 1
        command(self.folder, f'{self.sequence}\n{operation}\n{source}\n')

    def finish(self):
        self.state = wait_state(self.folder, self.process, lambda state: state['sequence'] == str(self.sequence))
        assert self.state['result'] == 'ok', self.state
        return self.state

    def send(self, operation, source=''):
        self.begin(operation, source)
        return self.finish()

    def choose(self, identity):
        return self.send('menu', str(identity))

    def stop(self):
        super().stop()
        if hasattr(self, 'log'):
            self.log.close()


class Child:
    def __init__(self, pid):
        self.pid = pid
        self.returncode = None

    def poll(self):
        if self.returncode is not None:
            return self.returncode
        if sys.platform == 'win32':
            import ctypes
            from ctypes import wintypes
            kernel = ctypes.WinDLL('kernel32', use_last_error=True)
            kernel.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
            kernel.OpenProcess.restype = wintypes.HANDLE
            kernel.GetExitCodeProcess.argtypes = [wintypes.HANDLE, ctypes.POINTER(wintypes.DWORD)]
            kernel.CloseHandle.argtypes = [wintypes.HANDLE]
            handle = kernel.OpenProcess(0x1000, False, self.pid)
            if not handle:
                self.returncode = 0
            else:
                code = wintypes.DWORD()
                assert kernel.GetExitCodeProcess(handle, ctypes.byref(code))
                kernel.CloseHandle(handle)
                if code.value != 259:
                    self.returncode = code.value
        else:
            try:
                os.kill(self.pid, 0)
            except ProcessLookupError:
                self.returncode = 0
        return self.returncode

    def terminate(self):
        os.kill(self.pid, signal.SIGTERM)

    def wait(self, timeout):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if self.poll() is not None:
                return self.returncode
            time.sleep(.01)
        raise subprocess.TimeoutExpired(f'child {self.pid}', timeout)


def wait_child(root, previous=()):
    deadline = time.monotonic() + 15
    while time.monotonic() < deadline:
        for path in root.iterdir():
            if path.name not in previous and path.name.isdigit() and (path / 'state').exists():
                app = Process.__new__(Process)
                app.folder = path
                app.sequence = 0
                app.process = Child(int(path.name))
                # Match Process startup: the first Wayland snapshot can precede
                # the native decoration/layout configuration.
                app.state = wait_state(path, app.process, lambda state: state['sequence'] == '0' and
                                       (state['backend'] != 'GdkWaylandDisplay' or state['active'] == '1'))
                return app
        time.sleep(.01)
    raise AssertionError('Restart/New Window did not produce an observed independent child')


def environment(package, root, preferred='en'):
    result = dict(os.environ, WXLANGUAGE=preferred, WXNOTEPAD_OBSERVE_ROOT=str(root))
    # The process must find resources in the delivered package, independently of
    # the build tree, working directory and document directory.
    if sys.platform != 'win32':
        result['LD_LIBRARY_PATH'] = str(package.parent / 'lib')
        result['LC_ALL'] = 'C.UTF-8'
        result.pop('NO_AT_BRIDGE', None)
    return result

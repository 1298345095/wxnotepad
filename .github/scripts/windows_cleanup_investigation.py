"""Bounded, native Windows evidence for caret-process temporary EXE cleanup."""

import argparse
import ctypes
from ctypes import wintypes
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import threading
import time
import traceback


BASELINE = '9c4aa0d5365adc443812c37f06f085b8ab555a80'
EXPECTED_EXE_SHA256 = 'd2f59861033b829d660c3bd919685515884ddc2ea085b71d81116ad881e1cb8f'
CARET_PASS = 'PASS: original packed caret fixtures, system/custom/zero periods, seed, independent restart and INI persistence'
SCRIPT = Path(__file__).resolve()


def write_json(path, value):
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')


def failure(error):
    return {'type': type(error).__name__, 'message': str(error),
            'winerror': getattr(error, 'winerror', None), 'filename': str(getattr(error, 'filename', ''))}


def error_chain(error):
    result, seen = [], set()
    while error is not None and id(error) not in seen:
        seen.add(id(error))
        result.append(failure(error))
        error = error.__cause__ or error.__context__
    return result


class Native:
    def __init__(self):
        self.kernel = ctypes.WinDLL('kernel32', use_last_error=True)
        for name, args, result in (
                ('CloseHandle', [wintypes.HANDLE], wintypes.BOOL),
                ('GetCurrentProcess', [], wintypes.HANDLE),
                ('DuplicateHandle', [wintypes.HANDLE, wintypes.HANDLE, wintypes.HANDLE,
                                     ctypes.POINTER(wintypes.HANDLE), wintypes.DWORD,
                                     wintypes.BOOL, wintypes.DWORD], wintypes.BOOL),
                ('WaitForSingleObject', [wintypes.HANDLE, wintypes.DWORD], wintypes.DWORD),
                ('GetExitCodeProcess', [wintypes.HANDLE, ctypes.POINTER(wintypes.DWORD)], wintypes.BOOL),
                ('GetProcessTimes', [wintypes.HANDLE] + [ctypes.POINTER(wintypes.FILETIME)] * 4,
                 wintypes.BOOL),
                ('CreateFileW', [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD, ctypes.c_void_p,
                                 wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE], wintypes.HANDLE)):
            function = getattr(self.kernel, name)
            function.argtypes, function.restype = args, result

    def process(self, process):
        handle = int(process._handle)
        code = wintypes.DWORD()
        times = [wintypes.FILETIME() for _ in range(4)]
        result = {'pid': process.pid, 'returncode': process.returncode,
                  'handle': handle, 'handle_closed': process._handle.closed,
                  'native_wait': int(self.kernel.WaitForSingleObject(handle, 0)),
                  'monotonic_ns': time.monotonic_ns()}
        if self.kernel.GetExitCodeProcess(handle, ctypes.byref(code)):
            result['native_exit_code'] = code.value
        else:
            result['exit_query_error'] = ctypes.get_last_error()
        if self.kernel.GetProcessTimes(handle, *(ctypes.byref(value) for value in times)):
            result['creation_filetime'] = (times[0].dwHighDateTime << 32) | times[0].dwLowDateTime
            result['exit_filetime'] = (times[1].dwHighDateTime << 32) | times[1].dwLowDateTime
        else:
            result['times_query_error'] = ctypes.get_last_error()
        return result

    def close(self, handle):
        if not self.kernel.CloseHandle(handle):
            raise ctypes.WinError(ctypes.get_last_error())


class Child:
    """Own each diagnostic worker and its descendants through the existing test job helper."""
    def __init__(self, args, directory, mode, extra=(), cwd=None):
        from windows_hang_monitor import OwnedChildJob
        directory.mkdir(parents=True, exist_ok=True)
        self.directory, self.process, self.job = directory, None, OwnedChildJob()
        self.started = time.monotonic()
        gate = directory / 'start-gate'
        command = [sys.executable, '-X', 'utf8', str(SCRIPT), '--mode', mode,
                   '--repo-root', str(args.repo_root), '--report-dir', str(directory),
                   '--gate', str(gate), *map(str, extra)]
        try:
            with (directory / 'stdout.log').open('wb') as stdout, (directory / 'stderr.log').open('wb') as stderr:
                self.process = subprocess.Popen(command, cwd=cwd, stdout=stdout, stderr=stderr)
            self.job.assign(self.process)
            gate.write_text('assigned\n', encoding='ascii')
        except BaseException:
            self.close()
            raise

    def finish(self, timeout, *, from_now=False):
        timed_out = False
        try:
            remaining = timeout if from_now else max(0, timeout - (time.monotonic() - self.started))
            code = self.process.wait(timeout=remaining)
        except subprocess.TimeoutExpired:
            timed_out, code = True, None
        finally:
            self.close()
        return {'pid': self.process.pid, 'exit_code': code, 'timed_out': timed_out,
                'elapsed_seconds': time.monotonic() - self.started}

    def close(self):
        self.job.close()
        if self.process is not None:
            if self.process.poll() is None:
                self.process.kill()
            self.process.wait(timeout=5)


def owners(args):
    class UniqueProcess(ctypes.Structure):
        _fields_ = [('pid', wintypes.DWORD), ('started', wintypes.FILETIME)]

    class ProcessInfo(ctypes.Structure):
        _fields_ = [('process', UniqueProcess), ('application', wintypes.WCHAR * 256),
                    ('service', wintypes.WCHAR * 64), ('type', ctypes.c_int),
                    ('status', wintypes.ULONG), ('session', wintypes.DWORD), ('restartable', wintypes.BOOL)]

    library = ctypes.WinDLL('rstrtmgr', use_last_error=True)
    library.RmStartSession.argtypes = [ctypes.POINTER(wintypes.DWORD), wintypes.DWORD, wintypes.LPWSTR]
    library.RmRegisterResources.argtypes = [wintypes.DWORD, wintypes.UINT,
                                           ctypes.POINTER(wintypes.LPCWSTR), wintypes.UINT,
                                           ctypes.POINTER(UniqueProcess), wintypes.UINT,
                                           ctypes.POINTER(wintypes.LPCWSTR)]
    library.RmGetList.argtypes = [wintypes.DWORD, ctypes.POINTER(wintypes.UINT),
                                 ctypes.POINTER(wintypes.UINT), ctypes.POINTER(ProcessInfo),
                                 ctypes.POINTER(wintypes.DWORD)]
    library.RmEndSession.argtypes = [wintypes.DWORD]
    for name in ('RmStartSession', 'RmRegisterResources', 'RmGetList', 'RmEndSession'):
        getattr(library, name).restype = wintypes.DWORD
    session = wintypes.DWORD()
    result = {'path': str(args.target), 'query_pid': os.getpid(), 'started_monotonic_ns': time.monotonic_ns()}
    # ctypes releases the GIL around these native calls. A stuck Restart Manager
    # call cannot outlive this independent two-second diagnostic process budget.
    watchdog = threading.Timer(2, lambda: os._exit(124))
    watchdog.daemon = True
    write_json(args.report_dir / 'owners.json', {**result, 'status': 'querying', 'deadline_seconds': 2})
    watchdog.start()
    code = library.RmStartSession(ctypes.byref(session), 0, ctypes.create_unicode_buffer(33))
    result['start_code'] = code
    try:
        if code:
            return result
        names = (wintypes.LPCWSTR * 1)(str(args.target))
        code = library.RmRegisterResources(session, 1, names, 0, None, 0, None)
        result['register_code'] = code
        if code:
            return result
        needed, count, reasons = wintypes.UINT(), wintypes.UINT(), wintypes.DWORD()
        entries = None
        for _ in range(3):
            code = library.RmGetList(session, ctypes.byref(needed), ctypes.byref(count), entries,
                                     ctypes.byref(reasons))
            if code != 234:
                break
            count.value = needed.value
            entries = (ProcessInfo * count.value)()
        result.update(list_code=code, reboot_reasons=reasons.value, owners=[])
        if code == 0 and entries is not None:
            for item in entries[:count.value]:
                result['owners'].append({'pid': item.process.pid, 'application': item.application,
                                         'service': item.service, 'type': item.type, 'status': item.status,
                                         'session': item.session,
                                         'creation_filetime': (item.process.started.dwHighDateTime << 32)
                                         | item.process.started.dwLowDateTime})
        return result
    finally:
        if result['start_code'] == 0:
            result['end_code'] = library.RmEndSession(session)
        result['finished_monotonic_ns'] = time.monotonic_ns()
        write_json(args.report_dir / 'owners.json', result)
        watchdog.cancel()


def hold_file(args):
    native = Native()
    # GENERIC_READ; FILE_SHARE_READ | FILE_SHARE_WRITE, intentionally no FILE_SHARE_DELETE.
    handle = native.kernel.CreateFileW(str(args.target), 0x80000000, 3, None, 3, 0x80, None)
    if handle == ctypes.c_void_p(-1).value:
        raise ctypes.WinError(ctypes.get_last_error())
    started = time.monotonic()
    release_started = None
    try:
        write_json(args.report_dir / 'holder-ready.json', {'pid': os.getpid(), 'path': str(args.target),
                                                          'handle': int(handle),
                                                          'monotonic_ns': time.monotonic_ns()})
        while time.monotonic() - started < 15:
            if (args.report_dir / 'release').exists():
                break
            if (args.report_dir / 'begin-cleanup').exists():
                if release_started is None:
                    release_started = time.monotonic()
                if args.hold_seconds and time.monotonic() - release_started >= args.hold_seconds:
                    break
            time.sleep(0.01)
    finally:
        native.close(handle)
        write_json(args.report_dir / 'holder-released.json', {'pid': os.getpid(),
                                                             'monotonic_ns': time.monotonic_ns()})


def wait_file(path, process, timeout=5):
    deadline = time.monotonic() + timeout
    while not path.exists():
        if process.poll() is not None:
            raise RuntimeError(f'Worker {process.pid} exited before {path.name}: {process.returncode}')
        if time.monotonic() >= deadline:
            raise TimeoutError(f'Timed out waiting for {path}')
        time.sleep(0.01)


def handle_contract(args):
    native = Native()
    executable = args.report_dir / 'exited-cmd.exe'
    shutil.copy2(Path(os.environ['SystemRoot']) / 'System32' / 'cmd.exe', executable)
    process = subprocess.Popen([str(executable), '/d', '/c', 'exit', '0'])
    duplicate = wintypes.HANDLE()
    result = {}
    try:
        assert process.wait(timeout=10) == 0
        current = native.kernel.GetCurrentProcess()
        if not native.kernel.DuplicateHandle(current, int(process._handle), current,
                                              ctypes.byref(duplicate), 0, False, 2):
            raise ctypes.WinError(ctypes.get_last_error())
        result['before_unlink'] = native.process(process)
        result['duplicate_handle'] = int(duplicate.value)
        result['duplicate_wait'] = int(native.kernel.WaitForSingleObject(duplicate, 0))
        try:
            executable.unlink()
            result['unlink_while_both_process_handles_open'] = True
        except OSError as error:
            result['unlink_while_both_process_handles_open'] = False
            result['unlink_error'] = failure(error)
        result['after_unlink'] = native.process(process)
        result['path_absent'] = not executable.exists()
        result['passed'] = (result['unlink_while_both_process_handles_open'] and result['path_absent']
                            and result['before_unlink']['native_wait'] == 0 and result['duplicate_wait'] == 0)
    finally:
        if duplicate.value:
            native.close(duplicate)
        if process.poll() is None:
            process.kill()
            process.wait(timeout=10)
        process._handle.Close()
        if executable.exists():
            try:
                executable.unlink()
                result['unlink_after_closing_owned_process_handles'] = True
            except OSError as error:
                result['after_close_error'] = failure(error)
        write_json(args.report_dir / 'result.json', result)
    return int(not result['passed'])


def lock_contracts(args):
    from temporary_directory import TemporaryDirectory
    results = []
    for name, directory_type, seconds, body_error in (
            ('original-short', tempfile.TemporaryDirectory, 0.6, False),
            ('candidate-short', TemporaryDirectory, 0.6, False),
            ('candidate-persistent', TemporaryDirectory, 0, False),
            ('candidate-body-error', TemporaryDirectory, 0.6, True)):
        directory = directory_type(prefix=f'{name}-', dir=args.report_dir)
        target = Path(directory.name) / 'held.exe'
        shutil.copy2(Path(os.environ['SystemRoot']) / 'System32' / 'cmd.exe', target)
        holder = Child(args, args.report_dir / name, 'hold',
                       ['--target', target, '--hold-seconds', seconds])
        item = {'name': name, 'passed': False}
        try:
            wait_file(holder.directory / 'holder-ready.json', holder.process)
            item['holder'] = json.loads((holder.directory / 'holder-ready.json').read_text(encoding='utf-8'))
            try:
                target.unlink()
            except PermissionError as error:
                item['initial_unlink'] = failure(error)
                assert error.winerror == 32, item
            else:
                raise AssertionError('The real CreateFileW holder did not prevent deletion')
            (holder.directory / 'begin-cleanup').write_text('begin\n', encoding='ascii')
            started = time.monotonic()
            try:
                with directory:
                    if body_error:
                        raise LookupError('fixture-body-sentinel')
            except BaseException as error:
                item['outcome'] = failure(error)
                item['exception_chain'] = error_chain(error)
            else:
                item['outcome'] = {'type': 'success'}
            item['cleanup_seconds'] = time.monotonic() - started
            item['path_absent_at_return'] = not Path(directory.name).exists()
            kind = item['outcome']['type']
            if name == 'original-short':
                item['passed'] = kind == 'PermissionError' and item['outcome']['winerror'] == 32
            elif name == 'candidate-persistent':
                item['passed'] = (kind == 'PermissionError' and item['outcome']['winerror'] == 32
                                  and 4.9 <= item['cleanup_seconds'] < 6.0
                                  and target.exists() and holder.process.poll() is None)
            elif body_error:
                item['passed'] = (kind == 'LookupError' and item['outcome']['message'] == 'fixture-body-sentinel'
                                  and item['path_absent_at_return'])
            else:
                item['passed'] = kind == 'success' and item['path_absent_at_return']
        finally:
            (holder.directory / 'release').write_text('release\n', encoding='ascii')
            item['holder_exit'] = holder.finish(3, from_now=True)
            released = holder.directory / 'holder-released.json'
            item['holder_release'] = json.loads(released.read_text(encoding='utf-8')) if released.exists() else None
            item['passed'] = (item['passed'] and item['holder_exit']['exit_code'] == 0
                              and not item['holder_exit']['timed_out'] and item['holder_release'] is not None)
            # The controlled owner has exited before final housekeeping.
            tempfile.TemporaryDirectory.cleanup(directory)
            results.append(item)
            write_json(args.report_dir / 'result.json', {'cases': results, 'passed': all(r['passed'] for r in results)})
    return int(not all(item['passed'] for item in results))


def caret(args):
    import settings_process
    native = Native()
    observations, cleanup_errors, queries, context_exits = [], [], [], []
    owner_attempted = False
    original_init = settings_process.Application.__init__
    original_wait = settings_process.Application.wait_for_close
    original_stop = settings_process.Application.stop

    def observe(app, phase):
        if hasattr(app, 'process'):
            observations.append({'phase': phase, **native.process(app.process)})

    def initialize(app, *values, **keywords):
        try:
            return original_init(app, *values, **keywords)
        finally:
            observe(app, 'after_start')

    def wait(app, *values, **keywords):
        try:
            return original_wait(app, *values, **keywords)
        finally:
            observe(app, 'after_close_wait')

    def stop(app):
        try:
            return original_stop(app)
        finally:
            observe(app, 'after_stop')

    settings_process.Application.__init__ = initialize
    settings_process.Application.wait_for_close = wait
    settings_process.Application.stop = stop
    original_exit = tempfile.TemporaryDirectory.__exit__

    def context_exit(directory, kind, value, trace):
        context_exits.append({'directory': str(directory.name),
                              'body_error': failure(value) if value is not None else None})
        return original_exit(directory, kind, value, trace)

    tempfile.TemporaryDirectory.__exit__ = context_exit
    original_rmtree = tempfile.TemporaryDirectory._rmtree.__func__

    def rmtree(cls, *values, **keywords):
        nonlocal owner_attempted
        try:
            return original_rmtree(cls, *values, **keywords)
        except OSError as error:
            entry = {**failure(error), 'monotonic_ns': time.monotonic_ns(),
                     'last_application': observations[-1] if observations else None}
            cleanup_errors.append(entry)
            if getattr(error, 'winerror', None) == 32 and not owner_attempted:
                owner_attempted = True
                write_json(args.report_dir / 'first-sharing-violation.json',
                           {'error': entry, 'applications': observations})
                if error.filename:
                    try:
                        queries.append(Child(args, args.report_dir / 'owners', 'owners',
                                             ['--target', error.filename]))
                    except Exception as query_error:
                        write_json(args.report_dir / 'owner-start-error.json', failure(query_error))
            raise

    tempfile.TemporaryDirectory._rmtree = classmethod(rmtree)
    source = args.source.read_bytes()
    namespace = {'__name__': '__main__', '__file__': str(args.repo_root / 'tests' / 'caret_process.py')}
    sys.argv = [namespace['__file__'], str(args.executable)]
    result = {'variant': args.variant, 'source_sha256': hashlib.sha256(source).hexdigest(), 'passed': False}
    try:
        exec(compile(source, namespace['__file__'], 'exec'), namespace)
        result['passed'] = True
    except BaseException as error:
        result['exception_chain'] = error_chain(error)
        traceback.print_exc()
    finally:
        result['applications'] = observations
        result['cleanup_errors'] = cleanup_errors
        result['context_exits'] = context_exits
        result['owner_queries'] = [query.finish(2) for query in queries]
        write_json(args.report_dir / 'result.json', result)
    return int(not result['passed'])


def investigate(args):
    assert sys.version_info[:3] == (3, 14, 10), sys.version
    executable_hash = hashlib.sha256(args.executable.read_bytes()).hexdigest()
    assert executable_hash == EXPECTED_EXE_SHA256, executable_hash
    original = subprocess.run(['git', 'show', f'{BASELINE}:tests/caret_process.py'], cwd=args.repo_root,
                              capture_output=True, check=True).stdout
    original_path = args.report_dir / 'original-caret.py.txt'
    original_path.write_bytes(original)
    candidate_path = args.repo_root / 'tests' / 'caret_process.py'
    helper_path = args.repo_root / 'tests' / 'temporary_directory.py'
    result = {'python': sys.version, 'executable': str(args.executable), 'executable_sha256': executable_hash,
              'baseline': BASELINE, 'original_sha256': hashlib.sha256(original).hexdigest(),
              'candidate_sha256': hashlib.sha256(candidate_path.read_bytes()).hexdigest(),
              'helper_sha256': hashlib.sha256(helper_path.read_bytes()).hexdigest(), 'attempts': [], 'contracts': {}}
    write_json(args.report_dir / 'summary.json', result)
    for mode, timeout in (('handles', 20), ('locks', 25)):
        child = Child(args, args.report_dir / mode, mode)
        outcome = child.finish(timeout)
        path = child.directory / 'result.json'
        outcome['result'] = json.loads(path.read_text(encoding='utf-8')) if path.exists() else None
        result['contracts'][mode] = outcome
        write_json(args.report_dir / 'summary.json', result)
    for index in range(1, 16):
        for variant, source in (('original', original_path), ('candidate', candidate_path)):
            directory = args.report_dir / f'{variant}-{index:02}'
            child = Child(args, directory, 'caret',
                          ['--variant', variant, '--source', source, '--executable', args.executable],
                          cwd=args.executable.parent)
            outcome = child.finish(60)
            outcome.update(variant=variant, attempt=index)
            path = directory / 'result.json'
            outcome['result'] = json.loads(path.read_text(encoding='utf-8')) if path.exists() else None
            outcome['pass_text'] = CARET_PASS in (directory / 'stdout.log').read_text(encoding='utf-8', errors='replace')
            result['attempts'].append(outcome)
            write_json(args.report_dir / 'summary.json', result)
            print(f'{variant} {index}/15: exit={outcome["exit_code"]} timeout={outcome["timed_out"]} '
                  f'PASS={outcome["pass_text"]}', flush=True)
    result['contracts_passed'] = all(item['exit_code'] == 0 and item['result'] and item['result']['passed']
                                     for item in result['contracts'].values())
    result['candidate_passed'] = all(item['exit_code'] == 0 and item['pass_text']
                                     and item['result'] and item['result']['passed']
                                     for item in result['attempts'] if item['variant'] == 'candidate')
    result['original_passes'] = sum(item['exit_code'] == 0 and item['pass_text']
                                    for item in result['attempts'] if item['variant'] == 'original')
    original_valid = []
    for item in result['attempts']:
        if item['variant'] != 'original':
            continue
        detail = item['result'] or {}
        chain = detail.get('exception_chain', [])
        cleanup_only = (not item['timed_out'] and bool(chain) and bool(detail.get('cleanup_errors'))
                        and bool(detail.get('context_exits'))
                        and all(exit_['body_error'] is None for exit_ in detail['context_exits'])
                        and bool(detail.get('applications'))
                        and all(error['type'] == 'PermissionError' and error['winerror'] == 32 for error in chain)
                        and all(event['native_wait'] == 0 and event.get('native_exit_code') == 0
                                for event in detail.get('applications', []) if event['phase'] == 'after_stop'))
        item['expected_cleanup_failure'] = cleanup_only
        original_valid.append((item['exit_code'] == 0 and item['pass_text'] and detail.get('passed')) or cleanup_only)
    result['original_observations_valid'] = all(original_valid)
    result['passed'] = (result['contracts_passed'] and result['candidate_passed']
                        and result['original_observations_valid'])
    write_json(args.report_dir / 'summary.json', result)
    return int(not result['passed'])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--executable', type=Path)
    parser.add_argument('--report-dir', type=Path, required=True)
    parser.add_argument('--repo-root', type=Path, default=Path.cwd())
    parser.add_argument('--mode', choices=('investigate', 'caret', 'handles', 'locks', 'hold', 'owners'), default='investigate')
    parser.add_argument('--variant', choices=('original', 'candidate'))
    parser.add_argument('--source', type=Path)
    parser.add_argument('--target', type=Path)
    parser.add_argument('--hold-seconds', type=float, default=0)
    parser.add_argument('--gate', type=Path)
    args = parser.parse_args()
    if os.name != 'nt':
        parser.error('This diagnostic requires real Windows APIs')
    if args.mode in ('investigate', 'caret') and args.executable is None:
        parser.error('--executable is required')
    args.report_dir = args.report_dir.resolve()
    args.repo_root = args.repo_root.resolve()
    if args.executable is not None:
        args.executable = args.executable.resolve()
    if args.mode == 'investigate' and args.report_dir.exists() and any(args.report_dir.iterdir()):
        parser.error('--report-dir must be fresh so prior worker gates cannot be reused')
    args.report_dir.mkdir(parents=True, exist_ok=True)
    sys.path.insert(0, str(args.repo_root / 'tests'))
    if args.gate:
        deadline = time.monotonic() + 10
        while not args.gate.exists():
            if time.monotonic() >= deadline:
                raise TimeoutError('Parent did not assign the worker job')
            time.sleep(0.01)
    functions = {'investigate': investigate, 'caret': caret, 'handles': handle_contract,
                 'locks': lock_contracts, 'hold': hold_file, 'owners': owners}
    value = functions[args.mode](args)
    return value if isinstance(value, int) else 0


if __name__ == '__main__':
    raise SystemExit(main())

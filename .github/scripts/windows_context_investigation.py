"""Run focused Windows context-menu investigations through registered CTest cases."""
import argparse
from contextlib import nullcontext
import json
import os
from pathlib import Path
import re
import subprocess

import ci
import windows_foreground
from report_paths import report_path

CONTEXT_CASE = 'original_editor_context_menu'
SWAP_CASE = 'original_clipboard_swap'
FOCUS_CASE = 'native_windows_clipboard_focus_transfer'
# CTest omits an unbuilt executable's command from its JSON listing. Keep the
# inspected predecessor targets explicit so cold investigations can build them.
PREFIX_TARGETS = (
    'context_menu', 'date_time_format', 'default_encoding', 'document_tools',
    'menu_hierarchy', 'new_window', 'open_targets', 'recent_files', 'run_command',
    'scroll_past_last_line', 'sdk_capabilities', 'selection_options',
    'settings_read_failure', 'wxnotepad',
)
SWAP_PREFIX_TARGETS = PREFIX_TARGETS + (
    'align_lines', 'base64_conversion', 'caret_appearance', 'clipboard_smoke',
    'current_encoding', 'date_time', 'feature_sessions', 'file_times', 'full_screen',
    'header_insert', 'line_wrap', 'modify_lines', 'pad_spaces', 'path_text',
    'periodic_backup', 'printing', 'read_only', 'read_only_file', 'reopen_document',
    'revert_document', 'save_backup', 'save_cleanup', 'save_copy', 'special_insert',
    'url_conversion',
)


def plan(cmake_dir, scenario):
    cmake_dir = cmake_dir.resolve()
    cases = json.loads(subprocess.check_output(
        ['ctest', '--test-dir', str(cmake_dir), '-C', 'Release', '--show-only=json-v1'],
        text=True, encoding='utf-8'))['tests']
    names = [case['name'] for case in cases]
    required = (FOCUS_CASE if scenario == 'focus-transfer' else
                SWAP_CASE if scenario in ('swap', 'swap-prefix', 'ci-swap-prefix') else CONTEXT_CASE)
    if required not in names:
        raise SystemExit(f'Required registered test is missing: {required}')
    if scenario == 'prefix':
        selected = cases[:names.index(CONTEXT_CASE) + 1]
        targets = PREFIX_TARGETS
    elif scenario in ('swap-prefix', 'ci-swap-prefix'):
        selected = cases[:names.index(SWAP_CASE) + 1]
        targets = SWAP_PREFIX_TARGETS
    elif scenario == 'clipboard':
        selected = [cases[names.index(CONTEXT_CASE)]]
        targets = ('context_menu',)  # Includes its existing clipboard_peer dependency.
    else:
        selected = [cases[names.index(required)]]
        targets = ('clipboard_smoke',)  # Includes its existing clipboard_peer dependency.
    return {'cmake_dir': str(cmake_dir), 'scenario': scenario,
            'tests': [case['name'] for case in selected], 'targets': list(targets)}


def build(selection, reports, jobs):
    ci.run(ci.cmake_command(['--build', selection['cmake_dir'], '--config', 'Release',
                            '--parallel', str(jobs), '--target', *selection['targets']]), reports / 'build.log')


def run(selection, reports, runs):
    expression = '^(' + '|'.join(re.escape(name) for name in selection['tests']) + ')$'
    for attempt in range(1, runs + 1):
        print(f'Investigation {attempt}/{runs}: {len(selection["tests"])} registered cases', flush=True)
        attempt_reports = reports / f'run-{attempt:03}'
        if selection['scenario'] == 'ci-swap-prefix':
            warm_context(Path(selection['cmake_dir']), attempt_reports / 'warmup')
            attempt_reports /= 'tests'
        ci.test('windows', Path(selection['cmake_dir']), expression, 1,
                report_directory=attempt_reports)


def warm_context(cmake_dir, reports):
    """Retain the full CI's context-menu prelude before reaching Swap."""
    run(plan(cmake_dir, 'prefix'), reports / 'prefix', 1)
    pointer_result = ci.probe([str(cmake_dir / 'context_menu.exe'), '--pointer-probe'],
                              reports / 'pointer', 60, 100, cmake_dir)
    if pointer_result:
        raise SystemExit(pointer_result)
    run(plan(cmake_dir, 'clipboard'), reports / 'menu', 20)


def hide_terminal_windows(reports):
    import ctypes
    from ctypes import wintypes

    user = ctypes.WinDLL('user32', use_last_error=True)
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    callback = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    user.EnumWindows.argtypes = [callback, wintypes.LPARAM]
    user.GetClassNameW.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]
    user.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
    user.IsWindowVisible.argtypes = [wintypes.HWND]
    user.ShowWindow.argtypes = [wintypes.HWND, ctypes.c_int]
    kernel.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
    kernel.OpenProcess.restype = wintypes.HANDLE
    kernel.QueryFullProcessImageNameW.argtypes = [wintypes.HANDLE, wintypes.DWORD, wintypes.LPWSTR,
                                                ctypes.POINTER(wintypes.DWORD)]
    kernel.WaitForSingleObject.argtypes = [wintypes.HANDLE, wintypes.DWORD]
    kernel.WaitForSingleObject.restype = wintypes.DWORD
    kernel.CloseHandle.argtypes = [wintypes.HANDLE]
    windows = []

    @callback
    def visit(window, unused):
        name = ctypes.create_unicode_buffer(256)
        user.GetClassNameW(window, name, len(name))
        if name.value == 'CASCADIA_HOSTING_WINDOW_CLASS':
            windows.append(window)
        return True

    if not user.EnumWindows(visit, 0):
        raise ctypes.WinError(ctypes.get_last_error())
    records = []
    process_query_limited_information = 0x1000
    process_synchronize = 0x100000
    wait_timeout = 0x102
    window_hide = 0
    for window in windows:
        process = wintypes.DWORD()
        user.GetWindowThreadProcessId(window, ctypes.byref(process))
        handle = kernel.OpenProcess(process_query_limited_information | process_synchronize, False, process.value)
        if not handle:
            raise ctypes.WinError(ctypes.get_last_error())
        try:
            image = ctypes.create_unicode_buffer(32768)
            size = wintypes.DWORD(len(image))
            if not kernel.QueryFullProcessImageNameW(handle, 0, image, ctypes.byref(size)):
                raise ctypes.WinError(ctypes.get_last_error())
            if Path(image.value).name.casefold() != 'windowsterminal.exe':
                continue
            before = bool(user.IsWindowVisible(window))
            user.ShowWindow(window, window_hide)
            records.append({'window': window, 'pid': process.value, 'process_image': image.value,
                            'visible_before': before, 'visible_after': bool(user.IsWindowVisible(window)),
                            'process_alive': kernel.WaitForSingleObject(handle, 0) == wait_timeout})
        finally:
            kernel.CloseHandle(handle)
    (reports / 'terminal-isolation.json').write_text(json.dumps(records, indent=2) + '\n', encoding='utf-8')
    if not records or any(item['visible_after'] or not item['process_alive'] for item in records):
        raise SystemExit('Terminal hiding was not established; inspect terminal-isolation.json')
    print(f'Hidden {len(records)} Terminal windows; their processes remain running', flush=True)


def parse_arguments():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=('plan', 'build', 'run'))
    parser.add_argument('--scenario',
                        choices=('prefix', 'clipboard', 'swap', 'swap-prefix', 'ci-swap-prefix', 'focus-transfer'),
                        default='prefix')
    parser.add_argument('--cmake-dir', type=Path, default=ci.BUILD / 'cmake')
    parser.add_argument('--report-dir', type=Path, required=True)
    parser.add_argument('--runs', type=int, default=3)
    parser.add_argument('--jobs', type=int, default=os.cpu_count() or 1)
    parser.add_argument('--hide-terminal', action='store_true', help='Hide CI Terminal windows without ending processes')
    parser.add_argument('--zero-foreground-timeout', action='store_true',
                        help='Temporarily disable the hosted desktop foreground lock timeout')
    arguments = parser.parse_args()
    if arguments.runs < 1 or arguments.jobs < 1:
        parser.error('--runs and --jobs must be positive')
    if arguments.hide_terminal and arguments.action != 'run':
        parser.error('--hide-terminal is only valid for run')
    if arguments.zero_foreground_timeout and (arguments.action != 'run' or os.environ.get('GITHUB_ACTIONS') != 'true'):
        parser.error('--zero-foreground-timeout is only valid for run on GitHub Actions')
    if arguments.action != 'plan' and not ci.WINDOWS:
        parser.error('Native Windows investigations must run on Windows')
    try:
        arguments.report_dir = report_path(arguments.report_dir, Path.cwd().resolve())
    except (OSError, RuntimeError, ValueError) as error:
        parser.error(str(error))
    return arguments


def main():
    arguments = parse_arguments()
    selection = plan(arguments.cmake_dir, arguments.scenario)
    reports = arguments.report_dir
    reports.mkdir(parents=True, exist_ok=False)
    (reports / 'plan.json').write_text(json.dumps(selection, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(selection, indent=2), flush=True)
    if arguments.action == 'build':
        build(selection, reports, arguments.jobs)
    elif arguments.action == 'run':
        policy = (windows_foreground.temporary(reports / 'foreground-timeout.json')
                  if arguments.zero_foreground_timeout else nullcontext())
        with policy:
            if arguments.hide_terminal:
                hide_terminal_windows(reports)
            run(selection, reports, arguments.runs)


if __name__ == '__main__':
    main()

"""Repeat original registered cases on four persistent private X11 sessions."""
from concurrent.futures import ThreadPoolExecutor, as_completed
import json
import os
from pathlib import Path
import re
import sys
import time

sys.path.insert(0, str(Path.cwd() / '.github/scripts'))
import ci

ROOT = ci.ROOT
BUILD = ROOT / '.ci/build' / os.environ['WXNOTEPAD_SEARCH_PROBE_BUILD']
REPORTS = ROOT / '.ci/reports/search-release-investigation'
SCRIPT = Path(__file__).resolve()
SESSIONS = 4
PHASES = {
    'original-control': {
        'tests': ['original_search_continuation'], 'runs': 1, 'timeout': 90,
    },
    'red-continuous-release': {
        'tests': ['original_search_continuation'], 'runs': 1, 'timeout': 90,
        'environment': {'WXNOTEPAD_CONTINUOUS_RELEASE': '1'},
    },
    'bounded-continuous-release': {
        'tests': ['original_search_continuation'], 'runs': 2, 'timeout': 90,
        'environment': {'WXNOTEPAD_CONTINUOUS_RELEASE': '1', 'WXNOTEPAD_BOUNDED_RELEASE': '1'},
    },
    'bounded-original': {
        'tests': ['original_search_continuation'], 'runs': 2, 'timeout': 90,
        'environment': {'WXNOTEPAD_BOUNDED_RELEASE': '1'},
    },
    'bounded-wayland': {
        'tests': ['original_search_continuation'], 'runs': 1, 'timeout': 90, 'backend': 'wayland',
        'environment': {'WXNOTEPAD_CONTINUOUS_RELEASE': '1', 'WXNOTEPAD_BOUNDED_RELEASE': '1'},
    },
    'bounded-held-feedback': {
        'tests': ['native_search_feedback_release'], 'runs': 2, 'timeout': 90,
        'environment': {'WXNOTEPAD_BOUNDED_RELEASE': '1'},
    },
    'bounded-held-feedback-wayland': {
        'tests': ['native_search_feedback_release'], 'runs': 2, 'timeout': 90, 'backend': 'wayland',
        'environment': {'WXNOTEPAD_BOUNDED_RELEASE': '1'},
    },
}


def inside_session(directory):
    plan = json.loads((directory / 'plan.json').read_text(encoding='utf-8'))
    # One probe invocation retains run-001.log ... on the same DISPLAY, D-Bus,
    # and native focus driver. CTest keeps each registered case's own timeout.
    return ci.probe(plan['command'], directory / 'attempts', plan['timeout'],
                    plan['runs'], ROOT)


def session(phase, number, helper):
    selected = PHASES[phase]
    directory = REPORTS / phase / f'session-{number:02}'
    record = {'phase': phase, 'session': number, 'report': str(directory),
              'requested_runs': selected['runs'], 'tests': selected['tests']}
    started = time.monotonic()
    try:
        expression = '^(' + '|'.join(re.escape(name) for name in selected['tests']) + ')$'
        [(workspace, names)] = ci.test_shards(BUILD, expression, 1, directory / 'ctest')
        if names != selected['tests']:
            raise RuntimeError(f'Registered test selection/order changed: {names}')
        # Keep the requested prefix order even after CTest records test costs.
        # These dependencies affect this private workspace only.
        with (workspace / 'CTestTestfile.cmake').open('a', encoding='utf-8') as output:
            for previous, following in zip(names, names[1:]):
                output.write(f'set_tests_properties("{following}" PROPERTIES DEPENDS "{previous}")\n')
        command = ['ctest', '--test-dir', str(workspace), '-C', 'Release',
                   '--parallel', '1', '-V', '--output-on-failure', '--no-tests=error',
                   '--stop-on-failure', '-R', expression,
                   '--output-junit', str(directory / 'last-tests.xml')]
        plan = dict(record, command=command, runs=selected['runs'], timeout=selected['timeout'])
        (directory / 'plan.json').write_text(json.dumps(plan, indent=2) + '\n', encoding='utf-8')
        environment = dict(os.environ, GIO_LAUNCH_DESKTOP=str(helper),
                           WXNOTEPAD_DISPLAY_LOG_DIR=str(directory / 'display'))
        environment.update(selected.get('environment', {}))
        isolated = [sys.executable, ROOT / 'tools/xvfb_session.py',
                    'dbus-run-session', '--', sys.executable, ROOT / 'tools/clipboard_isolated.py',
                    selected.get('backend', 'x11'), sys.executable, SCRIPT, '--session', directory]
        record['exit_code'] = ci.run(isolated, directory / 'session.log', environment, check=False)
        summary_path = directory / 'attempts/summary.json'
        if summary_path.exists():
            probe = json.loads(summary_path.read_text(encoding='utf-8'))
            record.update(completed_runs=probe['completed_runs'],
                          successful_runs=sum(item['status'] == 'completed' and item['exit_code'] == 0
                                              for item in probe['results']),
                          success=record['exit_code'] == 0 and probe['success'],
                          attempts_summary=str(summary_path))
        else:
            record.update(completed_runs=0, successful_runs=0, success=False,
                          error='The private session ended before publishing probe results')
    except (Exception, SystemExit) as error:
        record.update(success=False, error=str(error))
    record['seconds'] = round(time.monotonic() - started, 3)
    return record


def main():
    if len(sys.argv) == 3 and sys.argv[1] == '--session':
        return inside_session(Path(sys.argv[2]))
    if len(sys.argv) != 1:
        raise SystemExit('Usage: warm_unicode_investigation.py')
    REPORTS.mkdir(parents=True, exist_ok=False)
    helper = ci.linux_gio_helper(ci.read_cmake_cache(BUILD))
    results = []
    summary_path = REPORTS / 'summary.json'
    for phase in PHASES:
        # Each phase changes only the stated fixture input or observation mode.
        # Failure in one session is retained while the others finish independently.
        with ThreadPoolExecutor(max_workers=SESSIONS) as workers:
            pending = [workers.submit(session, phase, number, helper) for number in range(1, SESSIONS + 1)]
            for future in as_completed(pending):
                result = future.result()
                results.append(result)
                summary_path.write_text(json.dumps({
                    'sessions_per_phase': SESSIONS, 'phases': PHASES,
                    'completed_sessions': len(results),
                    'failed_sessions': sum(not item['success'] for item in results),
                    'results': results,
                }, indent=2) + '\n', encoding='utf-8')
                print(json.dumps(result), flush=True)
    return 1 if any(not item['success'] for item in results) else 0


if __name__ == '__main__':
    raise SystemExit(main())

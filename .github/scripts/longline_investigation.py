"""Measure original dispatch and validate a finite native-map-aware candidate."""
from concurrent.futures import ThreadPoolExecutor, as_completed
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import time
import xml.etree.ElementTree as ET

sys.path.insert(0, str(Path(__file__).resolve().parent))
import ci
import decode_longline_probe

ROOT = ci.ROOT
BUILD = ROOT / '.ci/build/coverage-cmake'
REPORTS = ROOT / '.ci/reports/longline-investigation'
FIXTURES = ROOT / '.github/scripts/fixtures/longline'
SCRIPT = Path(__file__).resolve()
SOURCES = ('tests/search_continuation.cpp', 'tests/native_key_helpers.h',
           'src/editor/editor_core.cpp', 'src/diagnostics/longline_probe.h',
           'tests/syntax_large.cpp')
TESTS = ('original_search_continuation', 'native_search_feedback_release', 'large_language_selection')
DEADLINES = dict(zip((*TESTS, 'original_extra_escape'), (60, 60, 120, 60)))
TARGETS = ('search_continuation', 'syntax_large', 'extra_escape')
PHASES = {
    'original-x11': {'variant': 'original', 'backend': 'x11', 'sessions': 4},
    'candidate-x11': {'variant': 'candidate', 'backend': 'x11', 'sessions': 4},
    'candidate-wayland': {'variant': 'candidate', 'backend': 'wayland', 'sessions': 4},
    'candidate-extra-x11': {'variant': 'candidate', 'backend': 'x11', 'sessions': 1,
                            'tests': ['original_extra_escape']},
    'candidate-extra-wayland': {'variant': 'candidate', 'backend': 'wayland', 'sessions': 1,
                                'tests': ['original_extra_escape']},
}


def write_json(path, value):
    path.write_text(json.dumps(value, indent=2) + '\n', encoding='utf-8')


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def build():
    REPORTS.mkdir(parents=True, exist_ok=False)
    clean = {name: (ROOT / name).read_bytes() if (ROOT / name).exists() else None
             for name in SOURCES}
    record = {'profile': 'coverage', 'sources': {}, 'binaries': {}}
    try:
        for variant in ('original', 'candidate'):
            tree = FIXTURES / variant
            record['sources'][variant] = {}
            for name in SOURCES:
                source = tree / name
                if not source.is_file():
                    raise FileNotFoundError(source)
                (ROOT / name).write_bytes(source.read_bytes())
                record['sources'][variant][name] = sha(source)
            ci.run(['cmake', '--build', BUILD, '--parallel', '--target', *TARGETS],
                   REPORTS / f'build-{variant}.log')
            destination = REPORTS / 'binaries' / variant
            destination.mkdir(parents=True)
            record['binaries'][variant] = {}
            for target in TARGETS:
                shutil.copy2(BUILD / target, destination / target)
                record['binaries'][variant][target] = sha(destination / target)
    finally:
        for name, content in clean.items():
            if content is None:
                (ROOT / name).unlink(missing_ok=True)
            else:
                (ROOT / name).write_bytes(content)
        write_json(REPORTS / 'build.json', record)
    listing = json.loads(subprocess.check_output(
        ['ctest', '--test-dir', BUILD, '-C', 'Release', '--show-only=json-v1',
         '-R', '^(' + '|'.join(DEADLINES) + ')$'], text=True, encoding='utf-8'))['tests']
    actual = {item['name']: item for item in listing}
    if set(actual) != set(DEADLINES):
        raise RuntimeError(f'Unexpected registered test selection: {list(actual)}')
    for name, expected in DEADLINES.items():
        properties = {item['name']: item['value'] for item in actual[name].get('properties', [])}
        if properties.get('TIMEOUT') != expected:
            raise RuntimeError(f'Original deadline changed for {name}: {properties}')
    record['registrations'] = listing
    write_json(REPORTS / 'build.json', record)
    return 0


def inside_session(directory):
    plan = json.loads((directory / 'plan.json').read_text(encoding='utf-8'))
    return ci.probe(plan['command'], directory / 'attempts', plan['outer_watchdog_seconds'], 1, ROOT)


def session(phase, number, helper):
    selected = PHASES[phase]
    requested = selected.get('tests', TESTS)
    directory = REPORTS / phase / f'session-{number:02}'
    directory.mkdir(parents=True, exist_ok=False)
    [(workspace, names)] = ci.test_shards(BUILD, '^(' + '|'.join(requested) + ')$', 1,
                                         directory / 'ctest')
    if set(names) != set(requested):
        raise RuntimeError(f'Registered selection changed: {names}')
    with (workspace / 'CTestTestfile.cmake').open('a', encoding='utf-8') as stream:
        for previous, following in zip(requested, requested[1:]):
            stream.write(f'set_tests_properties({following} PROPERTIES DEPENDS {previous})\n')
    command = ['ctest', '--test-dir', workspace, '-C', 'Release', '--parallel', '1',
               '-V', '--output-on-failure', '--no-tests=error',
               '-R', '^(' + '|'.join(requested) + ')$',
               '--output-junit', directory / 'tests.xml']
    plan = {'phase': phase, 'variant': selected['variant'], 'backend': selected['backend'],
            'session': number, 'command': [str(item) for item in command],
            'test_deadlines_seconds': {name: DEADLINES[name] for name in requested},
            'outer_watchdog_seconds': sum(DEADLINES[name] for name in requested) + 40}
    write_json(directory / 'plan.json', plan)
    environment = dict(os.environ, GIO_LAUNCH_DESKTOP=str(helper),
                       WXNOTEPAD_DISPLAY_LOG_DIR=str(directory / 'display'),
                       WXNOTEPAD_PAINT_TRACE=str(directory / 'paint-trace.bin'),
                       GCOV_PREFIX=str(directory / 'coverage'), GCOV_PREFIX_STRIP='0')
    isolated = [sys.executable, ROOT / 'tools/xvfb_session.py', 'dbus-run-session', '--',
                sys.executable, ROOT / 'tools/clipboard_isolated.py', selected['backend'],
                sys.executable, SCRIPT, '--session', directory]
    started = time.monotonic()
    record = {'phase': phase, 'session': number, 'backend': selected['backend']}
    try:
        record['exit_code'] = ci.run(isolated, directory / 'session.log', environment, check=False)
        summary = json.loads((directory / 'attempts/summary.json').read_text(encoding='utf-8'))
        record['success'] = record['exit_code'] == 0 and summary['success']
        root = ET.parse(directory / 'tests.xml').getroot()
        record['tests'] = [{'name': case.get('name'), 'seconds': float(case.get('time', 0)),
                            'status': case.get('status'),
                            'failed': case.find('failure') is not None,
                            'skipped': case.find('skipped') is not None}
                           for case in root.iter('testcase')]
        if {item['name'] for item in record['tests']} != set(requested):
            raise RuntimeError('JUnit does not retain all requested tests')
        logs = '\n'.join(path.read_text(encoding='utf-8', errors='replace')
                         for path in (directory / 'session.log',
                                      directory / 'attempts/run-001.log'))
        record['registry_warnings'] = [line for line in logs.splitlines()
                                       if 'Disabling unresponsive app' in line or 'Re-enabling app' in line]
        record['trace_exists'] = (directory / 'paint-trace.bin').is_file()
        if 'original_search_continuation' in requested and not record['trace_exists']:
            raise RuntimeError('Search paint trace missing')
        if record['trace_exists']:
            manifest = json.loads((FIXTURES / selected['variant'] / 'manifest.json').read_text(encoding='utf-8'))
            decoded = decode_longline_probe.decode(directory / 'paint-trace.bin', manifest)
            write_json(directory / 'paint-trace.json', decoded)
            record['timing'] = {key: decoded[key] for key in
                                ('pid', 'published', 'dropped', 'scenario_complete',
                                 'counts', 'durations', 'open_spans')}
            if decoded['dropped']:
                raise RuntimeError('Paint timing capacity exhausted')
        record['syntax_phases'] = [dict(sequence=int(match[1]), phase=match[2],
                                        wall_us=int(match[3]), cpu_us=int(match[4]),
                                        exception=bool(int(match[5])))
                                   for match in re.finditer(
                                       r'\[SYNTAX-PROBE\] event=end sequence=(\d+) phase=(\S+) '
                                       r'clock_us=\d+ wall_us=(\d+) cpu_us=(-?\d+) exception=(\d+)', logs)]
    except (Exception, SystemExit) as error:
        record.update(success=False, collection_error=str(error))
    record['seconds'] = round(time.monotonic() - started, 3)
    write_json(directory / 'result.json', record)
    return record


def run():
    built = json.loads((REPORTS / 'build.json').read_text(encoding='utf-8'))
    helper = ci.linux_gio_helper(ci.read_cmake_cache(BUILD))
    results = []
    for phase, spec in PHASES.items():
        for target in TARGETS:
            source = REPORTS / 'binaries' / spec['variant'] / target
            if sha(source) != built['binaries'][spec['variant']][target]:
                raise RuntimeError('Measured binary changed after build')
            shutil.copy2(source, BUILD / target)
        with ThreadPoolExecutor(max_workers=spec['sessions']) as workers:
            pending = [workers.submit(session, phase, number, helper)
                       for number in range(1, spec['sessions'] + 1)]
            for future in as_completed(pending):
                record = future.result()
                results.append(record)
                write_json(REPORTS / 'summary.json', {'phases': PHASES, 'results': results})
                print(json.dumps(record), flush=True)
    # Baseline failures remain evidence; the comparison verdict requires both
    # complete baseline collection and every candidate's original assertions.
    complete = len(results) == sum(spec['sessions'] for spec in PHASES.values())
    complete &= all('collection_error' not in record for record in results)
    candidates = [record for record in results if record['phase'].startswith('candidate-')]
    success = complete and all(record['success'] for record in candidates)
    write_json(REPORTS / 'summary.json', {'phases': PHASES, 'results': results,
                                        'complete_collection': complete,
                                        'candidate_success': success})
    return 0 if success else 1


if __name__ == '__main__':
    if len(sys.argv) == 3 and sys.argv[1] == '--session':
        raise SystemExit(inside_session(Path(sys.argv[2])))
    if len(sys.argv) != 2 or sys.argv[1] not in ('build', 'run'):
        raise SystemExit('Usage: longline_investigation.py build|run')
    raise SystemExit(build() if sys.argv[1] == 'build' else run())

"""Compare modal feedback scheduling with finite native key/map dispatch."""
from concurrent.futures import ThreadPoolExecutor, as_completed
import hashlib
import json
import os
from pathlib import Path
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
REPORTS = ROOT / '.ci/reports/longline-feedback-investigation'
FIXTURES = ROOT / '.github/scripts/fixtures/longline-feedback'
SCRIPT = Path(__file__).resolve()
SOURCES = ('tests/search_continuation.cpp', 'tests/native_key_helpers.h',
           'src/editor/editor_core.cpp', 'tests/native_search_helpers.h',
           'src/diagnostics/longline_probe.h')
VARIANTS = ('callafter50', 'timer50', 'timer600')
SEARCH = 'original_search_continuation'
FEEDBACK = 'native_search_feedback_release'
ESCAPE = 'original_extra_escape'
DEADLINES = {SEARCH: 60, FEEDBACK: 60, ESCAPE: 60}
TARGETS = ('search_continuation', 'extra_escape')
PHASES = {
    'control-x11': {'variant': 'callafter50', 'backend': 'x11', 'sessions': 2,
                    'tests': (SEARCH,)},
    'candidate-stress-x11': {'variant': 'timer50', 'backend': 'x11', 'sessions': 2,
                             'tests': (SEARCH,)},
    'candidate-normal-x11': {'variant': 'timer600', 'backend': 'x11', 'sessions': 2,
                             'tests': (SEARCH, FEEDBACK)},
    'candidate-normal-wayland': {'variant': 'timer600', 'backend': 'wayland', 'sessions': 2,
                                 'tests': (SEARCH, FEEDBACK)},
    'candidate-extra-x11': {'variant': 'timer600', 'backend': 'x11', 'sessions': 1,
                            'tests': (ESCAPE,)},
    'candidate-extra-wayland': {'variant': 'timer600', 'backend': 'wayland', 'sessions': 1,
                                'tests': (ESCAPE,)},
}
FEEDBACK_KINDS = ('feedback_enter', 'feedback_queued', 'feedback_fired',
                  'feedback_click_begin', 'feedback_click_end', 'feedback_exit')


def write_json(path, value):
    path.write_text(json.dumps(value, indent=2) + '\n', encoding='utf-8')


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def checked_manifest(tree):
    path = tree / 'manifest.json'
    manifest = json.loads(path.read_text(encoding='utf-8'))
    if set(manifest['outputs']) != set(SOURCES):
        raise RuntimeError(f'Unexpected measured source set: {tree}')
    for name in SOURCES:
        if sha(tree / name) != manifest['outputs'][name]:
            raise RuntimeError(f'Measured source differs from its manifest: {tree / name}')
    return manifest, sha(path)


def build():
    REPORTS.mkdir(parents=True, exist_ok=False)
    clean = {name: (ROOT / name).read_bytes() if (ROOT / name).exists() else None
             for name in SOURCES}
    record = {'profile': 'coverage', 'sources': {}, 'manifests': {}, 'binaries': {}}
    try:
        for variant in VARIANTS:
            tree = FIXTURES / variant
            manifest, manifest_sha = checked_manifest(tree)
            record['manifests'][variant] = manifest_sha
            record['sources'][variant] = dict(manifest['outputs'])
            for name in SOURCES:
                (ROOT / name).write_bytes((tree / name).read_bytes())
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
        record['sources_restored'] = all(
            not (ROOT / name).exists() if content is None else (ROOT / name).read_bytes() == content
            for name, content in clean.items())
        write_json(REPORTS / 'build.json', record)
    if not record['sources_restored']:
        raise RuntimeError('Original sources were not restored after building the variants')
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


def feedback_observation(decoded):
    # Click also observes the Find window's Cancel button. Match only the
    # Search feedback begins (d == 1) and the ends belonging to those spans.
    click_spans = {row['sequence'] for row in decoded['events']
                   if row['kind'] == 'feedback_click_begin' and row['values'][3] == 1}
    events = [row for row in decoded['events'] if row['kind'] in FEEDBACK_KINDS and
              (row['sequence'] in click_spans if row['kind'] == 'feedback_click_begin' else
               row['span'] in click_spans if row['kind'] == 'feedback_click_end' else True)]
    counts = {kind: sum(row['kind'] == kind for row in events) for kind in FEEDBACK_KINDS}
    clicks = [row for row in events if row['kind'] == 'feedback_click_begin']
    released_before_click = bool(clicks) and all(
        any(release['shortcut'] == click['shortcut'] and release['sequence'] < click['sequence']
            for release in decoded['releases']) for click in clicks)
    entered = counts['feedback_enter']
    complete = (entered > 0 and counts['feedback_queued'] >= entered and
                counts['feedback_fired'] >= entered and released_before_click and
                all(counts[kind] == entered for kind in
                    ('feedback_click_begin', 'feedback_click_end', 'feedback_exit')))
    pending_after_release = (entered > 0 and counts['feedback_queued'] >= entered and
                             not any(counts[kind] for kind in FEEDBACK_KINDS[2:]) and
                             any(release['shortcut'] == row['shortcut']
                                 for row in events if row['kind'] == 'feedback_enter'
                                 for release in decoded['releases']))
    return {'counts': counts, 'events': events, 'released_before_click': released_before_click,
            'complete_response': complete, 'queued_unfired_after_release': pending_after_release}


def session(phase, number, helper, manifest):
    selected = PHASES[phase]
    requested = selected['tests']
    directory = REPORTS / phase / f'session-{number:02}'
    directory.mkdir(parents=True, exist_ok=False)
    traces = directory / 'traces'
    traces.mkdir()
    [(workspace, names)] = ci.test_shards(BUILD, '^(' + '|'.join(requested) + ')$', 1,
                                         directory / 'ctest')
    if set(names) != set(requested):
        raise RuntimeError(f'Registered selection changed: {names}')
    trace_paths = {name: traces / f'{name}.bin' for name in requested}
    with (workspace / 'CTestTestfile.cmake').open('a', encoding='utf-8') as stream:
        for previous, following in zip(requested, requested[1:]):
            stream.write(f'set_tests_properties({following} PROPERTIES DEPENDS {previous})\n')
        for name, path in trace_paths.items():
            value = f'WXNOTEPAD_PAINT_TRACE={path.as_posix()}'
            delimiter = '='
            while f']{delimiter}]' in value:
                delimiter += '='
            stream.write(f'set_property(TEST {name} APPEND PROPERTY ENVIRONMENT '
                         f'[{delimiter}[{value}]{delimiter}])\n')
    command = ['ctest', '--test-dir', workspace, '-C', 'Release', '--parallel', '1',
               '-V', '--output-on-failure', '--no-tests=error',
               '-R', '^(' + '|'.join(requested) + ')$',
               '--output-junit', directory / 'tests.xml']
    plan = {'phase': phase, 'variant': selected['variant'], 'backend': selected['backend'],
            'session': number, 'command': [str(item) for item in command],
            'test_deadlines_seconds': {name: DEADLINES[name] for name in requested},
            'trace_paths': {name: str(path) for name, path in trace_paths.items()},
            'outer_watchdog_seconds': sum(DEADLINES[name] for name in requested) + 40}
    write_json(directory / 'plan.json', plan)
    environment = dict(os.environ, GIO_LAUNCH_DESKTOP=str(helper),
                       WXNOTEPAD_DISPLAY_LOG_DIR=str(directory / 'display'),
                       GCOV_PREFIX=str(directory / 'coverage'), GCOV_PREFIX_STRIP='0')
    isolated = [sys.executable, ROOT / 'tools/xvfb_session.py', 'dbus-run-session', '--',
                sys.executable, ROOT / 'tools/clipboard_isolated.py', selected['backend'],
                sys.executable, SCRIPT, '--session', directory]
    started = time.monotonic()
    record = {'phase': phase, 'variant': selected['variant'], 'session': number,
              'backend': selected['backend']}
    try:
        record['exit_code'] = ci.run(isolated, directory / 'session.log', environment, check=False)
        summary = json.loads((directory / 'attempts/summary.json').read_text(encoding='utf-8'))
        root = ET.parse(directory / 'tests.xml').getroot()
        record['tests'] = [{'name': case.get('name'), 'seconds': float(case.get('time', 0)),
                            'status': case.get('status'),
                            'failed': case.find('failure') is not None,
                            'skipped': case.find('skipped') is not None}
                           for case in root.iter('testcase')]
        if len(record['tests']) != len(requested) or {item['name'] for item in record['tests']} != set(requested):
            raise RuntimeError('JUnit does not retain exactly the requested tests')
        record['success'] = (record['exit_code'] == 0 and summary['success'] and
                             all(not item['failed'] and not item['skipped'] for item in record['tests']))
        logs = '\n'.join(path.read_text(encoding='utf-8', errors='replace')
                         for path in (directory / 'session.log', directory / 'attempts/run-001.log'))
        record['registry_warnings'] = [line for line in logs.splitlines()
                                       if 'Disabling unresponsive app' in line or 'Re-enabling app' in line]
        record['baseline_timeout'] = (phase == 'control-x11' and
                                      all(item['failed'] and 59 <= item['seconds'] <= 66 for item in record['tests']) and
                                      '***Timeout' in logs)
        record['traces'] = {}
        for name, path in trace_paths.items():
            if not path.is_file():
                if name == SEARCH:
                    raise RuntimeError('SelectionLimit trace missing')
                continue  # Held-only and Escape keep their existing assertion-based checks.
            decoded = decode_longline_probe.decode(path, manifest)
            write_json(path.with_suffix('.json'), decoded)
            observed = {key: decoded[key] for key in
                        ('pid', 'published', 'dropped', 'scenario_complete', 'counts', 'durations', 'open_spans')}
            record['traces'][name] = observed
            if decoded['dropped']:
                raise RuntimeError('Paint/feedback observation capacity exhausted')
            if name == SEARCH:
                record['timing'] = observed
                record['feedback'] = feedback_observation(decoded)
                if phase != 'control-x11':
                    record['success'] &= decoded['scenario_complete'] and record['feedback']['complete_response']
        record['baseline_modal_starvation'] = (
            record['baseline_timeout'] and not record.get('timing', {}).get('scenario_complete', True) and
            record.get('feedback', {}).get('queued_unfired_after_release', False))
    except (Exception, SystemExit) as error:
        record.update(success=False, collection_error=str(error))
    record['seconds'] = round(time.monotonic() - started, 3)
    write_json(directory / 'result.json', record)
    return record


def run():
    built = json.loads((REPORTS / 'build.json').read_text(encoding='utf-8'))
    if not built['sources_restored']:
        raise RuntimeError('Measured build did not restore the source tree')
    helper = ci.linux_gio_helper(ci.read_cmake_cache(BUILD))
    results = []
    for phase, spec in PHASES.items():
        manifest, manifest_sha = checked_manifest(FIXTURES / spec['variant'])
        if manifest_sha != built['manifests'][spec['variant']]:
            raise RuntimeError('Measured source manifest changed after build')
        for target in TARGETS:
            source = REPORTS / 'binaries' / spec['variant'] / target
            if sha(source) != built['binaries'][spec['variant']][target]:
                raise RuntimeError('Measured binary changed after build')
            shutil.copy2(source, BUILD / target)
        # Every private session and its children finish before swapping binaries.
        with ThreadPoolExecutor(max_workers=spec['sessions']) as workers:
            pending = [workers.submit(session, phase, number, helper, manifest)
                       for number in range(1, spec['sessions'] + 1)]
            for future in as_completed(pending):
                record = future.result()
                results.append(record)
                write_json(REPORTS / 'summary.json', {'phases': PHASES, 'results': results})
                print(json.dumps(record), flush=True)
    expected_sessions = sum(spec['sessions'] for spec in PHASES.values())
    expected_tests = sum(spec['sessions'] * len(spec['tests']) for spec in PHASES.values())
    complete = (len(results) == expected_sessions and
                sum(len(record.get('tests', [])) for record in results) == expected_tests and
                all('collection_error' not in record for record in results))
    candidates = [record for record in results if record['phase'].startswith('candidate-')]
    controls = [record for record in results if record['phase'] == 'control-x11']
    baseline_reproduced = len(controls) == 2 and all(record.get('baseline_modal_starvation') for record in controls)
    success = complete and baseline_reproduced and all(record['success'] for record in candidates)
    write_json(REPORTS / 'summary.json', {'phases': PHASES, 'results': results,
                                        'expected_sessions': expected_sessions, 'expected_tests': expected_tests,
                                        'complete_collection': complete,
                                        'baseline_modal_starvation_reproduced': baseline_reproduced,
                                        'candidate_success': all(record['success'] for record in candidates),
                                        'comparison_success': success})
    return 0 if success else 1


if __name__ == '__main__':
    if len(sys.argv) == 3 and sys.argv[1] == '--session':
        raise SystemExit(inside_session(Path(sys.argv[2])))
    if len(sys.argv) != 2 or sys.argv[1] not in ('build', 'run'):
        raise SystemExit('Usage: longline_feedback_investigation.py build|run')
    raise SystemExit(build() if sys.argv[1] == 'build' else run())

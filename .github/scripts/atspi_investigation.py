"""Compare native accessibility replies while the unchanged 444 cases execute."""
from concurrent.futures import ThreadPoolExecutor, as_completed
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time

sys.path.insert(0, str(Path(__file__).resolve().parent))
import ci

ROOT = ci.ROOT
BUILD = ROOT / '.ci/build/coverage-cmake'
REPORTS = ROOT / '.ci/reports/atspi-investigation'
SOURCE = ROOT / 'tests/auto_insertion.cpp'
FIXTURES = ROOT / '.github/scripts/fixtures'
SCRIPT = Path(__file__).resolve()
TEST = 'original_auto_insertion'
PHASES = {
    'original-x11': {'backend': 'x11', 'variant': 'original', 'expect': 'timeout', 'sessions': 2},
    'fixed-x11': {'backend': 'x11', 'variant': 'fixed', 'expect': 'responsive', 'sessions': 2},
    'fixed-wayland': {'backend': 'wayland', 'variant': 'fixed', 'expect': 'responsive', 'sessions': 2},
    'clean-x11': {'backend': 'x11', 'sessions': 1},
    'clean-wayland': {'backend': 'wayland', 'sessions': 1},
}


def write_json(path, value):
    path.write_text(json.dumps(value, indent=2) + '\n', encoding='utf-8')


def build():
    REPORTS.mkdir(parents=True, exist_ok=True)
    clean = SOURCE.read_bytes()
    record = {'profile': 'coverage', 'clean_source_sha256': hashlib.sha256(clean).hexdigest(),
              'sources': {}, 'binaries': {}}
    try:
        for variant in ('original', 'fixed'):
            source = FIXTURES / f'auto_insertion_{variant}_probe.cpp'
            content = source.read_bytes()
            record['sources'][variant] = hashlib.sha256(content).hexdigest()
            SOURCE.write_bytes(content)
            ci.run(['cmake', '--build', BUILD, '--parallel', '--target', 'auto_insertion'],
                   REPORTS / f'build-{variant}.log')
            destination = BUILD / f'auto_insertion-{variant}-probe'
            shutil.copy2(BUILD / 'auto_insertion', destination)
            record['binaries'][variant] = {'path': str(destination),
                                          'sha256': hashlib.sha256(destination.read_bytes()).hexdigest()}
    finally:
        SOURCE.write_bytes(clean)
        write_json(REPORTS / 'build.json', record)
    ci.run(['cmake', '--build', BUILD, '--parallel', '--target', 'auto_insertion'],
           REPORTS / 'build-clean.log')
    listing = json.loads(subprocess.check_output(
        ['ctest', '--test-dir', BUILD, '-C', 'Release', '--show-only=json-v1', '-R', f'^{TEST}$'],
        text=True, encoding='utf-8'))['tests']
    if len(listing) != 1 or listing[0]['name'] != TEST:
        raise RuntimeError('Unexpected registered test')
    properties = {item['name']: item['value'] for item in listing[0].get('properties', [])}
    if properties.get('TIMEOUT') != 90:
        raise RuntimeError(f'Expected original 90s test guard, got {properties}')
    record['registration'] = listing[0]
    record['clean_binary_sha256'] = hashlib.sha256((BUILD / 'auto_insertion').read_bytes()).hexdigest()
    write_json(REPORTS / 'build.json', record)
    return 0


def inside_session(directory):
    plan = json.loads((directory / 'plan.json').read_text(encoding='utf-8'))
    return ci.probe(plan['command'], directory / 'attempts', plan['outer_timeout'],
                    1, Path(plan['working_directory']))


def session(phase, number, helper, registration):
    selected = PHASES[phase]
    directory = REPORTS / phase / f'session-{number:02}'
    directory.mkdir(parents=True, exist_ok=False)
    properties = {item['name']: item['value'] for item in registration.get('properties', [])}
    working = Path(properties.get('WORKING_DIRECTORY', BUILD))
    environment = dict(os.environ, GIO_LAUNCH_DESKTOP=str(helper),
                       WXNOTEPAD_DISPLAY_LOG_DIR=str(directory / 'display'),
                       GCOV_PREFIX=str(directory / 'coverage'), GCOV_PREFIX_STRIP='0')
    for item in properties.get('ENVIRONMENT', []):
        name, value = item.split('=', 1)
        environment[name] = value
    if 'variant' in selected:
        command = ['/usr/bin/python3', ROOT / '.github/scripts/atspi_ping_probe.py',
                   '--executable', BUILD / f'auto_insertion-{selected["variant"]}-probe',
                   '--expect', selected['expect'], '--report-dir', directory / 'peer',
                   '--timeout', '90']
        outer_timeout = 110
    else:
        [(workspace, names)] = ci.test_shards(BUILD, f'^{TEST}$', 1, directory / 'ctest')
        if names != [TEST]:
            raise RuntimeError(f'Test selection changed: {names}')
        command = ['ctest', '--test-dir', workspace, '-C', 'Release', '--parallel', '1',
                   '-V', '--output-on-failure', '--no-tests=error', '-R', f'^{TEST}$',
                   '--output-junit', directory / 'tests.xml']
        outer_timeout = 100
    plan = {'phase': phase, 'backend': selected['backend'], 'session': number,
            'command': [str(item) for item in command], 'working_directory': str(working),
            'case_timeout': 90, 'outer_timeout': outer_timeout}
    write_json(directory / 'plan.json', plan)
    started = time.monotonic()
    isolated = [sys.executable, ROOT / 'tools/xvfb_session.py', 'dbus-run-session', '--',
                sys.executable, ROOT / 'tools/clipboard_isolated.py', selected['backend'],
                sys.executable, SCRIPT, '--session', directory]
    record = dict(phase=phase, session=number, backend=selected['backend'])
    try:
        record['exit_code'] = ci.run(isolated, directory / 'session.log', environment, check=False)
        summary = json.loads((directory / 'attempts/summary.json').read_text(encoding='utf-8'))
        record['success'] = record['exit_code'] == 0 and summary['success']
        peer = directory / 'peer/summary.json'
        if peer.exists():
            record['peer'] = json.loads(peer.read_text(encoding='utf-8'))
        # Preserve daemon warnings independently of the external request verdict.
        text = (directory / 'session.log').read_text(encoding='utf-8')
        record['registry_warnings'] = [line for line in text.splitlines()
                                       if 'Disabling unresponsive app' in line or 'Re-enabling app' in line]
    except (Exception, SystemExit) as error:
        record.update(success=False, error=str(error))
    record['seconds'] = round(time.monotonic() - started, 3)
    write_json(directory / 'result.json', record)
    return record


def run():
    REPORTS.mkdir(parents=True, exist_ok=True)
    built = json.loads((REPORTS / 'build.json').read_text(encoding='utf-8'))
    helper = ci.linux_gio_helper(ci.read_cmake_cache(BUILD))
    results = []
    for phase, spec in PHASES.items():
        with ThreadPoolExecutor(max_workers=spec['sessions']) as workers:
            pending = [workers.submit(session, phase, n, helper, built['registration'])
                       for n in range(1, spec['sessions'] + 1)]
            for future in as_completed(pending):
                record = future.result()
                results.append(record)
                write_json(REPORTS / 'summary.json',
                           {'phases': PHASES, 'results': results,
                            'success': all(item['success'] for item in results),
                            'completed_sessions': len(results)})
                print(json.dumps(record), flush=True)
    return 0 if all(item['success'] for item in results) else 1


if __name__ == '__main__':
    if len(sys.argv) == 3 and sys.argv[1] == '--session':
        raise SystemExit(inside_session(Path(sys.argv[2])))
    if len(sys.argv) != 2 or sys.argv[1] not in ('build', 'run'):
        raise SystemExit('Usage: atspi_investigation.py build|run')
    raise SystemExit(build() if sys.argv[1] == 'build' else run())


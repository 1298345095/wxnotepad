"""Run focused Windows context-menu investigations through registered CTest cases."""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess

import ci

CONTEXT_CASE = 'original_editor_context_menu'
# CTest omits an unbuilt executable's command from its JSON listing. Keep the
# inspected predecessor targets explicit so cold investigations can build them.
PREFIX_TARGETS = (
    'context_menu', 'date_time_format', 'default_encoding', 'document_tools',
    'menu_hierarchy', 'new_window', 'open_targets', 'recent_files', 'run_command',
    'scroll_past_last_line', 'sdk_capabilities', 'selection_options',
    'settings_read_failure', 'wxnotepad',
)


def plan(cmake_dir, scenario):
    cmake_dir = cmake_dir.resolve()
    cases = json.loads(subprocess.check_output(
        ['ctest', '--test-dir', str(cmake_dir), '-C', 'Release', '--show-only=json-v1'],
        text=True, encoding='utf-8'))['tests']
    names = [case['name'] for case in cases]
    if CONTEXT_CASE not in names:
        raise SystemExit(f'Required registered test is missing: {CONTEXT_CASE}')
    if scenario == 'prefix':
        selected = cases[:names.index(CONTEXT_CASE) + 1]
        targets = PREFIX_TARGETS
    else:
        selected = [cases[names.index(CONTEXT_CASE)]]
        targets = ('context_menu',)  # Includes its existing clipboard_peer dependency.
    return {'cmake_dir': str(cmake_dir), 'scenario': scenario,
            'tests': [case['name'] for case in selected], 'targets': list(targets)}


def build(selection, reports, jobs):
    ci.run(ci.cmake_command(['--build', selection['cmake_dir'], '--config', 'Release',
                            '--parallel', str(jobs), '--target', *selection['targets']]), reports / 'build.log')


def run(selection, reports, runs):
    expression = '^(' + '|'.join(re.escape(name) for name in selection['tests']) + ')$'
    for attempt in range(1, runs + 1):
        print(f'Investigation {attempt}/{runs}: {len(selection["tests"])} registered cases', flush=True)
        ci.test('windows', Path(selection['cmake_dir']), expression, 1,
                report_directory=reports / f'run-{attempt:03}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=('plan', 'build', 'run'))
    parser.add_argument('--scenario', choices=('prefix', 'clipboard'), default='prefix')
    parser.add_argument('--cmake-dir', type=Path, default=ci.BUILD / 'cmake')
    parser.add_argument('--report-dir', type=Path, required=True)
    parser.add_argument('--runs', type=int, default=3)
    parser.add_argument('--jobs', type=int, default=os.cpu_count() or 1)
    arguments = parser.parse_args()
    if arguments.runs < 1 or arguments.jobs < 1:
        parser.error('--runs and --jobs must be positive')
    if arguments.action != 'plan' and not ci.WINDOWS:
        parser.error('Native Windows investigations must run on Windows')
    selection = plan(arguments.cmake_dir, arguments.scenario)
    reports = arguments.report_dir.resolve()
    reports.mkdir(parents=True, exist_ok=False)
    (reports / 'plan.json').write_text(json.dumps(selection, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(selection, indent=2), flush=True)
    if arguments.action == 'build':
        build(selection, reports, arguments.jobs)
    elif arguments.action == 'run':
        run(selection, reports, arguments.runs)


if __name__ == '__main__':
    main()

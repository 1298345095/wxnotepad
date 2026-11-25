"""Temporary PR 275 diagnosis: preserve every original test outcome."""
from concurrent.futures import ThreadPoolExecutor
import json
from pathlib import Path
import threading
import time

import ci

REPORTS = Path('.ci/reports/unicode-investigation').resolve()
BUILD = Path('.ci/build/cmake').resolve()
SESSIONS = 4
RUNS = 100
MAX_FAILURES = 5
records = []
lock = threading.Lock()
finished = threading.Event()


def check(session):
    for attempt in range(1, RUNS + 1):
        if finished.is_set():
            return
        report = REPORTS / f'session-{session:02}' / f'run-{attempt:03}'
        started = time.monotonic()
        record = {'session': session, 'attempt': attempt, 'report': str(report)}
        try:
            ci.test('x11', BUILD, '^original_unicode_controls$', 1, report_directory=report)
        except (Exception, SystemExit) as error:
            record.update(success=False, error=str(error))
        else:
            record['success'] = True
        record['seconds'] = round(time.monotonic() - started, 3)
        with lock:
            records.append(record)
            failed = sum(not item['success'] for item in records)
            (REPORTS / 'summary.json').write_text(json.dumps({
                'sessions': SESSIONS, 'runs_per_session': RUNS,
                'completed': len(records), 'failed': failed, 'results': records,
            }, indent=2) + '\n', encoding='utf-8')
            print(f'Unicode investigation: completed={len(records)} failed={failed}', flush=True)
            if failed >= MAX_FAILURES:
                finished.set()


REPORTS.mkdir(parents=True, exist_ok=False)
with ThreadPoolExecutor(max_workers=SESSIONS) as pool:
    list(pool.map(check, range(1, SESSIONS + 1)))
raise SystemExit(1 if any(not item['success'] for item in records) else 0)

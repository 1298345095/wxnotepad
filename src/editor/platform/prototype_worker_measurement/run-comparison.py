"""Run baseline and worker-owned candidates serially after the user's ready signal."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import time

parser = argparse.ArgumentParser()
parser.add_argument('build', type=Path)
parser.add_argument('output', type=Path)
parser.add_argument('--suffix', default='')
parser.add_argument('--fixtures', default='multi,long')
parser.add_argument('--caps', default='1,0')
parser.add_argument('--baseline-name', default='worker_baseline_layout')
parser.add_argument('--candidate-name', default='worker_layout')
parser.add_argument('--require-geometry', action='store_true')
args = parser.parse_args()
args.output.mkdir(parents=True)
executables = {name: (args.build / (target + args.suffix)).resolve() for name, target in
               [('baseline', args.baseline_name), ('worker-owned', args.candidate_name)]}
report = dict(binaries={name: dict(path=str(path), sha256=hashlib.sha256(path.read_bytes()).hexdigest())
                       for name, path in executables.items()}, rows=[])
for fixture in args.fixtures.split(','):
    for repetition in range(1, 4):
        for cap in map(int, args.caps.split(',')):
            order = list(executables)
            if repetition % 2 == 0:
                order.reverse()
            for variant in order:
                log = args.output / f'{fixture}-{repetition}-cap{cap}-{variant}.log'
                started = time.monotonic()
                with log.open('w', encoding='utf-8') as stream:
                    result = subprocess.run([str(executables[variant]), str(cap), fixture],
                                            stdout=stream, stderr=subprocess.STDOUT, timeout=180)
                lines = log.read_text(encoding='utf-8', errors='replace').splitlines()
                records = [json.loads(line) for line in lines if line.startswith('{')]
                timings = [record for record in records if 'phase' in record]
                memories = [record for record in records if record.get('metric') == 'memory']
                geometries = [record for record in records if record.get('metric') == 'geometry']
                checks = [line for line in lines if line.startswith('CHECK ')]
                ok = result.returncode == 0 and len(timings) == 2 and len(memories) == 4 and len(checks) == 1
                if args.require_geometry:
                    ok = ok and len(geometries) == 14
                row = dict(fixture=fixture, repetition=repetition, requested_cap=cap, variant=variant,
                           exit=result.returncode, ok=ok, process_seconds=time.monotonic()-started,
                           timings=timings, memory=memories, geometries=geometries, checks=checks, log=log.name)
                report['rows'].append(row)
                (args.output / 'results.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
                print(fixture, repetition, cap, variant, ok, flush=True)
                if not ok:
                    raise SystemExit(f'Comparison stopped; inspect retained log {log}')

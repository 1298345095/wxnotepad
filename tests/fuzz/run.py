"""Replay checked-in seeds or run bounded libFuzzer sessions; retain logs and inputs."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import time

HERE = Path(__file__).resolve().parent
TARGETS = ('codecs', 'detection', 'file_variables')
MAX_INPUT_BYTES = 64 * 1024
INPUT_TIMEOUT_SECONDS = 10
PROCESS_GRACE_SECONDS = 30
RSS_LIMIT_MIB = 2048


def materialize(target, directory):
    directory.mkdir(parents=True, exist_ok=True)
    seeds = json.loads((HERE / 'corpus.json').read_text(encoding='utf-8'))[target]
    inputs = []
    for seed in seeds:
        if 'hex' in seed:
            payload = bytes.fromhex(seed['hex'])
        elif 'text' in seed:
            payload = seed['text'].encode('utf-8')
        else:
            payload = (HERE.parent / seed['file']).read_bytes()
        payload = bytes.fromhex(seed.get('prefix', '')) + b' ' * seed.get('padding', 0) + payload
        path = directory / (seed['name'] + '.bin')
        path.write_bytes(payload)
        inputs.append(path)
    return inputs


def execute(command, log, timeout):
    environment = dict(os.environ)
    environment.setdefault('LC_ALL', 'C.UTF-8')
    symbolizer = shutil.which('llvm-symbolizer-18') or shutil.which('llvm-symbolizer')
    if symbolizer:
        environment.setdefault('ASAN_SYMBOLIZER_PATH', symbolizer)
    environment.setdefault('UBSAN_OPTIONS', 'print_stacktrace=1:halt_on_error=1')
    started = time.monotonic()
    with log.open('w', encoding='utf-8') as output:
        try:
            result = subprocess.run([str(item) for item in command], env=environment,
                                    stdout=output, stderr=subprocess.STDOUT, timeout=timeout, check=False)
            exit_code = result.returncode
        except subprocess.TimeoutExpired:
            output.write(f'\nHarness process exceeded its {timeout}s deadline.\n')
            exit_code = 124
    print(log.read_text(encoding='utf-8', errors='replace'), end='', flush=True)
    return dict(command=[str(item) for item in command], exit_code=exit_code,
                seconds=round(time.monotonic() - started, 3), log=str(log))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='mode', required=True)
    replay = commands.add_parser('replay')
    replay.add_argument('--target', choices=TARGETS, required=True)
    replay.add_argument('--binary', type=Path, required=True)
    replay.add_argument('--output', type=Path, required=True)
    fuzz = commands.add_parser('fuzz')
    fuzz.add_argument('--bin-dir', type=Path, required=True)
    fuzz.add_argument('--corpus', type=Path, required=True)
    fuzz.add_argument('--output', type=Path, required=True)
    fuzz.add_argument('--seconds', type=int, default=60, help='Mutation time per target')
    args = parser.parse_args()
    if args.mode == 'fuzz' and args.seconds < 1:
        parser.error('--seconds must be positive')
    args.output.mkdir(parents=True, exist_ok=True)
    results = []
    targets = (args.target,) if args.mode == 'replay' else TARGETS
    for target in targets:
        if args.mode == 'replay':
            inputs = materialize(target, args.output / 'corpus')
            command = [args.binary.resolve(), *inputs]
            deadline = PROCESS_GRACE_SECONDS
        else:
            corpus = args.corpus.resolve() / target
            materialize(target, corpus)
            artifacts = args.output.resolve() / target / 'artifacts'
            artifacts.mkdir(parents=True, exist_ok=True)
            command = [(args.bin_dir / f'fuzz_{target}').resolve(), corpus,
                       f'-max_total_time={args.seconds}', f'-max_len={MAX_INPUT_BYTES}',
                       f'-timeout={INPUT_TIMEOUT_SECONDS}', f'-rss_limit_mb={RSS_LIMIT_MIB}',
                       f'-artifact_prefix={artifacts}{os.sep}', '-print_final_stats=1']
            deadline = args.seconds + PROCESS_GRACE_SECONDS
        result = execute(command, args.output / f'{target}.log', deadline)
        results.append(dict(target=target, **result))
    (args.output / 'summary.json').write_text(json.dumps(results, indent=2) + '\n', encoding='utf-8')
    return int(any(result['exit_code'] for result in results))


if __name__ == '__main__':
    raise SystemExit(main())

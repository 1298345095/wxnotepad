"""Decode the actual mapped records, including spans left open by CTest timeout."""
import argparse
from collections import Counter
import json
from pathlib import Path
import struct

HEADER = struct.Struct('<8s7Q')
ROW = struct.Struct('<QqqQQQqqqq')
KINDS = {
    1: 'scenario_begin', 2: 'scenario_end', 3: 'stage',
    10: 'shortcut_begin', 11: 'shortcut_end', 12: 'focus_begin', 13: 'focus_end',
    14: 'request_begin', 15: 'request_end', 16: 'dispatch_begin', 17: 'dispatch_end',
    18: 'release', 19: 'final_dispatch_begin', 20: 'final_dispatch_end',
    30: 'draw_begin', 31: 'draw_end', 32: 'viewport', 33: 'paint_begin', 34: 'paint_end',
    40: 'caret_tick_begin', 41: 'caret_tick_end', 42: 'idle_begin', 43: 'idle_end',
    50: 'native_idle_added', 51: 'native_idle_removed',
    60: 'map_shown', 61: 'map_acknowledged', 62: 'map_wait_begin', 63: 'map_wait_end',
}
BEGIN_END = {1: 2, 10: 11, 12: 13, 14: 15, 16: 17, 19: 20,
             30: 31, 33: 34, 40: 41, 42: 43, 62: 63}


def decode(path, manifest=None):
    data = path.read_bytes()
    magic, version, size, capacity, published, dropped, pid, reserved = HEADER.unpack_from(data)
    if magic != b'WXLP275\0' or version != 1 or size != ROW.size or published > capacity:
        raise ValueError('Invalid longline trace header')
    if len(data) != HEADER.size + capacity * size:
        raise ValueError('Truncated longline trace storage')
    stage_names = manifest.get('stage_names', {}) if manifest else {}
    events, open_spans, spans = [], {}, []
    zero = None
    stage = None
    for index in range(published):
        seq, wall, cpu, kind, shortcut, span, a, b, c, d = ROW.unpack_from(data, HEADER.size + index * size)
        if seq != index + 1 or kind not in KINDS or wall < 0 or cpu < 0:
            raise ValueError(f'Invalid committed longline trace row {index + 1}')
        if zero is None:
            zero = wall
        if kind == 3:
            token = str(a & ((1 << 64) - 1))
            stage = stage_names.get(token, token)
        row = {'sequence': seq, 'kind': KINDS[kind], 'wall_ns': wall, 'thread_cpu_ns': cpu,
               'since_start_ms': round((wall - zero) / 1e6, 4), 'shortcut': shortcut,
               'span': span, 'stage': stage, 'values': [a, b, c, d]}
        if kind in (16, 17):
            row['received'] = bool(a)
        if kind == 30:
            row.update(document_bytes=a, wrapping=bool(b), caret_period_ms=c, core=hex(d))
        if kind == 32:
            row.update(width=a, height=b, wrap_width=c, text_start_and_right_margin=d)
        if kind in (60, 61):
            row.update(window=hex(a), show_epoch=b, acknowledged_epoch=c)
            row['shown' if kind == 60 else 'gdk_event_type'] = bool(d) if kind == 60 else d
        if kind in (62, 63):
            row['complete'] = bool(a)
        events.append(row)
        if kind in BEGIN_END:
            open_spans[seq] = (kind, row)
        elif kind in BEGIN_END.values():
            if span not in open_spans:
                raise ValueError(f'Unmatched span end at row {seq}')
            begin_kind, begin = open_spans.pop(span)
            if BEGIN_END[begin_kind] != kind:
                raise ValueError(f'Mismatched span end at row {seq}')
            spans.append({'kind': KINDS[begin_kind].removesuffix('_begin'),
                          'begin': span, 'end': seq, 'shortcut': begin['shortcut'],
                          'stage': begin['stage'], 'begin_ms': begin['since_start_ms'],
                          'end_ms': row['since_start_ms'],
                          'wall_ms': round((wall - begin['wall_ns']) / 1e6, 4),
                          'thread_cpu_ms': round((cpu - begin['thread_cpu_ns']) / 1e6, 4),
                          'values_begin': begin['values'], 'values_end': row['values']})
    counts = Counter(row['kind'] for row in events)
    durations = {}
    for kind in ('draw', 'paint', 'dispatch', 'focus', 'final_dispatch', 'caret_tick', 'idle', 'map_wait'):
        matching = [item for item in spans if item['kind'] == kind]
        durations[kind] = {'completed': len(matching),
                           'wall_ms_total': round(sum(item['wall_ms'] for item in matching), 4),
                           'wall_ms_max': max((item['wall_ms'] for item in matching), default=0),
                           'thread_cpu_ms_total': round(sum(item['thread_cpu_ms'] for item in matching), 4)}
    return {'file': str(path), 'pid': pid, 'published': published, 'dropped': dropped,
            'scenario_complete': counts['scenario_end'] == 1,
            'counts': dict(counts), 'durations': durations,
            'open_spans': [dict(row, expected_end=KINDS[BEGIN_END[kind]])
                           for kind, row in open_spans.values()],
            'releases': [row for row in events if row['kind'] == 'release'],
            'spans': spans, 'events': events}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('trace', type=Path)
    parser.add_argument('--manifest', type=Path)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    manifest = json.loads(args.manifest.read_text()) if args.manifest else None
    result = decode(args.trace, manifest)
    raw = json.dumps(result, indent=2) + '\n'
    if args.output:
        args.output.write_text(raw, encoding='utf-8')
        print(json.dumps({key: result[key] for key in
                          ('file', 'pid', 'published', 'dropped', 'scenario_complete', 'counts', 'durations', 'open_spans')}))
    else:
        print(raw, end='')


if __name__ == '__main__':
    main()

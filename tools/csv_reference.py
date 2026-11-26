"""Format independent original CSV capture states into the offline native fixture."""
import argparse
import json
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('capture', type=Path)
parser.add_argument('output', type=Path)
args = parser.parse_args()
rows = json.loads(args.capture.read_text(encoding='utf-8'))
lines = ['# Pinned Notepad4 89b8b36e0ee2c23f73be922315f17261ccfbbe1a; capture_csv.py; BSD-3-Clause.']
for row in rows:
    options = row.get('options')
    selection = ([str(options[0]), str(options[1]), str(int(options[2])), str(int(options[3])), options[4].encode('utf-8').hex()]
                 if options else [])
    for state in row['states']:
        lines.append('\t'.join([row['name'], str(row['position']), state['stage'], state['text'],
                                state['styles'], ','.join(map(str, state['lines'])),
                                ','.join(map(str, state['folds']))] + selection).rstrip('	'))
args.output.write_text('\n'.join(lines) + '\n', encoding='utf-8', newline='\n')

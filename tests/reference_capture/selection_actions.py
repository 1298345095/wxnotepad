"""Extract pinned original selection actions for an independent native capture."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess

REVISION = '89b8b36e0ee2c23f73be922315f17261ccfbbe1a'


def function(source, signature):
    start = source.index(signature)
    opening = source.index('{', start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end] + '\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--upstream', type=Path, required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    args = parser.parse_args()
    args.output_dir.mkdir(parents=True, exist_ok=False)
    provenance = {'revision': REVISION, 'sources': {}}
    for filename, signatures in {
        'src/Edit.cpp': ('LPWSTR EditURLEncodeSelection(', 'void EditSelectionAction('),
        'src/Helpers.cpp': ('bool ExtractFirstArgument(',),
        'src/Helpers.h': ('template <DWORD cchDest>\n[[nodiscard]] inline bool ExpandEnvironmentStringsEx(',),
    }.items():
        source = subprocess.check_output(['git', '-C', str(args.upstream), 'show', f'{REVISION}:{filename}'])
        text = source.decode('utf-8').replace('\r\n', '\n')
        snippets = ''.join(function(text, signature) for signature in signatures)
        destination = args.output_dir / (Path(filename).name + '.inc')
        destination.write_text(snippets, encoding='utf-8')
        provenance['sources'][filename] = {
            'sha256': hashlib.sha256(source).hexdigest(),
            'extracted_sha256': hashlib.sha256(destination.read_bytes()).hexdigest()}
    defaults = []
    for resource in ('src/Notepad4.rc', 'locale/zh-Hans/Notepad4.rc', 'locale/zh-Hant/Notepad4.rc'):
        source = subprocess.check_output(['git', '-C', str(args.upstream), 'show', f'{REVISION}:{resource}'])
        provenance['sources'][resource] = {'sha256': hashlib.sha256(source).hexdigest()}
        defaults.append(re.findall(r'IDS_(?:GOOGLE|BING|WIKI)_SEARCH_URL\s+"([^"]+)"', source.decode('utf-8')))
    (args.output_dir / 'defaults.inc').write_text(
        'const wchar_t* defaults[3][3] = {\n' + ''.join(
            '{' + ','.join('L"' + value + '"' for value in language) + '},\n' for language in defaults) + '};\n',
        encoding='utf-8')
    (args.output_dir / 'provenance.json').write_text(json.dumps(provenance, indent=2) + '\n', encoding='utf-8')


if __name__ == '__main__':
    main()

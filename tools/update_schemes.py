"""Import full original scheme tables from the pinned Notepad4 revision (offline)."""
import argparse
import re
from pathlib import Path

from scheme_data import generate_scheme, original_lexer, read_pinned

ROOT = Path(__file__).resolve().parents[1]
# Explicit symbol/file aliases reflect the original source, not separate parsers.
SCHEMES = {'Rust': 'Rust', 'Go': 'Go', 'Lua': 'Lua', 'CPP': 'CPP',
           'Python': 'Python', 'Bash': 'Bash', 'JavaScript': 'JS', 'TypeScript': 'TS',
           'JSON': 'JSON', 'HTML': 'HTML', 'XML': 'XML', 'CSS': 'CSS', 'VBS': 'VBS', 'PHP': 'PHP',
           'TOML': 'TOML', 'YAML': 'YAML', 'CMake': 'CMake', 'Markdown': 'Markdown', 'SQL': 'SQL',
           'Java': 'Java', 'CSharp': 'CSharp', 'Kotlin': 'Kotlin',
           'Batch': 'Batch', 'PowerShell': 'PS1', 'AutoHotkey': 'AHK',
           'INI': 'INI', 'Make': 'MAK', 'Diff': 'Diff'}
SOURCE_FILES = {'INI': 'Default', 'Diff': 'Default'}


def main(schemes=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--output-dir', type=Path, default=ROOT / 'src/syntax/generated')
    parser.add_argument('--schemes', nargs='+', choices=SCHEMES, default=schemes or list(SCHEMES))
    args = parser.parse_args()
    args.output_dir.mkdir(parents=True, exist_ok=True)
    for filename in args.schemes:
        source_file = SOURCE_FILES.get(filename, filename)
        data = read_pinned(args.source, f'src/EditLexers/stl{source_file}.cpp')
        # Resource identities belong to the original catalog record, never enum order.
        identity = re.search(rb'\bNP2LEX_\w+', original_lexer(data, SCHEMES[filename]).encode())
        scheme_id = None
        if identity:
            resources = read_pinned(args.source, 'src/EditLexer.h')
            ids = dict((key, int(value)) for key, value in re.findall(rb'#define\s+(NP2LEX_\w+)\s+(\d+)', resources))
            scheme_id = ids[identity[0]] - ids[b'NP2LEX_TEXTFILE']
        name = filename.lower()
        output = generate_scheme(data, source_file, SCHEMES[filename], name,
                                 include_extensions=filename not in {'CPP', 'Python', 'JSON', 'Bash'},
                                 scheme_id=scheme_id)
        (args.output_dir / f'{name}_scheme_data.h').write_text(output, encoding='utf-8', newline='\n')


if __name__ == '__main__':
    main()

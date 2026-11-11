"""Import full original scheme tables from the pinned Notepad4 revision (offline)."""
import argparse
from pathlib import Path

from scheme_data import generate_scheme, read_pinned

ROOT = Path(__file__).resolve().parents[1]
# Explicit symbol/file aliases reflect the original source, not separate parsers.
SCHEMES = {'Rust': 'Rust', 'Go': 'Go', 'Lua': 'Lua', 'CPP': 'CPP',
           'Python': 'Python', 'Bash': 'Bash', 'JavaScript': 'JS', 'TypeScript': 'TS',
           'JSON': 'JSON', 'HTML': 'HTML', 'XML': 'XML'}


def main(schemes=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--output-dir', type=Path, default=ROOT / 'src/syntax/generated')
    parser.add_argument('--schemes', nargs='+', choices=SCHEMES, default=schemes or list(SCHEMES))
    args = parser.parse_args()
    args.output_dir.mkdir(parents=True, exist_ok=True)
    for filename in args.schemes:
        data = read_pinned(args.source, f'src/EditLexers/stl{filename}.cpp')
        name = filename.lower()
        output = generate_scheme(data, filename, SCHEMES[filename], name,
                                 include_extensions=filename not in {'CPP', 'Python', 'JSON', 'Bash'})
        (args.output_dir / f'{name}_scheme_data.h').write_text(output, encoding='utf-8', newline='\n')


if __name__ == '__main__':
    main()

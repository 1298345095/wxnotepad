"""Check the shared importer against pinned Ruby/Perl/Tcl facts."""
import argparse
from pathlib import Path
import subprocess
import sys
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--source', required=True)
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory() as directory:
    subprocess.run([sys.executable, str(root / 'tools/update_schemes.py'), '--source', args.source,
                    '--output-dir', directory, '--schemes', 'Ruby', 'Perl', 'Tcl'], check=True)
    for name, title, extensions, identity, flags, styles in [
            ('ruby', 'Ruby Script', ['rb', 'ruby', 'rbw', 'rake', 'rjs', 'gemspec', 'podspec', 'rbs', 'cr'], 33,
             {0: 2, 1: 10, 2: 10, 3: 2, 4: 12, 5: 2, 6: 2, 7: 2, 8: 4},
             ['SCE_RB_POD', 'SCE_RB_DATASECTION', 'SCE_RB_STRING_QQ']),
            ('perl', 'Perl Script', ['pl', 'pm', 'cgi', 'pod', 'plx', 'stp'], 31,
             {0: 2, 1: 10, 2: 4}, ['SCE_PL_POD', 'SCE_PL_DATASECTION', 'SCE_PL_REGSUBST']),
            ('tcl', 'Tcl Script', ['tcl', 'itcl', 'tm'], 35,
             {0: 0, 1: 0, 2: 0}, ['SCE_TCL_SUBSTITUTION', 'SCE_TCL_SUB_BRACE', 'SCE_TCL_IN_QUOTE'])]:
        content = (Path(directory) / f'{name}_scheme_data.h').read_text(encoding='utf-8')
        assert f'{name}SettingsSection = "{title}"' in content
        for extension in extensions:
            assert f'"{extension}"' in content
        for index, attributes in flags.items():
            assert f'{name}Keywords[{index}] ? {name}Keywords[{index}] : "", {attributes}U << 8U' in content
        for style in styles:
            assert style in content
        metadata = next(line for line in content.splitlines()
                        if line.startswith(f'constexpr SyntaxOriginalMetadata {name}Original'))
        assert metadata.endswith(f', {identity}}};')
        if name == 'perl':
            assert 'SCE_PL_PUNCTUATION' not in content, 'commented original styles are not active styles'
        assert content == (root / f'src/syntax/generated/{name}_scheme_data.h').read_text(encoding='utf-8')
print('PASS: reproducible original Ruby/Perl/Tcl tables, metadata and identities')

"""Check the public shared import CLI against pinned configuration scheme literals."""
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
                    '--output-dir', directory, '--schemes', 'TOML', 'YAML', 'CMake'], check=True)
    headers = {name: (Path(directory) / f'{name}_scheme_data.h').read_text(encoding='utf-8')
               for name in ['toml', 'yaml', 'cmake']}
    # Literal facts from original stlTOML/YAML/CMake.cpp, independent of extraction.
    assert '"pylintrc"' in headers['toml']
    assert 'tomlOriginal = {11, 2, 2,' in headers['toml']
    assert 'SCE_TOML_TABLE, 0x000000, 0xFFC040, true, false, true' in headers['toml']
    assert 'yamlOriginal = {59, 2, 2,' in headers['yaml']
    assert '"clang-format"' in headers['yaml'] and '"clangd"' in headers['yaml']
    assert 'SCE_YAML_DOCUMENT, 0x000000, 0xFFC040, true, false, true' in headers['yaml']
    assert 'cmakeKeywords[6] ? cmakeKeywords[6] : "", 4U << 8U' in headers['cmake']
    assert 'cmakeKeywords[7] ? cmakeKeywords[7] : "", 4U << 8U' in headers['cmake']
    assert 'cmakeOriginal = {1152, 4, 4,' in headers['cmake']
    assert '"ctest"' in headers['cmake'] and 'target_link_libraries(' in headers['cmake']
    for name, content in headers.items():
        assert content == (root / f'src/syntax/generated/{name}_scheme_data.h').read_text(encoding='utf-8')
print('PASS: shared import CLI preserves TOML/YAML/CMake literal styles, flags, defaults and extensions')

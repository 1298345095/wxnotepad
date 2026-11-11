"""Check the public pinned scheme-import CLI against original literal data."""
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
                    '--output-dir', directory, '--schemes', 'Rust', 'Go', 'Lua'], check=True)
    rust = (Path(directory) / 'rust_scheme_data.h').read_text(encoding='utf-8')
    go = (Path(directory) / 'go_scheme_data.h').read_text(encoding='utf-8')
    lua = (Path(directory) / 'lua_scheme_data.h').read_text(encoding='utf-8')
    # Literal source facts from pinned stlRust/Go/Lua.cpp, independent of extraction.
    assert 'rustKeywords[1] ? rustKeywords[1] : "", 10U << 8U' in rust
    assert 'rustKeywords[11] ? rustKeywords[11] : "", 4U << 8U' in rust
    assert 'SCE_RUST_LIFETIME' in rust and 'SCE_RUST_RAW_BYTESTRING' in rust
    assert '"gop"' in go and '"ql"' in go
    assert 'goKeywords[9] ? goKeywords[9] : "", 4U << 8U' in go
    assert '"wlua"' in lua and '"luadoc"' in lua and '"luax"' in lua
    assert 'luaKeywords[3] ? luaKeywords[3] : "", 4U << 8U' in lua
    assert 'coroutine.isyieldable(' in lua and 'table.create(' in lua
    for name, content in [('rust', rust), ('go', go), ('lua', lua)]:
        assert content == (root / f'src/syntax/generated/{name}_scheme_data.h').read_text(encoding='utf-8')
print('PASS: shared CLI preserves original full groups, flags, styles, extensions and reproducible output')

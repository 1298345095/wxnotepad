"""Check Markdown's public importer against literal pinned original style facts."""
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
                    '--output-dir', directory, '--schemes', 'Markdown'], check=True)
    content = (Path(directory) / 'markdown_scheme_data.h').read_text(encoding='utf-8')
    assert 'markdownKeywords[0] ? markdownKeywords[0] : "", 10U << 8U' in content
    assert '"md", "markdown", "mdown", "mkdn", "mkd", "litcoffee", "rmd", "jmd"' in content
    assert '{SCE_MARKDOWN_STRIKEOUT, 0x000000, 0xFFFFFF, false, false, false, false, true, false}' in content
    assert '{SCE_MARKDOWN_SUPERSCRIPT, 0x000000, 0xFFFFFF, false, false, false, false, false, true}' in content
    assert '{SCE_MARKDOWN_SUBSCRIPT, 0x000000, 0xFFFFFF, false, false, false, true, false, false}' in content
    assert '{SCE_MARKDOWN_HRULE, 0x000000, 0xFFC040, true, false, true}' in content
    assert content == (root / 'src/syntax/generated/markdown_scheme_data.h').read_text(encoding='utf-8')
print('PASS: shared importer retains pinned Markdown styles, attributes, groups and extensions reproducibly')

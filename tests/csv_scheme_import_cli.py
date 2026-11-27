"""Compare the offline CSV importer with pinned original table literals."""
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
                    '--output-dir', directory, '--schemes', 'CSV'], check=True)
    generated = (Path(directory) / 'csv_scheme_data.h').read_text(encoding='utf-8')
    assert 'csvSettingsSection = "CSV File"' in generated
    assert 'csvOriginal = {2060, 4, 4,' in generated and ', 80};' in generated
    assert 'csvKeywords[0] ? csvKeywords[0] : "", 4U << 8U' in generated
    assert 'SCE_CSV_COLUMN_0, 0xE24000' in generated
    assert 'SCE_CSV_COLUMN_9, 0x408080' in generated
    assert 'SCE_CSV_DELIMITER, 0x608060' in generated
    assert '"csv", "tsv"' in generated
    assert generated == (root / 'src/syntax/generated/csv_scheme_data.h').read_text(encoding='utf-8')
print('PASS: CSV imports via the shared no-keyword path with original identity and all eleven styles')

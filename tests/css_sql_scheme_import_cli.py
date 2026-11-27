"""Exercise the shared importer against independent pinned CSS/SQL source facts."""

import argparse
from pathlib import Path
import subprocess
import sys
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--source", required=True)
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory() as directory:
    subprocess.run(
        [
            sys.executable,
            str(root / "tools/update_schemes.py"),
            "--source",
            args.source,
            "--output-dir",
            directory,
            "--schemes",
            "CSS",
            "SQL",
        ],
        check=True,
    )
    css = (Path(directory) / "css_scheme_data.h").read_text(encoding="utf-8")
    sql = (Path(directory) / "sql_scheme_data.h").read_text(encoding="utf-8")
    assert '"wxss"' in css and '"hss"' in css
    assert 'cssKeywords[5] ? cssKeywords[5] : "", 4U << 8U' in css
    assert "SCE_CSS_OPERATOR2" in css and "SCE_CSS_VARIABLE" in css
    assert "SCE_CSS_URL, 27};" in css
    assert '"mysql"' in sql and '"hsql"' in sql
    assert 'sqlKeywords[3] ? sqlKeywords[3] : "", 4U << 8U' in sql
    assert "SCE_SQL_QOPERATOR" in sql and "SCE_SQL_USER1" in sql
    assert "SCE_SQL_BIT, 28};" in sql
    for name, content in [("css", css), ("sql", sql)]:
        assert content == (root / f"src/syntax/generated/{name}_scheme_data.h").read_text(encoding="utf-8")
print("PASS: pinned CSS/SQL full groups, attributes, styles and reproducible shared imports")

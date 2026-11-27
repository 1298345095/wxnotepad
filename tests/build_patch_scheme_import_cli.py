"""Check the public importer against independent pinned INI/Make/Diff literals."""

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
            "INI",
            "Make",
            "Diff",
        ],
        check=True,
    )
    headers = {
        name: (Path(directory) / f"{name}_scheme_data.h").read_text(encoding="utf-8")
        for name in ("ini", "make", "diff")
    }
    assert 'iniSettingsSection = "Ini Config File"' in headers["ini"]
    assert "iniOriginal = {8, 4, 4," in headers["ini"] and ", 48};" in headers["ini"]
    assert "SCE_PROPS_SECTION, 0x000000, 0xFFC040, true, false, true" in headers["ini"]
    assert 'iniKeywords[0] ? iniKeywords[0] : "", 4U << 8U' in headers["ini"]
    assert '"abnf"' in headers["ini"] and '"cnf"' not in headers["ini"]
    assert 'makeSettingsSection = "Makefile"' in headers["make"]
    assert "makeOriginal = {10, 4, 4," in headers["make"] and ", 45};" in headers["make"]
    assert "SCE_MAKE_VARIABLE3, 0x003CE6" in headers["make"]
    assert '"ninja"' in headers["make"] and "!ELSEIFNDEF" in headers["make"]
    assert 'diffSettingsSection = "Diff File"' in headers["diff"]
    assert "diffOriginal = {2060, 4, 4," in headers["diff"] and ", 50};" in headers["diff"]
    assert "SCE_DIFF_REMOVED_PATCH_ADD, 0x002000, 0x80FF80, false, false, true" in headers["diff"]
    assert 'diffKeywords[0] ? diffKeywords[0] : "", 4U << 8U' in headers["diff"]
    for name, content in headers.items():
        assert content == (root / f"src/syntax/generated/{name}_scheme_data.h").read_text(encoding="utf-8")
print("PASS: shared importer preserves INI/Make/Diff identities, shared-source tables and original attributes")

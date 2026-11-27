"""Check the shared importer against pinned Batch/PowerShell/AutoHotkey facts."""

import argparse
import subprocess
import sys
import tempfile
from pathlib import Path

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
            "Batch",
            "PowerShell",
            "AutoHotkey",
        ],
        check=True,
    )
    for name, title, extensions, identity, flags, styles in [
        (
            "batch",
            "Batch File",
            ["bat", "cmd"],
            36,
            {0: 2, 1: 2, 2: 4, 3: 4, 4: 4, 5: 4},
            ["SCE_BAT_LINE_CONTINUATION", "SCE_BAT_LABEL_LINE", "SCE_BAT_VARIABLE"],
        ),
        (
            "powershell",
            "PowerShell Script",
            ["ps1", "psm1", "psd1"],
            38,
            {},
            ["SCE_POWERSHELL_HERE_STRING_SQ", "SCE_POWERSHELL_HERE_STRING_DQ"],
        ),
        (
            "autohotkey",
            "AutoHotkey Script",
            ["ahk", "ia", "scriptlet"],
            95,
            {0: 3, 1: 12, 2: 12, 3: 3, 4: 3, 5: 3, 6: 3, 7: 4},
            ["SCE_AHK_SECTION_NQ", "SCE_AHK_HOTSTRING_VALUE", "SCE_AHK_FORMAT_SPECIFIER"],
        ),
    ]:
        content = (Path(directory) / f"{name}_scheme_data.h").read_text(encoding="utf-8")
        assert f'{name}SettingsSection = "{title}"' in content
        for extension in extensions:
            assert f'"{extension}"' in content
        for index, attributes in flags.items():
            assert f'{name}Keywords[{index}] ? {name}Keywords[{index}] : "", {attributes}U << 8U' in content
        for style in styles:
            assert style in content
        metadata = next(
            line for line in content.splitlines() if line.startswith(f"constexpr SyntaxOriginalMetadata {name}Original")
        )
        assert metadata.endswith(f", {identity}}};")
        assert content == (root / f"src/syntax/generated/{name}_scheme_data.h").read_text(encoding="utf-8")
print("PASS: reproducible original Batch/PowerShell/AutoHotkey tables, metadata and identities")

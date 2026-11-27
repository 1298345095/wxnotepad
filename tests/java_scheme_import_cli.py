"""Check the shared import CLI against pinned Java/C#/Kotlin source facts."""

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
            "Java",
            "CSharp",
            "Kotlin",
        ],
        check=True,
    )
    for name, title, extensions, flags, styles in [
        (
            "java",
            "Java Source",
            ["java", "jad", "aidl", "bsh"],
            {7: 12, 9: 12, 10: 4},
            ["SCE_JAVA_ANNOTATION", "SCE_JAVA_TRIPLE_STRING", "SCE_JAVA_TRIPLE_TEMPLATE"],
        ),
        (
            "csharp",
            "C# Source",
            ["cs", "csx", "vala", "vapi", "cake"],
            {2: 10, 3: 10, 10: 12, 11: 4},
            ["SCE_CSHARP_COMMENTTAG_XML", "SCE_CSHARP_INTERPOLATED_RAWSTRING_ML"],
        ),
        (
            "kotlin",
            "Kotlin Source",
            ["kt", "kts"],
            {6: 4, 7: 4, 8: 12},
            ["SCE_KOTLIN_ANNOTATION", "SCE_KOTLIN_RAWSTRING", "SCE_KOTLIN_BACKTICKS"],
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
        # Literal NP2LEX offsets from pinned EditLexer.h; independent of catalog order.
        identity = {"java": 2, "csharp": 3, "kotlin": 68}[name]
        metadata = next(
            line for line in content.splitlines() if line.startswith(f"constexpr SyntaxOriginalMetadata {name}Original")
        )
        assert metadata.endswith(f", {identity}}};")
        assert content == (root / f"src/syntax/generated/{name}_scheme_data.h").read_text(encoding="utf-8")
print("PASS: reproducible complete original Java/C#/Kotlin groups, attributes, styles and identities")

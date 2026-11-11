"""Reproduce HTML/XML and their embedded JS/CSS style data from pinned Notepad4."""
import argparse
from pathlib import Path
import re

from scheme_data import generate_scheme, provenance, read_pinned, style_initializers

ROOT = Path(__file__).resolve().parents[1]


def styles(text, group, offset=""):
    table = text.split(f"static const EDITSTYLE Styles_{group}[] = {{", 1)[1].split("};", 1)[0]
    return style_initializers(table, "SCE_", eol_filled=True, offset=offset)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    args = parser.parse_args()
    output = ["// clang-format off"]
    for filename in ('HTML', 'XML'):
        data = read_pinned(args.source, f'src/EditLexers/stl{filename}.cpp')
        name = filename.lower()
        header = generate_scheme(data, filename, filename, name)
        (ROOT / f'src/syntax/generated/{name}_scheme_data.h').write_text(header, encoding='utf-8', newline='\n')
    output += ["constexpr SyntaxStyle markupEmbeddedStyles[] = {"]
    for filename, group, offset in [("stlJavaScript.cpp", "JS", "js_style"), ("stlCSS.cpp", "CSS", "css_style")]:
        path = "src/EditLexers/" + filename
        data = read_pinned(args.source, path)
        output.extend(provenance("update_markup_schemes.py", path, data))
        output.extend(styles(data.decode("utf-8"), group, offset))
    output += ["};", "// clang-format on", ""]
    (ROOT / "src/syntax/generated/markup_scheme_data.h").write_text("\n".join(output), encoding="utf-8", newline="\n")


if __name__ == "__main__":
    main()

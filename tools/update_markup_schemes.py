"""Reproduce HTML/XML and their embedded JS/CSS style data from pinned Notepad4."""
import argparse
from pathlib import Path
import re

from scheme_data import provenance, read_pinned

ROOT = Path(__file__).resolve().parents[1]


def styles(text, group, offset=""):
    table = text.split(f"static const EDITSTYLE Styles_{group}[] = {{", 1)[1].split("};", 1)[0]
    result = []
    for line in table.splitlines():
        value = re.search(r'L"([^"]*)"', line)
        if not value:
            continue
        value = value[1]
        fore = re.search(r"fore:#([0-9A-Fa-f]{6})", value)
        back = re.search(r"back:#([0-9A-Fa-f]{6})", value)
        for style in re.findall(r"SCE_\w+", line):
            if offset:
                style = f"{offset}({style})"
            result.append(f"    {{{style}, 0x{fore[1] if fore else '000000'}, "
                          f"0x{back[1] if back else 'FFFFFF'}, "
                          f"{str('bold' in value).lower()}, {str('italic' in value).lower()}, "
                          f"{str('eolfilled' in value).lower()}}},")
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    args = parser.parse_args()
    output = ["// clang-format off"]
    for filename, group, name, consumed, attrs in [
            ("stlHTML.cpp", "HTML", "html", 8, [2, 2, 2, 2, 2, 3, 2, 2]),
            ("stlXML.cpp", "XML", "xml", 7, [0, 0, 0, 0, 0, 0, 2])]:
        path = "src/EditLexers/" + filename
        data = read_pinned(args.source, path)
        text = data.decode("utf-8").replace("\r\n", "\n")
        output.extend(provenance("update_markup_schemes.py", path, data))
        keywords = text.split(f"static const char * const Keywords_{group}[] = {{", 1)[1].split("};", 1)[0]
        # The final attribute/value lists marked NoLexer are intentionally excluded.
        keywords = keywords.split(f", // {consumed} ", 1)[0]
        output += [f"constexpr const char* {name}KeywordText[] = {{" + keywords + "};",
                   f"constexpr SyntaxKeywords {name}Lists[] = {{"]
        output += [f'    {{{name}KeywordText[{index}] ? {name}KeywordText[{index}] : "", {attr}U << 8U}},'
                   for index, attr in enumerate(attrs)]
        output += ["};", f"constexpr SyntaxStyle {name}Styles[] = {{", *styles(text, group), "};"]
        extensions = re.search(r'\n\s*L"([^"\n]+;[^"\n]+)"', text)[1].split("; ")
        output += [f"constexpr const char* {name}Extensions[] = {{" +
                   ", ".join(f'"{extension}"' for extension in extensions) + "};"]
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

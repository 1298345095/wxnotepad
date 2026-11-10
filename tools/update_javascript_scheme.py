"""Reproduce shared JavaScript/TypeScript lexer data from the pinned checkout."""
import argparse
import re
from pathlib import Path

from scheme_data import provenance, read_pinned, style_initializers

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, default=ROOT / "src/syntax/generated")
    args = parser.parse_args()
    for language, symbol, consumed in (("JavaScript", "JS", {0, 1, 2, 3, 6}),
                                        ("TypeScript", "TS", {0, 1, 2, 4, 6})):
        source_path = f"src/EditLexers/stl{language}.cpp"
        data = read_pinned(args.source, source_path)
        text = data.decode("utf-8").replace("\r\n", "\n")
        keywords = text.split(f"static const char * const Keywords_{symbol}[] = {{", 1)[1].split(", // 8 function", 1)[0]
        styles = text.split(f"static const EDITSTYLE Styles_{symbol}[] = {{", 1)[1].split("};", 1)[0]
        name = language.lower()
        output = provenance("update_javascript_scheme.py", source_path, data) + [
            "// Lists 8-11 are completion-only (KeywordAttr_NoLexer), so omitted.",
            "// Empty lexer lists retain their original indices and attributes.",
            "// clang-format off",
            f"constexpr const char* {name}Keywords[] = {{" + keywords + "};", "",
            f"constexpr SyntaxKeywords {name}Lists[] = {{"]
        for index in range(8):
            output.append(f'    {{{name}Keywords[{index}] ? {name}Keywords[{index}] : "", '
                          f'{"2U << 8U" if index in consumed else "0"}}},')
        output.extend(["};", "", f"constexpr SyntaxStyle {name}Styles[] = {{"])
        output.extend(style_initializers(styles, "SCE_JS"))
        output.extend(["};", ""])
        extensions = re.search(r'L"([^"\n]+)"\s*,\n\s*Keywords_' + symbol, text)[1]
        output.append(f"constexpr const char* {name}Extensions[] = {{" +
                      ", ".join(f'"{ext.strip()}"' for ext in extensions.split(';') if ext.strip() not in {'jsx', 'tsx'}) + "};")
        output.append("// clang-format on\n")
        (args.output_dir / f"{name}_scheme_data.h").write_text("\n".join(output), encoding="utf-8", newline="\n")


if __name__ == "__main__":
    main()

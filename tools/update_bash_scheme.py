"""Reproduce Shell Script lexer-consumed data from the pinned checkout (offline)."""
import argparse
from pathlib import Path

from scheme_data import provenance, read_pinned, style_initializers

ROOT = Path(__file__).resolve().parents[1]


def generate(data):
    source_path = "src/EditLexers/stlBash.cpp"
    text = data.decode("utf-8").replace("\r\n", "\n")
    keywords = text.split("static const char * const Keywords_Bash[] = {", 1)[1].split(", // 2 variables", 1)[0]
    styles = text.split("static const EDITSTYLE Styles_Bash[] = {", 1)[1].split("};", 1)[0]
    output = provenance("update_bash_scheme.py", source_path, data) + [
        "// Lists 0-1 retain KeywordAttr_PreSorted (2). List 1 is NoAutoComp.",
        "// List 2 (variables) is completion-only (KeywordAttr_NoLexer), so omitted.",
        "// clang-format off",
        "constexpr const char* bashKeywords[] = {" + keywords + "};", "",
        "constexpr SyntaxStyle bashStyles[] = {"]
    output.extend(style_initializers(styles, "SCE_SH_", eol_filled=True))
    output.append("};\n// clang-format on\n")
    return "\n".join(output)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    args = parser.parse_args()
    data = read_pinned(args.source, "src/EditLexers/stlBash.cpp")
    (ROOT / "src/syntax/generated/bash_scheme_data.h").write_text(generate(data), encoding="utf-8", newline="\n")


if __name__ == "__main__":
    main()

"""Reproduce Python lexer-consumed syntax data from the pinned checkout (offline)."""
import argparse
from pathlib import Path

from scheme_data import provenance, read_pinned, style_initializers

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    args = parser.parse_args()
    source_path = "src/EditLexers/stlPython.cpp"
    data = read_pinned(args.source, source_path)
    text = data.decode("utf-8").replace("\r\n", "\n")
    keywords = text.split("static const char * const Keywords_Python[] = {", 1)[1].split(", // 7 decorator", 1)[0]
    styles = text.split("static const EDITSTYLE Styles_Python[] = {", 1)[1].split("};", 1)[0]
    output = provenance("update_python_scheme.py", source_path, data) + [
        "// Lists 7-12 are completion-only (KeywordAttr_NoLexer), so omitted.",
        "// Lists 0-6 retain KeywordAttr_PreSorted (2).",
        "// clang-format off",
        "constexpr const char* pythonKeywords[] = {" + keywords + "};", "",
        "constexpr SyntaxStyle pythonStyles[] = {"]
    output.extend(style_initializers(styles, "SCE_PY_"))
    output.append("};\n// clang-format on\n")
    (ROOT / "src/python_scheme_data.h").write_text("\n".join(output), encoding="utf-8", newline="\n")


if __name__ == "__main__":
    main()

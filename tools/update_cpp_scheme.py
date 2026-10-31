"""Reproduce selected C++ syntax data from the pinned Notepad4 checkout (offline)."""
import argparse
from pathlib import Path

from scheme_data import provenance, read_pinned, style_initializers

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    args = parser.parse_args()
    source_path = "src/EditLexers/stlCPP.cpp"
    data = read_pinned(args.source, source_path)
    text = data.decode("utf-8").replace("\r\n", "\n")
    keywords = text.split("static const char * const Keywords_CPP[] = {", 1)[1].split(", // 13 C Function", 1)[0]
    styles = text.split("static const EDITSTYLE Styles_CPP[] = {", 1)[1].split("};", 1)[0]
    output = provenance("update_cpp_scheme.py", source_path, data) + [
        "// Lists 13-15 are completion-only (KeywordAttr_NoLexer), so omitted.",
        "// No lexer-consumed attributes: NoAutoComp (2,3,11,12) is app-only.",
        "// clang-format off",
        "constexpr const char* cppKeywords[] = {" + keywords + "};", "",
        "constexpr SyntaxStyle cppStyles[] = {"]
    output.extend(style_initializers(styles, "SCE_C_"))
    output.append("};\n// clang-format on\n")
    (ROOT / "src/syntax/generated/cpp_scheme_data.h").write_text("\n".join(output), encoding="utf-8", newline="\n")


if __name__ == "__main__":
    main()

"""Reproduce selected JSON syntax data from the pinned Notepad4 checkout (offline)."""
import argparse
from pathlib import Path

from scheme_data import provenance, read_pinned, style_initializers

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    args = parser.parse_args()
    source_path = "src/EditLexers/stlJSON.cpp"
    data = read_pinned(args.source, source_path)
    text = data.decode("utf-8").replace("\r\n", "\n")
    keywords = text.split("static const char * const Keywords_JSON[] = {", 1)[1].split("};", 1)[0]
    styles = text.split("static const EDITSTYLE Styles_JSON[] = {", 1)[1].split("};", 1)[0]
    # The pinned table marks its sole lexer-consumed list as PreSorted (2).
    assert "KeywordAttr32(0, KeywordAttr_PreSorted)" in text
    output = provenance("update_json_scheme.py", source_path, data) + [
        "// The sole keyword group uses KeywordAttr_PreSorted (2).",
        "// clang-format off",
        "constexpr uintptr_t jsonKeywordAttributes = 2U << 8U;",
        "constexpr const char* jsonKeywords[] = {" + keywords + "};", "",
        "constexpr SyntaxStyle jsonStyles[] = {"]
    output.extend(style_initializers(styles, "SCE_JSON_"))
    output.append("};\n// clang-format on\n")
    (ROOT / "src/json_scheme_data.h").write_text("\n".join(output), encoding="utf-8", newline="\n")


if __name__ == "__main__":
    main()

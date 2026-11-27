"""Import original runtime style keys/defaults and the distributed dark profile."""

import argparse
import codecs
import hashlib
import json
from pathlib import Path
import re

from scheme_data import read_pinned, provenance, original_scheme
import update_schemes

ROOT = Path(__file__).resolve().parents[1]


def style_records(text, symbol, names):
    table = text.split(f"static const EDITSTYLE Styles_{symbol}[] = {{", 1)[1].split("};", 1)[0]
    table = re.sub(r"//[^\n]*", "", table)
    records = []
    for entry in re.finditer(r"EDITSTYLE_DEFAULT|\{([^{}]+)\}", table, re.DOTALL):
        if entry[0] == "EDITSTYLE_DEFAULT":
            records.append(("Default", "", ["STYLE_DEFAULT"]))
            continue
        line = entry[1]
        identity = re.search(r"NP2(?:StyleX|STYLEX)_(\w+)", line)
        if not identity:
            continue
        key = names[identity[1]]
        value = re.search(r'L"([^"\n]*)"', line)[1]
        ids = re.findall(r"\b(?:SCE_\w+|STYLE_\w+)\b", re.split(r"NP2(?:StyleX|STYLEX)_", line, maxsplit=1)[0])
        records.append((key, value, ids))
    return records


def generate(source):
    path = "src/EditLexers/EditStyleX.h"
    resources = read_pinned(source, path)
    names = dict(
        re.findall(r'#define NP2(?:StyleX|STYLEX)_(\w+)\s+EDITSTYLE_\w+\(\w+, L"([^"]+)"', resources.decode("utf-8"))
    )
    output = provenance("style_profiles.py", path, resources) + ["// clang-format off"]
    schemes = []
    for filename, symbol in [("Default", "Global"), *update_schemes.SCHEMES.items()]:
        source_file = getattr(update_schemes, "SOURCE_FILES", {}).get(filename, filename)
        path = f"src/EditLexers/stl{source_file}.cpp"
        data = read_pinned(source, path)
        records = style_records(data.decode("utf-8"), symbol, names)
        if symbol != "Global":
            schemes.append((original_scheme(data, symbol)[-1], filename.lower()))
        output += provenance("style_profiles.py", path, data)
        output += [f"constexpr StyleDefinition {filename.lower()}ProfileStyles[] = {{"]
        for key, value, ids in records:
            output += [
                "    {wxTRANSLATE("
                + json.dumps(key)
                + "), "
                + json.dumps(value)
                + ", {"
                + ", ".join(ids)
                + "}, "
                + str(len(ids))
                + "},"
            ]
        output += ["};"]
    output += ["constexpr StyleSchemeDefinition profileSchemes[] = {"]
    output += ["    {" + json.dumps(section) + ", " + name + "ProfileStyles}," for section, name in schemes]
    output += ["};"]
    data = read_pinned(source, "doc/Notepad4 DarkTheme.ini")
    output += provenance("style_profiles.py", "doc/Notepad4 DarkTheme.ini", data)
    # Git stores UTF-8/LF; .gitattributes materializes UTF-16LE-BOM/CRLF.
    text = data.decode("utf-8").replace("\r\n", "\n")
    distributed = codecs.BOM_UTF16_LE + text.replace("\n", "\r\n").encode("utf-16le")
    output += ['constexpr const char DarkProfile[] = R"NP4(' + text + ')NP4";', "// clang-format on", ""]
    return "\n".join(output), distributed


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    args = parser.parse_args()
    header, dark = generate(args.source)
    (ROOT / "src/syntax/generated/style_profiles.h").write_text(header, encoding="utf-8", newline="\n")
    (ROOT / "resources").mkdir(exist_ok=True)
    (ROOT / "resources/Notepad4 DarkTheme.ini-default").write_bytes(dark)
    (ROOT / "resources/Notepad4 DarkTheme.NOTICE.txt").write_text(
        "Notepad4 dark style profile\n"
        "Source: https://github.com/zufuliu/notepad4\n"
        "Revision: 89b8b36e0ee2c23f73be922315f17261ccfbbe1a\n"
        "Original path: doc/Notepad4 DarkTheme.ini\n"
        f"Original SHA-256: {hashlib.sha256(dark).hexdigest()}\n"
        "The original UTF-16LE file is distributed without modification.\n"
        "Copyright and BSD 3-Clause license: licenses/Notepad4.txt.\n",
        encoding="utf-8",
        newline="\n",
    )


if __name__ == "__main__":
    main()

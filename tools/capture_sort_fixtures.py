"""Capture literal Sort Lines fixtures through the original Windows APIs."""
import ctypes
from functools import cmp_to_key
import json
from pathlib import Path


def capture() -> None:
    shell = ctypes.WinDLL("shlwapi")
    compare = shell.StrCmpLogicalW
    compare.argtypes = [ctypes.c_wchar_p, ctypes.c_wchar_p]
    compare.restype = ctypes.c_int
    extension = shell.PathFindExtensionW
    extension.argtypes = [ctypes.c_wchar_p]
    extension.restype = ctypes.c_wchar_p

    def ordinal(left: str, right: str) -> int:
        left_bytes = left.encode("utf-16-be")
        right_bytes = right.encode("utf-16-be")
        return (left_bytes > right_bytes) - (left_bytes < right_bytes)

    def cmp(left: str, right: str, file_type: bool = False) -> int:
        a, b = (extension(left), extension(right)) if file_type else (left, right)
        return (compare(a, b) or (compare(left, right) if file_type else 0)
                or ordinal(a, b) or (ordinal(left, right) if file_type else 0))

    logical = ["a2", "a02", "a002", "a10", "a1", "A1", "a01"]
    logical += [f"a{char}1" for char in " -.'!#$%&(),/:;@[\\^_`{|~+="]
    logical += ["é2", "e2", "É2", "é2", "中2", "中10", "😀2", "😀10", "a١", "a２", "", "2", "10", "-2", "-10", ".2", ".10", "1.2", "1.10", "1.02"]
    logical += ["a" + "0" * 36 + "1", "a" + "9" * 37, "00", "0", "000", "阿", "波", "一", "丁"]
    logical += ["ß2", "ss2", "ﬃ2", "ffi2", "æ2", "ae2", "œ2", "oe2", "Å2", "Å2", "ä2", "ä2"]
    logical += ["한10", "한2", "한2", "한10", "กข2", "เก2", "แก2", "กเ2", "กแ2", "か2", "が2", "カ2", "ｶ2", "ب2", "بِ2", "بَ2"]
    logical += ["é10", "é10", "ạ́2", "ạ́10", "ạ́10", "ạ́2", "👩‍💻2", "👩‍💻10", "😀١", "😀２", "a1٢3", "a١2３", "a123"]
    logical += ["ぁ2", "あ2", "ァ2", "ア2", "ｧ2", "ｱ2", "ぁ10", "あ10", "あ-2", "あ'2"]
    paths = ["b2.txt", "b10.txt", "a.cpp", "z.TXT", "file", "a.tar.gz", ".gitignore", "name.", "dir.ext/file", "dir.ext\\file", "a b.ext", "a.txt:stream", "file..ext", "x2.a02", "x1.a2", "x10.a2"]
    result = {
        "logicalInput": "\n".join(logical),
        "logicalExpected": "\n".join(sorted(logical, key=cmp_to_key(cmp))),
        "fileTypeInput": "\n".join(paths),
        "fileTypeExpected": "\n".join(sorted(paths, key=cmp_to_key(lambda a, b: cmp(a, b, True)))),
    }
    root = Path(__file__).resolve().parents[1]
    header = root / "tests/sort_reference_fixtures.h"
    with header.open("w", encoding="utf-8", newline="\n") as file:
        file.write("// Captured by tools/capture_sort_fixtures.py on Windows 11 build 26200, zh-SG.\n#pragma once\nnamespace SortReference {\n")
        for name, value in result.items():
            file.write(f'inline constexpr const char* {name} = R"fixture({value})fixture";\n')
        file.write("}\n")
    print(json.dumps(result, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    capture()

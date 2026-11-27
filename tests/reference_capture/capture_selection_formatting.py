"""Capture the native wsprintfW boundary used verbatim by pinned EditSelectionAction."""

import argparse
import ctypes
import hashlib
import json
import os
import re
import subprocess
from pathlib import Path

REVISION = "89b8b36e0ee2c23f73be922315f17261ccfbbe1a"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--upstream", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()
    args.output_dir.mkdir(parents=True, exist_ok=False)
    resources = subprocess.check_output(["git", "-C", str(args.upstream), "show", f"{REVISION}:src/Notepad4.rc"])
    template = re.search(r'IDS_GOOGLE_SEARCH_URL\s+"([^"]+)"', resources.decode("utf-8")).group(1)
    original = subprocess.check_output(["git", "-C", str(args.upstream), "show", f"{REVISION}:src/Edit.cpp"])
    assert b"wsprintf(lpszCommand, szCmdTemplate, pszEscapedW);" in original
    formatter = ctypes.WinDLL("user32").wsprintfW
    formatter.restype = ctypes.c_int
    records = []
    for length in (5, 1024, 2000):
        # As in the original NP2HeapAlloc, the output buffer starts zero-filled.
        output = ctypes.create_unicode_buffer(16000)
        returned = formatter(output, ctypes.c_wchar_p(template), ctypes.c_wchar_p("a" * length))
        records.append(
            {
                "selected_ascii_count": length,
                "return": returned,
                "output_utf16_units": len(output.value.encode("utf-16-le")) // 2,
                "output": output.value,
            }
        )
    user32 = Path(os.environ["SystemRoot"]) / "System32/user32.dll"
    result = {
        "revision": REVISION,
        "original_edit_sha256": hashlib.sha256(original).hexdigest(),
        "resources_sha256": hashlib.sha256(resources).hexdigest(),
        "native_user32_sha256": hashlib.sha256(user32.read_bytes()).hexdigest(),
        "template": template,
        "records": records,
    }
    (args.output_dir / "original-formatting.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print([(row["selected_ascii_count"], row["return"], row["output_utf16_units"]) for row in records])


if __name__ == "__main__":
    main()

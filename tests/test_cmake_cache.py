"""Keep configured paths and scalar values identical across build consumers."""

import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from cmake_cache import read_cmake_cache


class CMakeCache(unittest.TestCase):
    def test_typed_entries_preserve_literal_values_and_ignore_comments(self):
        with tempfile.TemporaryDirectory(prefix="wxnotepad cache ") as folder:
            directory = Path(folder)
            (directory / "CMakeCache.txt").write_text(
                "# Ignored:STRING=comment\n// Another:PATH=comment\n\n"
                "VCPKG_INSTALLED_DIR:PATH=C:/SDK 测试/with spaces=retained\n"
                "VCPKG_TARGET_TRIPLET:STRING=x64-windows-ci\n"
                "EMPTY:STRING=\nENABLED:BOOL=OFF\nLIST:STRING=one;two\n"
                "DUPLICATE:STRING=old\nDUPLICATE:STRING=new\n"
                "NOT_AN_ENTRY=value\n",
                encoding="utf-8",
            )
            self.assertEqual(
                read_cmake_cache(directory),
                {
                    "VCPKG_INSTALLED_DIR": "C:/SDK 测试/with spaces=retained",
                    "VCPKG_TARGET_TRIPLET": "x64-windows-ci",
                    "EMPTY": "",
                    "ENABLED": "OFF",
                    "LIST": "one;two",
                    "DUPLICATE": "new",
                },
            )

    def test_missing_cache_retains_the_read_failure(self):
        with tempfile.TemporaryDirectory(prefix="wxnotepad cache ") as folder:
            with self.assertRaises(FileNotFoundError):
                read_cmake_cache(Path(folder))


if __name__ == "__main__":
    unittest.main()

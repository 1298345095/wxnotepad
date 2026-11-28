"""Exercise the real Windows shortcut fixture across the active ANSI code page."""

import ctypes
import json
import sys
import tempfile
import unittest
from pathlib import Path

from file_favorites_packaged import favorite_workspace, shortcut


@unittest.skipUnless(sys.platform == "win32", "Windows Unicode ShellLink API")
class FavoriteShortcutTests(unittest.TestCase):
    def test_unicode_link_filename_and_target_roundtrip(self):
        acp = ctypes.windll.kernel32.GetACP()
        unicode_name = "😀"
        for candidate in ("😀", "文档", "Ж", "𐐀"):
            try:
                candidate.encode(f"cp{acp}")
            except UnicodeEncodeError:
                unicode_name = candidate
                break
        # UTF-8 and GB18030 can represent every candidate; still test wide paths.
        root = Path(self.enterContext(tempfile.TemporaryDirectory())).resolve()
        for index, (link_name, target_name) in enumerate(
            (("ascii", "ascii"), (unicode_name, "ascii"), ("ascii", unicode_name), (unicode_name, unicode_name))
        ):
            with self.subTest(acp=acp, link=link_name, target=target_name):
                folder = root / str(index)
                folder.mkdir()
                target = folder / f"{target_name}.txt"
                target.write_bytes(b"Unicode shortcut fixture\n")
                link = folder / f"{link_name}.lnk"
                self.assertEqual(Path(shortcut(link, target)), target)
                self.assertTrue(link.is_file())
                self.assertEqual(Path(shortcut(link)), target)

    def test_failure_retains_original_link_bytes_and_native_results(self):
        evidence = Path(self.enterContext(tempfile.TemporaryDirectory()))
        original = b"L\0\0\0unsupported Windows shell link"
        with self.assertRaisesRegex(AssertionError, "fixture failure"):
            with favorite_workspace(evidence) as root:
                target = root / "target.txt"
                target.write_bytes(b"target\n")
                shortcut(root / "valid.lnk", target)
                (root / "damaged.lnk").write_bytes(original)
                raise AssertionError("fixture failure")
        self.assertFalse(root.exists())
        output = evidence / "shortcut-failure"
        report = json.loads((output / "results.json").read_text(encoding="utf-8"))
        self.assertEqual(report["system_acp"], ctypes.windll.kernel32.GetACP())
        links = {Path(record["path"]).name: record for record in report["links"]}
        valid = links["valid.lnk"]
        self.assertEqual(Path(valid["reader_result"]), target)
        self.assertEqual(valid["native_reader"]["Load"], 0)
        self.assertEqual(valid["native_reader"]["GetPath"], 0)
        damaged = links["damaged.lnk"]
        self.assertEqual((output / damaged["original_bytes"]).read_bytes(), original)
        self.assertEqual(damaged["length"], len(original))
        self.assertLess(damaged["native_reader"]["Load"], 0)
        self.assertIn("Load failed: HRESULT", damaged["reader_error"])

    def test_capture_failure_keeps_original_fixture_error(self):
        evidence = Path(self.enterContext(tempfile.TemporaryDirectory())) / "blocked"
        evidence.write_bytes(b"not a directory")
        with self.assertRaisesRegex(AssertionError, "original fixture failure") as caught:
            with favorite_workspace(evidence):
                raise AssertionError("original fixture failure")
        self.assertTrue(any("Shortcut evidence capture failed:" in note for note in caught.exception.__notes__))


if __name__ == "__main__":
    unittest.main()

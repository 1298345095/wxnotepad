"""Check the measurement tool's independent complete-byte comparison."""
import importlib.util
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('resources', ROOT / 'tools/resources.py')
RESOURCES = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(RESOURCES)


class SavedDocumentVerification(unittest.TestCase):
    def test_complete_saved_bytes_and_failures(self):
        # Cross the comparison's read-block boundary and retain embedded NULs.
        content = b'\0line\r\n' * 150000
        with tempfile.TemporaryDirectory(prefix='wxnotepad resource check ') as folder:
            source = Path(folder) / 'source.txt'
            saved = Path(folder) / 'saved.txt'
            source.write_bytes(content)
            saved.write_bytes(b'X' + content)
            RESOURCES.verify_saved_bytes(saved, source)
            for invalid in (content, b'Y' + content, b'X' + content[:-1], b'X' + content + b'extra'):
                with self.subTest(length=len(invalid), prefix=invalid[:1]):
                    saved.write_bytes(invalid)
                    with self.assertRaises(ValueError):
                        RESOURCES.verify_saved_bytes(saved, source)

    def test_invalid_observation_is_rejected(self):
        with self.assertRaises(ValueError):
            RESOURCES.require(False, {'dirty': 'unexpected'})


if __name__ == '__main__':
    unittest.main()

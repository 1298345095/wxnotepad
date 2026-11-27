"""Check static size reports, malformed binary bounds and complete artwork retention."""

from contextlib import redirect_stderr
import importlib.util
import io
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("binary_size", ROOT / ".github/scripts/binary_size.py")
SIZE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(SIZE)
PATH_ERROR = "Report path must stay within the working directory"


def run_cli(work, *arguments):
    return subprocess.run(
        [sys.executable, ROOT / ".github/scripts/binary_size.py", *arguments], cwd=work, capture_output=True, text=True
    )


def elf_fixture():
    names = b"\0.shstrtab\0.dynsym\0.relr.dyn\0.dynamic\0.bss\0.debug_info\0"
    symbols = bytes(24) + struct.pack("<IBBHQQ", 0, 0x12, 0, 1, 0x4000, 32)
    # The address word relocates 0x4000; the bitmap relocates 0x4008 and 0x4018.
    relr = struct.pack("<QQ", 0x4000, 0b1011)
    dynamic = struct.pack("<qQqQqQ", 35, 8, 35, 16, 0, 0)
    payload = bytearray(64)
    sections = [bytes(64)]
    for name, kind, content, entry_size in (
        (".shstrtab", 3, names, 0),
        (".dynsym", 11, symbols, 24),
        (".relr.dyn", 19, relr, 8),
        (".dynamic", 6, dynamic, 16),
        (".bss", 8, b"", 0),
        (".debug_info", 1, b"dwarf", 0),
    ):
        offset, size = (0xFFFFFFFF, 4096) if kind == 8 else (len(payload), len(content))
        flags = 0 if name in (".shstrtab", ".debug_info") else 2
        sections.append(
            struct.pack("<IIQQQQIIQQ", names.index(name.encode()), kind, flags, 0, offset, size, 0, 0, 1, entry_size)
        )
        payload.extend(content)
    header = struct.pack(
        "<16sHHIQQQIHHHHHH",
        b"\x7fELF\x02\x01\x01" + bytes(9),
        3,
        62,
        1,
        0,
        0,
        len(payload),
        0,
        64,
        0,
        0,
        64,
        len(sections),
        1,
    )
    payload[:64] = header
    return bytes(payload) + b"".join(sections)


def pe_fixture():
    data = bytearray(1024)
    data[:2] = b"MZ"
    struct.pack_into("<I", data, 0x3C, 0x80)
    data[0x80:0x84] = b"PE\0\0"
    struct.pack_into("<HHIIIHH", data, 0x84, 0x8664, 1, 0, 0, 0, 240, 0x22)
    struct.pack_into("<H", data, 0x98, 0x20B)
    struct.pack_into("<I", data, 0x98 + 108, 16)
    struct.pack_into("<II", data, 0x98 + 112 + 6 * 8, 0x1010, 28)
    data[0x188:0x190] = b".rdata\0\0"
    struct.pack_into("<IIII", data, 0x190, 0x200, 0x1000, 0x200, 0x200)
    struct.pack_into("<IIHHIIII", data, 0x210, 0, 0, 0, 0, 2, 4, 0x1060, 0x260)
    data[0x260:0x264] = b"RSDS"
    return bytes(data)


def artwork_fixture(source):
    directory = source / "src/app/artwork"
    directory.mkdir(parents=True)
    strips = []
    declarations = []
    for size in (16, 24, 32, 40, 48):
        content = (
            b"\x89PNG\r\n\x1a\n"
            + struct.pack(">I", 13)
            + b"IHDR"
            + struct.pack(">II", size * 27, size)
            + bytes((8, 6, 0, 0, 0))
        )
        strips.append(content)
        declarations.append(
            f"inline constexpr unsigned char Strip{size}[] = {{" + ",".join(str(value) for value in content) + ",};"
        )
    (directory.parent / "toolbar_art.h").write_text("\n".join(declarations), encoding="utf-8")
    images = (b"icon image 16", b"icon image 256")
    icon = bytearray(struct.pack("<HHH", 0, 1, len(images)))
    offset = 6 + len(images) * 16
    for size, content in zip((16, 0), images):
        icon.extend(struct.pack("<BBBBHHII", size, size, 0, 0, 1, 32, len(content), offset))
        offset += len(content)
    icon.extend(b"".join(images))
    (directory / "wxnotepad.ico").write_bytes(icon)
    return strips, images


class StaticBinarySize(unittest.TestCase):
    def test_elf_counts_dynamic_symbols_relr_and_nobits_separately(self):
        report = SIZE.describe(elf_fixture())
        self.assertEqual(report["format"], "ELF64")
        self.assertEqual(report["dynamic_symbol_entries"], 2)
        self.assertEqual(report["defined_dynamic_symbols"], 1)
        self.assertEqual(report["relr_bytes"], 16)
        self.assertEqual(report["dynamic_relr_bytes"], 16)
        self.assertEqual(report["relr_relocations"], 3)
        self.assertEqual(report["debug_bytes"], 5)
        self.assertEqual(
            next(section for section in report["sections"] if section["name"] == ".bss"),
            {"name": ".bss", "file_bytes": 0, "memory_bytes": 4096},
        )

    def test_pe_reports_small_debug_metadata_without_treating_it_as_a_pdb(self):
        report = SIZE.describe(pe_fixture())
        self.assertEqual(report["format"], "PE32+")
        self.assertEqual(report["symbol_table_entries"], 0)
        self.assertEqual(report["debug_records"], [{"type": 2, "bytes": 4}])
        self.assertEqual(report["sections"], [{"name": ".rdata", "file_bytes": 512, "memory_bytes": 512}])

    def test_truncated_tables_and_out_of_file_debug_payloads_fail(self):
        damaged = bytearray(pe_fixture())
        struct.pack_into("<I", damaged, 0x210 + 24, len(damaged) - 1)
        for data in (b"\x7fELF", elf_fixture()[:-1], pe_fixture()[:200], bytes(damaged)):
            with self.subTest(size=len(data)):
                with self.assertRaises(ValueError):
                    SIZE.describe(data)
        directories = bytearray(pe_fixture())
        struct.pack_into("<I", directories, 0x98 + 108, 17)
        with self.assertRaises(ValueError):
            SIZE.describe(directories)

    def test_report_is_deterministic_and_catches_missing_whole_strips_or_icon_images(self):
        with tempfile.TemporaryDirectory(prefix="binary size ") as folder:
            work = Path(folder)
            strips, images = artwork_fixture(work / "source")
            artifact = work / "artifact"
            artifact.mkdir()
            executable = artifact / "wxnotepad.exe"
            original = pe_fixture() + b"".join(strips) + b"".join(images)
            executable.write_bytes(original)
            (artifact / "README.txt").write_bytes(b"fixture")
            report = SIZE.inspect(artifact, work / "source")
            self.assertTrue(report["success"], report)
            self.assertEqual(report, SIZE.inspect(artifact, work / "source"))
            self.assertEqual(report["artifact_bytes"], len(original) + 7)
            self.assertEqual(len(report["artwork"]["images"]), 7)
            self.assertEqual([image["image_count"] for image in report["artwork"]["images"][:5]], [27] * 5)
            baseline = work / "baseline.json"
            baseline.write_text(json.dumps(report), encoding="utf-8")
            (artifact / "README.txt").write_bytes(b"changed fixture")
            comparison = SIZE.inspect(artifact, work / "source", baseline)["comparison"]
            self.assertEqual(comparison, {"artifact_bytes_delta": 8, "application_bytes_delta": 0})
            for missing in (strips[-1], images[-1]):
                executable.write_bytes(original.replace(missing, b""))
                failed = SIZE.inspect(artifact, work / "source")
                self.assertFalse(failed["success"])
                self.assertFalse(failed["artwork"]["all_present"])

    def test_source_icon_bounds_and_missing_source_fail_closed(self):
        with tempfile.TemporaryDirectory() as folder:
            source = Path(folder)
            strips, images = artwork_fixture(source)
            icon_path = source / "src/app/artwork/wxnotepad.ico"
            icon = bytearray(icon_path.read_bytes())
            struct.pack_into("<I", icon, 6 + 12, len(icon))
            icon_path.write_bytes(icon)
            executable = b"".join(strips) + b"".join(images)
            with self.assertRaises(ValueError):
                SIZE.artwork(source, executable)
            self.assertFalse(SIZE.inspect(source / "missing", source)["success"])

    def test_cli_saves_a_failure_report_from_an_unrelated_working_directory(self):
        with tempfile.TemporaryDirectory(prefix="binary size CLI ") as folder:
            work = Path(folder)
            output = work / "reports/result.json"
            process = run_cli(work, "--artifact-dir", work / "missing", "--output", output)
            self.assertEqual(process.returncode, 1, process.stdout + process.stderr)
            report = json.loads(output.read_text(encoding="utf-8"))
            self.assertFalse(report["success"])
            self.assertIn("exactly one", report["errors"][0])

    def test_cli_rejects_relative_and_absolute_output_outside_working_directory(self):
        with tempfile.TemporaryDirectory(prefix="binary size bounds ") as folder:
            root = Path(folder)
            work = root / "work"
            work.mkdir()
            outside = root / "sentinel.json"
            for output in (Path("../sentinel.json"), outside):
                with self.subTest(output=output):
                    outside.write_bytes(b"preserve external report")
                    process = run_cli(work, "--artifact-dir", work / "missing", "--output", output)
                    self.assertEqual(process.returncode, 2, process.stdout + process.stderr)
                    self.assertIn(PATH_ERROR, process.stderr)
                    self.assertEqual(outside.read_bytes(), b"preserve external report")

    def test_cli_rejects_external_baseline_before_reading_or_inspecting(self):
        with tempfile.TemporaryDirectory(prefix="binary size baseline ") as folder:
            root = Path(folder)
            work = root / "work"
            work.mkdir()
            baseline = root / "baseline.json"
            baseline.write_bytes(b"external baseline must not be read")
            output = work / "report.json"
            output.write_bytes(b"preserve internal report")
            arguments = [
                "binary_size.py",
                "--artifact-dir",
                str(work / "missing"),
                "--baseline-report",
                str(baseline),
                "--output",
                str(output),
            ]
            error = io.StringIO()
            with (
                mock.patch.object(sys, "argv", arguments),
                mock.patch.object(Path, "cwd", return_value=work),
                mock.patch.object(SIZE, "inspect") as inspect,
                mock.patch.object(Path, "read_text") as read,
            ):
                with redirect_stderr(error), self.assertRaises(SystemExit) as stopped:
                    SIZE.main()
                self.assertEqual(stopped.exception.code, 2)
                inspect.assert_not_called()
                read.assert_not_called()
            self.assertIn(PATH_ERROR, error.getvalue())
            self.assertEqual(output.read_bytes(), b"preserve internal report")
            self.assertEqual(baseline.read_bytes(), b"external baseline must not be read")

    @unittest.skipUnless(sys.platform.startswith("linux"), "Linux symlink boundary check")
    def test_cli_rejects_internal_symlink_to_an_external_output(self):
        with tempfile.TemporaryDirectory(prefix="binary size symlink ") as folder:
            root = Path(folder)
            work = root / "work"
            work.mkdir()
            outside = root / "external"
            outside.mkdir()
            sentinel = outside / "report.json"
            sentinel.write_bytes(b"preserve symlink target")
            (work / "linked").symlink_to(outside, target_is_directory=True)
            process = run_cli(work, "--artifact-dir", work / "missing", "--output", Path("linked/report.json"))
            self.assertEqual(process.returncode, 2, process.stdout + process.stderr)
            self.assertIn(PATH_ERROR, process.stderr)
            self.assertEqual(sentinel.read_bytes(), b"preserve symlink target")

    def test_cli_reads_external_artifact_and_source_with_internal_reports(self):
        with tempfile.TemporaryDirectory(prefix="binary size readonly inputs ") as folder:
            root = Path(folder)
            work = root / "work"
            work.mkdir()
            source = root / "source"
            strips, images = artwork_fixture(source)
            artifact = root / "artifact"
            artifact.mkdir()
            (artifact / "wxnotepad.exe").write_bytes(pe_fixture() + b"".join(strips) + b"".join(images))
            baseline = work / "baseline.json"
            baseline.write_text(json.dumps(SIZE.inspect(artifact, source)), encoding="utf-8")
            process = run_cli(
                work,
                "--artifact-dir",
                artifact,
                "--source-root",
                source,
                "--baseline-report",
                "baseline.json",
                "--output",
                "reports/result.json",
            )
            self.assertEqual(process.returncode, 0, process.stdout + process.stderr)
            report = json.loads((work / "reports/result.json").read_text(encoding="utf-8"))
            self.assertTrue(report["success"], report)
            self.assertTrue(report["artwork"]["all_present"])
            self.assertEqual(report["comparison"], {"artifact_bytes_delta": 0, "application_bytes_delta": 0})


if __name__ == "__main__":
    unittest.main()

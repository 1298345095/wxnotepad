"""Measure packaged PE/ELF files and verify that all source icon images remain embedded."""

import argparse
import hashlib
import json
import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
from report_paths import report_path  # noqa: E402 -- The tools path must be initialized first.


def region(data, offset, size):
    if offset < 0 or size < 0 or offset > len(data) or size > len(data) - offset:
        raise ValueError("Binary range extends beyond the file")
    return data[offset : offset + size]


def unpack(data, offset, layout):
    return struct.unpack(layout, region(data, offset, struct.calcsize(layout)))


def elf_sections(data):
    if region(data, 0, 6) != b"\x7fELF\x02\x01":
        raise ValueError("Expected a little-endian ELF64 file")
    header = unpack(data, 16, "<HHIQQQIHHHHHH")
    section_offset, section_stride, section_count, names_index = header[5], header[10], header[11], header[12]
    if section_stride < 64 or not section_count or names_index >= section_count:
        raise ValueError("Missing or unsupported ELF section table")
    region(data, section_offset, section_stride * section_count)
    sections = [unpack(data, section_offset + index * section_stride, "<IIQQQQIIQQ") for index in range(section_count)]
    names = region(data, sections[names_index][4], sections[names_index][5])
    records = []
    for name_offset, kind, flags, _, offset, size, _, _, _, entry_size in sections:
        if name_offset >= len(names):
            raise ValueError("ELF section name is outside the string table")
        terminator = names.find(b"\0", name_offset)
        if terminator < 0:
            raise ValueError("Unterminated ELF section name")
        name = names[name_offset:terminator].decode("ascii")
        content = b"" if kind == 8 else region(data, offset, size)  # SHT_NOBITS occupies no file bytes.
        records.append((name, kind, flags, size, entry_size, content))
    return header[1], records


def elf_table(content, entry_size, layout, name):
    expected_size = struct.calcsize(layout)
    if entry_size != expected_size or len(content) % expected_size:
        raise ValueError(f"Invalid ELF {name} entry size")
    return struct.iter_unpack(layout, content)


def elf_metadata(kind, content, entry_size):
    if kind in (2, 11):  # SHT_SYMTAB, SHT_DYNSYM
        entries = elf_table(content, entry_size, "<IBBHQQ", "symbol")
        if kind == 2:
            return {"symbol_table_entries": len(content) // entry_size}
        return {
            "dynamic_symbol_entries": len(content) // entry_size,
            "defined_dynamic_symbols": sum(entry[3] != 0 for entry in entries),
        }
    if kind == 19:  # SHT_RELR
        entries = elf_table(content, entry_size, "<Q", "RELR")
        count = sum((value >> 1).bit_count() if value & 1 else 1 for (value,) in entries)
        return {"relr_bytes": len(content), "relr_relocations": count}
    if kind == 6:  # SHT_DYNAMIC; repeated DT_RELRSZ entries retain the last value.
        entries = elf_table(content, entry_size, "<qQ", "dynamic")
        result = {}
        for tag, value in entries:
            if tag == 35:
                result["dynamic_relr_bytes"] = value
        return result
    return {}


def elf(data):
    machine, sections = elf_sections(data)
    result = {
        "format": "ELF64",
        "machine": machine,
        "sections": [],
        "debug_bytes": 0,
        "symbol_table_entries": 0,
        "dynamic_symbol_entries": 0,
        "defined_dynamic_symbols": 0,
        "relr_bytes": 0,
        "relr_relocations": 0,
        "dynamic_relr_bytes": 0,
    }
    for name, kind, flags, size, entry_size, content in sections:
        result["sections"].append(
            {"name": name, "file_bytes": len(content), "memory_bytes": size if flags & 2 else 0}
        )  # SHF_ALLOC
        if name.startswith((".debug_", ".zdebug_")):
            result["debug_bytes"] += size
        for field, value in elf_metadata(kind, content, entry_size).items():
            if field == "dynamic_relr_bytes":
                result[field] = value
            else:
                result[field] += value
    return result


def pe_address_range(data, sections, address, size):
    for base, raw_size, raw_offset in sections:
        if base <= address and address - base <= raw_size and size <= raw_size - (address - base):
            return region(data, raw_offset + address - base, size)
    raise ValueError("PE directory is outside a file-backed section")


def pe_debug_records(data, sections, optional, optional_size):
    (directory_count,) = unpack(data, optional + 108, "<I")
    if directory_count > (optional_size - 112) // 8:
        raise ValueError("PE data directories exceed the optional header")
    if directory_count <= 6:
        return []
    address, size = unpack(data, optional + 112 + 6 * 8, "<II")
    if not size:
        return []
    if size % 28:
        raise ValueError("Invalid PE debug directory size")
    content = pe_address_range(data, sections, address, size)
    records = []
    for offset in range(0, size, 28):
        _, _, _, _, kind, raw_size, _, raw_offset = unpack(content, offset, "<IIHHIIII")
        region(data, raw_offset, raw_size)
        records.append({"type": kind, "bytes": raw_size})
    return records


def pe(data):
    (signature_offset,) = unpack(data, 0x3C, "<I")
    if region(data, signature_offset, 4) != b"PE\0\0":
        raise ValueError("Invalid PE signature")
    machine, section_count, _, _, symbol_count, optional_size, _ = unpack(data, signature_offset + 4, "<HHIIIHH")
    optional = signature_offset + 24
    if optional_size < 112 or unpack(data, optional, "<H")[0] != 0x20B:
        raise ValueError("Expected a PE32+ optional header")
    region(data, optional, optional_size)
    section_offset = optional + optional_size
    region(data, section_offset, section_count * 40)
    sections = []
    result = {
        "format": "PE32+",
        "machine": machine,
        "sections": [],
        "symbol_table_entries": symbol_count,
        "debug_records": [],
    }
    for index in range(section_count):
        offset = section_offset + index * 40
        name = region(data, offset, 8).rstrip(b"\0").decode("ascii")
        virtual_size, address, raw_size, raw_offset = unpack(data, offset + 8, "<IIII")
        region(data, raw_offset, raw_size)
        sections.append((address, raw_size, raw_offset))
        result["sections"].append({"name": name, "file_bytes": raw_size, "memory_bytes": virtual_size})

    result["debug_records"] = pe_debug_records(data, sections, optional, optional_size)
    return result


def describe(data):
    if data.startswith(b"\x7fELF"):
        return elf(data)
    if data.startswith(b"MZ"):
        return pe(data)
    return None


def toolbar_images(source, executable):
    header = (source / "src/app/toolbar_art.h").read_text(encoding="utf-8")
    arrays = re.findall(r"unsigned char (Strip\d+)\[\]\s*=\s*\{([\d,\s]+)\}", header)
    if {name for name, _ in arrays} != {"Strip16", "Strip24", "Strip32", "Strip40", "Strip48"} or len(arrays) != 5:
        raise ValueError("Expected the five complete toolbar image strips")
    images = []
    for name, values in arrays:
        content = bytes(int(value) for value in values.split(",") if value.strip())
        if region(content, 0, 8) != b"\x89PNG\r\n\x1a\n":
            raise ValueError("Toolbar strip is not PNG data")
        width, height = unpack(content, 16, ">II")
        if height != int(name[5:]) or width != height * 27:
            raise ValueError("Expected all 27 toolbar images at each original resolution")
        images.append(
            {
                "name": name,
                "bytes": len(content),
                "sha256": hashlib.sha256(content).hexdigest(),
                "image_count": width // height,
                "present": content in executable,
            }
        )
    return images


def artwork(source, executable):
    images = toolbar_images(source, executable)
    icon = (source / "src/app/artwork/wxnotepad.ico").read_bytes()
    reserved, kind, count = unpack(icon, 0, "<HHH")
    if reserved or kind != 1 or not count:
        raise ValueError("Invalid source application icon")
    region(icon, 6, 16 * count)
    for index in range(count):
        width, height, _, _, _, _, size, offset = unpack(icon, 6 + index * 16, "<BBBBHHII")
        if not size or offset < 6 + 16 * count:
            raise ValueError("Invalid application icon image range")
        content = region(icon, offset, size)
        images.append(
            {
                "name": f"ApplicationIcon{index}",
                "width": width or 256,
                "height": height or 256,
                "bytes": size,
                "sha256": hashlib.sha256(content).hexdigest(),
                "present": content in executable,
            }
        )
    return {
        "source_icon_sha256": hashlib.sha256(icon).hexdigest(),
        "images": images,
        "all_present": all(image["present"] for image in images),
    }


def inspect(artifact, source, baseline=None):
    report = {"schema_version": 1, "success": False, "artifact_bytes": 0, "files": [], "errors": []}
    try:
        applications = [path for name in ("wxnotepad", "wxnotepad.exe") if (path := artifact / name).is_file()]
        if len(applications) != 1:
            raise ValueError("Expected exactly one packaged wxnotepad executable")
        application = applications[0]
        report["application"] = application.name
        paths = (path for path in artifact.rglob("*") if path.is_file())
        for path in sorted(paths, key=lambda entry: entry.relative_to(artifact).as_posix()):
            data = path.read_bytes()
            record = {
                "path": path.relative_to(artifact).as_posix(),
                "bytes": len(data),
                "sha256": hashlib.sha256(data).hexdigest(),
            }
            binary = describe(data)
            if binary is not None:
                record["binary"] = binary
            elif path == application:
                raise ValueError("Packaged application is not an ELF or PE binary")
            report["files"].append(record)
            report["artifact_bytes"] += len(data)
        report["artwork"] = artwork(source, application.read_bytes())
        if not report["artwork"]["all_present"]:
            missing = ", ".join(image["name"] for image in report["artwork"]["images"] if not image["present"])
            report["errors"].append("Packaged application lost source icon images: " + missing)
        if baseline is not None:
            previous = json.loads(baseline.read_text(encoding="utf-8"))
            if (
                not isinstance(previous, dict)
                or previous.get("schema_version") != 1
                or previous.get("application") != application.name
            ):
                raise ValueError("Baseline must use the same report schema and application platform")
            old_sizes = {entry["path"]: entry["bytes"] for entry in previous["files"]}
            report["comparison"] = {
                "artifact_bytes_delta": report["artifact_bytes"] - previous["artifact_bytes"],
                "application_bytes_delta": application.stat().st_size - old_sizes[application.name],
            }
    except (OSError, ValueError, KeyError, TypeError, struct.error) as error:
        report["errors"].append(str(error))
    report["success"] = not report["errors"]
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--artifact-dir", type=Path, required=True)
    parser.add_argument("--source-root", type=Path, default=ROOT)
    parser.add_argument("--baseline-report", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    arguments = parser.parse_args()
    try:
        directory = Path.cwd().resolve()
        output = report_path(arguments.output, directory)
        baseline = report_path(arguments.baseline_report, directory) if arguments.baseline_report is not None else None
    except (OSError, RuntimeError, ValueError) as error:
        parser.error(str(error))
    report = inspect(arguments.artifact_dir, arguments.source_root, baseline)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(
        f"Distribution: {report['artifact_bytes']} bytes; {len(report['files'])} files; "
        f"icons retained: {report.get('artwork', {}).get('all_present', False)}"
    )
    for error in report["errors"]:
        print(error)
    return int(not report["success"])


if __name__ == "__main__":
    raise SystemExit(main())

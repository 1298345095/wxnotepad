"""Prepare the pinned original core, build wxnotepad, and optionally run CTest."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tarfile

REVISION = "89b8b36e0ee2c23f73be922315f17261ccfbbe1a"
UPSTREAM = "https://github.com/zufuliu/notepad4.git"
ROOT = Path(__file__).resolve().parents[1]


def run(command, **kwargs):
    subprocess.run([str(item) for item in command], check=True, **kwargs)


def prepare(build, source, archive):
    patch = ROOT / "patches/original-core.patch"
    if not archive:
        if not source:
            source = build / "upstream"
            if not (source / ".git").exists():
                run(["git", "init", source])
                run(["git", "-C", source, "remote", "add", "origin", UPSTREAM])
            run(["git", "-C", source, "fetch", "--depth=1", "origin", REVISION])
        revision = subprocess.check_output(
            ["git", "-C", str(source), "rev-parse", f"{REVISION}^{{commit}}"], text=True).strip()
        if revision != REVISION:
            raise RuntimeError("Original core revision does not match the pinned commit")
        archive = build / "notepad4-source.tar"
        run(["git", "-C", source, "archive", "--format=tar", f"--output={archive}",
             REVISION, "scintilla", "License.txt"])
    # A supplied archive must be the exact git archive of the pinned paths.
    archive_digest = hashlib.sha256(archive.read_bytes()).hexdigest()
    expected = (ROOT / "patches/source.sha256").read_text(encoding="utf-8").strip()
    if archive_digest != expected:
        raise RuntimeError("Original source archive SHA-256 does not match the pinned source")
    record = {"revision": REVISION, "archive_sha256": archive_digest,
              "patch_sha256": hashlib.sha256(patch.read_bytes()).hexdigest(),
              "preparation_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest()}
    staged = build / "original-core"
    stamp = staged / "wxnotepad-source.json"
    if stamp.exists() and json.loads(stamp.read_text(encoding="utf-8")) == record:
        return staged
    if staged.exists():
        shutil.rmtree(staged)
    staged.mkdir()
    with tarfile.open(archive) as bundle:
        bundle.extractall(staged, filter="data")
    # Upstream mixes CRLF and LF. Canonicalize only existing patch targets so
    # git apply behaves identically under Windows and Linux Git defaults.
    for relative in re.findall(r"^--- a/(.+)$", patch.read_text(encoding="utf-8"), re.MULTILINE):
        target = staged / relative
        target.write_bytes(target.read_bytes().replace(b"\r\n", b"\n"))
    run(["git", "apply", "--check", patch], cwd=staged)
    run(["git", "apply", patch], cwd=staged)
    stamp.write_text(json.dumps(record, indent=2) + "\n", encoding="utf-8")
    return staged


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, help="Existing original checkout (never modified)")
    parser.add_argument("--archive", type=Path, help="Pinned source archive, useful for native WSL builds")
    parser.add_argument("--build-dir", type=Path, default=ROOT / "build")
    parser.add_argument("--vcpkg-root", type=Path, default=Path(os.environ.get(
        "VCPKG_ROOT", "G:/vcpkg" if sys.platform == "win32" else str(Path.home() / "vcpkg"))))
    parser.add_argument("--test", action="store_true", help="Run real GUI CTest checks; requires a desktop")
    parser.add_argument("--prepare-only", action="store_true")
    args = parser.parse_args()
    build = args.build_dir.resolve()
    build.mkdir(parents=True, exist_ok=True)
    staged = prepare(build, args.source.resolve() if args.source else None,
                     args.archive.resolve() if args.archive else None)
    if args.prepare_only:
        return
    configure = ["cmake", "-S", ROOT, "-B", build / "cmake",
                 f"-DNOTEPAD4_SOURCE={staged}",
                 f"-DCMAKE_TOOLCHAIN_FILE={args.vcpkg_root}/scripts/buildsystems/vcpkg.cmake",
                 "-DVCPKG_MANIFEST_MODE=OFF"]
    if sys.platform != "win32":
        configure += ["-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release"]
    run(configure)
    run(["cmake", "--build", build / "cmake", "--config", "Release", "--parallel", "8"])
    if args.test:
        run(["ctest", "--test-dir", build / "cmake", "-C", "Release", "--output-on-failure"])


if __name__ == "__main__":
    main()

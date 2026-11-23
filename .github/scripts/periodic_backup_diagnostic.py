"""Temporary Windows hosted observation; never changes periodic-backup acceptance."""
import argparse
import ctypes
import difflib
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

import ci

ROOT = Path(__file__).resolve().parents[2]
ASSETS = ROOT / ".github/diagnostics"
BASE = "7f285f7064463c459f076ce7e62e981a484b86f3"
SOURCES = ("tests/periodic_backup.cpp", "src/document/document_session.cpp")
RUN_LIMIT = 10
CASE_TIMEOUT_SECONDS = 90
BUILD_JOBS = 4
CONTINUOUS_FAILURE = "FAIL: continuously edited document is backed up during editing, without debounce"


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def write_json(path, value):
    path.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")


def fixture_tokens(text):
    tokens = re.findall(r'"(?:\\.|[^"\\])*"|//[^\n]*|/\*[\s\S]*?\*/|\w+|[^\s]', text)
    return [token for token in tokens if not token.startswith(("//", "/*"))]


def prepare(output):
    # git apply is confined to fresh generated copies under this checkout.
    relative = output.relative_to(ROOT)
    if ".ci" not in relative.parts:
        raise ValueError("Diagnostic output must be in this checkout's .ci directory")
    subprocess.run(["git", "diff", "--exit-code", BASE, "--", *SOURCES], cwd=ROOT, check=True)
    output.mkdir(parents=True, exist_ok=False)
    generated = output / "source"
    for name in SOURCES:
        target = generated / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text((ROOT / name).read_text(encoding="utf-8"), encoding="utf-8", newline="\n")
    shutil.copyfile(ASSETS / "periodic-bounded-trace.h", generated / "periodic-bounded-trace.h")
    subprocess.run(["git", "apply", "--directory=" + generated.relative_to(ROOT).as_posix(),
                    str(ASSETS / "periodic-backup-observation.patch")], cwd=ROOT, check=True)
    original = (ROOT / SOURCES[0]).read_text(encoding="utf-8")
    observed = (generated / SOURCES[0]).read_text(encoding="utf-8")
    stripped = observed.replace('#include "periodic-bounded-trace.h"\n', "")
    stripped = re.sub(r"PeriodicTrace::(?:Record|Configure|Dump)\([^;]*\);", "", stripped)
    if fixture_tokens(original) != fixture_tokens(stripped):
        raise ValueError("Original fixture input/assertion/control tokens changed")
    for name in SOURCES:
        before = (ROOT / name).read_text(encoding="utf-8")
        after = (generated / name).read_text(encoding="utf-8")
        (output / (Path(name).stem + ".diff")).write_text(
            "".join(difflib.unified_diff(before.splitlines(True), after.splitlines(True),
                                         fromfile="a/" + name, tofile="b/" + name)), encoding="utf-8")
    document = (generated / SOURCES[1]).read_text(encoding="utf-8")
    for call in ("CreateFileW(", "WriteFile(", "CloseHandle(file)", "backupTimer_.Start("):
        if document.count(call) != (ROOT / SOURCES[1]).read_text(encoding="utf-8").count(call):
            raise ValueError("Native operation count changed: " + call)
    write_json(output / "source-proof.json", {
        "base": BASE, "fixture_tokens_unchanged": True, "native_file_timer_call_counts_unchanged": True,
        "sources": {name: {"original_sha256": digest(ROOT / name), "observed_sha256": digest(generated / name)}
                    for name in SOURCES},
        "trace_header_sha256": digest(generated / "periodic-bounded-trace.h"),
        "context_difference": "Focused fresh Windows 2025 job; no preceding 70 GUI cases or original-reference prefix.",
        "image_version": os.environ.get("ImageVersion"), "runner_os": os.environ.get("RUNNER_OS"),
    })


def native_commands(build):
    data = subprocess.check_output(["ninja", "-t", "commands", "periodic_backup"], cwd=build)
    try:
        return data.decode("utf-8").splitlines()
    except UnicodeDecodeError:
        # Native Windows tool output uses the ANSI code page independently of Python UTF-8 mode.
        return data.decode("cp" + str(ctypes.windll.kernel32.GetACP())).splitlines()


def quote(value):
    return subprocess.list2cmdline([str(value)])


def substitute(command, before, after):
    if command.count(before) != 1:
        raise ValueError("Expected one generated-command path: " + before)
    quoted_before = '"' + before + '"'
    return command.replace(quoted_before if quoted_before in command else before, quote(after), 1)


def compile_observed(build, output):
    commands = native_commands(build)
    binary = output / "periodic-observed.exe"
    tracked = ["periodic_backup.exe", "app_shell.lib", "editor_features.lib", "editor_runtime.lib",
               "CMakeFiles/editor_features.dir/src/document/document_session.cpp.obj",
               "CMakeFiles/periodic_backup.dir/src/app/windows.rc.res", ".ninja_log", ".ninja_deps"]
    before = {name: digest(build / name) for name in tracked}
    write_json(output / "root-before.json", before)
    invocations = []

    def execute(label, command):
        invocations.append({"phase": label, "command": command, "cwd": str(build)})
        write_json(output / "commands.json", invocations)
        print(label + ": " + command, flush=True)
        subprocess.run(command, cwd=build, shell=True, check=True)

    for target, name, object_name in (
        ("periodic_backup", SOURCES[0], "observed-test.obj"),
        ("editor_features", SOURCES[1], "observed-document.obj"),
    ):
        original = str(ROOT / name)
        original_object = str(Path("CMakeFiles") / (target + ".dir") / (name + ".obj"))
        command = next(line for line in commands if original in line and " /Fo" in line)
        command = substitute(command, original, output / "source" / name)
        command = substitute(command, original_object, output / object_name)
        pdb = "/Fd" + str(Path("CMakeFiles") / (target + ".dir")) + "\\"
        if pdb not in command:
            raise ValueError("Missing original MSVC PDB option")
        command = command.replace(pdb, "/Fd" + quote(output / (target + "-compile.pdb")))
        command += " /I" + quote(output / "source") + " /I" + quote(ROOT / "tests")
        execute("Compile " + name, command)

    original_object = str(Path("CMakeFiles/periodic_backup.dir/tests/periodic_backup.cpp.obj"))
    link = next(line for line in commands if "vs_link_exe" in line and "/out:periodic_backup.exe" in line)
    link = link.replace(original_object, quote(output / "observed-test.obj") + " " + quote(output / "observed-document.obj"), 1)
    link = link.replace("--intdir=CMakeFiles\\periodic_backup.dir", "--intdir=" + quote(output))
    link, deployments = re.subn(r'--target-binary=(?:"[^"]*"|[^\s"]+)',
                                "--target-binary=" + quote(binary.as_posix()), link, count=1)
    if deployments != 1:
        raise ValueError("Missing original app-local deployment command")
    for option, extension in (("/out:", "exe"), ("/implib:", "lib"), ("/pdb:", "pdb")):
        link = link.replace(option + "periodic_backup." + extension,
                            option + quote(output / ("periodic-observed." + extension)), 1)
    link = link.replace("/nologo", "/nologo /VERBOSE:LIB /MAP:" + quote(output / "periodic-observed.map"), 1)
    if link.index(str(output / "observed-document.obj")) >= link.index("editor_features.lib"):
        raise ValueError("Observed DocumentSession must precede the original library")
    execute("Link observed DocumentSession", link)
    after = {name: digest(build / name) for name in tracked}
    write_json(output / "root-after.json", after)
    map_text = (output / "periodic-observed.map").read_text(encoding="utf-8", errors="replace")
    proof = [line for line in map_text.splitlines() if "DocumentSession" in line and "observed-document.obj" in line]
    if before != after or "document_session.cpp.obj" in map_text or not any("SaveBackup" in line for line in proof):
        raise ValueError("Original build changed or observed object-link proof failed")
    (output / "document-session-map-proof.txt").write_text("\n".join(proof) + "\n", encoding="utf-8")
    write_json(output / "build-proof.json", {
        "original_build_unchanged": True, "original_document_object_extracted": False,
        "observed_document_map_lines": len(proof), "binary_sha256": digest(binary),
        "compiler_parallelism": 1, "native_case_executed_during_build": False,
    })


def build_observed(build, output):
    ci.run(ci.cmake_command(["--build", build, "--config", "Release", "--parallel", BUILD_JOBS,
                            "--target", "periodic_backup"]), "periodic-diagnostic-target-build.log")
    prepare(output)
    driver = output / "compile-observed.cmake"
    arguments = [sys.executable, "-X", "utf8", Path(__file__).resolve(), "compile",
                 "--cmake-dir", build, "--output-dir", output]
    driver.write_text("execute_process(COMMAND " + " ".join("[=[" + str(arg) + "]=]" for arg in arguments)
                      + " COMMAND_ERROR_IS_FATAL ANY)\n", encoding="utf-8")
    ci.run(ci.cmake_command(["-P", driver]), "periodic-diagnostic-observed-build.log")


def run_observed(build, output):
    summary = {"limit": RUN_LIMIT, "timeout_seconds_per_case": CASE_TIMEOUT_SECONDS,
               "defect_resolved": False, "results": []}
    for index in range(1, RUN_LIMIT + 1):
        report = output / ("case-" + str(index).zfill(2))
        trace = report / "trace/periodic-trace.tsv"
        previous = os.environ.get("WXNOTEPAD_PERIODIC_TRACE_DIR")
        try:
            os.environ["WXNOTEPAD_PERIODIC_TRACE_DIR"] = str(trace.parent)
            result = ci.probe([output / "periodic-observed.exe"], report, CASE_TIMEOUT_SECONDS, 1, build)
        finally:
            if previous is None:
                os.environ.pop("WXNOTEPAD_PERIODIC_TRACE_DIR", None)
            else:
                os.environ["WXNOTEPAD_PERIODIC_TRACE_DIR"] = previous
        process = json.loads((report / "summary.json").read_text(encoding="utf-8"))["results"][0]
        log = (report / "run-001.log").read_text(encoding="utf-8", errors="replace")
        trace_status = trace.read_text(encoding="utf-8").splitlines()[0] if trace.exists() else None
        summary["results"].append({"case": index, "process": process, "trace_status": trace_status})
        if result:
            summary["classification"] = ("continuous_edit_assertion" if CONTINUOUS_FAILURE in log
                                         else "other_assertion_or_process_failure")
        elif trace_status is None or "dropped=0" not in trace_status:
            summary["classification"] = "missing_or_incomplete_trace"
            result = 1
        else:
            summary["classification"] = "not_reproduced"
        write_json(output / "diagnostic-summary.json", summary)
        if result:
            print("Stopped at first failure: " + summary["classification"])
            return 1
    print("NOT REPRODUCED in ten observed full fixtures; the CI defect remains open.")
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("phase", choices=("prepare", "build", "compile", "run"))
    parser.add_argument("--cmake-dir", type=Path, default=ROOT / ".ci/build/cmake")
    parser.add_argument("--output-dir", type=Path, default=ROOT / ".ci/reports/periodic-diagnostic")
    args = parser.parse_args()
    if sys.platform != "win32" and args.phase != "prepare":
        parser.error("Native diagnostic phases require Windows")
    build, output = args.cmake_dir.resolve(), args.output_dir.resolve()
    if args.phase == "prepare":
        prepare(output)
        return 0
    if args.phase == "build":
        build_observed(build, output)
        return 0
    if args.phase == "compile":
        compile_observed(build, output)
        return 0
    return run_observed(build, output)


if __name__ == "__main__":
    raise SystemExit(main())

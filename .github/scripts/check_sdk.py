"""Verify the configured wx SDK and distributed dependency footprint."""
import argparse
import json
from pathlib import Path
import re


def stc_artifact(path):
    name = path.name.lower()
    core_library = name.startswith(('scintilla', 'lexilla', 'libscintilla', 'liblexilla'))
    wx_stc = name.startswith(('wx', 'libwx')) and any(part in name for part in ('_stc', 'scintilla', 'lexilla'))
    return ((core_library or wx_stc) and
            (path.suffix.lower() in ('.lib', '.dll', '.pdb', '.a') or '.so' in path.suffixes))


def inspect(cmake_directory, artifact_directory=None):
    report = dict(success=False, sdk_root=None, triplet=None, sdk_library_count=0, runtime_file_count=0,
                  runtime_checked=artifact_directory is not None,
                  forbidden_sdk_artifacts=[], forbidden_runtime_artifacts=[], errors=[])
    try:
        cache = {}
        for line in (cmake_directory / 'CMakeCache.txt').read_text(encoding='utf-8').splitlines():
            entry = re.match(r'([^#/][^:]*):[^=]+=(.*)', line)
            if entry:
                cache[entry[1]] = entry[2]
        triplet = cache['VCPKG_TARGET_TRIPLET']
        prefix = (Path(cache['VCPKG_INSTALLED_DIR']) / triplet).resolve()
        configuration = Path(cache['wxWidgets_DIR']).resolve()
        report.update(sdk_root=str(prefix), triplet=triplet)
        if configuration != prefix / 'share/wxwidgets' or not configuration.is_dir():
            report['errors'].append('Configured wxWidgets must come from the selected vcpkg installation')
        headers = [prefix / 'include/wx/version.h', *(prefix / 'include').glob('wx-*/wx/version.h')]
        if not any(header.is_file() for header in headers):
            report['errors'].append('Selected SDK has no wxWidgets version header')
        files = sorted(path for directory in ('lib', 'bin', 'debug/lib', 'debug/bin')
                       for path in (prefix / directory).rglob('*') if path.is_file())
        libraries = [path for path in files if path.suffix.lower() in ('.lib', '.dll', '.a') or '.so' in path.suffixes]
        report['sdk_library_count'] = len(libraries)
        if not any(path.name.lower().startswith(('wx', 'libwx')) and '_core' in path.name.lower()
                   for path in libraries) or not any(
                path.name.lower().startswith(('wxbase', 'libwx_base')) for path in libraries):
            report['errors'].append('Selected SDK must contain actual wx core and base libraries')
        report['forbidden_sdk_artifacts'] = [str(path) for path in files if stc_artifact(path)]
        if artifact_directory is not None:
            windows = 'windows' in triplet
            binary = artifact_directory / ('wxnotepad.exe' if windows else 'wxnotepad')
            if not binary.is_file():
                report['errors'].append('Distribution has no application executable')
            runtime = sorted(path for path in artifact_directory.rglob('*') if path.is_file() and
                             (path.suffix.lower() in ('.exe', '.dll') or '.so' in path.suffixes or path == binary))
            report['runtime_file_count'] = len(runtime)
            unused_windows = ('libwebp', 'webp', 'libjpeg', 'jpeg', 'turbojpeg',
                              'libtiff', 'tiff', 'libpcre2', 'pcre2')
            report['forbidden_runtime_artifacts'] = [str(path) for path in runtime if stc_artifact(path) or
                (windows and path.name.lower().startswith(unused_windows))]
    except (OSError, KeyError) as error:
        report['errors'].append(f'Cannot inspect configured SDK: {error}')
    report['success'] = not (report['errors'] or report['forbidden_sdk_artifacts'] or
                             report['forbidden_runtime_artifacts'])
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cmake-dir', type=Path, required=True)
    parser.add_argument('--artifact-dir', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    arguments = parser.parse_args()
    report = inspect(arguments.cmake_dir, arguments.artifact_dir)
    arguments.output.parent.mkdir(parents=True, exist_ok=True)
    arguments.output.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(report, indent=2))
    return int(not report['success'])


if __name__ == '__main__':
    raise SystemExit(main())

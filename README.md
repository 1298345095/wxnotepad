# wxnotepad

A native wxWidgets editor using Notepad4's custom Scintilla core. The original
core owns the text, edit history and save point, and paints through the
wx platform adapter. There is no wxSTC control or second text document.

The editor starts with an empty document. Type ordinary characters, Enter and
Tab; use Backspace and Delete to remove text. Edit → Undo/Redo and Ctrl+Z/Ctrl+Y
follow the core's history. A `*` in the title marks changes from the save point;
undoing to that point removes it. Ctrl+Shift+Z also redoes.

This covers [#9](https://github.com/1298345095/wxnotepad/issues/9) and
[#10](https://github.com/1298345095/wxnotepad/issues/10). Selection/navigation,
clipboard, file operations and dedicated input-method handling follow in later tickets.

## Build and run

Prerequisites: Git, Python 3.12+, CMake 3.25+, an x64 C++23 compiler, and wxWidgets
**3.3.3#1** from vcpkg. Tested compilers are MSVC 14.51 and GCC 13.3. On Linux use
GTK3 with `gtk+-3.0`, `wayland-egl` and `wayland-cursor` development packages,
Ninja, and a working desktop. Keep WSL sources and builds on its Linux filesystem.

`vcpkg.json` fixes the registry baseline and wxWidgets port revision. To provision
dependencies on another machine, use the manifest with the corresponding triplet:

```text
vcpkg install --triplet x64-windows --x-install-root=G:/vcpkg/installed
vcpkg install --triplet x64-linux --x-install-root=/home/ctrls/vcpkg/installed
```

The build entry point reuses an existing vcpkg installation and does not install
packages. Adapt those example installation paths to `<vcpkg-root>/installed` on
your machine. Pass its root explicitly when it is not `G:/vcpkg` (Windows) or
`~/vcpkg` (Linux). The CMake package check requires wxWidgets 3.3.3 exactly.

Windows, from the repository root in PowerShell 7:

```powershell
python -X utf8 tools/build.py --source E:/NewNotepad4/notepad4 --vcpkg-root G:/vcpkg --test
./build/cmake/Release/wxnotepad.exe
```

Linux, from a native filesystem checkout:

```sh
python3 tools/build.py --source /path/to/notepad4 --vcpkg-root "$HOME/vcpkg"
GDK_BACKEND=wayland ctest --test-dir build/cmake --output-on-failure
GDK_BACKEND=x11 ctest --test-dir build/cmake --output-on-failure
GDK_BACKEND=wayland ./build/cmake/wxnotepad
GDK_BACKEND=x11 ./build/cmake/wxnotepad
```

Without `--source`, the entry point fetches the exact original revision into its
build directory. It never checks out or patches the supplied original repository.
For an offline/native WSL build, copy `build/notepad4-source.tar` from a prepared
build and use `--archive /path/to/notepad4-source.tar`. Archive SHA-256 is checked
against the pinned `patches/source.sha256` before extraction. Use `--prepare-only`
to create the archive without compiling. See [patch provenance](patches/README.md)
and [dependency licenses](licenses/README.md).

`--test` runs two separate real GUI processes: UTF-8 character events, basic
editing, Undo/Redo, menu availability and save-point/title state; and native
focus transfers plus show/resize/uncover/hide-show paint handling. These checks
need a desktop and fail on a GUI process error or timeout. See the
[input validation record](docs/evidence/issue-10/README.md) and the earlier
[read-only display assessment](docs/evidence/issue-09/README.md).

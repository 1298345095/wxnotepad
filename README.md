# wxnotepad

A native wxWidgets editor using Notepad4's custom Scintilla core. The original
core owns the text, edit history and save point, and paints through the
wx platform adapter. There is no wxSTC control or second text document.

The editor starts with an empty document. Type ordinary characters, Enter and
Tab; use Backspace and Delete to remove text. Edit → Undo/Redo and Ctrl+Z/Ctrl+Y
follow the core's history. A `*` in the title marks changes from the save point;
undoing to that point removes it. Ctrl+Shift+Z also redoes.

On Windows the native caret, composition window and candidate window follow the
core's caret, including after scrolling and regaining focus. Chinese character
events retain their UTF-8 conversion storage through insertion.

Phase 1 supports keyboard navigation, Shift selection, mouse selection and
scrolling, plus native system clipboard Cut/Copy/Paste and their Ctrl+X/C/V
shortcuts. Clipboard edits participate in the same Undo/Redo history.

File → New / Ctrl+N and Open / Ctrl+O replace the current document after any
unsaved changes are saved or discarded. Close uses the same Save/Discard/Cancel
confirmation. Save / Ctrl+S opens a native chooser for an unnamed document and
reuses the current path thereafter; Save As / Ctrl+Shift+S chooses a new target
and confirms overwrite. Cancellation or failure retains the current document,
identity and history. A successful save safely replaces the target and updates
the title and save point.

New documents save as UTF-8 without a BOM. Open accepts UTF-8 with or without a
BOM and recognizes BOM-marked UTF-16LE; Open As UTF-16LE also handles files
without a BOM. Open As GBK explicitly decodes CP936. Save and Save As preserve
the loaded encoding and BOM, shown in the status bar. GBK character loss offers
Save as UTF-8, Replace in GBK, or Cancel. Newline insertion defaults to CRLF;
loaded files use their detected newline style for Enter, while existing CRLF,
LF and CR bytes remain intact when saved.

Microsoft Pinyin human validation passed; see the
[IME validation record](docs/evidence/issue-13/README.md). Linux IME remains
deferred. Search/replace UI, syntax highlighting, advanced editing, settings
migration and release packaging are outside this phase. See the
[phase 1 spec](https://github.com/1298345095/wxnotepad/issues/8) and the
[full-application resource observations](docs/observations/issue20-README.md).

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

`--test` runs separate real GUI processes covering input and Undo/Redo,
clipboard exchange with a peer process, repaint/focus handling, native
Open/Save/Save As/close dialogs, byte-exact UTF-8/UTF-16LE/GBK roundtrips,
loaded newline insertion, and cancellation/write-failure state retention.
Windows also checks IMM composition/candidate positions and font after
scrolling, refocus and composition start. These checks need a desktop and fail
on a GUI process error or timeout. Native navigation and Microsoft Pinyin
human checks are recorded separately in the
[navigation](docs/evidence/issue-11/README.md) and
[IME](docs/evidence/issue-13/README.md) evidence. File workflow records cover
[Open](docs/evidence/issue-16/README.md),
[Save/Save As](docs/evidence/issue-17/README.md),
[UTF-16LE](docs/evidence/issue-18/README.md) and
[GBK](docs/evidence/issue-19/README.md).

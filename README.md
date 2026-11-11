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

Windows binaries declare Per-Monitor V2 DPI awareness, and offscreen text buffers
inherit the window's display scale. See the [font validation record](docs/observations/windows-font-README.md).

View → Zoom offers Zoom In (Ctrl++ / Ctrl+= and keypad +), Zoom Out (Ctrl+- and
keypad -), Zoom Level and Reset Zoom (Ctrl+\\). The original core uses 10%–500%
zoom, with 10-point steps below 200% and 25-point steps above it; reset restores
100%. Ctrl+wheel changes zoom; horizontal wheel and Shift+wheel scroll
horizontally first, including when Ctrl is held. The `ZoomLevel` preference uses
the shared [settings lifecycle](docs/settings.md).

The editor supports keyboard navigation, Shift selection, mouse selection and
scrolling, plus native system clipboard Cut/Copy/Paste and their Ctrl+X/C/V
shortcuts. Clipboard edits participate in the same Undo/Redo history.

Home/End first visit a wrapped row's start/end, then the logical line boundary;
Home retains the original indentation/start toggle. Shift extends the selection
with the same rules. Ctrl+PageUp/PageDown move between paragraphs separated by
blank or whitespace-only lines; adding Shift extends the selection.

Search → Goto → Goto Line / Ctrl+G navigates to an absolute, 1-based document line and
reveals its folded ancestors. The dialog validates the current line range;
Cancel retains the previous selection, viewport and folds.

Search → Bookmarks offers Toggle (Ctrl+F2), Goto Next (F2), Goto Previous
(Shift+F2), Select All (Alt+F6) and Clear All (Alt+F2). Navigation wraps and
reveals folded destinations. Select All selects each marked whole line with
the nearest bookmark as the main selection, preferring the earlier line on a
tie. These commands share the bookmark marker used by Find All.

Edit → Selection → Toggle Line Comment / Ctrl+/ (also Ctrl+keypad Divide) uses `//` for
C/C++ and JSON, `#` for Python/Bash/C Shell, and `dnl ` for m4. It follows the original common indentation,
mixed-line and selection rules, with one Undo step per edit. The command is
available for these writable schemes; rectangular selections report the
original unsupported-operation warning. `AutoInsertMask` controls optional
spacing through the shared [settings lifecycle](docs/settings.md).

View → Line Numbers / Ctrl+Shift+N toggles a margin sized for the current line
count, font and display scale. View → Word Wrap / Ctrl+Shift+W wraps long lines
to the window width without changing newline bytes. View → Show Whitespace /
Ctrl+Shift+8 displays spaces and tabs. Line numbers start on; wrapping and
whitespace markers start off. These choices stay with the current window through
New, Open, Save As and language changes; they are not saved between runs.
See the [navigation and view validation record](docs/observations/navigation-view-README.md).

The status bar shows the 1-based logical line and core column at the active
caret. Tabs follow the current tab stops; wrapped rows do not create document
lines. A nonempty stream selection also shows its Unicode scalar count, rather
than UTF-8 bytes or UTF-16 units. Combining code points count separately, and CR
and LF each count once. Search feedback has a separate field, retaining the
encoding/BOM, language and EOL information.

File → Line Endings offers Windows (CR+LF), Unix/macOS (LF) and Classic Mac OS (CR). Each command
normalizes existing line endings without adding a final newline and sets the
mode used by Enter. A text-changing conversion is one undo action; Undo restores
the text while retaining the explicitly chosen insertion mode. The `EOL:` status
label describes that insertion mode, including when Undo restores mixed endings.
No-op conversions retain the save point and pending Redo history. New/Open keep
the existing new-document default and loaded-file EOL detection.

Settings → Insert Tabs as Spaces and Tab Settings configure tabs/spaces and
indentation widths. Changing options leaves existing whitespace and history intact.
Edit → Selection → Indent / Unindent operates on the current or selected logical lines.
Tab retains ordinary insertion and indents multiline selections; Shift+Tab
unindents the current or selected lines. A selection ending exactly at the next
line's start excludes that line. Text-changing block commands undo in one step.
See the [everyday editing validation record](docs/observations/everyday-editing-README.md).

Edit also provides the original text transformations. The [delivered menu map](docs/menu-map.md)
lists their Selection, Lines, Convert, Insert and Special groups:

| Command | Shortcut |
| --- | --- |
| Convert → Upper Case / Lower Case | Ctrl+Shift+U / Ctrl+U |
| Tabify Selection / Untabify Selection | Ctrl+Shift+T / Ctrl+Shift+S |
| Tabify Indent / Untabify Indent | Ctrl+Alt+T / Ctrl+Alt+S |
| Strip Leading Blanks / Strip Trailing Blanks | Menu / Alt+T |
| Compress Whitespace | Alt+W |
| Merge Blank Lines / Remove Blank Lines | Alt+B / Alt+R |
| Strip First Character / Strip Last Character | Alt+Z / Alt+L |
| Join Lines / Join Paragraphs | Ctrl+J / Ctrl+Shift+J |
| Pad With Spaces | Alt+P |
| Enclose Selection → With... | Alt+Q |
| Triple Single Quotes / Triple Double Quotes / Triple Backticks | Ctrl+3 / Ctrl+6 / Ctrl+9 |
| Merge Duplicate Lines / Remove Duplicate Lines | Menu |

The same operations serve Plain Text, C/C++, Python, JSON and Shell Script. Each retains its
original selection scope, rectangular-selection handling and history behavior.
Save As remains F6; Ctrl+Shift+S converts selected tabs to spaces.

Search → Find / Ctrl+F opens a modeless search dialog. F3 finds the next match;
Shift+F3 finds the previous match. Wrap around is enabled by default, while
Match case, Whole word and Regular expression are initially off. Find and
Replace share the query and options for the current application session.
Opening imports selected text up to 1 MiB, or the clipboard under the original
selection options; Replace then focuses replacement text for an imported
selection. Closing discards unsubmitted edits. Settings → Other Settings → Remember Search Options
enables original search-history/flag persistence through the shared
[settings lifecycle](docs/settings.md).

Find All counts and marks matches across the whole document, including folded
content, while preserving the selection and folds. Bookmark matched line is off
by default. Empty regex matches count without visible marks or bookmarks; the
original single-anchor `^` and `$` line-count rules are retained. Query/options
changes and edits clear the count and indicators. Find All bookmarks remain until
the next Find All operation; New/Open clear them with the replaced document.

Search → Replace / Ctrl+H opens the replacement view. Replace revalidates the
selected match before editing; Replace All searches the complete current
document and reports its count in the dialog. Each text-changing single
replacement or Replace All operation can be undone once. Byte-identical
single replacements and Replace All preserve the save point and redo history.
In Selection uses the current stream range, reports its replacement count, and
groups edits into one Undo operation. It retains the original search context and
stops before a match crossing the selection end. Rectangular selections show the
original unsupported-operation warning. See the
[selection replacement record](docs/observations/replace-selection-README.md).

Regular expressions use the pinned original core's Boost engine and capture
formatter, including numeric `$1` / `\1` references and `$&` / `$0` for the whole
match. Whole word is disabled in regex mode; use explicit regex word boundaries
instead. Find resumes at the selection endpoint, including repeated zero-length
matches. Forward assertions use target-local context; backward regex search is
line by line. Replace All uses the original character/line progress and EOF
stopping rules. See the [original regex behavior record](docs/observations/original-regex-README.md).
Invalid patterns report an error without editing the document.

Transform Backslashes converts literal Find/Replace input using the original
control escapes, `\\`, raw-byte `\xHH` and Unicode `\uHHHH` forms. It starts off
and selecting it clears Regular Expression. Literal replacements keep capture
spellings such as `$1` and `\1` as text. See the
[backslash transform behavior record](docs/observations/search-backslashes.md).

File → New / Ctrl+N and Open / Ctrl+O replace the current document after any
unsaved changes are saved or discarded. Close uses the same Save/Discard/Cancel
confirmation. Save / Ctrl+S opens a native chooser for an unnamed document and
reuses the current path thereafter; Save As / F6 chooses a new target
and confirms overwrite. Cancellation or failure retains the current document,
identity and history. A successful save safely replaces the target and updates
the title and save point.

File → New Window / Alt+N starts a separate blank process using the saved startup
settings. The current document, selection and undo history stay in the parent.
The child requests the parent's normal window size and maximized state. Windows
uses its exact normal rectangle with a 10-pixel offset, wrapping to the monitor
origin at the work-area edge. Linux leaves placement and native decorations to
the compositor. See the [platform validation record](docs/observations/new-window.md).

File → Save with Original Time preserves the destination's writable file times
through the normal save flow. Update File Time changes the named backing file's
modification time while retaining its bytes and the live editor state. See
[file-time behavior](docs/file-times.md) for platform fields and failure behavior.

New/Open/Close skip this confirmation for untitled documents shorter than 2048
UTF-8 bytes containing only ASCII space or bytes `0x09` through `0x0d` (including
the empty document). Explicit Save and Save As still save these documents.

New documents save as UTF-8 without a BOM. Open accepts UTF-8 with or without a
BOM and recognizes BOM-marked UTF-16LE; Open As UTF-16LE also handles files
without a BOM. Open As GBK explicitly decodes CP936. File → Encoding and More
(F9) choose the live document's destination format, including BOM-less UTF-16LE
and GBK, GB18030 (CP54936), and Big5 (CP950); Reload → With Encoding (F8)
reinterprets disk bytes. Encoding-only
changes participate in Save and unsaved-change decisions while retaining text
and history. Save and Save As write the chosen encoding and BOM, shown in the
status bar. [Encoding behavior](docs/encoding.md) covers empty files and native
ANSI conversion. GBK character loss offers
OK/Cancel. Accepting writes replacement bytes while retaining the editor text and
establishing its save point. Newline insertion defaults to CRLF;
loaded files use their detected newline style for Enter, while existing CRLF,
LF and CR bytes remain intact when saved.
Selecting a shell scheme for a fresh empty document with no Undo actions uses
UTF-8/LF. Existing BOMs remain intact; an empty document with edit history keeps
its encoding and newline mode.

Automatic language selection uses these extensions, ignoring ASCII case:

| Language | Extensions |
| --- | --- |
| C/C++ | `.c`, `.cpp`, `.cxx`, `.cc`, `.h`, `.hpp`, `.hxx`, `.hh` |
| Python | `.py`, `.pyw`, `.pyi` |
| JSON | `.json` |
| Bash | `.sh`, `.zsh`, `.bash`, `.in` |
| C Shell | `.csh`, `.tcsh` |
| m4 | `.m4`, `.ac` |
| JavaScript / JSX | `.js`, `.jse`, `.jsm`, `.cjs`, `.mjs`, `.qs` / `.jsx` |
| TypeScript / TSX | `.ts`, `.cts`, `.mts`, `.ets`, `.osts` / `.tsx` |
| Web Source Code | `.html`, `.htm`, `.shtml`, `.xhtml`, `.asp`, `.aspx`, `.asax`, `.ascx`, `.cshtml`, `.vbhtml`, `.razor`, `.jsp`, `.jspf`, `.jspx`, `.rhtml`, `.vue`, `.hbs`, `.svelte`, `.mht`, `.htd`, `.htt`, `.hta`, `.htc`, `.cfm`, `.tpl`, `.jd` |
| XML Document | `.xml`, `.xsl`, `.xslt`, `.xsd`, `.dtd`, `.rss`, `.svg`, `.xul`, `.axl`, `.rdf`, `.xaml`, `.resx`, `.plist`, `.pom`, `.mm`, `.xrc`, `.fbp`, `.wxml` |
| Rust | `.rs` |
| Go | `.go`, `.gop`, `.ql` |
| Lua | `.lua`, `.wlua`, `.nse`, `.luadoc`, `.luax` |

HTML and XML share the original HTML lexer. The Scheme menu and status expose ASP
VBScript/JavaScript, ASP.NET C#/VB, JSP/JSPX, SVG, XML Schema, XSLT, DTD and Property
List dialects. Embedded JavaScript and CSS use the original offset styles and
region-specific comment commands. HTML dialects share the `[Web Source Code]`
tab section (2/2 spaces, independent); XML dialects share `[XML Document]`
(4/4 tabs, global inheritance).

For unmatched filenames, the original 255-byte prefix guess recognizes XML
declarations, document types, HTML tags and other markup. Configuration files
accept XML declarations. `[Settings2] NoHTMLGuess` disables the general markup
guess while retaining declarations, document types and a leading HTML tag; the
load-only setting stays in its original INI location. Known unrelated extensions
retain their filename association. Document-type and server-page language
directives use the original larger 3999-byte header window.

For unmatched filenames, Automatic also recognizes the original Python, shell and Node.js
interpreter line forms, including direct paths and the original `env` and `winpty`
token handling. Shell dotfiles (`.bash*`, `.profile`, case sensitive), `mozconfig`,
`APKBUILD` and `PKGBUILD` select Bash. A trailing `.in` first checks the underlying
filename association. Known filename associations take precedence; languages whose lexer has not
been delivered retain the Plain Text fallback. The original `[Styles] AutoSelect`
and `[Settings2] NoCGIGuess` settings control this selection. It runs on Open and
explicit Automatic selection, without changing text or history; ordinary edits
retain the selected language. Other unmatched content uses Plain Text. The Scheme menu offers
Automatic, Plain Text and each supported language. A manual choice applies to the
current document. New and opening a different supplied path restore Automatic.
Reopening the current path retains its effective scheme, caret/anchor and view,
using the original line/column fallback when changed bytes move the caret far from
its saved line. Windows recognizes case variants before load-path normalization;
other native path spellings follow ordinary Open. First Save and Save As
offer the current scheme first, the other supported types, and All Files last.
A specific type chooses that language even when the filename differs; All Files
restores Automatic and resolves from the filename. For an untitled document whose
current and selected types are both Plain Text (the default), the filename also
selects the language. Named Plain Text documents keep the selected Plain Text type.
These rules also apply to unchanged extensions and identical paths. Ordinary Save
retains the current choice; failed or cancelled file operations retain it too. The status bar
shows the effective language and an explicit override alongside encoding/BOM.
Highlighting continues to follow the selected language above 32 MiB. Select Plain
Text to use the existing no-lexer path. See the
[original scheme behavior validation record](docs/observations/original-scheme-README.md).
JavaScript/TypeScript fixture comparisons and workflow checks are described in the
[JavaScript validation record](docs/observations/javascript-README.md).
Rust, Go and Lua use their original dedicated lexers and inherit global tab
settings by default, retaining independent four-column tab/indent defaults.
Their original captures and native workflow matrix are documented in the
[Rust/Go/Lua validation record](docs/observations/syntax-native-languages-README.md).

Highlighting uses the pinned original lexers, consumed keyword lists and default
light syntax colors while retaining the native font and point size. Typing and
Undo/Redo update syntax through the core's incremental styling lifecycle. The
`tools/update_schemes.py --source <original-checkout>` command reproduces the
full scheme tables from the pinned original checkout. Generated headers record
revision, source hash and license, preserving completion-only groups and original
attributes. Only lexer-consumed lists are installed in the original lexer.
The historical per-language commands remain aliases; markup embedded style
offsets are reproduced by `tools/update_markup_schemes.py`.

C/C++, Python, JSON, Shell Script, HTML, XML, JavaScript/JSX, TypeScript/TSX, Rust, Go and Lua support code folding. JavaScript
and TypeScript use the original shared JavaScript lexer with separate keyword lists
and four dialect properties. Their tab settings default to two spaces; JSX shares
the JavaScript section and TSX shares TypeScript. Click a header's margin marker to
collapse or expand its block; View → Toggle Folds → Collapse All and Expand All include nested
blocks. These commands change visibility without editing text or history. Find
reveals the complete matched range, including hidden intermediate lines, while
leaving unrelated folds collapsed. Replacement still searches the full document.
See the [syntax and folding validation record](docs/observations/syntax-README.md).

Microsoft Pinyin human validation passed; see the
[IME validation record](docs/evidence/issue-13/README.md). Linux IME remains
deferred. More syntax languages, advanced editing, settings migration and release
packaging remain future work. See the
[phase 1 spec](https://github.com/1298345095/wxnotepad/issues/8),
[search/replace spec](https://github.com/1298345095/wxnotepad/issues/21),
[syntax/folding spec](https://github.com/1298345095/wxnotepad/issues/30) and the
[full-application resource observations](docs/observations/issue20-README.md).

## Code structure

See the [code structure and evolution plan](docs/architecture.md) for module
responsibilities, state ownership and future feature placement. The source is
organized into app, document, search, syntax and editor modules, built as runtime,
feature and application-shell libraries.

## Build and run

GitHub Actions build and test Windows and isolated Linux X11/Wayland, check
formatting/static analysis, and run CodeQL. See [CI workflows and local checks](docs/ci.md)
for triggers, cached dependencies, downloadable builds and retained evidence.

For opt-in native input, clipboard and shutdown tracing, see
[native diagnostics](docs/diagnostics.md). Ordinary builds exclude tracing.

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

Run Windows regressions serially on the normal, unlocked input desktop. For a
local run, retain each attempt in a fresh report directory:

```powershell
python -X utf8 .github/scripts/ci.py test --backend windows --cmake-dir build/cmake --report-dir .ci/reports/local-windows-01
```

The runner records desktop names and rejects alternate desktops. CTest keeps its
per-test timeouts and writes JUnit at completion; there is no extra ten-minute
batch cutoff. These tests replace the shared clipboard and can focus their own
windows. Choose a different report directory for each subsequent attempt.

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

Search tests exercise native dialog controls and commands, explicit UTF-8 match
ranges, regex captures, zero-length progress, editing history and independently
read saved bytes on Windows, private X11 and private Wayland displays.
See the [search validation record](docs/observations/search-README.md) for final
regression, review and static-analysis results.

Format tracked C++ files under `src/` and `tests/` with clang-format **23.1.2**
and the repository's `.clang-format`. The configuration also parses with the
installed Linux clang-format 18.1.3; version 23.1.2 is the canonical formatter.

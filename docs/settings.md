# Original settings lifecycle

The application owns one `SettingsStore` for its lifetime. It loads once before
creating the frame and applies the stored values to the existing editor. Feature
modules do not read configuration files. Later settings work adds original fields
to this same store and applies them through the responsible feature module.

`[Settings2] DateTimeShort` and `DateTimeLong` are original load-only `wcsftime`
templates for Time/Date (Short Form) and Time/Date (Long Form). Missing or empty
values use the host-local time without seconds, one space, and the host short or
long date. Quoted values retain their contents. The original 256-WCHAR input
buffer admits at most 255 UTF-16 code units, including on Linux; explicit and
exit saves preserve the original fields in place. They do not create missing
template keys. See [date/time insertion](observations/original-date-time.md) for
the platform locale contract and verification.

The first slice restores `[Settings]` fields `ShowLineNumbers` (default `1`),
`WordWrap` (default `1`), `ViewWhiteSpace` (default `0`) and `SaveSettings`
(default `1`). Missing or invalid boolean values use the built-in default;
Notepad4's boolean parser accepts a leading `0` or `1`. There is no second
configuration overlay. An existing INI takes precedence over a distribution's
`Notepad4.ini-default`, which seeds a missing INI; built-in defaults fill missing
fields. This is a partial original-settings implementation, not complete settings
or appearance import.

Automatic language selection also loads `[Styles] AutoSelect` (default `1`) and
`[Settings2] NoCGIGuess` (default `0`). AutoSelect controls filename selection and
first-line interpreter guessing; NoCGIGuess disables the interpreter guess while
retaining known extension selection. They use the same original boolean parser
and store lifecycle. Save Settings Now and enabled exit saving persist AutoSelect
in `[Styles]`; NoCGIGuess remains an original load-only flag, preserved in place.
`NoAutoDetection` does not disable original shebang guessing.

`FixLineEndings` and `FixTrailingBlanks` both default to `0`. Their original
options are in File → Line Endings → Default: Ensure consistent line endings when saving,
then Strip trailing blanks when saving. A normal writable Save/Save As first
normalizes to the current EOL mode, then removes only trailing spaces and tabs
throughout the document, ignoring the selection. The operations retain the EOL
mode, do not append a final newline and remain separate undo actions. Read-only
documents bypass cleanup. A cancelled filename dialog or failed destination
preparation does not edit text; cancellation at the encoding warning or a later
write failure retains the cleanup edits and undo history. Safe replacement still
protects existing destination bytes. The same settings lifecycle loads and saves
both fields and omits their default values.

`ZoomLevel` uses the original percentage default of `100`, clamped to `10` through
`500` when loaded. Decimal prefixes (for example `175%`) follow the original
integer parser. Zoom extends this same settings lifecycle: disabled exit saving
retains the stored value, Save Settings Now writes the current value, and the
default `100` is omitted. It scales the existing native point-size font through
the original core and does not replace the base font size or DPI handling.

`HighlightCurrentLine` defaults to `12`: Outline Frame with Highlight Subline.
The original integer-prefix parser takes `value % 10`, clamped to 0–2,
for No Highlight, Background Color and Outline Frame; values at least 10 enable
the independent subline option. Save packs mode + 10 * subline and omits 12.
The View submenu and Ctrl+Shift+I/F select background/frame without changing
the subline choice. Current-line styling uses the original default colors and
alpha under the text, with frame width scaled by window DPI and zoom. It follows
the existing explicit/exit-save lifecycle and survives document/language changes.

`[Settings] ShowBookmarkMargin` defaults to `0`; View > Bookmark Margin and
Ctrl+Shift+M switch shared manual/Find All bookmarks between the original vertical
green symbol and alpha-40 green line background. `ChangeHistoryMarker` defaults
to `0` and retains the original integer-prefix value on import and save. The
native toggle selects `3` (enabled plus margin markers) or `0`. Enabling from
disabled requires the original `CanUndo == false` gate and preserves pending
Undo/Redo actions. Both preferences use the existing explicit/exit-save lifecycle
and omit defaults. See [bookmark and change-history evidence](observations/bookmark-history.md).

`[Settings] ShowIndentGuides` and `MarkLongLines` default to `0`. View exposes
Indentation Guides (Ctrl+Shift+G), Long Line Marker (Ctrl+Shift+L), and Long Line
Settings. Guides follow the original language metadata: Plain Text and Python
use look-forward guides; C/C++ and JSON use look-both. The advanced load-only
`[Settings2] SimpleIndentGuides` flag defaults to `0`; enabling it selects real
indentation guides for every language. Saves preserve that flag in place.

`[Settings] LongLinesLimit` defaults to `80`, bounded to `0..4096` on load.
`LongLineMode` defaults to `1` (edge line), bounded to `1..2` (background).
The original guide foreground is orange; the edge-line foreground is yellow.
Background mode uses the native system 3D-light color. The existing native font,
zoom and DPI behavior remain in use.

New initializes the live boundary from the global limit. Open applies the
original `fill-column` file-variable override. Accepting Long Line Settings
updates both the live and global limit, remembers the mode, and enables the
marker. Cancel retains the previous state. Language changes retain the live
boundary; reopening reparses the file variables. Saving persists the global
limit through the existing explicit/exit lifecycle, omitting default values.

Column Wrap uses this live boundary for its initial Boundary. Its accepted value
is separately bounded to `1..512` and recalled until New or a successful Open
replaces the document. This session recall does not change the marker limit.

Find/Replace extends this store with the original `[Settings]` fields:

| Field | Built-in default | Behavior |
| --- | --- | --- |
| `SelectOption` | `15` | Bit `1` enables multiple selection; bit `2` enables selection seeding; bit `4` enables clipboard fallback; bit `8` enables selection and viewport restoration on Undo/Redo. Raw bits are retained. |
| `SaveFindReplace` | `0` | Loads/saves submitted query and replacement histories and packed search flags. |
| `FindReplaceOption` | `2` | Original low option bits and upper dialog-behavior bits, including No Find Wrap. |
| `FindReplaceFlag` | `0` | Original search flags and search-option bits; used only with SaveFindReplace enabled. |
| `MarkOccurrences` | `1` | Bit `1` enables automatic occurrence marking, `2` matches case, `4` requires a whole-word selection and `8` bookmarks matched lines. Raw integer bits are retained. |

The View menu exposes Mark Occurrences with Off, Match Case, Match Whole Word Only
and Bookmark Matched Line. Off toggles the enable bit and retains the other options.
Automatic occurrences and Find All share indicator 8 and bookmark marker 0, including
the original clearing and ownership transitions. Single-line nonempty selections
seed automatic marking without changing the Find/Replace query or history. Work
continues in the original measured 1 MiB chunks with a 100 ms time slot.

Settings → Other Settings exposes Remember Search Options. Settings → Select and
Edit Options exposes Copy Selection As Find Text and Copy Paste Buffer As Find
Text. Submitted searches retain their query/options through
New and Open. Closing the dialog discards unsubmitted edits; Ctrl+F/Ctrl+H switches
retain edited text, restore submitted options, and apply original seeding again.
Repeating the command for an already-open dialog retains its query control.
The explicit Switch to Find/Replace link retains edited text without selection
or clipboard seeding and focuses the query, including when switching to Replace.

Selection seeding accepts at most 1 MiB of selected document bytes and preserves
regex punctuation. Original `AddBackslashW` control escaping enables Transform
Backslashes for a non-regex query; a plain backslash stays unchanged when no
control character is present. A new dialog or Ctrl+F/Ctrl+H switch may use the
clipboard only when no usable selected text was imported and its option is on.
Clipboard line endings first follow the document's insertion EOL mode. Replace
focuses the replacement field when selected text was imported; clipboard and
stored-query fallback focus the query.

`[Recent Find]` and `[Recent Replace]` retain original quoted, numbered entries,
with exact comparison and at most 32 entries. A submitted nonempty value enters
history only below 2048 UTF-16 code units. Empty replacement retains the original
Last Replace Empty flag and suppresses replacement-history fallback. MRUs merge
on application exit when SaveFindReplace is enabled, independently of SaveSettings;
Save Settings Now writes flags/options but leaves MRU saving to that exit path.

`LineSelection` restores the original four modes: `0` None, `1` Visual Studio
(the built-in default), `2` Normal and `3` Old Visual Studio. It uses the original
integer-prefix parser without an added range clamp. Settings → Select and Edit
Options → Line Selection Mode changes the
same value, and Save Settings Now/enabled exit saving persist it while omitting
the default. None keeps empty-selection Copy/Cut as no-ops. The enabled modes
copy or cut the current line; Visual Studio modes tag it for insertion at the
destination line start, while Normal publishes stream text. Cut keeps the
expanded caret column on the next line, or the previous line in Old Visual
Studio mode. Explicit Ctrl+Shift+C/X line commands use the same tag option.

Save Settings On Exit is enabled by default. A disabled exit save writes only
`SaveSettings=0`, retaining the previously stored view choices. Save Settings Now
writes current choices even while exit saving is disabled. Default-valued fields
are omitted, as in the original. Other keys, sections and comments survive. Writes
use the original Unicode INI format (UTF-16LE with BOM); the reader also accepts
existing text INI files through the platform text converter.
If an existing INI cannot be read completely, settings writes fail without replacing it.

`[Settings] AutoInsertMask` defaults to `511` and uses the original integer-prefix
parser. Settings > Auto Completion Settings edits the original independent bits:
parentheses `1`, braces `2`, square brackets `4`, angle brackets `8`, double quotes
`16`, single quotes `32`, backticks `64`, space after comma `128`, and space after
comment `256`. Typed insertion uses the pinned language/style, escape, character
literal, generic-type and matching-delimiter rules. Tentative IME input and
multiple-selection notifications retain the original suppression. The maintained
core handles selection enclosure and overwrite typing. Toggle Line Comment reads
the same bit `256`. OK commits these controls and Auto Indent; Cancel commits
nothing. Other option bits remain intact. The complete mask is retained through reloads,
Save Settings Now and enabled exit saving; the default `511` is omitted on Save.

Tab settings use this same store. `[Settings]` defaults are `TabWidth=4`,
`IndentWidth=4`, `TabsAsSpaces=0`, `TabIndents=1`, `BackspaceUnindents=2`, and
`DetectIndentation=1`.
Tab widths range from 1 to 256; indentation widths range from 0 to 256, with 0
using the tab width. Backspace's original bit 1 unindents the line; bit 2 deletes
spaces to the previous indentation stop. Both choices are exposed in Tab Settings.

The original scheme sections are `[Text File]`, `[C/C++ Source]`,
`[Python Script]`, `[JSON Document]`, `[Shell Script]`, `[JavaScript]` and `[TypeScript]`. Bash, C Shell and m4
share the Shell Script section. All support `TabWidth`, `IndentWidth`,
`TabsAsSpaces`, and `UseGlobalTabSettings`. The first three default to 4/4, tabs,
and global inheritance, as does Shell Script. JSON, JavaScript and TypeScript default to 2/2, spaces,
and independent settings. JSX uses `[JavaScript]`; TSX uses `[TypeScript]`.
Tab Settings edits global, current scheme and current file settings. Scheme
inheritance takes precedence over stored scheme widths; current file overrides
take precedence over the effective scheme. The Tabs as Spaces command overrides
only that file option, so later language changes still apply scheme widths.
New resets file overrides. Successful Open initializes them from original file
variables and indentation detection. Language changes, first Save and
Save As apply the effective scheme while retaining file overrides. Preferences
do not rewrite whitespace or change selection, modification state or Undo/Redo.
Accepting Tab Settings immediately saves the current scheme section, as the
original dialog does, including when Save Settings On Exit is disabled. Global
preferences still follow the normal explicit/exit save rule. Cancelling the
dialog retains all prior values; disabling inheritance restores the values from
before the dialog opened.
Detect Indentation is exposed in Tab Settings and follows the same global
explicit/exit-save lifecycle. It applies on the next Open. The original first/last
512-byte file-variable windows, case-sensitive names, Emacs boolean/numeric
grammar, `enable-local-variables`, `NoFileVariables` and `NoEncodingTags` gates
are shared with encoding detection. Supported tab variables are `tab-width`,
`*basic-indent`/`*basic-offset`, `indent-tabs-mode` and `*tab-always-indent`.
One width supplies the other. Any recognized beginning variable prevents tail
fallback, including original non-tab variables and enabled encoding tags.

A complete tab/indent width and tabs/spaces set bypasses detection. Otherwise
the original detector samples up to 1 MiB, counts indentation deltas of 1–8 spaces
and leading tabs, and corrects the alignment space before Javadoc stars. The
lowest category wins ties; an ambiguous winner retains the original fallback.
A space winner overrides both widths and tab choice, including partial variables;
a tab winner overrides only the choice. Unset fields fall back to the effective
global/scheme settings. Language switches preserve file overrides; unrelated
Open and New do not inherit them. Detection changes neither text nor history.

`[Settings] AutoIndent` defaults to `1` and follows the same explicit/exit save
rules. Settings > Auto Indent and Auto Completion Settings share the option. Enter preserves the original
leading whitespace and applies the pinned delimiter, colon and block-comment
opener rules using the active file tab settings. These rules are shared by the
delivered schemes; they do not add Python keyword completion or comment-prefix
continuation. The port adapts `EditAutoC.cpp::EditAutoIndent` and the
`Notepad4.cpp::SCN_CHARADDED` gate from the pinned original revision.

Windows keeps the pinned original discovery rule: a resolved executable path beginning
with `C:\Program Files`, or containing `WinGet` or `hocolatey`, uses
`FOLDERID_LocalAppData/Notepad4/Notepad4.ini`. Other locations use
`Notepad4.ini` next to the executable. If the known-folder lookup fails, it falls
back to the executable directory.

Linux maps native installation prefixes `/usr/`, `/bin/`, `/opt/`, `/app/` and
`/snap/` to `$XDG_CONFIG_HOME/Notepad4/Notepad4.ini`, falling back to
`$HOME/.config/Notepad4/Notepad4.ini` when XDG_CONFIG_HOME is unset or relative.
Other executable locations retain the portable directory rule. Paths use Linux
separators; original section and setting names stay unchanged. No mode switch or
additional configuration format is introduced.

The pinned `FindIniFile` has no INI redirection or configuration-file command-line
override. Its `/f` positioning option means full area, not configuration selection.
This slice adds no settings command-line switch.

The behavior and field names come from pinned Notepad4 revision
`89b8b36e0ee2c23f73be922315f17261ccfbbe1a`, `src/Notepad4.cpp`:
`FindIniFile`, `LoadSettings`, `SaveSettings`, and `SaveSettingsNow`, together with
`IniSectionParser::GetBoolImpl` in `src/Helpers.cpp`. The portable parser adapts
those rules without pulling Windows profile APIs into the editor runtime. The
upstream license remains in `licenses/Notepad4.txt`.

Search feedback reads the original `[Suppressed Messages]` keys `MsgFindWrap1`,
`MsgFindWrap2` and `MsgNotFound` when presenting a message. Their default is `0`:
show the message with its suppression checkbox enabled. Value `1` returns OK
without showing it; `2` shows it with the checkbox disabled. Choosing suppression
writes `1` immediately, including when Cancel is selected or exit saving is
disabled. These writes preserve the other settings and sections.

`DefaultEOLMode` defaults to `0` (CRLF); `1` selects LF and `2` selects CR.
Values are clamped to that range, matching the original settings order, which
differs from Scintilla's numeric EOL order. Open counts CRLF pairs separately
from standalone CR and LF. It retains the configured default when that mode
ties for the largest count, including files with no line endings. Otherwise it
chooses the largest count, preferring CRLF and then LF on a tie.

`WarnLineEndings` defaults to `1`. Mixed endings show the original counts,
CRLF/LF/CR selector, Yes/No choices and Always Show preference after the file has
opened. No retains the mixed bytes and loaded save point. Yes converts through
the original core, retaining the selected insertion mode while making one
undoable edit. Save writes the current bytes; it does not normalize them again.
These fields use the same settings load/save lifecycle described above.

The warning defaults to No for a newly loaded unknown language when the encoding
detector classified the file as native ANSI. Recognized Plain Text and same-path
reopens retain the original Yes default. The original actual-Diff-lexer branch
remains outside the delivered language catalog.

Encoding selection adds the original `[Settings]` fields `DefaultEncoding`
(built-in UTF-8, omitted on Save), `SkipUnicodeDetection=1`, `LoadANSIasUTF8=1`,
`LoadASCIIasUTF8=1`, `LoadNFOasOEM=1` and `NoEncodingTags=0`. The original INI
encoding-number mapping is retained, including `936` for CP936 and the special
0–8 values; an explicit UTF-8 default is normalized to the omitted built-in
default. `[Settings2] NoFileVariables=0` controls the file-variable gate and is
retained in place. The same SettingsStore loads these options before the frame
creates its document session. See [encoding selection](encoding.md) for the
precedence and remaining codec catalog.

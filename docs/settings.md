# Original settings lifecycle

The application owns one `SettingsStore` for its lifetime. It loads once before
creating the frame and applies the stored values to the existing editor. Feature
modules do not read configuration files. Later settings work adds original fields
to this same store and applies them through the responsible feature module.

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

`ZoomLevel` uses the original percentage default of `100`, clamped to `10` through
`500` when loaded. Decimal prefixes (for example `175%`) follow the original
integer parser. Zoom extends this same settings lifecycle: disabled exit saving
retains the stored value, Save Settings Now writes the current value, and the
default `100` is omitted. It scales the existing native point-size font through
the original core and does not replace the base font size or DPI handling.

Find/Replace extends this store with the original `[Settings]` fields:

| Field | Built-in default | Behavior |
| --- | --- | --- |
| `SelectOption` | `15` | Bit `2` enables selection seeding; bit `4` enables clipboard fallback. Other bits are retained. |
| `SaveFindReplace` | `0` | Loads/saves submitted query and replacement histories and packed search flags. |
| `FindReplaceOption` | `2` | Original low option bits and upper dialog-behavior bits, including No Find Wrap. |
| `FindReplaceFlag` | `0` | Original search flags and search-option bits; used only with SaveFindReplace enabled. |

The View menu exposes Save Find/Replace, Copy Selection As Find Text and Copy
Paste Buffer As Find Text. Submitted searches retain their query/options through
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

Save Settings On Exit is enabled by default. A disabled exit save writes only
`SaveSettings=0`, retaining the previously stored view choices. Save Settings Now
writes current choices even while exit saving is disabled. Default-valued fields
are omitted, as in the original. Other keys, sections and comments survive. Writes
use the original Unicode INI format (UTF-16LE with BOM); the reader also accepts
existing text INI files through the platform text converter.

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

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

`LineSelection` restores the original four modes: `0` None, `1` Visual Studio
(the built-in default), `2` Normal and `3` Old Visual Studio. It uses the original
integer-prefix parser without an added range clamp. The Edit menu changes the
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

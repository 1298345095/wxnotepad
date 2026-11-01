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

Save Settings On Exit is enabled by default. A disabled exit save writes only
`SaveSettings=0`, retaining the previously stored view choices. Save Settings Now
writes current choices even while exit saving is disabled. Default-valued fields
are omitted, as in the original. Other keys, sections and comments survive. Writes
use the original Unicode INI format (UTF-16LE with BOM); the reader also accepts
existing text INI files through the platform text converter.

Tab settings use this same store. `[Settings]` defaults are `TabWidth=4`,
`IndentWidth=4`, `TabsAsSpaces=0`, `TabIndents=1`, and `BackspaceUnindents=2`.
Tab widths range from 1 to 256; indentation widths range from 0 to 256, with 0
using the tab width. Backspace's original bit 1 unindents the line; bit 2 deletes
spaces to the previous indentation stop. Both choices are exposed in Tab Settings.

The original scheme sections are `[Text File]`, `[C/C++ Source]`,
`[Python Script]`, and `[JSON Document]`. All support `TabWidth`, `IndentWidth`,
`TabsAsSpaces`, and `UseGlobalTabSettings`. The first three default to 4/4, tabs,
and global inheritance. JSON defaults to 2/2, spaces, and independent settings.
Tab Settings edits global, current scheme and current file settings. Scheme
inheritance takes precedence over stored scheme widths; current file overrides
take precedence over the effective scheme. The Tabs as Spaces command overrides
only that file option, so later language changes still apply scheme widths.
New and successful Open reset file overrides. Language changes, first Save and
Save As apply the effective scheme while retaining file overrides. Preferences
do not rewrite whitespace or change selection, modification state or Undo/Redo.
Accepting Tab Settings immediately saves the current scheme section, as the
original dialog does, including when Save Settings On Exit is disabled. Global
preferences still follow the normal explicit/exit save rule. Cancelling the
dialog retains all prior values; disabling inheritance restores the values from
before the dialog opened.
Indentation detection and file-variable parsing are separate work.

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

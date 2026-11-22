# Main menu hierarchy

The reference is Notepad4 `89b8b36e0ee2c23f73be922315f17261ccfbbe1a`,
`src/Notepad4.rc::IDR_MAINWND`. Delivered commands retain its top-level order:
File, Edit, Search, View, Scheme, Settings and Tools. Unimplemented commands
and empty menus are omitted.

File keeps New/Open/Save, File Mode/Revert/Reload, Encoding/Line Endings,
printing, containing-folder/file-time, history and Exit in separate original
groups. The existing explicit-codec Open commands remain available together
under Open As, next to Open. Exit uses the existing window-close workflow,
including unsaved-document handling.

Edit includes the existing Delete action after Paste. Complete Word is the
first Insert command, followed by a separator; Special starts with Character
to Hex. Both existing HTML/XML Tag entries remain available. Existing command
handlers, availability rules and shortcuts are preserved.

Scheme puts Use Default Code Style before language selection. The existing
Automatic override is retained. Delivered dialects follow the original Text
File, JavaScript, Web Source Code, Markdown, Shell Script and XML Document
families and their within-family order. Remaining schemes follow alphabetically.
Check items reflect the single document-language override across all submenus;
native radio groups would select a separate item within each submenu.

Settings starts with indentation, wrapping, completion and selection options.
Appearance contains Language at its end, as in the localized original menu.
File Change Notification precedes AutoSave and Other Settings; Remember Recent
Files precedes Remember Search Options. Advanced Settings (Windows) and saving
settings form the final group. View keeps default fonts and Caret Style in the
same group. Tools puts Restart between the save-before-tools preference and
the document-launch commands, separated as in the original.

`original_menu_hierarchy` checks the delivered hierarchy, separator boundaries,
all scheme choices across families, checked/disabled state, unchanged document
bytes/history during inspection, actual edits and Ctrl+D dispatch. Existing
completion, document/restart and packaged localization tests exercise commands
through the moved menu entries in English, Simplified and Traditional Chinese.

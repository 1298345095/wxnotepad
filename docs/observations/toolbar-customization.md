# Native toolbar customization

Implements [issue #253](https://github.com/1298345095/wxnotepad/issues/253)
against Notepad4 `89b8b36e0ee2c23f73be922315f17261ccfbbe1a`.

Settings → Appearance retains the original order: Show Menu, Show Toolbar,
Customize Toolbar, Auto Scale Toolbar, Use Large Toolbar, Show Statusbar.
Customization is disabled while the toolbar is hidden. The native dialog offers
available/current buttons, a reusable separator, Add, Remove, Move Up, Move Down,
Reset and Close. Changes apply immediately; Close retains them. The native lists,
buttons and mnemonics support keyboard operation and have translated accessible
names. Reset projects the pinned original default onto delivered commands,
including Delete and Exit. Print and Execute Document are available choices but
are absent from the original default.

A single command catalog joins original button identities to existing translated
menu commands. Artwork index is identity minus one; zero represents a separator.
The layout never stores generated wx menu IDs. Existing command handlers provide
enabled/checked state and toolbar tooltips retain their menu accelerators.
Repeated imported identities remain valid. The native toolbar synchronizes every
repeated instance because wx's ordinary UpdateUI lookup updates only the first
tool with an ID.

`ToolbarButtons` follows the original bounded numeric-prefix parser: clamp numbers
to the original 0–27 table, stop at the first malformed token, and omit actions
not delivered by this host. An empty/invalid layout falls back to the delivered
default. Explicit separators remain in custom layouts. Saving retains the
original 64-item/160-character bounds and uses the existing settings lifecycle,
including its preservation of unrelated INI content.

`AutoScaleToolbar` remains an integer with default 96. Auto Scale maps any
nonzero value to 0 and maps 0 to 96. Use Large adds 48 when the value is at least
96 and below 192; otherwise it selects 96. The usual cycle is 96 → 144 → 192 →
96. Imported nonstandard and negative values retain the same arithmetic and
menu-check rules.

With automatic sizing enabled, toolbar DPI is the current display DPI plus the
preference's excess above 96. The original 16-pixel base is scaled to that DPI
and rounded up to an even pixel dimension. With automatic sizing disabled, the
original 16/24/32/40/48-pixel resource selection is retained. The native bitmap
bundle supplies the same physical size to GTK and Windows while retaining
original alpha and resolution variants. Existing light/dark disabled artwork,
asynchronous GTK clipboard offers and editor HiDPI fixes remain in use.

Rebuilding tools retains the native toolbar widget, its clipboard subscription
and shared command routing. It recalculates bar/editor geometry while preserving
visibility preferences and fullscreen placement. Toolbar operations do not edit
document text, selection, current encoding/BOM/EOL, save point or Undo/Redo.

`original_toolbar_customization` and its localized/configuration variants use
the actual menus, native dialog controls, physical shortcuts, toolbar activation
and settings save/reload. Their expectations include independently observed
original default identities, malformed-prefix handling, out-of-range clamping,
the numeric size cycle, Reset, separators and reordered Undo behavior. The
external ticket archive is `original-controls-implementation-2026-11-22/253/`.

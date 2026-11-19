# Manual Save Backup

File → **Save Backup** is always available and sits between Save As and Save
Copy. It has no shortcut or destination dialog. An empty document produces no
file. A named document first tries the current directory; a new document uses
the application data folder. A backup does not change the current document's
name, encoding, save point, selection, history, Recent Files or file watching.

The command adapts the manual `FileSaveFlag_SaveAlways` branch of
`AutoSave_DoWork` and `AutoSave_GetDefaultFolder` from Notepad4 at
`89b8b36e0ee2c23f73be922315f17261ccfbbe1a`. See `licenses/Notepad4.txt`.
The default directory is `Notepad4/AutoSave` under Windows LocalAppData; if
Windows cannot resolve LocalAppData, use the executable's `AutoSave` child.
Linux maps that directory to `$XDG_DATA_HOME/Notepad4/AutoSave`, or
`$HOME/.local/share/Notepad4/AutoSave`. Portable INI placement is independent.

The original uses exclusive creation and never overwrites a backup. If creating
the sibling fails, it tries the same leaf name in the fallback directory and
adds `AutoSave for `, the full source path, and the active document EOL. A write
failure removes the newly created incomplete file and does not retry elsewhere.
Creation or write failures remain silent. There is no scheduled rotation.

The typical name is `name.ext YYYYMMDD HHMMSS.ext`, based on local time. The
unnamed prefix is literally `Untitled`, and the default extension is `.bak`.
The original scans for the first dot in the full path. The captured
`parent.dot/file.txt` case therefore fell back to the bare `file.txt` leaf. A
space in the extension also affects the original's last-space suffix removal:
`file.my suffix` retained milliseconds and PID and ended in `.my.my suffix`.
The counterpart preserves these exceptions to normal manual naming.

The writer saves the live buffer's bytes, with no BOM or ordinary save cleanup.
An ANSI document's Windows-native buffer is restored from the migrated editor's
Unicode text and preserved native bytes. OEM, explicit legacy encodings and
Unicode use UTF-8 editor bytes. Named fallback metadata uses the ANSI code page
only for native ANSI; otherwise it is UTF-8. Mixed line endings and embedded NULs
remain in the payload. Untitled fallback adds no metadata.

`tests/reference_capture/capture_save_backup.py` runs the pinned Windows executable
on a private desktop. It waits for the intended document's title, invokes the
native File menu command, archives the raw backup files and JSON, and removes
only its own files from the Windows fallback directory. It uses no clipboard.
Run it according to `docs/ci.md` using the tracked `ci.py probe` wrapper and a
fresh output folder:

```text
python -X utf8 .github/scripts/ci.py probe --report-dir F:/evidence/save-backup-probe --timeout 90 -- python -X utf8 tests/reference_capture/capture_save_backup.py F:/reference/Notepad4.exe F:/evidence/save-backup-original
```

The November 18, 2026 capture archived the menu labels, original snapshots, raw
backup files, local timestamp bounds, and binary SHA-256 under
`native-workflows-implementation-2026-11-18/232/original-02`. The supplemental
`original-03` adds GB18030, Big5, DOS437, and both byte orders of BOMless UTF-16.
The native counterpart test `original_save_backup` checks the recorded bytes
and naming through the actual File menu on Windows, X11 and Wayland.

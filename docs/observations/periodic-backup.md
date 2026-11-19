# Periodic AutoSave backups

Settings → **AutoSave Settings** provides periodic enablement, seconds, manual
backup deletion, Open Folder and OK/Cancel. Periodic backups default off; the
original interval is five seconds. OK updates the live repeating timer. Cancel
retains the previous options and timer. Seconds use the original float conversion
and truncation to milliseconds, including fractional milliseconds becoming zero.
Zero disables scheduling. Defined negative conversions retain the original DWORD
bits; native timer intervals use the original Win32 maximum of 2147483647 ms.
Non-finite or out-of-range conversions remain in the dialog for correction.

`AutoSaveOption` and `AutoSavePeriod` use their original INI names and the normal
Save Settings/Save Settings Now lifecycle. Only bit 1 (periodic) and bit 8 (manual
deletion) have active controls. Imported suspend (2), shutdown (4), overwrite (16)
and other option bits survive editing and saving. **Suspend/shutdown backups and
current-file overwrite are not delivered.** Every periodic output is a separate
backup, even with an imported overwrite bit.

The implementation adapts `AutoSave_Start`, `AutoSave_Stop`, `AutoSave_DoWork`
and `AutoSaveSettingsDlgProc` at Notepad4 commit
`89b8b36e0ee2c23f73be922315f17261ccfbbe1a`; see `licenses/Notepad4.txt`.
The timer starts on editing and does not restart on every keystroke. Changing the
interval resets it. Empty/unmodified documents and already-backed-up revisions
are skipped. Successful manual Save Backup also marks its revision backed up;
failed backups leave that revision eligible for a later period.

Automatic names append ` YYYYMMDD HHMMSS mmmPID` and the original extension.
Manual naming, exclusive creation, sibling/fallback order, native ANSI versus
UTF-8 bytes, metadata EOL and incomplete-write deletion share the delivered
[manual writer](save-backup.md). The folder is `Notepad4/AutoSave` under Windows
LocalAppData or Linux XDG data home, with the documented executable/home fallback.
Open Folder invokes the platform's file manager.

Only successfully created periodic files enter the six-entry managed list.
A seventh success removes the oldest from tracking and deletes it unless manual
deletion is selected. Manual backups, earlier lifecycle files and other processes'
files never enter that list. Creating or rotating a backup does not change the
live path, encoding, save point, selection, folds, history, Recent Files or watcher.

| Successful action | Timer/revisions | Managed files |
| --- | --- | --- |
| Ordinary Save, Save As, first Save or Save Copy | Stop/reset | Delete unless manual deletion selected |
| Same-file load/reload | Stop/reset | Delete unless manual deletion selected |
| Different-file open or New | Stop/reset | Keep; release tracking |
| Explicit Discard | Stop/reset | Delete unless manual deletion selected |
| Clean close/end-session | Stop/reset | Keep; release tracking |
| Disable periodic checkbox | Stop/reset | Delete unless manual deletion selected |
| Enabled checkbox with zero period | Stop; revisions retained | Keep tracked |
| Manual Save Backup | Timer retained; mark revision | Manual output stays untracked |

Cancelled or failed file actions do not apply successful-action transitions.
An explicit Save or Discard already completed while resolving a later action
retains its own transition.

`original_periodic_backup` drives the actual menu/dialog/timer and reads disk bytes
independently. It covers continuous edits, interval reset, period boundaries,
rotation, retention, manual revision interaction, cancelled/failed actions,
reload/New/open/discard, untitled output and real Linux short-write retry.
`original_periodic_backup_process` uses the real development bundler and shown
application processes, native edits, Save/close/restart ownership and localized
English/Simplified Chinese/Traditional Chinese dialogs with measured native layout
screenshots. Windows captures temporarily raise only the verified owned dialog to
topmost and restore its prior state. Existing manual-backup, watcher and document
lifecycle suites remain applicable.

The November 18, 2026 isolated original capture recorded actual 120 ms periodic
timers, unchanged-revision skipping, seventh-file rotation, manual retention and
ordinary-save cleanup under
`next-native-file-tools-2026-11-18/246/original-02`, including complete bytes and
original executable SHA-256. Reproduce with the bounded `ci.py probe` entry point
from `docs/ci.md`, running `tests/reference_capture/capture_periodic_backup.py`
against a copied pinned original executable and a fresh output directory.

# Responsibility and ownership refactor

Baseline: `4fa3240a8a572c42b4e6e1e821d20ac1cea23ec2`.

The review followed command entry points, state ownership and file/format
dependencies across `app`, `document`, `editor`, `settings` and `syntax`.
The implemented changes concentrate behavior that previously required knowledge
of several unrelated concerns in one class or translation unit.

## Findings and changes

| Area | Finding | Implemented responsibility |
| --- | --- | --- |
| Frame commands | One 2,670-line translation unit mixed file, editing, presentation, settings, shortcut and native-dialog dependencies. | The composition/file-command unit is 593 lines. Editing, view, settings and keyboard bindings have their own implementation files. Zoom dialog construction joins the other native dialogs. |
| Wrapping state | Startup, New/Open, settings capture, toggles and the dialog each knew how to distinguish the window preference from a file's `truncate-lines` override. | `WordWrapState` owns preference capture, override application, wrap symbols and accepted choices. The dialog edits `WordWrapOptions` instead of copying the complete application settings. |
| Document saves | `DocumentSession` mixed document identity and encoding decisions with temporary files, replacement, OS permissions, timestamps and backup naming/fallback. | `DocumentFiles::SaveTransaction` owns prepared writes and cleanup. `DocumentBackups` owns backup publication. The session retains save/load ordering and commits document state after success. Its implementation falls from 1,063 to 777 lines. |
| Automatic typing | The Core notification handler implemented typing policy while related completion settings, callbacks and state lived on its host. | `TypingAssistance` owns automatic indentation, paired insertion, tag closure and completion notification state. Core forwards notifications after recording changes and retains native adaptation and exception handling. |
| Style persistence | `SettingsStore` combined configuration lifecycle with style-specific parsing, defaults, delta serialization and import/export. | `StyleProfileIO` owns those format operations; `SettingsStore` retains file identity, theme transitions and saved snapshots. Its implementation falls from 775 to 644 lines. |
| Syntax selection | The static language catalog also implemented filename, favorite, extension, shebang, markup and INI detection precedence. | `syntax_detection.*` owns automatic detection and consumes the catalog through `SyntaxCatalog()`/`SyntaxFor()`. The catalog falls from 1,473 to 1,153 lines and keeps language metadata. |

The line counts describe where maintainers now navigate. The state and operation
ownership changes are the reason for the new modules. The frame remains the
window's composition point, consistent with the existing architecture.

## Preserved transition rules

### File transactions

`Prepare` reserves a sibling file and checks destination access. `Commit` writes
all bytes, flushes/closes the sibling, replaces the destination and applies the
selected timestamp policy. Destroying an uncommitted transaction removes its
sibling.

The session still chooses the preparation point. Save Copy creates a new empty
destination before cleanup and encoding confirmation. Original-time saves
capture timestamps on their first preparation. Save cleanup checks write access
before changing editor text. The destination is supplied at both preparation and
commit, preserving the existing lookup after modal encoding confirmation.

Backup publication falls back to the platform backup directory only after a
failure to create the sibling backup. A partial write is cleaned up and returned
as a failure at that location.

### Typing lifetime and notification order

Typing state is constructed before Core and outlives Core's shutdown. The
notification order remains: record document changes, process typing/completion,
then notify ordinary UI state. Completion acceptance consumes its notification.
The original ordering of newline indentation, tag/member access, paired insertion
and typed-word completion remains in one module, covered by the existing
native input and allocation-failure regressions.

### Window wrapping preference

Before a document transition, the live mode becomes the remembered preference
only when the current file has no override. Successful Open applies the new
file's optional override. New restores the remembered preference. Saving settings
while a file override is active writes the remembered preference. Explicit
toggles and accepted dialog choices update it. The editor owns its remembered
nonzero mode and rendering geometry.

## Verification

New behavior-focused coverage:

| Test | Contract |
| --- | --- |
| `feature_session_contracts` / `document_files.cpp` | Six real-file scenarios: cancellation cleanup, complete binary replacement, destination replacement failure, first-preparation timestamp retention, destination changes after a decision and Save Copy early creation. |
| `syntax_detection_contracts` | Twenty-four filename/content precedence, override, family/dialect and bounded-sniffing checks through the detection interface. |
| Existing word-wrap settings regression | Adds cancelled and failed Open cases while a file override is active, before checking persisted preference and New/reload behavior. |
| Existing dialog-layout regression | Uses the focused wrap-options value and continues exercising the real native dialog. |

Existing save/copy/backup/time, style import/theme, automatic insertion,
completion/IME, menu/shortcut and wrap regressions remain registered. New CTest
registration is appended after the existing groups so existing private-display
shard ordering is retained.

Local verification uses the canonical formatter, Python syntax checks,
the helper test suite and the pinned gettext extraction/catalog checks described
in `docs/ci.md`. Native compilation and Windows/X11/Wayland regressions use the
repository's configured GitHub Actions jobs; their exact results belong to the
PR check records.

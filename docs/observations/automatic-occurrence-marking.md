# Automatic occurrence marking

Issue #155 restores the pinned Notepad4 `89b8b36` Mark Occurrences behavior.
`OccurrenceMarking` ports `EditMarkAll` as one editor-owned state shared by
automatic selection marking and explicit Find All. Indicator 8 and bookmark
marker 0 remain shared with search and manual bookmark commands.

The original default integer is 1. Off toggles bit 1; case, whole-word and
matched-line bookmarks use bits 2, 4 and 8. Turning Off retains and disables the
other options. Settings import and save retain signed and unknown integer bits,
including original integer-prefix parsing and default omission.

The needle is the original selected text, with its main-selection single-line
and whole-word eligibility checks. It does not seed the Find/Replace request or
its histories. Empty or multiline selections clear marking; whitespace and
Unicode selections remain eligible. Case-insensitive needles without any
case-sensitive character use the pinned Unicode 17 sensitivity table and the
original exact-case cache flag. This matters when toggling Match Case: unchanged
numeric needles retain manually added bookmarks rather than resetting marking.

Updates follow the original UpdateUI selection/content flags, including its
one-update exemption after successful bookmark navigation. Work starts
immediately, searches line-aligned measured 1 MiB ranges, aggregates the original
240-range batches and continues through the event loop with a 100 ms time slot
and the original adaptive increment. The original MarkAll path has no additional
file-size or lexer-highlighting gate. Document replacement stops pending work;
the timer handler checks the pending flag before continuing. Focus and syntax
changes retain the document's marks and matching work.

## Independent original observations

Raw captures and scripts are preserved at
`E:/NewNotepad4/wxnotepad-evidence/editor-navigation-controls-2026-11-07/155/`.
The designated baseline executable is
`E:/NewNotepad4/wxnotepad/build/notepad4-baseline/Notepad4.exe`.
Its adjacent `notepad4-baseline.log` identifies the original vcxproj build; the
source checkout is pinned at `89b8b36`. The build log does not stamp a commit.

- `original.json` covers native menu states, whitespace, multiline, reversed,
  Unicode, rectangular/multiple selections, edits, Undo, caret and new-document
  transitions, bookmark navigation and numeric-needle cache behavior. For
  `one ONE stone one\nONE one\n`, selecting bytes 0–3 marks
  `[0,3), [4,7), [10,13), [14,17), [18,21), [22,25)` by default. Case plus
  whole-word options leave `[0,3), [14,17), [22,25)`.
- Its numeric native paint probe compares an unselected occurrence with Off
  using GDI pixels, without screenshots or clipboard access. The original
  recorded 1276 changed pixels in the sampled rectangle on this host.
- `original-find-all.json` drives the actual Find dialog and native Find All
  button. A FindNext selection takes automatic ownership of the indicator while
  retaining Find All bookmarks. A later Find All without bookmarks retains those
  lines after that ownership transition. These captures justify the updated
  assertions in the existing `find_all` fixture.
- `original-settings.json` captures default, 0, 1, 2, 15, 31, -1, `14suffix` and
  invalid imports, actual native Off/option menu states and explicit saved values.

## Verification

The approved seams are real editor commands/document state, native menu and mouse
events, numeric native paint observations and independent app processes with
independently read settings bytes. `occurrence_marking` covers those editor/native
seams and a 3 MiB document; `occurrence_settings_process.py` covers seed import,
reload, explicit/exit save, retained integer bits and unrelated configuration.

The evidence directory preserves red and green logs for native command absence,
shared Find All ownership, preference import, native result invalidation,
bookmark navigation and case-invariant query caching. Initial command absence
was recorded by a failing assertion; the fixture's exit-status reporting was
then corrected before the subsequent red runs, which exited 1.

Targeted Windows CTest command:

```text
ctest --test-dir .ci/native-windows -C Release -R "original_occurrence_|original_find_all" --output-on-failure
```

The integration owner runs the final Windows/private X11/private Wayland suites,
canonical formatting, clang-tidy and independent Standards/Spec review.

# Original automatic word completion

Issue #192 extends the manual collector and native popup delivered by #191.
The reference is Notepad4 `89b8b36e0ee2c23f73be922315f17261ccfbbe1a`:
`Notepad4.cpp` character-added/deleted/selection notifications,
`EditAutoC.cpp::EditCompleteWordCore` and `EditCompleteWord`, and
`Dialogs.cpp::AutoCompletionSettingsDlgProc`.

`tests/automatic_completion_reference.h` freezes independent original native
input observations. Reproduce the raw JSON on Windows with:

```
python tests/reference_capture/capture_automatic_completion.py ORIGINAL_EXE EVIDENCE_DIRECTORY
python tests/reference_capture/capture_completion_dialog.py ORIGINAL_EXE EVIDENCE_DIRECTORY
```

The capture programs create their own portable reference host. Run them on an
owned private desktop; they send messages only to their reference process and
never read or write the clipboard. Raw JSON belongs outside the checkout.

The frozen cases cover document candidates, function suffix/caret placement,
Enter and Tab disabled, Space confirmation, deletion, member-arrow handling,
read-only typing, LaTeX fallback and large-list refresh. The original preserves
an 80-item popup when typing leaves 70 matches, rebuilds it when only 20 remain,
and restores 80 after deleting the character. Disabled Enter inserts CRLF;
disabled Tab inserts a tab, or invokes the existing LaTeX input method when
enabled. Function-confirmation Undo restores the selected prefix. Read-only
typing can still offer a popup while leaving document bytes unchanged.

The native Auto Completion Settings dialog commits on OK and leaves preferences
untouched on Cancel. Original lower bounds are eight visible rows, one word
character, zero number characters and 50 ms scanning timeout. Zero number length
disables number completion. Invalid timeout text retains its previous value;
other numeric fields accept a decimal prefix and clamp invalid input from zero.
Fill-up punctuation retains ASCII punctuation in order, including duplicates.

Persistence uses the existing settings lifecycle and original keys:
`AutoCompleteOption`, `AutoCompleteScope`, `AutoCScanWordsTimeout`,
`AutoCVisibleItemCount`, `AutoCMinWordLength`, `AutoCMinNumberLength`,
`AutoCFillUpMask`, and `AutoCFillUpPunctuation`. The existing Ignore Case and
LaTeX settings remain on their shared menu/settings path.

`original_automatic_completion` drives native input and checks frozen candidates,
text, selection and Undo/Redo. It also covers scopes, document scanning filters,
language/document replacement, Escape, read-only and multiple selections. Its
12 MB scanning case logs elapsed time with the original 50 ms scan timeout; it
introduces no product performance threshold. `original_auto_completion_settings`
checks native controls, Cancel/OK, normalization, independent save/reopen, and
control visibility at the active font/DPI. Completion popup and font tests cover
the existing nonactivating popup and text metrics separately.

Microsoft Pinyin preedit remains a separate human check coordinated on the final
integrated application. Linux IME validation remains deferred.

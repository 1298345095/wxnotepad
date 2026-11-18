# Search continuation

Issue [#230](https://github.com/1298345095/wxnotepad/issues/230) restores the
Search commands from Notepad4 `89b8b36e0ee2c23f73be922315f17261ccfbbe1a`.
Save Find Text and Replace Next occupy the original Search menu positions.
Find Next Selection and Find Previous Selection have shortcuts only.

An explicit selection command accepts fewer than 1 MiB of selection bytes.
The existing dialog prefill accepts exactly 1 MiB as well. Explicit saving
and selection navigation may select the word or fall back to the indented
line. First-use regular Next/Previous only considers existing selected text
when the original selection preference permits it. Saving clears regex and
backslash transform, leaves case, whole-word and wildcard options intact,
and does not add an MRU entry. Alt+F3 in the dialog refreshes its query;
the menu command and selection search preserve an open dialog's draft.

Read-in history does not make a replacement active. Even an empty replacement
becomes initialized when submitted from Replace. In a fresh session with the
default flags and no query seed, the original empty replacement initially
advances without editing. The dialog's Replace button and F4 do the same.
An empty replacement deletes a matching range after a nonempty submission,
a query seed, or a change to the search flags. The runtime initialization
state does not add a setting. With the original C++ regex option enabled,
Replace Next inserts once into a zero-width match and supports Undo and Redo.

The original fixture drives frame and dialog command IDs on a private Windows
desktop. It records public editor positions, literal text, dialog state and
feedback, menu order and the saved portable INI. It uses only the supplied
executable copy and does not access the clipboard. The native counterpart
regression `original_search_continuation` exercises native menus, keys and
dialog controls with the captured expected document and settings values.

```text
python -X utf8 .github/scripts/ci.py probe --report-dir F:/search-continuation/probe --timeout 120 -- python -X utf8 tests/reference_capture/capture_search_continuation.py F:/pinned-original/Notepad4.exe F:/search-continuation/original
```

The independent capture is retained in
`F:/wxnotepad-evidence/native-workflows-implementation-2026-11-18/230/`.

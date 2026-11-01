# Original search backslash transforms

Compatibility source: Notepad4 `89b8b36e0ee2c23f73be922315f17261ccfbbe1a`,
`src/EditEncoding.cpp::TransformBackslashes` and
`src/Edit.cpp::EditFindReplace::Prepare` / `EditFindReplaceDlgProc`.
The adapted code retains the BSD 3-Clause attribution in `licenses/Notepad4.txt`.

Transform Backslashes defaults to off. Checking it clears Regular Expression;
checking Regular Expression clears it. The checkbox remains enabled. Query
seeding and persistence use the same option supplied by the dialog lifecycle.

| Input | Prepared bytes |
| --- | --- |
| `\a`, `\b`, `\e`, `\f`, `\n`, `\r`, `\t`, `\v` | Bell, backspace, escape, form feed, LF, CR, tab, vertical tab |
| `\\` | One literal backslash |
| `\xHH` | One raw byte, including NUL and bytes above ASCII |
| `\uHHHH` | UTF-16 code unit converted to editor UTF-8; adjacent surrogate units retain the original pairing behavior |
| Unknown, incomplete, uppercase `\X` / `\U`, octal-looking forms | Retained literally |

Find, Previous, Find All, Replace, Replace All and In Selection prepare their
input through the same search-session path. The explicit byte lengths retain
embedded NUL and keep document positions in UTF-8 bytes independently of file
encoding. Literal transforms leave `$1` and `\1` literal; regex capture formatting
remains the original core's separate operation.

`tests/search_backslashes.cpp` uses the actual editor and native controls. It
checks multiline query positions, every control form, raw hex/NUL, Unicode and
surrogate forms, incomplete escapes, replacement bytes, selection ranges,
one-step Undo, unchanged/no-match history, Find All marks/bookmarks, and
independently read UTF-16LE disk bytes. The native dialog checks the default,
regex exclusion, literal mode and transformed replacement.

Implementation evidence is retained outside the source checkout at
`E:/NewNotepad4/wxnotepad-evidence/original-parity-implementation/issue-80/`.
`red-windows.log` records the missing transformed multiline query before the
implementation. Windows runs use the owned hidden desktop probe; Linux runs
use private X11 and Wayland sessions. No user desktop is switched.

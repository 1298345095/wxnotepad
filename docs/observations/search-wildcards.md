# Original wildcard search

Compatibility source: Notepad4 `89b8b36e0ee2c23f73be922315f17261ccfbbe1a`,
`src/Edit.cpp` `EscapeWildcards`, `EditFindReplace::Prepare` and
`EditFindReplaceDlgProc`; retained BSD-3-Clause provenance is recorded in
`licenses/Notepad4.txt`.

The shared search preparation translates `*` to `.*` and `?` to `.`. It escapes
only `.`, `^`, `$`, `\`, `[`, `]` and `+`, as the original does. Parentheses,
braces and `|` retain their literal meaning in the original built-in matcher.
Wildcard mode uses that matcher without the C++ regex flag. Its greedy matches
stay within a line, and `?` consumes one byte, including in UTF-8 documents.
The original whole-word flag is retained; the built-in regex path does not
enforce it. Match case continues to control its ASCII case matching.

Find Next/Previous, Find All, Replace, Replace All and Replace in Selection all
use the same prepared query and effective regex mode. This includes the original
replacement formatter: `$0`, `\0` and `$&` refer to the complete match. Regex and
wildcard checkboxes exclude one another.

`tests/search_wildcards.cpp` exercises the native controls and real editor state.
Its source-derived fixtures cover greedy/empty stars, question marks, literal
metacharacters, UTF-8 byte matching, case/word options, Find All indicators and
bookmarks, replacement formatting, selection replacement and exact Undo/Redo.
Validation logs live in the external ticket evidence directory.

# Original newline autoindent

Issue #67 adapts `src/EditAutoC.cpp::EditAutoIndent` and the character-added
branch in `src/Notepad4.cpp` from Notepad4
`89b8b36e0ee2c23f73be922315f17261ccfbbe1a` (license retained in
`licenses/Notepad4.txt`). The operation uses the existing core document and undo
history, after the core inserts the entire EOL. It preserves the original
`listType` exclusion and the CRLF condition that handles CR and skips LF.

Plain Text, C/C++, Python and JSON share the original delimiter, colon and
block-comment opener behavior. None has an active `EditKeywordIndent` case, so
there is no keyword-closing metadata to consume for these delivered schemes.
Existing scheme tab metadata and file settings still determine the active
widths and tabs/spaces choice. Tab Indents selects a tab or tab-width spaces;
when disabled, the original raw indentation width supplies spaces, including
the original zero-width behavior.

The original closing-delimiter test checks only the first byte on the new line;
it does not match brace types or skip whitespace. A trailing space after an
opener prevents the extra indentation. `/**` and `/*!` insert ` *`; other
comment lines only retain leading whitespace. These details are intentional.

`original_newline_autoindent` tests public editor commands, exact UTF-8/EOL
bytes, caret/selection, save point and one-step Undo/Redo. The existing
independent-process settings test covers the AutoIndent default, import,
toggle, restoration, disabled exit save and explicit save. The port does not
change the Windows IME composition path; native IME geometry regression remains
separate from human commit/cancel verification. Linux IME remains deferred.

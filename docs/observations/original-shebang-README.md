# Original Python first-line selection

Issue #68 follows Notepad4 revision
`89b8b36e0ee2c23f73be922315f17261ccfbbe1a`:
`Styles.cpp::Style_SniffShebang`, `Style_GetLexerFromFile`,
`Style_SetLexerFromFile`, `Style_Load`/`Style_Save`, and
`Notepad4.cpp::LoadFlags`. The original BSD license is retained in
`licenses/Notepad4.txt`.

The syntax module adapts the interpreter parser for the delivered Python lexer.
It retains the first 255 decoded UTF-8 document bytes, byte-zero `#!`, original
space/tab and basename handling, case-sensitive `env*`/`winpty*` wrapper test,
and ASCII-letter interpreter prefix. Thus `py*` and `ipy*` forms work, while
`env -S`, `env NAME=value`, leading whitespace and a second-line shebang do not.
Quotes receive no shell parsing: a quoted absolute path can still yield a Python
basename, while a quoted bare interpreter does not.

Delivered filename associations and explicit choices retain precedence. A small
pinned extension list, reproduced with `tools/update_detection_extensions.py`,
also prevents unknown-to-the-port associations such as `.js` and `.sh` from
becoming Python. Original special filenames follow the same precedence.
Unported associated schemes remain Plain Text; this change adds no lexer or
additional extension support. Original Config associations permit shebang
selection, including MySQL ini/cnf names. CGI/fCGI sniffing precedes associated
extension fallback as in the original.

`[Styles] AutoSelect=1` and `[Settings2] NoCGIGuess=0` are loaded through the
existing settings store. Invalid values use the original defaults. The original
`NoAutoDetection` flag governs a later content-detection stage and therefore does
not suppress shebang recognition.

The session stores the automatic result when Open or explicit Automatic chooses
it. Ordinary edits and same-path reopen retain the effective language; selecting
Automatic explicitly rechecks current text. Save As with All Files applies the
same detection rules. This does not add a live-edit detection policy or duplicate
the encoding/EOL load path.

`original_python_shebang` drives real frame commands and the original editor to
check supported/negative/conflicting interpreter forms, real lexer and keyword
styles, menu/status, UTF-8 and UTF-16 BOMs, exact independent disk bytes,
selection, undo/redo, save, Save As and same-path reopen.
`original_shebang_settings` starts independent application processes with original
INI sections and verifies loaded flags, defaults, explicit choices and explicit
settings persistence. Native execution results and red/green/analyzer logs are
retained in the implementation evidence directory for issue #68.

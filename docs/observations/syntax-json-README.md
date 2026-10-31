# JSON syntax validation (#35)

JSON uses the pinned Notepad4 `LexJSON.cxx`, its keyword list with the consumed
`PreSorted` attribute, and all expanded original default light style groups.
`patches/README.md` documents offline data regeneration and source/license
provenance. Automatic selection covers only ASCII case-insensitive `.json`.
The Language menu can select JSON for any named or unnamed document.

`tests/syntax_json.cpp` drives native Open and Save As choosers, real language and
Undo/Redo commands, and owned editor Delete events. It checks public core style
IDs for properties, Chinese string content, escapes, numbers, keywords and
punctuation; default colors/background/size; exact independently read UTF-8 CRLF
saved bytes; override and Automatic Save As rules; Plain Text/C++ transitions;
and quote/escape restyling across Undo/Redo. Incomplete JSON remains editable and
saves its exact bytes. The test observes natural styling without forcing
`Colourise`. Folding remains a later slice (#38).

The first Windows red failed because `.JsOn` opened as Plain Text. After the
automatic slice passed, the second red failed because the native Language menu
did not yet offer JSON. The pre-integration focused group passed **5/5 Windows,
5/5 private X11 and 5/5 private Wayland**. After merging Python integration
`e02a428`, the combined C++/Python/JSON, automatic-save, manual-language and native
font group passed **6/6 Windows, 6/6 private X11 and 6/6 private Wayland**.
Evidence, source snapshots and targeted
clang-tidy logs live at
`E:/NewNotepad4/wxnotepad-evidence/syntax-phase3/issue-35`.

Canonical clang-format 23.1.2 and offline data reproducibility checks pass.
Targeted clang-tidy retains existing header/enum conventions and search findings;
the Win32 test callback uses the standard pointer/LPARAM round trip. New test
position conversions are explicit. Broad final scans belong to the integration
task.

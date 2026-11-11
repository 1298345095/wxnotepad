# Original manual word completion

Issue #191 restores Edit > Special > Complete Word and Alt+/ (including keypad
Divide). An empty document disables the command. A missing prefix or result
retains the original no-op behavior; a sole longer candidate inserts immediately.
The original Scintilla document owns completion, selection and Undo state.

`word_completion.cpp` adapts pinned Notepad4
`89b8b36e0ee2c23f73be922315f17261ccfbbe1a` `EditAutoC.cpp`'s word-list tree,
prefix boundaries, number/CJK cases, bounded document scan, keyword attributes,
style exclusions and special-prefix fallbacks. It consumes the shared full scheme
tables and `completion_styles.cpp` cache. CSS/VBScript/PHP embedded completion
groups use the same `update_schemes.py` importer. Additional Doxygen/XML groups
come from the pinned `EditAutoC_Data0.h`.

The native host follows wxWidgets 3.3.3 `src/stc/PlatWX.cpp`'s nonactivating popup
and nonfocusable virtual list. Item sizing uses actual font/DPI metrics, including
two separately rounded border margins. Positioning fits the current monitor's
work area. Keyboard, native mouse confirmation and wheel routing feed the
original core contract; opening or using the list retains editor keyboard focus.
`completion_selection.cpp` retains the original selected-function caret/braces
handling and fill-up keys. Its optional LaTeX lookup compiles the unchanged
pinned algorithm and tables.

## Independent reference and tests

`tests/reference_capture/capture_word_completion.py ORIGINAL_EXE OUTPUT_DIR`
creates a separate portable original host and captures command results plus each
candidate through the original public completion API and native Down/Enter
messages. It does not use the clipboard. Reviewed literals are frozen in
`tests/completion_reference.h`: 28 Plain Text, C/C++, Python, Bash, JSON,
JavaScript/TypeScript, Rust/Go/Lua, Web Source Code and XML/XSD/XSLT cases,
including embedded CSS/JavaScript, case matching, numbers, CJK and prefix
fallbacks.

The public application/native regressions are:

- `original_complete_word`: command/default state, sole candidate and Undo,
  physical Linux Alt+/ and keypad Divide, frozen candidate lists and native
  Down/Enter results, function caret inside parentheses.
- `original_completion_popup`: native double-click confirmation and Undo,
  unchanged keyboard focus, Escape, scrolled monitor-edge placement, wheel
  routing to the list, and native focus-loss cancellation. Wayland uses an owned
  fullscreen window for edge placement because clients cannot move top-level
  windows.
- `original_completion_settings`: independent-process default/import/bound
  checks, live Ignore Case cancellation, explicit/exit-save behavior and
  preservation of unknown settings.

These targets passed native Windows (150% DPI) and isolated X11/Wayland at
`GDK_SCALE=1` and `2`. Windows native message/accelerator tests ran on an owned
private desktop. Native clang-tidy 23 (Windows) and 18 (Linux) checked the ten
modified production/test translation units. Logs and frozen raw captures are in
`E:/NewNotepad4/wxnotepad-evidence/completion-languages-2026-11-10/issue-191/`.

Human visual/IME checks remain part of the root's final integrated validation;
Linux IME is deferred. Automatic triggers and the full settings controls belong
to #192.

# Calculate Expression

Tools → Action on Selection → Calculate Expression follows Notepad4
`89b8b36e0ee2c23f73be922315f17261ccfbbe1a`, `Bridge.cpp::EditCalculateExpr`.
It evaluates the selected JavaScript with `with(Math)`, inserts a space and the
formatted result after the original expression, and selects that entire insertion
(including the space). `^` is JavaScript XOR. There is no new shortcut.

With multiple selections, the command evaluates the original concatenated
selection text and inserts at the main selection's end. The result becomes one
forward selection. Undo restores the original selections
and main selection; Redo restores the result. Empty selections disable the menu.
Empty/null/undefined results do not edit or create history. Rectangles retain the
original warning. Read-only selections keep the command enabled: Scintilla blocks
insertion but the original command still selects the intended result range.
Evaluation does not use the clipboard. Each command creates a fresh engine state.

Windows preserves the original engine sequence: system Chakra ActiveScript, then
the `JavaScript` ProgID fallback. It invokes object `toString()` and formats the
VARIANT through system `propsys!VariantToString`. Native ANSI documents retain
the existing original native-code-page conversion boundary. File encodings,
BOMs, EOL metadata and document save points use the existing document commands.
The original-supported primitive `Symbol("x")` result converts to `Symbol(x)`
on both engines through its explicit `toString()` method.

Linux statically links QuickJS-ng 0.16.2, overlay port revision 1, without
its optional I/O modules or command-line programs. The CMake target must be a
static library. Existing shared system runtimes remain the runtime boundary.
The original Windows Automation numeric conversion is retained: its 15-digit
candidate is used when it round-trips exactly, otherwise the original 18-digit
format is used. Captured examples include PI = `3.14159265358979312`,
`0.1+0.2` = `0.300000000000000044`, true = `-1`, NaN = `-1.#IND`, infinity = `inf`.
The engine overlay retains the original canonical NaN/sign behavior through
Number, Math, unary operations, variables and object results; positive NaN prints
`1.#QNAN`, including `-NaN`. The formatter reads the actual value/sign, with no
expression-text inference. Thirty-six independent original system samples match
the patched installed library; see the overlay's README and patch provenance.
The original null-terminated result insertion also stops at embedded NULs. The
original `wsprintfW` Math wrapper has a 1024 UTF-16-unit source limit; Linux
retains that same limit and rejects the independently captured over-limit sample
without changing the document. This retains an original limit, rather than
substituting a smaller expression language.

On 2026-11-27 the user approved two Linux engine differences: localized timezone
display-name suffixes in Date strings and engine-specific error wording.
For the same America/Los_Angeles zone, original `new Date(0)` returned
`Wed Dec 31 1969 16:00:00 GMT-0800 (太平洋标准时间)` while QuickJS returned
`Wed Dec 31 1969 16:00:00 GMT-0800`. Error notifications retain the original text
and document. Generated Math-wrapper offsets are removed. QuickJS columns count
UTF-8 bytes; Linux notification anchors remain inside the selected source on valid
UTF-8 boundaries. Runtime source tokens may differ: `throw new Error("owned error")`
points at `throw` in Chakra and the Error constructor in QuickJS. Windows retains
the original UTF-16-column/byte-position handling, including its Unicode quirk.
For concatenated selections, Linux maps the engine's source-byte offset through
the sorted document ranges and the actual default/custom copy separator, so a
failure in a later range points to that selected token even when another range
is main. Native popup-coordinate regressions calibrate the existing notification
surface at independently known source tokens, including gaps, CJK, supplementary
characters and the JavaScript Unicode line separator, for either main range.

The independent real original capture is retained outside Git at
`F:/wxnotepad-evidence/window-and-tools-implementation-2026-11-27/292/original-native-02/`.
It contains 86 forward/reverse expression observations and four selection-mode
observations, with original executable SHA-256, saved bytes and complete history.
`tests/reference_capture/capture_calculate_expression.py` reproduces it with an
independent original executable copy. `calculate_expression_reference.h` replays
84 non-Date expression observations, plus embedded-NUL, primitive-throw, numeric
conversion and long-source observations from independent original system probes.
The two environment-dependent
Date rows are retained in the raw engine comparison and approved difference.
The native fixture also checks all ten delivered encodings and clipboard bytes.
The packaged fixture replays the fixed non-Date matrix in English, Simplified
Chinese and Traditional Chinese through the actual app's public menus, files,
selection observations and Undo/Redo. The registered focused cases are
`original_calculate_expression` and `packaged_calculate_expression`.

The Linux bundle includes the full MIT/Unicode-3.0 notices, actual vcpkg SPDX
package/source records, and an `expression_engine` record in `build.json`.

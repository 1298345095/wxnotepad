# Base64 conversions

The Tools / Base64 group adapts the five commands and shared codec algorithms
from Notepad4 `89b8b36e0ee2c23f73be922315f17261ccfbbe1a`:
Standard Encode, URL Safe Encode, Encode as HTML Embedded Image, Decode and
Decode as Hex. The original padding, accepted decode alphabet/stopping rules,
HTML extension wrapper, byte grouping, selection replacement and command-state
rules are preserved. The commands have no additional shortcuts or dependency.

The UTF-8 editor retains the original native ANSI boundary separately from an
explicit choice of the same legacy encoding. Decoded native bytes that cannot
round-trip through Unicode retain their byte origin, including native aliases
such as CP936 `A2 E3`. Primitive insertion/deletion undo actions retain the raw
parts of that action; normal edits shift or clip the live ranges. Undo and Redo
restore those parts through the same editor notifications. This bookkeeping runs
before public modification callbacks and remains active during batched edits.
Clearing undo history retains live byte origins; replacing the document clears
them through the actual text changes.

Save, Base64 input, Show Hex Code and Character Info share the same native-byte
interpretation. Explicit legacy encoding retains its existing invalid-UTF-8
validation and conversion-loss policy.

## Native validation

The native command fixture compares 498 frozen original samples, including all
five commands, forward/reverse and multiple selections, newline modes, edge
lengths, invalid/partial input and supported encoding representations. Additional
checks cover original empty/read-only/rectangular behavior, zero-result edits,
pending Redo, arbitrary ANSI bytes, CP936 aliases, Save/Undo/Redo, Show Hex and
Character Info. The test uses the real menu handlers and native Backspace input.

On the CP936 Windows host, separately decoding `wg==` and `oA==` on adjacent
lines gives `C2 0A A0`. Native Backspace joins the lines into `C2 A0`, which also
happens to be valid UTF-8. The original saves `C2 A0`; the earlier adapter instead
interpreted it as NBSP and substituted `3F` after a loss prompt. The final adapter
saves the original bytes without a prompt, including after Undo and Redo. The
same bytes re-encode to `wqA=`, appear as `[C2 A0]` in Show Hex, and produce
CP936 U+807D in Character Info.

Focused native validation passed on Windows, GTK/X11 and GTK/Wayland. Linux's
native ANSI identity uses UTF-8, so CP936-specific byte provenance is exercised
on Windows. These tests establish the listed conversion and history behavior;
they do not establish every native DBCS caret, word or deletion rule for arbitrary
raw byte sequences in a UTF-8 core.

Evidence is retained under
`F:/wxnotepad-evidence/native-editor-controls-2026-11-17/217/`:
`reference-final`, `ansi-composition-original-02`,
`ansi-provenance-composition-probe-02`, `provenance-regression-windows`,
`provenance-tools-x11-02` and `provenance-regression-wayland`.
Packaged multilingual acceptance and full integrated validation are recorded by
the integration task.

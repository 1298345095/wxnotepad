# Original regex target context and continuation

Issue #76 restores Notepad4 `89b8b36e0ee2c23f73be922315f17261ccfbbe1a`
behavior in the supported Boost ECMAScript mode (`RegExp | Cxx11RegEx`), UTF-8,
DotAll off. The fixtures use case-insensitive matching and WholeWord off unless
the native option regression explicitly toggles them. Ranges are UTF-8 bytes,
end exclusive. Original source expectations were recorded before production edits.

| Operation / fixture | Expected result | Pinned source reason |
| --- | --- | --- |
| Next, `aab`, `aa|(?<=aa)b`, no wrap | `[0,2]`, then no match | `scintilla/src/Document.cxx:3580-3589` uses the five-argument Boost matcher. The next target starts at 2, which is also the lookbehind backstop. |
| Next, `aab`, `(?<=aa)b`, caret 2 | no match; caret 0 finds `[2,3]` | Lookbehind cannot cross target start. |
| Next, `ab`, `\A.`, caret 1 | `[1,2]` | `boost/regex/v5/perl_matcher_common.hpp:569-575` compares buffer start to the target backstop. `MatchFlags` sets no `match_not_bob`. |
| Previous, `ab`, `a(?=b)`, caret 1 | no match | Original backward `MatchOnLines` clips the first line at the caret, so lookahead cannot see `b`. |
| Previous, `ab`, `a\z`, caret 1 | `[0,1]` | The clipped target end is matcher `last`; `MatchFlags` sets no `match_not_eob`. |
| Previous, `中\n文 中\n文`, `中\n文` | no match, even with wrap | `Document.cxx:3591-3607` uses separate line ranges without newline bytes. Forward finds `[0,7]` and `[8,15]`. |
| Next/Previous, `中文ab`, `a*`, empty selection | repeats the same empty selection | `src/Edit.cpp:5315-5341` and `5358-5385` resume directly at selection end/start. No extra empty-match progress is applied by Find. |
| Replace, `aab`, `(?<=aa)(b)`, `$1!` | initial Find selects `[2,3]`; Replace leaves bytes unchanged | `Edit.cpp:5402-5425` re-runs forward at selection start, losing the earlier lookbehind context. |
| Replace after Previous, `a1 a2`, `a([0-9])`, `$1` | `1 a2` | The fresh forward search restores replacement captures before setting the exact replacement target (`5402-5429`). |
| Replace, `a a`, `(a)`, selection `[0,3]` | selects `[0,1]` without editing | Original revalidation selects its first candidate when its endpoints differ (`5423-5425`); it does not restart at the old selection end. |
| Replace at matching empty caret, `中文中文`, `(?=中)`, `_` | `_中文中文`, then `__中文中文` | Matching selection endpoints suffice; after insertion, original search resumes at insertion end (`5423-5434`). |
| Replace, empty document, `^`, `X` | `X` immediately | No prior confirmation revision is required. Subsequent search from 1 cannot match line start. |
| Replace All, `a`, `a*` or `a|$`, `_` | `_`, one replacement | Original loop stops when replacement end equals updated document length (`5820-5825`). |
| Replace All, `中文\n测试\n`, `^`, empty replacement | unchanged, two replacements | When replacement ends at a line start, pattern-leading `^` advances to the next line and stops if that equals EOF (`5833-5842`). With `>` instead, insertion ends inside the line and the final empty line is included (three replacements). |
| Replace All, `中\n`, `\A|\z`, `X` | `X中X\nX`, three replacements | Each new target becomes the buffer-anchor base; ordinary zero-width progress moves over one original character (`5828-5830`). |

The previous port document-base matcher patch and backward forward-scan are
removed. Replacement capture confirmation no longer uses application revision
stamps. Byte-identical replacements retain the established no-op history behavior;
text-changing Replace All remains one undo group.

Regression coverage uses actual SearchSession and native Find/Replace commands,
selection endpoints, UTF-8 text, invalid-pattern errors, literal search,
Undo/Redo/save points and independent saved BOM/mixed-EOL bytes. Native Windows
checks use an owned hidden desktop, and Linux checks use private X11 and Wayland
sessions. Detailed red/green and analyzer logs are retained outside the repository
under `wxnotepad-evidence/original-parity-implementation/issue-76/`.
